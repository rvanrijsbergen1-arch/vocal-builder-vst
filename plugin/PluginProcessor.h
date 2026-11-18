#pragma once
#include "EngineJob.h"
#include "LicenceManager.h"
#include "Session.h"
#include <JuceHeader.h>

namespace vb
{
struct VoiceEntry
{
    juce::String id, name, description, licence;
    juce::StringArray languages, tags;
    bool hasIndex = false;
};

// Audio for preview, already resampled to the host rate. Swapped in under a SpinLock that the
// audio thread only ever try-locks, so loading never blocks playback.
struct PreviewSet
{
    juce::AudioBuffer<float> a, b, inst; // A = original vocal, B = converted, inst = instrumental
    float gainA = 1, gainB = 1;          // loudness-matching gains (B matched to A)
    bool hasB = false, hasInst = false;
    int length = 0;
};

class VocalBuilderProcessor : public juce::AudioProcessor, public juce::ChangeBroadcaster
{
public:
    VocalBuilderProcessor();
    ~VocalBuilderProcessor() override;

    // --- AudioProcessor -----------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vocal Builder"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- workflow (message thread) -------------------------------------------------------
    juce::String importFile (const juce::File& f, bool isolatedVocal); // returns error text or empty
    void setRegion (double start, double end);
    void processSection (bool fullSong);   // extract (or copy, for isolated vocals)
    void convert (const VoiceEntry& voice, double pitch, double indexRate, double protect, double dynamics, double tune);
    std::atomic<bool> leadOnly { true }, dereverb { false }; // extraction options (UI)
    void exportAudio (const juce::String& source, int sampleRate, const juce::String& bits, bool alignToSongStart,
                      std::function<void (juce::File, juce::String)> done);
    void cancelJob();
    void refreshVoicesAsync();
    juce::String addVoiceFromFolder (const juce::File& voiceJson); // copies a voice package into the user's voices folder
    static juce::File userVoicesDir();
    bool voicesLoading = false;

    // --- preview (message thread controls, audio thread renders) --------------------------
    void selectPreviewConversion (int index);
    void setPlaying (bool p);
    void stopPlayback();
    void seekPreview (double fraction);
    std::atomic<bool> loop { true }, playB { false }, withInstrumental { false }, matchLoudness { true };
    std::atomic<float> volume { 0.8f };
    std::atomic<bool> muteInputWhilePreviewing { true };
    bool isPlaying() const { return playing.load(); }
    double previewPositionSeconds() const;
    double previewLengthSeconds() const;
    int previewConversionIndex() const { return previewConv; }
    float matchDb() const { return lastMatchDb.load(); }

    // --- state for the UI ------------------------------------------------------------------
    Session session;
    LicenceManager licence;
    juce::Array<VoiceEntry> voices;
    juce::String voicesError;
    bool separationModelInstalled = false, mdxInstalled = false, leadBackingAvailable = false, dereverbAvailable = false;
    EngineJob job;
    juce::String lastError, lastInfo;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbCache { 4 };
    juce::AudioThumbnail sourceThumb { 512, formats, thumbCache };
    juce::AudioThumbnail previewThumbA { 512, formats, thumbCache }, previewThumbB { 512, formats, thumbCache };

private:
    struct VoiceScan
    {
        juce::Array<VoiceEntry> voices;
        juce::String error;
        bool separationModel = false, mdx = false, leadBacking = false, dereverb = false;
    };
    VoiceScan scanVoices() const;
    void startJob (const juce::StringArray& args, const juce::String& label, std::function<void (const juce::var&)> onResult);
    void loadPreviewAsync();
    juce::StringArray baseArgs (const juce::String& cmd) const;

    std::atomic<bool> playing { false };
    std::atomic<juce::int64> playPos { 0 };
    std::atomic<double> hostRate { 44100.0 };
    std::atomic<float> lastMatchDb { 0.0f };
    juce::SpinLock previewLock;
    std::unique_ptr<PreviewSet> preview;          // owned; swapped under previewLock
    std::unique_ptr<PreviewSet> retired;          // freed on the message thread
    juce::SmoothedValue<float> sgA, sgB, sgI;
    int previewConv = -1;
    juce::ThreadPool loader { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalBuilderProcessor)
};
} // namespace vb
