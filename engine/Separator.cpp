#include "Separator.h"
#include "Dsp.h"
#include "Mdx.h"
#include "OrtModel.h"
#include <cmath>

namespace vb
{
namespace
{
constexpr int kFrame = 4096, kHop = 1024, kBins = kFrame / 2 + 1, kT = 512, kF = 1536;
constexpr float kEps = 1e-10f;
} // namespace

SeparationResult separate (const Audio& mixIn, const std::filesystem::path& modelFile, int threads,
                           const std::function<void (double, const std::string&)>& progress)
{
    progress (0.01, "Loading separation model…");
    OrtModel model (modelFile, threads);

    progress (0.03, "Preparing audio…");
    const Audio mix = toStereo44k (mixIn);
    const size_t n = mix.numSamples();

    // Pad: one frame of zeros in front (as Spleeter does), zeros at the end to fill the last frame.
    const size_t padded = kFrame + n;
    const size_t numFrames = (padded + kHop - 1) / kHop;
    const size_t bufLen = (numFrames - 1) * kHop + kFrame;
    std::vector<std::vector<float>> x (2, std::vector<float> (bufLen, 0.0f));
    for (int c = 0; c < 2; ++c)
        std::copy (mix.channels[(size_t) c].begin(), mix.channels[(size_t) c].end(), x[(size_t) c].begin() + kFrame);

    const auto window = dsp::hannPeriodic (kFrame);
    dsp::RealFFT fft (kFrame);

    // Only the vocal is synthesised; the instrumental is mix - vocal (the two ratio masks sum to 1),
    // which halves memory use for long songs.
    std::vector<std::vector<float>> outV (2, std::vector<float> (bufLen, 0.0f));
    auto wsumAt = [&] (size_t pos) { // sum of squared windows at buffer position pos
        float s = 0.0f;
        const size_t fLast = std::min (numFrames - 1, pos / kHop);
        for (size_t f = (pos >= (size_t) kFrame ? (pos - kFrame) / kHop + 1 : 0); f <= fLast; ++f)
        {
            const size_t i = pos - f * kHop;
            if (i < (size_t) kFrame)
                s += window[i] * window[i];
        }
        return std::max (s, 1e-8f);
    };

    const size_t numSegments = (numFrames + kT - 1) / kT;
    std::vector<std::complex<float>> spec ((size_t) kT * kBins * 2);
    std::vector<float> mag ((size_t) kT * kF * 2);
    std::vector<float> frameBuf (kFrame), ifr (kFrame);
    std::vector<std::complex<float>> bins (kBins), vb_ (kBins);

    for (size_t seg = 0; seg < numSegments; ++seg)
    {
        const size_t f0 = seg * kT;
        const size_t framesHere = std::min<size_t> (kT, numFrames - f0);

        // STFT for this segment, both channels. Layout of mag: [t][f][c] (NHWC as in Spleeter).
        std::fill (mag.begin(), mag.end(), 0.0f);
        for (int c = 0; c < 2; ++c)
            for (size_t t = 0; t < framesHere; ++t)
            {
                const float* src = x[(size_t) c].data() + (f0 + t) * kHop;
                for (int i = 0; i < kFrame; ++i)
                    frameBuf[(size_t) i] = src[i] * window[(size_t) i];
                fft.forward (frameBuf.data(), bins.data());
                for (int k = 0; k < kBins; ++k)
                    spec[(t * kBins + (size_t) k) * 2 + (size_t) c] = bins[(size_t) k];
                for (int k = 0; k < kF; ++k)
                    mag[(t * kF + (size_t) k) * 2 + (size_t) c] = std::abs (bins[(size_t) k]);
            }

        std::vector<Ort::Value> in;
        in.push_back (OrtModel::tensor (mag, { 1, kT, kF, 2 }));
        auto out = model.run (in);
        const float* vEst = out[0].GetTensorData<float>();
        const float* aEst = out[1].GetTensorData<float>();

        // Ratio masks, extended with zeros above F, applied to the complex mix, then iSTFT (overlap-add).
        for (int c = 0; c < 2; ++c)
            for (size_t t = 0; t < framesHere; ++t)
            {
                for (int k = 0; k < kBins; ++k)
                {
                    float mv = 0.0f;
                    if (k < kF)
                    {
                        const size_t idx = (t * kF + (size_t) k) * 2 + (size_t) c;
                        const float v2 = vEst[idx] * vEst[idx], a2 = aEst[idx] * aEst[idx];
                        mv = (v2 + kEps / 2) / (v2 + a2 + kEps);
                    }
                    vb_[(size_t) k] = spec[(t * kBins + (size_t) k) * 2 + (size_t) c] * mv;
                }
                const size_t off = (f0 + t) * kHop;
                fft.inverse (vb_.data(), ifr.data());
                for (int i = 0; i < kFrame; ++i)
                    outV[(size_t) c][off + (size_t) i] += ifr[(size_t) i] * window[(size_t) i];
            }

        progress (0.05 + 0.85 * (double) (seg + 1) / (double) numSegments,
                  "Separating vocal… " + std::to_string ((int) (100.0 * (seg + 1) / numSegments)) + "%");
    }

    progress (0.92, "Finishing stems…");
    SeparationResult r;
    r.vocals.sampleRate = r.instrumental.sampleRate = mixIn.sampleRate;
    const size_t targetLen = mixIn.numSamples();
    const int outCh = mixIn.numChannels();
    for (int c = 0; c < outCh; ++c)
    {
        std::vector<float> v (n);
        for (size_t i = 0; i < n; ++i)
        {
            const float w = wsumAt (kFrame + i);
            v[i] = outCh == 1 ? 0.5f * (outV[0][kFrame + i] + outV[1][kFrame + i]) / w
                              : outV[(size_t) c][kFrame + i] / w;
        }
        auto vr = dsp::resample (v, 44100.0, mixIn.sampleRate);
        vr.resize (targetLen, 0.0f);
        // Instrumental = original mix - vocal, at the original sample rate (exact complement).
        std::vector<float> ar (targetLen);
        const auto& src = mixIn.channels[(size_t) c];
        for (size_t i = 0; i < targetLen; ++i)
            ar[i] = src[i] - vr[i];
        r.vocals.channels.push_back (std::move (vr));
        r.instrumental.channels.push_back (std::move (ar));
    }
    return r;
}

static Audio toSourceLayout (const Audio& a44, const Audio& src)
{
    Audio out;
    out.sampleRate = src.sampleRate;
    for (int c = 0; c < src.numChannels(); ++c)
    {
        std::vector<float> ch;
        if (src.numChannels() == 1)
        {
            ch.resize (a44.numSamples());
            for (size_t i = 0; i < ch.size(); ++i)
                ch[i] = 0.5f * (a44.channels[0][i] + a44.channels[1][i]);
        }
        else
            ch = a44.channels[(size_t) std::min (c, 1)];
        auto r = dsp::resample (ch, 44100.0, src.sampleRate);
        r.resize (src.numSamples(), 0.0f);
        out.channels.push_back (std::move (r));
    }
    return out;
}

static Audio subtract (const Audio& a, const Audio& b)
{
    Audio o = a;
    for (size_t c = 0; c < o.channels.size(); ++c)
        for (size_t i = 0; i < o.channels[c].size(); ++i)
            o.channels[c][i] -= b.channels[c][i];
    return o;
}

SeparationResult separateChain (const Audio& mix, const std::filesystem::path& modelsDir, const SeparationOptions& o,
                                int threads, const std::function<void (double, const std::string&)>& progress)
{
    const auto sepDir = modelsDir / "separation";
    std::filesystem::path vocalModel;
    for (const char* name : { "UVR-MDX-NET-Voc_FT.onnx", "Kim_Vocal_2.onnx" })
        if (mdxModelAvailable (sepDir / name))
        {
            vocalModel = sepDir / name;
            break;
        }
    const auto karaModel = sepDir / "UVR_MDXNET_KARA_2.onnx";
    const auto reverbModel = sepDir / "Reverb_HQ_By_FoxJoy.onnx";
    if (o.leadOnly && ! mdxModelAvailable (karaModel))
        throw EngineError { "MODEL_MISSING", "The lead/backing vocal model is not installed. Reinstall Vocal Builder." };
    if (o.dereverb && ! mdxModelAvailable (reverbModel))
        throw EngineError { "MODEL_MISSING", "The de-reverb model is not installed. Reinstall Vocal Builder." };

    // Progress budget per stage.
    const int stages = (o.isolatedVocal ? 0 : 1) + (o.leadOnly ? 1 : 0) + (o.dereverb ? 1 : 0);
    int stage = 0;
    auto stageProgress = [&] (double f, const std::string& m) {
        progress (0.03 + 0.92 * ((double) stage + f) / std::max (1, stages), m);
    };

    SeparationResult r;
    std::string engine;
    Audio vocals44;
    if (o.isolatedVocal)
    {
        vocals44 = toStereo44k (mix);
        engine = "isolated vocal";
    }
    else if (! vocalModel.empty())
    {
        progress (0.02, "Loading vocal model…");
        const auto mix44 = toStereo44k (mix);
        vocals44 = mdxPrimaryStem (mix44, vocalModel, threads, stageProgress, "Separating vocal…");
        ++stage;
        engine = vocalModel.stem().string();
    }
    else
    {
        // Legacy fallback: Spleeter (lower quality; kept so older installs still work).
        auto sp = separate (mix, sepDir / "spleeter_2stems.onnx", threads, stageProgress);
        ++stage;
        vocals44 = toStereo44k (sp.vocals);
        engine = "spleeter_2stems";
    }

    const Audio allVocals44 = vocals44; // full vocal (lead + backing + reverb) for the instrumental
    Audio backing44;
    if (o.leadOnly)
    {
        backing44 = mdxPrimaryStem (vocals44, karaModel, threads, stageProgress, "Removing backing vocals…");
        ++stage;
        vocals44 = subtract (vocals44, backing44);
        engine += " + lead/backing split";
    }
    if (o.dereverb)
    {
        auto rev = mdxPrimaryStem (vocals44, reverbModel, threads, stageProgress, "Removing reverb…");
        ++stage;
        vocals44 = subtract (vocals44, rev);
        engine += " + de-reverb";
    }

    progress (0.96, "Finishing stems…");
    r.vocals = toSourceLayout (vocals44, mix);
    if (o.leadOnly)
        r.backing = toSourceLayout (backing44, mix);
    if (! o.isolatedVocal) // instrumental = original mix minus the complete vocal
        r.instrumental = subtract (mix, toSourceLayout (allVocals44, mix));
    r.engine = engine;
    return r;
}
} // namespace vb
