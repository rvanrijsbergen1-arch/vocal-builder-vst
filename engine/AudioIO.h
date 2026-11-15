#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace vb
{
struct Audio
{
    double sampleRate = 44100.0;
    std::vector<std::vector<float>> channels; // [channel][sample]
    size_t numSamples() const { return channels.empty() ? 0 : channels[0].size(); }
    int numChannels() const { return (int) channels.size(); }
    double seconds() const { return sampleRate > 0 ? (double) numSamples() / sampleRate : 0.0; }
};

// Decodes WAV/MP3/AIFF/FLAC. Optional region [startSec, endSec); endSec <= 0 means to the end.
Audio readAudio (const std::filesystem::path& file, double startSec = 0.0, double endSec = -1.0, double maxSeconds = 1800.0);

// Writes 32-bit float WAV atomically (temp file + rename) so a cancelled job never leaves half a file.
void writeWavFloat (const std::filesystem::path& file, const Audio& audio);

Audio toStereo44k (const Audio& in);
std::vector<float> toMono (const Audio& in);
} // namespace vb
