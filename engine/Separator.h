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
    Audio vocals, instrumental, backing; // instrumental/backing may be empty (numSamples() == 0)
    std::string engine;                  // which models were used, for the UI
};

SeparationResult separate (const Audio& mix, const std::filesystem::path& modelFile, int threads,
                           const std::function<void (double, const std::string&)>& progress);

struct SeparationOptions
{
    bool isolatedVocal = false; // input is already a vocal: skip vocal/instrumental separation
    bool leadOnly = false;      // remove backing vocals / doubles / harmonies (karaoke model)
    bool dereverb = false;      // remove reverb/echo from the vocal
};

// Best available chain: MDX-Net vocals (falls back to Spleeter), optional lead/backing split and
// de-reverb. Every output has the source's sample rate and exact length.
SeparationResult separateChain (const Audio& mix, const std::filesystem::path& modelsDir, const SeparationOptions& opts,
                                int threads, const std::function<void (double, const std::string&)>& progress);
} // namespace vb
