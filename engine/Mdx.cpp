#include "Mdx.h"
#include "Dsp.h"
#include "OrtModel.h"
#include <juce_core/juce_core.h>

namespace vb
{
namespace fs = std::filesystem;
static constexpr int kHop = 1024;

static fs::path paramsFile (const fs::path& onnx)
{
    auto p = onnx;
    return p.replace_extension (".json");
}

bool mdxModelAvailable (const fs::path& onnx)
{
    std::error_code ec;
    return fs::exists (onnx, ec) && fs::exists (paramsFile (onnx), ec);
}

MdxParams loadMdxParams (const fs::path& onnx)
{
    juce::File f (juce::String::fromUTF8 (paramsFile (onnx).u8string().c_str()));
    auto j = juce::JSON::parse (f.loadFileAsString());
    if (! j.isObject())
        throw EngineError { "MODEL_CORRUPT", "Model settings missing: " + paramsFile (onnx).filename().string() };
    MdxParams p;
    p.nfft = (int) j["n_fft"];
    p.dimF = (int) j["dim_f"];
    p.dimTExp = (int) j["dim_t"];
    p.compensate = (float) (double) j["compensate"];
    p.primary = j["primary"].toString().toStdString();
    if (p.nfft <= 0 || p.dimF <= 0 || p.dimF > p.nfft / 2 + 1 || p.dimTExp < 4 || p.dimTExp > 12)
        throw EngineError { "MODEL_CORRUPT", "Invalid model settings in " + paramsFile (onnx).filename().string() };
    return p;
}

Audio mdxPrimaryStem (const Audio& in, const fs::path& onnx, int threads,
                      const std::function<void (double, const std::string&)>& progress, const std::string& label)
{
    if (in.numChannels() != 2 || std::abs (in.sampleRate - 44100.0) > 0.5)
        throw EngineError { "INTERNAL", "MDX input must be 44.1 kHz stereo." };
    const MdxParams p = loadMdxParams (onnx);
    OrtModel model (onnx, threads);

    const int nfft = p.nfft, bins = nfft / 2 + 1, dimF = p.dimF, dimT = 1 << p.dimTExp;
    const int chunk = kHop * (dimT - 1), trim = nfft / 2, gen = chunk - 2 * trim;
    const size_t N = in.numSamples();
    const size_t pad = (size_t) gen - N % (size_t) gen;
    const size_t total = (size_t) trim + N + pad + (size_t) trim;

    std::vector<std::vector<float>> xp (2, std::vector<float> (total, 0.0f));
    for (int c = 0; c < 2; ++c)
        std::copy (in.channels[(size_t) c].begin(), in.channels[(size_t) c].end(), xp[(size_t) c].begin() + trim);

    Audio out;
    out.sampleRate = in.sampleRate;
    out.channels.assign (2, std::vector<float> (N + pad, 0.0f));

    dsp::AnyFFT fft (nfft);
    const auto win = dsp::hannPeriodic (nfft);
    std::vector<float> frame ((size_t) nfft), seg ((size_t) chunk + (size_t) nfft), acc, wsum;
    std::vector<std::complex<float>> spec ((size_t) bins);
    std::vector<float> input ((size_t) 4 * dimF * dimT);
    const int padC = nfft / 2;
    const size_t numChunks = (N + pad) / (size_t) gen;

    for (size_t ci = 0; ci < numChunks; ++ci)
    {
        const size_t off = ci * (size_t) gen;
        std::fill (input.begin(), input.end(), 0.0f);
        // STFT of both channels (centred: reflect-pad n_fft/2 on each side of the chunk).
        for (int c = 0; c < 2; ++c)
        {
            const float* x = xp[(size_t) c].data() + off;
            for (int i = 0; i < chunk + 2 * padC; ++i)
            {
                int src = i - padC;
                if (src < 0)
                    src = -src;
                if (src >= chunk)
                    src = 2 * chunk - src - 2;
                seg[(size_t) i] = x[src];
            }
            for (int t = 0; t < dimT; ++t)
            {
                for (int i = 0; i < nfft; ++i)
                    frame[(size_t) i] = seg[(size_t) t * kHop + (size_t) i] * win[(size_t) i];
                fft.forwardReal (frame.data(), spec.data());
                for (int f = 3; f < dimF; ++f) // lowest 3 bins are zeroed, as in UVR
                {
                    input[(((size_t) (2 * c) * dimF + (size_t) f) * dimT) + (size_t) t] = spec[(size_t) f].real();
                    input[(((size_t) (2 * c + 1) * dimF + (size_t) f) * dimT) + (size_t) t] = spec[(size_t) f].imag();
                }
            }
        }

        std::vector<Ort::Value> ins;
        ins.push_back (OrtModel::tensor (input, { 1, 4, dimF, dimT }));
        auto res = model.run (ins);
        const float* y = res[0].GetTensorData<float>();

        // iSTFT (overlap-add, divide by summed squared window), then drop the trim on both sides.
        const size_t len = (size_t) chunk + (size_t) nfft;
        for (int c = 0; c < 2; ++c)
        {
            acc.assign (len, 0.0f);
            wsum.assign (len, 0.0f);
            for (int t = 0; t < dimT; ++t)
            {
                std::fill (spec.begin(), spec.end(), std::complex<float> {});
                for (int f = 0; f < dimF; ++f)
                    spec[(size_t) f] = { y[(((size_t) (2 * c) * dimF + (size_t) f) * dimT) + (size_t) t],
                                         y[(((size_t) (2 * c + 1) * dimF + (size_t) f) * dimT) + (size_t) t] };
                fft.inverseReal (spec.data(), frame.data());
                for (int i = 0; i < nfft; ++i)
                {
                    acc[(size_t) t * kHop + (size_t) i] += frame[(size_t) i] * win[(size_t) i];
                    wsum[(size_t) t * kHop + (size_t) i] += win[(size_t) i] * win[(size_t) i];
                }
            }
            for (int i = 0; i < gen; ++i)
            {
                const size_t k = (size_t) (padC + trim + i); // remove centring pad, then the trim
                out.channels[(size_t) c][off + (size_t) i] = acc[k] / std::max (wsum[k], 1e-8f) * p.compensate;
            }
        }
        progress ((double) (ci + 1) / (double) numChunks, label + " " + std::to_string ((int) (100.0 * (ci + 1) / numChunks)) + "%");
    }
    for (auto& c : out.channels)
        c.resize (N);
    return out;
}
} // namespace vb
