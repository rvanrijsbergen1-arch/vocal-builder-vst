// VocalEngine — local, offline processing helper started by the Vocal Builder plugin.
// It never opens network connections. All models are read from the installed models folder.
//
//   VocalEngine info     --models <dir>
//   VocalEngine separate --models <dir> --in <audio> --out-dir <dir> [--start s --end e]
//   VocalEngine convert  --models <dir> --in <vocal.wav> --out <file.wav> --voice <id>
//                        [--pitch st] [--index-rate x] [--protect x] [--dynamics x]
//   VocalEngine copy     --in <audio> --out <file.wav> [--start s --end e]   (isolated-vocal import)
#include "AudioIO.h"
#include "LicenceManager.h"
#include "Dsp.h"
#include "Protocol.h"
#include "Separator.h"
#include "VoiceConverter.h"
#include <chrono>
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

#ifndef VB_VERSION
#define VB_VERSION "0.0.0"
#endif

using namespace vb;
namespace fs = std::filesystem;

namespace
{
// Command-line arguments as UTF-8 (on Windows, read the wide command line so paths with
// accents or non-Latin characters survive).
std::vector<std::string> utf8Args (int argc, char** argv)
{
    std::vector<std::string> out;
#ifdef _WIN32
    (void) argc;
    (void) argv;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW (GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i)
        out.push_back (juce::String (w[i]).toStdString());
    LocalFree (w);
#else
    for (int i = 0; i < argc; ++i)
        out.emplace_back (argv[i]);
#endif
    return out;
}

std::map<std::string, std::string> parseArgs (const std::vector<std::string>& args)
{
    std::map<std::string, std::string> m;
    for (size_t i = 2; i < args.size(); ++i)
    {
        const std::string& k = args[i];
        if (k.rfind ("--", 0) == 0)
        {
            const std::string v = (i + 1 < args.size() && args[i + 1].rfind ("--", 0) != 0) ? args[++i] : "1";
            m[k.substr (2)] = v;
        }
    }
    return m;
}

std::string need (const std::map<std::string, std::string>& a, const std::string& k)
{
    auto it = a.find (k);
    if (it == a.end() || it->second.empty())
        throw EngineError { "BAD_ARGS", "Missing --" + k };
    return it->second;
}

double num (const std::map<std::string, std::string>& a, const std::string& k, double def)
{
    auto it = a.find (k);
    return it == a.end() ? def : std::stod (it->second);
}

int threadCount (const std::map<std::string, std::string>& a)
{
    if (a.count ("threads"))
        return std::max (1, std::stoi (a.at ("threads")));
    // Leave one core for the DAW.
    return (int) std::max (1u, std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 1 : 1u);
}

fs::path pathArg (const std::string& s) { return fs::u8path (s); }

std::string jsonStr (const std::string& s) { return juce::JSON::toString (juce::var (juce::String (s)), true).toStdString(); }

std::string audioJson (const fs::path& p, const Audio& a)
{
    const auto mono = toMono (a);
    return "{\"path\":" + jsonStr (p.u8string()) + ",\"sample_rate\":" + std::to_string ((int) a.sampleRate) + ",\"channels\":" + std::to_string (a.numChannels()) + ",\"samples\":" + std::to_string (a.numSamples()) + ",\"rms_db\":" + std::to_string (dsp::activeRmsDb (mono.data(), mono.size())) + "}";
}

size_t peakRssMb()
{
#ifdef _WIN32
    return 0; // reported by the plugin's own measurement on Windows
#else
    juce::File status ("/proc/self/status");
    for (auto& line : juce::StringArray::fromLines (status.loadFileAsString()))
        if (line.startsWith ("VmHWM:"))
            return (size_t) line.fromFirstOccurrenceOf (":", false, false).trim().getLargeIntValue() / 1024;
    return 0;
#endif
}

auto progressFn() { return [] (double f, const std::string& m) { sendProgress (f, m); }; }

int run (const std::string& cmd, const std::map<std::string, std::string>& a)
{
    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(); };

