// Runs the VocalEngine helper process on a background thread and reports progress.
// Nothing here ever touches the audio thread.
#pragma once
#include <JuceHeader.h>
#include <functional>

namespace vb
{
struct EngineMessage
{
    enum class Kind { progress, result, error, finished } kind;
    double progress = -1.0;
    juce::String text, code;
    juce::var json;
};

class EngineJob : private juce::Thread
{
public:
    using Callback = std::function<void (const EngineMessage&)>;

    EngineJob();
    ~EngineJob() override;

    // Returns false if a job is already running.
    bool start (const juce::File& engineExe, const juce::StringArray& args, const juce::String& label, Callback onMessage);
    void cancel();
    bool isRunning() const { return isThreadRunning(); }
    juce::String currentLabel() const;
    double currentProgress() const { return progress.load(); }
    juce::String currentMessage() const;

    static juce::File findEngine();
    static juce::File findModelsDir();

private:
    void run() override;
    void post (EngineMessage m);

    juce::File exe;
    juce::StringArray arguments;
    juce::String label;
    Callback callback;
    std::atomic<double> progress { 0.0 };
    std::atomic<bool> cancelRequested { false };
    mutable juce::CriticalSection lock;
    juce::String message;
    juce::ChildProcess process;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace vb
