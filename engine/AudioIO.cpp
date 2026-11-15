#include "AudioIO.h"
#include "Dsp.h"
#include "Protocol.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace vb
{
static juce::File toJuce (const std::filesystem::path& p)
{
#ifdef _WIN32
    return juce::File (juce::String (p.wstring().c_str()));
#else
    return juce::File (juce::String::fromUTF8 (p.string().c_str()));
#endif
}

Audio readAudio (const std::filesystem::path& file, double startSec, double endSec, double maxSeconds)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    auto jf = toJuce (file);
    if (! jf.existsAsFile())
        throw EngineError { "FILE_NOT_FOUND", "Audio file not found." };
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (jf));
    if (reader == nullptr)
        throw EngineError { "BAD_AUDIO", "This file could not be decoded. Use a WAV or MP3 file." };
    if (reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
        throw EngineError { "BAD_AUDIO", "The file contains no audio." };

    const double sr = reader->sampleRate;
    const auto total = reader->lengthInSamples;
    auto start = (juce::int64) std::llround (std::max (0.0, startSec) * sr);
    auto end = endSec > 0 ? (juce::int64) std::llround (endSec * sr) : total;
    end = std::min (end, total);
    if (end - start < (juce::int64) (0.5 * sr))
        throw EngineError { "BAD_REGION", "The selected region is shorter than 0.5 seconds." };
    if ((double) (end - start) / sr > maxSeconds)
        throw EngineError { "TOO_LONG", "Audio longer than 30 minutes is not supported." };

    const int nch = (int) std::min<unsigned> (reader->numChannels, 2);
    Audio a;
    a.sampleRate = sr;
    const auto len = (size_t) (end - start);
    try
    {
        a.channels.assign ((size_t) nch, std::vector<float> (len));
    }
    catch (const std::bad_alloc&)
    {
        throw EngineError { "OUT_OF_MEMORY", "Not enough memory to load this audio." };
    }
    juce::AudioBuffer<float> buf (nch, 1 << 16);
    for (size_t pos = 0; pos < len;)
    {
        const int n = (int) std::min<size_t> (1 << 16, len - pos);
        reader->read (&buf, 0, n, start + (juce::int64) pos, true, nch > 1);
        for (int c = 0; c < nch; ++c)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + n, a.channels[(size_t) c].begin() + (long) pos);
        pos += (size_t) n;
    }
    return a;
}

void writeWavFloat (const std::filesystem::path& file, const Audio& audio)
{
    auto target = toJuce (file);
    auto tmp = target.getSiblingFile (target.getFileName() + ".part");
    tmp.deleteFile();
    {
        std::unique_ptr<juce::FileOutputStream> os (tmp.createOutputStream());
        if (os == nullptr)
            throw EngineError { "WRITE_FAILED", "Could not write " + target.getFullPathName().toStdString() };
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), audio.sampleRate, (unsigned) audio.numChannels(), 32, {}, 0));
        if (w == nullptr)
            throw EngineError { "WRITE_FAILED", "Could not create WAV writer." };
        os.release();
        std::vector<const float*> ptrs;
        for (auto& c : audio.channels)
            ptrs.push_back (c.data());
        w->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) audio.numSamples());
    }
    if (! tmp.moveFileTo (target))
        throw EngineError { "WRITE_FAILED", "Could not finalise " + target.getFullPathName().toStdString() };
}

Audio toStereo44k (const Audio& in)
{
    Audio out;
    out.sampleRate = 44100.0;
    for (int c = 0; c < 2; ++c)
    {
        const auto& src = in.channels[(size_t) std::min (c, in.numChannels() - 1)];
        out.channels.push_back (dsp::resample (src, in.sampleRate, 44100.0));
    }
    return out;
}

std::vector<float> toMono (const Audio& in)
{
    std::vector<float> m (in.numSamples(), 0.0f);
    for (auto& c : in.channels)
        for (size_t i = 0; i < m.size(); ++i)
            m[i] += c[i] / (float) in.numChannels();
    return m;
}
} // namespace vb
