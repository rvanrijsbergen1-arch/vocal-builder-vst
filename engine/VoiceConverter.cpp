#include "VoiceConverter.h"
#include "Dsp.h"
#include "OrtModel.h"
#include <cmath>
#include <fstream>
#include <juce_core/juce_core.h>
#include <cstdlib>
#include <random>

namespace vb
{
namespace fs = std::filesystem;

static juce::var readJson (const fs::path& p)
{
    juce::File f (juce::String::fromUTF8 (p.u8string().c_str()));
    return juce::JSON::parse (f.loadFileAsString());
}

std::vector<VoiceInfo> listVoices (const fs::path& modelsDir, bool includeTestOnly, const fs::path& userVoicesDir)
{
    std::vector<VoiceInfo> out;
    std::vector<fs::path> dirs;
    for (const auto& root : { modelsDir / "voices", userVoicesDir })
    {
        std::error_code ec;
        if (root.empty() || ! fs::is_directory (root, ec))
            continue;
        for (auto& d : fs::directory_iterator (root, ec))
            dirs.push_back (d.path());
    }
    for (auto& path : dirs)
    {
        const fs::directory_entry d (path);
        if (! d.is_directory() || ! fs::exists (d.path() / "voice.json"))
            continue;
        auto j = readJson (d.path() / "voice.json");
        if (! j.isObject())
            continue;
        VoiceInfo v;
        v.dir = d.path();
        v.id = j["id"].toString().toStdString();
        v.name = j["name"].toString().toStdString();
        v.description = j["description"].toString().toStdString();
        v.licence = j["licence"].toString().toStdString();
        v.encoder = j["encoder"].toString().toStdString();
        v.sampleRate = (int) j["sample_rate"];
        v.hop = (int) j["hop"];
        v.featureDim = (int) j["feature_dim"];
        v.testOnly = (bool) j["test_only"];
        if (auto* a = j["languages"].getArray())
            for (auto& s : *a)
                v.languages.push_back (s.toString().toStdString());
        if (auto* a = j["tags"].getArray())
            for (auto& s : *a)
                v.tags.push_back (s.toString().toStdString());
        v.hasIndex = fs::exists (d.path() / "index.f32");
        if (v.id.empty() || v.sampleRate <= 0 || v.hop <= 0 || ! fs::exists (d.path() / "synth.onnx"))
            continue;
        if (v.testOnly && ! includeTestOnly)
            continue;
        out.push_back (v);
    }
    return out;
}

namespace
{
constexpr int kSr = 16000, kF0Hop = 160, kPad = 8000; // 0.5 s context on both sides of every chunk

struct FeatureIndex
{
    int rows = 0, dim = 0;
    std::vector<float> data;
    std::vector<float> norms;

    void load (const fs::path& p, int expectedDim)
    {
        std::ifstream f (p, std::ios::binary);
        int32_t hdr[2] {};
        f.read ((char*) hdr, sizeof (hdr));
        rows = hdr[0];
        dim = hdr[1];
        if (! f || rows <= 0 || dim != expectedDim || rows > 200000)
            throw EngineError { "MODEL_CORRUPT", "The voice's feature index is damaged. Reinstall the voice." };
        data.resize ((size_t) rows * (size_t) dim);
        f.read ((char*) data.data(), (std::streamsize) (data.size() * sizeof (float)));
        if (! f)
            throw EngineError { "MODEL_CORRUPT", "The voice's feature index is truncated. Reinstall the voice." };
        norms.resize ((size_t) rows);
        for (int r = 0; r < rows; ++r)
        {
            double s = 0;
            for (int d = 0; d < dim; ++d)
                s += (double) data[(size_t) r * dim + d] * data[(size_t) r * dim + d];
            norms[(size_t) r] = (float) s;
        }
    }