    if (cmd == "info")
    {
        const auto models = pathArg (need (a, "models"));
        std::string voices = "[";
        for (auto& v : listVoices (models, a.count ("include-test-voices") > 0))
        {
            if (voices.size() > 1)
                voices += ",";
            std::string langs = "[", tags = "[";
            for (auto& l : v.languages)
                langs += (langs.size() > 1 ? "," : "") + jsonStr (l);
            for (auto& t : v.tags)
                tags += (tags.size() > 1 ? "," : "") + jsonStr (t);
            voices += "{\"id\":" + jsonStr (v.id) + ",\"name\":" + jsonStr (v.name) + ",\"description\":" + jsonStr (v.description) + ",\"licence\":" + jsonStr (v.licence) + ",\"languages\":" + langs + "],\"tags\":" + tags + "],\"has_index\":" + (v.hasIndex ? "true" : "false") + ",\"test_only\":" + (v.testOnly ? "true" : "false") + "}";
        }
        voices += "]";
        const bool sep = fs::exists (models / "separation" / "spleeter_2stems.onnx");
        const bool pitch = fs::exists (models / "pitch" / "fcpe.onnx");
        sendResult (std::string ("{\"version\":\"") + VB_VERSION + "\",\"separation_model\":" + (sep ? "true" : "false") + ",\"pitch_model\":" + (pitch ? "true" : "false") + ",\"voices\":" + voices + ",\"threads\":" + std::to_string (threadCount (a)) + "}");
        return 0;
    }

    if (cmd == "licence-status")
    {
        vb::LicenceManager lm;
        sendResult ("{\"state\":" + jsonStr (lm.statusText().toStdString()) + ",\"request\":" + jsonStr (lm.activationRequest().toStdString()) + "}");
        return 0;
    }

    // The engine checks the same signed licence as the plugin, so it cannot be used on its own.
    if (cmd != "info")
    {
        vb::LicenceManager lm;
        if (! lm.canProcess())
            throw EngineError { "NOT_LICENSED", "This computer is not activated. Open the plugin and activate your licence." };
    }

    if (cmd == "copy")
    {
        auto in = readAudio (pathArg (need (a, "in")), num (a, "start", 0), num (a, "end", -1));
        const auto out = pathArg (need (a, "out"));
        writeWavFloat (out, in);
        sendResult ("{\"vocals\":" + audioJson (out, in) + "}");
        return 0;
    }

    if (cmd == "separate")
    {
        const auto models = pathArg (need (a, "models"));
        const auto outDir = pathArg (need (a, "out-dir"));
        fs::create_directories (outDir);
        sendProgress (0.0, "Reading audio…");
        auto mix = readAudio (pathArg (need (a, "in")), num (a, "start", 0), num (a, "end", -1));
        writeWavFloat (outDir / "input.wav", mix);
        auto r = separate (mix, models / "separation" / "spleeter_2stems.onnx", threadCount (a), progressFn());
        sendProgress (0.97, "Saving stems…");
        writeWavFloat (outDir / "vocals.wav", r.vocals);
        writeWavFloat (outDir / "instrumental.wav", r.instrumental);
        sendResult ("{\"input\":" + audioJson (outDir / "input.wav", mix) + ",\"vocals\":" + audioJson (outDir / "vocals.wav", r.vocals) + ",\"instrumental\":" + audioJson (outDir / "instrumental.wav", r.instrumental) + ",\"seconds\":" + std::to_string (elapsed()) + ",\"audio_seconds\":" + std::to_string (mix.seconds()) + ",\"peak_rss_mb\":" + std::to_string (peakRssMb()) + "}");
        return 0;
    }

    if (cmd == "convert")
    {
        const auto models = pathArg (need (a, "models"));
        const auto id = need (a, "voice");
        const auto voices = listVoices (models, a.count ("include-test-voices") > 0);
        auto it = std::find_if (voices.begin(), voices.end(), [&] (auto& v) { return v.id == id; });
        if (it == voices.end())
            throw EngineError { "VOICE_MISSING", "The selected voice is not installed." };
        ConversionSettings s;
        s.pitchShift = std::clamp (num (a, "pitch", 0), -24.0, 24.0);
        s.indexRate = it->hasIndex ? std::clamp (num (a, "index-rate", 0.5), 0.0, 1.0) : 0.0;
        s.protect = std::clamp (num (a, "protect", 0.33), 0.0, 0.5);
        s.dynamics = std::clamp (num (a, "dynamics", 1.0), 0.0, 1.0);
        sendProgress (0.0, "Reading vocal…");
        auto vocal = readAudio (pathArg (need (a, "in")));
        auto out = convertVoice (vocal, *it, models, s, threadCount (a), progressFn());
        const auto outPath = pathArg (need (a, "out"));
        writeWavFloat (outPath, out);
        sendResult ("{\"converted\":" + audioJson (outPath, out) + ",\"voice\":" + jsonStr (it->id) + ",\"seconds\":" + std::to_string (elapsed()) + ",\"audio_seconds\":" + std::to_string (vocal.seconds()) + ",\"peak_rss_mb\":" + std::to_string (peakRssMb()) + "}");
        return 0;
    }

