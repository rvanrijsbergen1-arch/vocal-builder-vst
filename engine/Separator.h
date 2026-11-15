#pragma once
#include "AudioIO.h"
#include <atomic>
#include <filesystem>
#include <functional>

namespace vb
{
// Spleeter 2-stem (vocals / accompaniment) separation, U-Net exported to ONNX.
// STFT 4096/1024 Hann, 512-frame segments, ratio masks (exponent 2), 16 kHz bandwidth (F = 1536).
struct SeparationResult
{
    Audio vocals, instrumental;
};

SeparationResult separate (const Audio& mix, const std::filesystem::path& modelFile, int threads,
                           const std::function<void (double, const std::string&)>& progress);
} // namespace vb
