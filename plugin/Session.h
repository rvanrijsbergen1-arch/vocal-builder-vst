// Per-plugin-instance working state. Each instance gets its own folder, so several
// instances in one FL Studio project never share or overwrite files.
#pragma once
#include <JuceHeader.h>

namespace vb
{
struct Conversion
{
    juce::String id, voiceId, voiceName;
    double pitch = 0, indexRate = 0, protect = 0.33, dynamics = 1;
    double rmsDb = -100;
    juce::File file;
};

struct Session
{
    juce::File dir;              // .../VocalBuilder/sessions/<uuid>
    juce::File source;           // the user's original file (read-only, never modified)
    bool isolatedVocal = false;
    double duration = 0, sourceRate = 0;
    int sourceChannels = 0;

    // Current section (seconds in the original's timeline). full == whole file.
    double regionStart = 0, regionEnd = 0;
    bool regionFull = true;
    juce::String segmentKey;     // folder name of the processed section
    bool extracted = false, hasInstrumental = false, hasBacking = false, leadOnly = false, dereverb = false;
    juce::String separationEngine;
    double rmsVocal = -100, rmsInstrumental = -100;
    juce::Array<Conversion> conversions;

    juce::File segmentDir() const { return dir.getChildFile ("segments").getChildFile (segmentKey); }
    juce::File vocalsFile() const { return segmentDir().getChildFile ("vocals.wav"); }
    juce::File instrumentalFile() const { return segmentDir().getChildFile ("instrumental.wav"); }
    juce::File backingFile() const { return segmentDir().getChildFile ("backing.wav"); }
    juce::File exportsDir() const { return dir.getChildFile ("exports"); }

    static juce::File rootDir();
    static Session createNew();
    juce::var toVar() const;
    static Session fromVar (const juce::var& v);
    void save() const;
    static juce::String keyFor (double start, double end, bool full);
};
} // namespace vb