    if (cmd == "export")
    {
        // Final WAV for the DAW: resample, optional leading silence (timeline alignment), bit depth.
        auto in = readAudio (pathArg (need (a, "in")));
        const double rate = num (a, "rate", 48000);
        if (rate != 44100.0 && rate != 48000.0)
            throw EngineError { "BAD_ARGS", "Sample rate must be 44100 or 48000." };
        const std::string bits = a.count ("bits") ? a.at ("bits") : "24";
        const int bitDepth = bits == "16" ? 16 : bits == "32f" ? 32 : 24;
        const double lead = std::max (0.0, num (a, "lead", 0));
        const double total = num (a, "total", -1);
        Audio out;
        out.sampleRate = rate;
        const size_t leadN = (size_t) std::llround (lead * rate);
        float peak = 0;
        for (auto& c : in.channels)
        {
            auto r = dsp::resample (c, in.sampleRate, rate);
            std::vector<float> y (leadN, 0.0f);
            y.insert (y.end(), r.begin(), r.end());
            if (total > 0)
                y.resize ((size_t) std::llround (total * rate), 0.0f);
            for (float v : y)
                peak = std::max (peak, std::abs (v));
            out.channels.push_back (std::move (y));
        }
        const bool clipped = bitDepth != 32 && peak > 1.0f;
        const auto outPath = pathArg (need (a, "out"));
        {
            juce::File target (juce::String::fromUTF8 (outPath.u8string().c_str()));
            auto tmp = target.getSiblingFile (target.getFileName() + ".part");
            tmp.deleteFile();
            std::unique_ptr<juce::FileOutputStream> os (tmp.createOutputStream());
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (os ? wav.createWriterFor (os.get(), rate, (unsigned) out.numChannels(), bitDepth, {}, 0) : nullptr);
            if (w == nullptr)
                throw EngineError { "WRITE_FAILED", "Could not write the export file." };
            os.release();
            std::vector<const float*> ptrs;
            std::vector<std::vector<float>> clippedCopies;
            for (auto& c : out.channels)
            {
                if (bitDepth == 32)
                {
                    ptrs.push_back (c.data());
                    continue;
                }
                auto& cc = clippedCopies.emplace_back (c);
                for (auto& v : cc)
                    v = std::clamp (v, -1.0f, 1.0f);
            }
            for (auto& cc : clippedCopies)
                ptrs.push_back (cc.data());
            w->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) out.numSamples());
            w.reset();
            if (! tmp.moveFileTo (target))
                throw EngineError { "WRITE_FAILED", "Could not finalise the export file (is it open in another program?)." };
        }
        std::string note;
        if (rate > in.sampleRate + 1)
            note += "Exported at a higher sample rate than the source; this does not add detail. ";
        if (clipped)
            note += "Peaks above 0 dBFS were clipped; choose 32-bit float to keep them. ";
        sendResult ("{\"out\":" + jsonStr (outPath.u8string()) + ",\"frames\":" + std::to_string (out.numSamples()) + ",\"lead_seconds\":" + std::to_string (lead) + ",\"note\":" + jsonStr (note) + "}");
        return 0;
    }

    throw EngineError { "BAD_ARGS", "Unknown command: " + cmd };
}
} // namespace

int main (int argc, char** argv)
{
#ifdef _WIN32
    // Keep the DAW responsive: run below normal priority.
    SetPriorityClass (GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    const auto args = utf8Args (argc, argv);
    if (args.size() < 2)
    {
        std::printf ("VocalEngine %s\nUsage: VocalEngine <info|separate|convert|copy> [options]\n", VB_VERSION);
        return 2;
    }
    try
    {
        return run (args[1], parseArgs (args));
    }
    catch (const EngineError& e)
    {
        sendError (e.code, e.message);
    }
    catch (const std::bad_alloc&)
    {
        sendError ("OUT_OF_MEMORY", "Not enough memory. Close other programs or process a shorter section.");
    }
    catch (const std::exception& e)
    {
        sendError ("INTERNAL", e.what());
    }
    return 1;
}
