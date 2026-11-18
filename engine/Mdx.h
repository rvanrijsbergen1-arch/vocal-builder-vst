// MDX-Net (UVR) separation models run with ONNX Runtime.
// Used for vocal extraction (Kim_Vocal_2 / UVR-MDX-NET-Voc_FT), lead/backing split (UVR_MDXNET_KARA_2)
// and de-reverb (Reverb_HQ_By_FoxJoy). Each model ships as <name>.onnx plus <name>.json:
//   {"n_fft": 7680, "dim_f": 3072, "dim_t": 8, "compensate": 1.009, "primary": "Vocals"}
// The inference mirrors UVR / audio-separator: complex STFT (hop 1024, centred, Hann), the real and
// imaginary parts of both channels as 4 input planes, lowest 3 bins zeroed, chunked with n_fft/2 trim.
#pragma once
#include "AudioIO.h"
#include <filesystem>
#include <functional>
#include <string>

namespace vb
{
struct MdxParams
{
    int nfft = 7680, dimF = 3072, dimTExp = 8;
    float compensate = 1.0f;
    std::string primary;
};

bool mdxModelAvailable (const std::filesystem::path& onnxFile);
MdxParams loadMdxParams (const std::filesystem::path& onnxFile);

// Runs the model on 44.1 kHz stereo audio and returns its primary stem (same length, compensated).
Audio mdxPrimaryStem (const Audio& stereo44k, const std::filesystem::path& onnxFile, int threads,
                      const std::function<void (double, const std::string&)>& progress, const std::string& label);
} // namespace vb
