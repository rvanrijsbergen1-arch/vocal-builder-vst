#include "EngineJob.h"

namespace vb
{
EngineJob::EngineJob() : juce::Thread ("VocalBuilder engine job") {}

EngineJob::~EngineJob()
{
    alive->store (false);
    cancel();
    stopThread (4000);
}

juce::File EngineJob::findEngine()
{
    const juce::String exeName =
#if JUCE_WINDOWS
        "VocalEngine.exe";
#else
        "VocalEngine";
#endif
    juce::Array<juce::File> candidates;
    if (auto env = juce::SystemStats::getEnvironmentVariable ("VB_ENGINE_DIR", {}); env.isNotEmpty())
        candidates.add (juce::File (env).getChildFile (exeName));
#if JUCE_WINDOWS
    auto installDir = juce::WindowsRegistry::getValue ("HKEY_LOCAL_MACHINE\\Software\\Vocal Builder\\InstallDir");
    if (installDir.isNotEmpty())
        candidates.add (juce::File (installDir).getChildFile (exeName));
    candidates.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Vocal Builder").getChildFile (exeName));
#endif
    // Next to the plugin binary (development builds).
    candidates.add (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile (exeName));
    for (auto& c : candidates)
        if (c.existsAsFile())
            return c;
    return {};
}

juce::File EngineJob::findModelsDir()
{
    if (auto env = juce::SystemStats::getEnvironmentVariable ("VB_MODELS_DIR", {}); env.isNotEmpty())
        return juce::File (env);
    auto engine = findEngine();
    return engine.existsAsFile() ? engine.getSiblingFile ("models") : juce::File();
}

bool EngineJob::start (const juce::File& engineExe, const juce::StringArray& args, const juce::String& jobLabel, Callback onMessage)
{
    if (isThreadRunning())
        return false;
    exe = engineExe;
    arguments = args;
    label = jobLabel;
    callback = std::move (onMessage);
    progress = 0.0;
    cancelRequested = false;
    {
        const juce::ScopedLock sl (lock);
        message = "Starting…";
    }
    startThread (juce::Thread::Priority::low);
    return true;
}

void EngineJob::cancel()
{
    cancelRequested = true;
    if (process.isRunning())
        process.kill();
}

juce::String EngineJob::currentLabel() const { return label; }

juce::String EngineJob::currentMessage() const
{
    const juce::ScopedLock sl (lock);
    return message;
}

void EngineJob::post (EngineMessage m)
{
    if (m.kind == EngineMessage::Kind::progress)
    {
        if (m.progress >= 0)
            progress = m.progress;
        const juce::ScopedLock sl (lock);
        message = m.text;
    }
    auto flag = alive;
    auto cb = callback;
    juce::MessageManager::callAsync ([flag, cb, m] {
        if (flag->load() && cb)
            cb (m);
    });
}

void EngineJob::run()
{
    if (! exe.existsAsFile())
    {
        post ({ EngineMessage::Kind::error, -1, "The Vocal Builder engine is not installed. Run the installer again.", "ENGINE_MISSING", {} });
        post ({ EngineMessage::Kind::finished });
        return;
    }
    juce::StringArray cmd;
    cmd.add (exe.getFullPathName());
    cmd.addArray (arguments);
    if (! process.start (cmd, juce::ChildProcess::wantStdOut))
    {
        post ({ EngineMessage::Kind::error, -1, "The processing engine could not be started.", "ENGINE_START", {} });
        post ({ EngineMessage::Kind::finished });
        return;
    }

    bool gotFinal = false;
    juce::String pending;
    char buffer[4096];
    while (! threadShouldExit())
    {
        const int n = process.readProcessOutput (buffer, sizeof (buffer));
        if (n <= 0)
        {
            if (! process.isRunning())
                break;
            juce::Thread::sleep (20);
            continue;
        }
        pending += juce::String::fromUTF8 (buffer, n);
        int nl;
        while ((nl = pending.indexOfChar ('\n')) >= 0)
        {
            const auto line = pending.substring (0, nl).trimEnd();
            pending = pending.substring (nl + 1);
            if (line.startsWith ("@progress "))
            {
                auto rest = line.substring (10);
                post ({ EngineMessage::Kind::progress, rest.upToFirstOccurrenceOf (" ", false, false).getDoubleValue(),
                        rest.fromFirstOccurrenceOf (" ", false, false), {}, {} });
            }
            else if (line.startsWith ("@result "))
            {
                gotFinal = true;
                post ({ EngineMessage::Kind::result, 1.0, {}, {}, juce::JSON::parse (line.substring (8)) });
            }
            else if (line.startsWith ("@error "))
            {
                gotFinal = true;
                auto rest = line.substring (7);
                post ({ EngineMessage::Kind::error, -1, rest.fromFirstOccurrenceOf (" ", false, false),
                        rest.upToFirstOccurrenceOf (" ", false, false), {} });
            }
        }
    }
    if (process.isRunning())
        process.kill();

    if (cancelRequested)
        post ({ EngineMessage::Kind::error, -1, "Cancelled. Nothing was changed.", "CANCELLED", {} });
    else if (! gotFinal)
        post ({ EngineMessage::Kind::error, -1,
                "The processing engine stopped unexpectedly (exit code " + juce::String ((int) process.getExitCode()) + "). Your DAW session is not affected; try a shorter section.",
                "ENGINE_CRASHED", {} });
    post ({ EngineMessage::Kind::finished });
}
} // namespace vb