    // RVC-style retrieval: 8 nearest neighbours weighted by 1/d^2.
    void blend (float* feat, int frames, double rate) const
    {
        constexpr int k = 8;
        std::vector<float> tmp ((size_t) dim);
        for (int t = 0; t < frames; ++t)
        {
            float* q = feat + (size_t) t * dim;
            double qn = 0;
            for (int d = 0; d < dim; ++d)
                qn += (double) q[d] * q[d];
            std::pair<float, int> best[k];
            for (auto& b : best)
                b = { 3.4e38f, -1 };
            for (int r = 0; r < rows; ++r)
            {
                const float* v = data.data() + (size_t) r * dim;
                double dot = 0;
                for (int d = 0; d < dim; ++d)
                    dot += (double) q[d] * v[d];
                const float dist = (float) std::max (0.0, qn + norms[(size_t) r] - 2.0 * dot);
                if (dist < best[k - 1].first)
                {
                    int i = k - 1;
                    while (i > 0 && best[i - 1].first > dist)
                    {
                        best[i] = best[i - 1];
                        --i;
                    }
                    best[i] = { dist, r };
                }
            }
            double wsum = 0;
            std::fill (tmp.begin(), tmp.end(), 0.0f);
            for (auto& b : best)
            {
                if (b.second < 0)
                    continue;
                const double w = 1.0 / std::max (1e-6, (double) b.first * b.first);
                wsum += w;
                const float* v = data.data() + (size_t) b.second * dim;
                for (int d = 0; d < dim; ++d)
                    tmp[(size_t) d] += (float) (w * v[d]);
            }
            if (wsum <= 0)
                continue;
            for (int d = 0; d < dim; ++d)
                q[d] = (float) (rate * tmp[(size_t) d] / wsum + (1.0 - rate) * q[d]);
        }
    }
};

std::vector<float> envelope (const std::vector<float>& x, int win, int hop, size_t frames)
{
    std::vector<float> e (frames, 0.0f);
    for (size_t f = 0; f < frames; ++f)
    {
        const long c = (long) (f * (size_t) hop);
        double s = 0;
        int n = 0;
        for (long i = c - win / 2; i < c + win / 2; ++i)
            if (i >= 0 && i < (long) x.size())
            {
                s += (double) x[(size_t) i] * x[(size_t) i];
                ++n;
            }
        e[f] = (float) std::sqrt (s / std::max (1, n));
    }
    return e;
}

// Pick chunk boundaries (multiples of 160 samples) at quiet points near every ~12 s.
std::vector<size_t> chunkBounds (const std::vector<float>& x)
{
    const size_t n = x.size(), target = (size_t) 12 * kSr, search = (size_t) 2 * kSr;
    std::vector<size_t> b { 0 };
    while (n - b.back() > target + search)
    {
        const size_t centre = b.back() + target;
        size_t bestPos = centre;
        double bestE = 1e30;
        for (size_t p = centre - search; p + kF0Hop <= centre + search; p += kF0Hop)
        {
            double e = 0;
            for (size_t i = p; i < p + 320 && i < n; ++i)
                e += (double) x[i] * x[i];
            if (e < bestE)
            {
                bestE = e;
                bestPos = p;
            }
        }
        b.push_back (bestPos / kF0Hop * kF0Hop);
    }
    b.push_back (n);
    return b;
}
} // namespace

Audio convertVoice (const Audio& vocal, const VoiceInfo& voice, const fs::path& modelsDir, const ConversionSettings& s,
                    int threads, const std::function<void (double, const std::string&)>& progress)
{
    progress (0.01, "Loading voice models…");
    const auto encFile = modelsDir / "encoders" / (voice.encoder + ".onnx");
    OrtModel encoder (encFile, threads);
    OrtModel pitch (modelsDir / "pitch" / "fcpe.onnx", threads);
    OrtModel synth (voice.dir / "synth.onnx", threads);
    FeatureIndex index;
    const bool useIndex = voice.hasIndex && s.indexRate > 0.0;
    if (useIndex)
        index.load (voice.dir / "index.f32", voice.featureDim);

    progress (0.04, "Preparing vocal…");
    const auto mono = toMono (vocal);
    auto x16 = dsp::resample (mono, vocal.sampleRate, kSr);
    const size_t origLen16 = x16.size();
    x16.resize ((origLen16 + kF0Hop - 1) / kF0Hop * kF0Hop, 0.0f);

    const auto bounds = chunkBounds (x16);
    const double shift = std::pow (2.0, s.pitchShift / 12.0);
    const double melMin = 1127.0 * std::log (1.0 + 50.0 / 700.0), melMax = 1127.0 * std::log (1.0 + 1100.0 / 700.0);
    std::mt19937 rng (s.seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);

    std::vector<float> outAll;
    outAll.reserve (x16.size() / kF0Hop * (size_t) voice.hop);

    for (size_t ci = 0; ci + 1 < bounds.size(); ++ci)
    {
        const size_t a = bounds[ci], b = bounds[ci + 1], len = b - a;
        // Chunk with reflected context on both sides.
        std::vector<float> chunk (len + 2 * kPad);
        for (long i = 0; i < (long) chunk.size(); ++i)
        {
            long src = (long) a - kPad + i;
            if (src < 0)
                src = -src;
            if (src >= (long) x16.size())
                src = 2 * (long) x16.size() - src - 2;
            chunk[(size_t) i] = x16[(size_t) std::clamp<long> (src, 0, (long) x16.size() - 1)];
        }
        const int frames = (int) (chunk.size() / kF0Hop);
        const double base = (double) ci / (double) (bounds.size() - 1);
        const double span = 1.0 / (double) (bounds.size() - 1);
        auto report = [&] (double f, const char* what) { progress (0.05 + 0.9 * (base + span * f), what); };

        // 1. Content features (encoder runs at 50 fps; repeat x2 to the 100 fps frame rate).
        report (0.05, "Analysing words and sounds…");
        std::vector<Ort::Value> ein;
        ein.push_back (OrtModel::tensor (chunk, { 1, (int64_t) chunk.size() }));
        auto eout = encoder.run (ein);
        const auto eshape = eout[0].GetTensorTypeAndShapeInfo().GetShape();
        const int encFrames = (int) eshape[1], dim = (int) eshape[2];
        if (dim != voice.featureDim)
            throw EngineError { "MODEL_MISMATCH", "This voice expects " + std::to_string (voice.featureDim) + "-dim features but the encoder produced " + std::to_string (dim) + "." };
        const float* ef = eout[0].GetTensorData<float>();
        std::vector<float> feats ((size_t) frames * dim), feats0;
        for (int t = 0; t < frames; ++t)
        {
            const int src = std::min (encFrames - 1, t / 2);
            std::copy (ef + (size_t) src * dim, ef + (size_t) (src + 1) * dim, feats.begin() + (long) t * dim);
        }
        if (useIndex)
        {
            feats0 = feats;
            report (0.25, "Matching the voice's timbre…");
            index.blend (feats.data(), frames, s.indexRate);
        }

        // 2. Melody (f0) at 100 fps.
        report (0.45, "Tracking the melody…");
        // FCPE front end (identical to torchfcpe): reflect-pad 432, |STFT| n_fft 1024 / hop 160,
        // Hann window, last frame repeated to reach N/160 + 1 frames.
        std::vector<float> spec;
        {
            const int nfft = 1024, hop = 160, padL = 432;
            const long n = (long) chunk.size();
            std::vector<float> padded ((size_t) (n + 2 * padL));
            for (long i = 0; i < (long) padded.size(); ++i)
            {
                long src = i - padL;
                if (src < 0)
                    src = -src;
                if (src >= n)
                    src = 2 * n - src - 2;
                padded[(size_t) i] = chunk[(size_t) std::clamp<long> (src, 0, n - 1)];
            }
            const int stftFrames = (int) ((padded.size() - (size_t) nfft) / hop + 1);
            const int wantFrames = (int) (n / hop + 1);
            spec.resize ((size_t) wantFrames * 513);
            dsp::RealFFT fft (nfft);
            const auto win = dsp::hannPeriodic (nfft);
            std::vector<float> fr (nfft);
            std::vector<std::complex<float>> bins (513);
            for (int t = 0; t < wantFrames; ++t)
            {
                const int tt = std::min (t, stftFrames - 1);
                for (int i = 0; i < nfft; ++i)
                    fr[(size_t) i] = padded[(size_t) tt * hop + (size_t) i] * win[(size_t) i];
                fft.forward (fr.data(), bins.data());
                for (int k = 0; k < 513; ++k)
                    spec[(size_t) t * 513 + (size_t) k] = std::sqrt (std::norm (bins[(size_t) k]) + 1e-9f);
            }
        }
        std::vector<Ort::Value> pin;
        pin.push_back (OrtModel::tensor (spec, { 1, (int64_t) (spec.size() / 513), 513 }));
        auto pout = pitch.run (pin);
        const float* f0raw = pout[0].GetTensorData<float>();
        const auto pframes = (int) pout[0].GetTensorTypeAndShapeInfo().GetElementCount();
        if (ci == 0)
            if (const char* dump = std::getenv ("VB_DEBUG_DUMP_F0")) // developer check against torchfcpe
            {
                std::ofstream df (dump, std::ios::binary);
                df.write ((const char*) chunk.data(), (std::streamsize) (chunk.size() * sizeof (float)));
                df.write ((const char*) f0raw, (std::streamsize) (pframes * sizeof (float)));
            }
        std::vector<float> f0 ((size_t) frames);
        std::vector<int64_t> coarse ((size_t) frames);
        for (int t = 0; t < frames; ++t)
        {
            float v = t < pframes ? f0raw[t] : 0.0f;
            v = v > 0 ? (float) (v * shift) : 0.0f;
            f0[(size_t) t] = v;
            double mel = 1127.0 * std::log (1.0 + v / 700.0);
            if (mel > 0)
                mel = (mel - melMin) * 254.0 / (melMax - melMin) + 1.0;
            coarse[(size_t) t] = (int64_t) std::lround (std::clamp (mel, 1.0, 255.0));
        }

        // Consonant protection: unvoiced frames keep more of the original features.
        if (useIndex && s.protect < 0.5)
            for (int t = 0; t < frames; ++t)
                if (f0[(size_t) t] <= 0)
                    for (int d = 0; d < dim; ++d)
                    {
                        auto& v = feats[(size_t) t * dim + d];
                        v = (float) (v * s.protect + feats0[(size_t) t * dim + d] * (1.0 - s.protect));
                    }

        // 3. Synthesis.
        report (0.6, "Singing with the new voice…");
        std::vector<int64_t> lengths { frames }, sid { 0 };
        std::vector<float> rnd ((size_t) 192 * frames);
        for (auto& r : rnd)
            r = nd (rng);
        std::vector<Ort::Value> sin;
        for (auto& name : synth.inputNames)
        {
            if (name == "phone")
                sin.push_back (OrtModel::tensor (feats, { 1, frames, dim }));
            else if (name == "phone_lengths")
                sin.push_back (OrtModel::tensorI64 (lengths, { 1 }));
            else if (name == "pitch")
                sin.push_back (OrtModel::tensorI64 (coarse, { 1, frames }));
            else if (name == "pitchf")
                sin.push_back (OrtModel::tensor (f0, { 1, frames }));
            else if (name == "ds")
                sin.push_back (OrtModel::tensorI64 (sid, { 1 }));
            else if (name == "rnd")
                sin.push_back (OrtModel::tensor (rnd, { 1, 192, frames }));
            else
                throw EngineError { "MODEL_MISMATCH", "Unexpected voice model input: " + name };
        }
        auto sout = synth.run (sin);
        const float* y = sout[0].GetTensorData<float>();
        const auto ylen = (size_t) sout[0].GetTensorTypeAndShapeInfo().GetElementCount();
        const size_t skip = (size_t) (kPad / kF0Hop) * (size_t) voice.hop;
        const size_t want = len / kF0Hop * (size_t) voice.hop;
        for (size_t i = 0; i < want; ++i)
            outAll.push_back (skip + i < ylen ? y[skip + i] : 0.0f);
        report (1.0, "Singing with the new voice…");
    }

    progress (0.96, "Aligning with the original…");
    // Loudness envelope: blend between the source's dynamics and the model's own.
    if (s.dynamics < 0.999)
    {
        const int hopOut = voice.hop;
        const size_t nf = outAll.size() / (size_t) hopOut + 1;
        auto eIn = envelope (x16, 800, kF0Hop, nf);
        auto eOut = envelope (outAll, voice.hop * 5, hopOut, nf);
        for (size_t i = 0; i < outAll.size(); ++i)
        {
            const size_t f = i / (size_t) hopOut;
            const double gi = std::max (1e-6f, eIn[f]), go = std::max (1e-6f, eOut[f]);
            outAll[i] *= (float) (std::pow (gi, 1.0 - s.dynamics) * std::pow (go, s.dynamics - 1.0));
        }
    }

    Audio out;
    out.sampleRate = vocal.sampleRate;
    auto y = dsp::resample (outAll, voice.sampleRate, vocal.sampleRate);
    y.resize (vocal.numSamples(), 0.0f);
    out.channels.push_back (std::move (y));
    return out;
}
} // namespace vb
