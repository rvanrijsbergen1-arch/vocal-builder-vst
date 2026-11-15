// Headless end-to-end test of the plugin's workflow code (the same processor class that ships in
// the VST3): import -> extract section -> convert -> preview through processBlock -> export.
// Uses the real VocalEngine and real models given by VB_ENGINE_DIR / VB_MODELS_DIR.
// The conversion step uses a voice flagged test_only (allowed only in this test build).
#include "../plugin/PluginProcessor.h"
#include <JuceHeader.h>

using namespace vb;

static void setEnv (const char* k, const char* v)
{
#ifdef _WIN32
    _putenv_s (k, v);
#else
    setenv (k, v, 1);
#endif
}

static int failures = 0;
#define CHECK(cond, what)                                                       \
    do                                                                          \
    {                                                                           \
        if (cond)                                                               \
            std::printf ("  ok   %s\n", what);                                  \
        else                                                                    \
        {                                                                       \
            std::printf ("  FAIL %s  (line %d)\n", what, __LINE__);             \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void pumpUntil (std::function<bool()> done, int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! done() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
}

static void waitJob (VocalBuilderProcessor& p, int timeoutMs = 600000)
{
    pumpUntil ([&] { return ! p.job.isRunning(); }, timeoutMs);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300); // deliver final callbacks
}

static juce::AudioBuffer<float> readFile (const juce::File& f, double& sr, int& bits)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    juce::AudioBuffer<float> b;
    if (r == nullptr)
        return b;
    sr = r->sampleRate;
    bits = (int) r->bitsPerSample;
    b.setSize ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    return b;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2)
    {
        std::printf ("usage: PluginWorkflowTest <song.wav>\n");
        return 2;
    }
    const juce::File song (juce::String::fromUTF8 (argv[1]));

    auto p = std::make_unique<VocalBuilderProcessor>();
    p->setPlayConfigDetails (2, 2, 48000.0, 512);
    p->prepareToPlay (48000.0, 512); // host rate differs from the 44.1 kHz source on purpose

    std::printf ("Licence: %s\n", p->licence.statusText().toRawUTF8());
    CHECK (p->licence.canProcess(), "licence verified (signed key + machine activation)");

    p->refreshVoicesAsync();
    pumpUntil ([&] { return ! p->voicesLoading; }, 20000);
    CHECK (p->separationModelInstalled, "engine reports separation model installed");
    CHECK (p->voices.size() >= 1, "test voice visible in test build");

    CHECK (p->importFile (juce::File ("/nonexistent/file.txt"), false).isNotEmpty(), "rejects unsupported file");
    CHECK (p->importFile (song, false).isEmpty(), "imports WAV");
    const auto originalHash = juce::SHA256 (song).toHexString();

    // Extract a 10 s section.
    p->setRegion (5.0, 15.0);
    p->processSection (false);
    waitJob (*p);
    CHECK (p->session.extracted, "section extracted");
    CHECK (p->lastError.isEmpty(), "no error during extraction");
    double sr = 0;
    int bits = 0;
    auto voc = readFile (p->session.vocalsFile(), sr, bits);
    CHECK (voc.getNumSamples() == (int) std::llround (10.0 * sr) && sr == 44100.0, "vocal stem is exactly the section length at the source rate");
    CHECK (p->session.hasInstrumental, "instrumental produced");

    // Convert.
    if (! p->voices.isEmpty())
    {
        p->convert (p->voices[0], 2.0, 0.0, 0.33, 1.0);
        waitJob (*p);
        CHECK (p->session.conversions.size() == 1, "conversion finished");
        auto conv = readFile (p->session.conversions[0].file, sr, bits);
        CHECK (conv.getNumSamples() == voc.getNumSamples(), "converted vocal has identical length (timeline aligned)");
    }

    // Optional UI snapshot of the real editor in this state.
    if (argc > 2)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        ed->setSize (940, 1400);
        pumpUntil ([] { return false; }, 1500);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File out (juce::String::fromUTF8 (argv[2]));
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
        CHECK (out.getSize() > 0, "editor snapshot written");
    }

    // Preview through processBlock (as the DAW would call it).
    pumpUntil ([&] { return p->previewLengthSeconds() > 9.0; }, 20000);
    CHECK (std::abs (p->previewLengthSeconds() - 10.0) < 0.01, "preview loaded and resampled to host rate");
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    p->setPlaying (true);
    float peakA = 0, peakB = 0;
    for (int i = 0; i < 300; ++i)
    {
        block.clear();
        p->processBlock (block, midi);
        peakA = std::max (peakA, block.getMagnitude (0, 512));
    }
    p->playB = true;
    for (int i = 0; i < 300; ++i)
    {
        block.clear();
        p->processBlock (block, midi);
        peakB = std::max (peakB, block.getMagnitude (0, 512));
    }
    CHECK (peakA > 1e-4f, "A (original vocal) audible in processBlock");
    CHECK (p->session.conversions.isEmpty() || peakB > 1e-6f, "B (converted) rendered in processBlock");
    p->stopPlayback();

    // Export aligned to song start, 48 kHz / 24-bit.
    juce::File exported;
    juce::String note;
    const juce::String src = p->session.conversions.isEmpty() ? juce::String ("vocals") : p->session.conversions[0].id;
    p->exportAudio (src, 48000, "24", true, [&] (juce::File f, juce::String n) { exported = f; note = n; });
    waitJob (*p);
    auto ex = readFile (exported, sr, bits);
    std::printf ("  export: %s\n", exported.getFileName().toRawUTF8());
    CHECK (exported.existsAsFile() && sr == 48000.0 && bits == 24, "aligned export is 48 kHz / 24-bit WAV");
    CHECK (std::abs (ex.getNumSamples() - (int) std::llround (p->session.duration * 48000.0)) <= 1, "aligned export spans the full song length");
    CHECK (ex.getMagnitude (0, 0, (int) (4.99 * 48000)) == 0.0f, "silence before the section start");
    CHECK (ex.getMagnitude (0, (int) (5.0 * 48000), 48000 * 9) > 1e-5f, "audio inside the section");
    CHECK (note.contains ("higher sample rate"), "warns that upsampling adds no detail");

    p->exportAudio ("vocals", 44100, "16", false, [&] (juce::File f, juce::String) { exported = f; });
    waitJob (*p);
    auto ex2 = readFile (exported, sr, bits);
    std::printf ("  export: %s\n", exported.getFileName().toRawUTF8());
    CHECK (ex2.getNumSamples() == (int) std::llround (10.0 * 44100) && bits == 16, "section-only export, 16-bit, exact length");
    CHECK (exported.getFileName().contains ("Region_from-0m05-000s"), "section start is in the file name");

    // Cancel a running job.
    p->processSection (true);
    juce::Thread::sleep (300);
    p->cancelJob();
    waitJob (*p, 20000);
    CHECK (p->lastInfo.contains ("Cancelled"), "cancellation reported");

    // Corrupt model file -> clean error, no crash.
    auto badModels = juce::File::createTempFile ("models");
    badModels.createDirectory();
    badModels.getChildFile ("separation").createDirectory();
    badModels.getChildFile ("separation/spleeter_2stems.onnx").replaceWithText ("not a model");
    const auto goodModels = juce::SystemStats::getEnvironmentVariable ("VB_MODELS_DIR", {});
    setEnv ("VB_MODELS_DIR", badModels.getFullPathName().toRawUTF8());
    p->processSection (false);
    waitJob (*p, 60000);
    std::printf ("  error shown: %s\n", p->lastError.toRawUTF8());
    CHECK (p->lastError.contains ("could not be loaded"), "corrupt model reported clearly");
    badModels.deleteRecursively();

    CHECK (juce::SHA256 (song).toHexString() == originalHash, "original file unchanged");

    // Second instance works independently.
    auto p2 = std::make_unique<VocalBuilderProcessor>();
    CHECK (p2->session.dir != p->session.dir, "each instance has its own working folder");

    // Destroying a processor while a job runs must not hang or crash.
    setEnv ("VB_MODELS_DIR", goodModels.toRawUTF8());
    p->processSection (true);
    juce::Thread::sleep (200);
    p.reset();
    CHECK (true, "processor destroyed during processing without crash");

    std::printf ("%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
