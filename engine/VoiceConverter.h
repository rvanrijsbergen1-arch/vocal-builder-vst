#pragma once
#include "AudioIO.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace vb
{
// A voice package: <models>/voices/<id>/voice.json + synth.onnx (+ optional index.f32).
struct VoiceInfo
{
    std::string id, name, description, licence, encoder;
    std::vector<std::string> languages, tags;
    int sampleRate = 40000, hop = 400, featureDim = 768;
    bool hasIndex = false, testOnly = false;
    std::filesystem::path dir;
};

// Voices are read from <models>/voices (installed with the product) and, if given, from a
// per-user folder (voices the user added themselves, e.g. trained from their own vocals).
std::vector<VoiceInfo> listVoices (const std::filesystem::path& modelsDir, bool includeTestOnly,
                                   const std::filesystem::path& userVoicesDir = {});

struct ConversionSettings
{
    double pitchShift = 0.0;   // semitones, applied to the detected melody
    double indexRate = 0.0;    // 0..1: how much the voice's own timbre features replace the source (needs an index)
    double protect = 0.33;     // 0..0.5: keeps breathy/unvoiced consonants closer to the source (needs an index)
    double dynamics = 1.0;     // 0..1: 0 = follow the source loudness envelope, 1 = model's own envelope
    unsigned seed = 1234;
};

// Converts a (dry, mono or stereo) vocal; returns mono audio at the source sample rate with the
// exact same length as the input, so it stays aligned with the original timeline.
Audio convertVoice (const Audio& vocal, const VoiceInfo& voice, const std::filesystem::path& modelsDir,
                    const ConversionSettings& settings, int threads,
                    const std::function<void (double, const std::string&)>& progress);
} // namespace vb
