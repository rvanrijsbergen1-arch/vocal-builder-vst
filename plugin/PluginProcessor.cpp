#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace vb
{
VocalBuilderProcessor::VocalBuilderProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    session = Session::createNew();
    // Voices are discovered when the editor opens (refreshVoicesAsync), never during plugin scanning.
}

VocalBuilderProcessor::~VocalBuilderProcessor()
{
    alive->store (false);
    job.cancel();
    loader.removeAllJobs (true, 5000);
}

bool VocalBuilderProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return l.getMainInputChannelSet().isDisabled() || l.getMainInputChannelSet() == out;
}

void VocalBuilderProcessor::prepareToPlay (double sr, int)
{
    const bool changed = std::abs (hostRate.load() - sr) > 0.5;
    hostRate = sr;
    for (auto* g : { &sgA, &sgB, &sgI })
        g->reset (sr, 0.02);
    if (changed)
        juce::MessageManager::callAsync ([this, flag = alive] { if (flag->load()) loadPreviewAsync(); });
}

// ------------------------------------------------------------------------------- audio thread
void VocalBuilderProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();
    for (int c = getTotalNumInputChannels(); c < nch; ++c)
        buffer.clear (c, 0, n);

    if (! playing.load())
        return;

    const juce::SpinLock::ScopedTryLockType tl (previewLock);
    if (! tl.isLocked() || preview == nullptr || preview->length == 0)
        return;

    if (muteInputWhilePreviewing.load())
        buffer.clear();

    auto& p = *preview;
    const bool b = playB.load() && p.hasB;
    const float vol = volume.load();
    const bool match = matchLoudness.load();
    sgA.setTargetValue (b ? 0.0f : vol);
    sgB.setTargetValue (b ? vol * (match ? p.gainB : 1.0f) : 0.0f);
    sgI.setTargetValue (withInstrumental.load() && p.hasInst ? vol * 0.7f : 0.0f);

    auto pos = playPos.load();
    const bool looping = loop.load();
    for (int i = 0; i < n; ++i)
    {
        if (pos >= p.length)
        {
            if (! looping)
            {
                playing = false;
                pos = 0;
                break;
            }
            pos = 0;
        }
        const float ga = sgA.getNextValue(), gb = sgB.getNextValue(), gi = sgI.getNextValue();
        for (int c = 0; c < nch; ++c)
        {
            float s = 0.0f;
            s += ga * p.a.getSample (juce::jmin (c, p.a.getNumChannels() - 1), (int) pos);
            if (p.hasB)
                s += gb * p.b.getSample (juce::jmin (c, p.b.getNumChannels() - 1), (int) pos);
            if (p.hasInst)
                s += gi * p.inst.getSample (juce::jmin (c, p.inst.getNumChannels() - 1), (int) pos);
            buffer.addSample (c, i, s);
        }
        ++pos;
    }
    playPos = pos;
}

// ------------------------------------------------------------------------------- state
void VocalBuilderProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("session", session.toVar());
    o->setProperty ("loop", loop.load());
    o->setProperty ("match", matchLoudness.load());
    o->setProperty ("volume", volume.load());
    juce::MemoryOutputStream mos (dest, false);
    mos.writeString (juce::JSON::toString (juce::var (o), true));
}

void VocalBuilderProcessor::setStateInformation (const void* data, int size)
{
    juce::MemoryInputStream mis (data, (size_t) size, false);
    auto v = juce::JSON::parse (mis.readString());
    if (! v.isObject())
        return;
    auto s = Session::fromVar (v["session"]);
    if (s.dir.isDirectory())
    {
        session.dir.deleteRecursively(); // the empty session created in the constructor
        session = s;
    }
    loop = (bool) v.getProperty ("loop", true);
    matchLoudness = (bool) v.getProperty ("match", true);
    volume = (float) (double) v.getProperty ("volume", 0.8);
    if (session.source.existsAsFile())
        sourceThumb.setSource (new juce::FileInputSource (session.source));
    loadPreviewAsync();
    sendChangeMessage();
}

juce::AudioProcessorEditor* VocalBuilderProcessor::createEditor() { return new VocalBuilderEditor (*this); }

// ------------------------------------------------------------------------------- engine
juce::StringArray VocalBuilderProcessor::baseArgs (const juce::String& cmd) const
{
    return { cmd, "--models", EngineJob::findModelsDir().getFullPathName(), "--user-voices", userVoicesDir().getFullPathName() };
}

juce::File VocalBuilderProcessor::userVoicesDir() { return Session::rootDir().getChildFile ("voices"); }

juce::String VocalBuilderProcessor::addVoiceFromFolder (const juce::File& voiceJson)
{
    auto j = juce::JSON::parse (voiceJson.loadFileAsString());
    const auto dir = voiceJson.getParentDirectory();
    const auto id = j["id"].toString().retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
    if (! j.isObject() || id.isEmpty() || ! dir.getChildFile ("synth.onnx").existsAsFile())
        return "This is not a Vocal Builder voice (voice.json + synth.onnx expected).";
    auto dest = userVoicesDir().getChildFile (id);
    if (dest == dir)
        return {};
    dest.deleteRecursively();
    if (! dest.createDirectory() || ! dir.copyDirectoryTo (dest))
        return "The voice could not be copied.";
    refreshVoicesAsync();
    return {};
}

void VocalBuilderProcessor::refreshVoicesAsync()
{
    auto flag = alive;
    voicesLoading = true;
    loader.addJob ([this, flag] {
        auto result = std::make_shared<VoiceScan> (scanVoices());
        juce::MessageManager::callAsync ([this, flag, result] {
            if (! flag->load())
                return;
            voices = result->voices;
            voicesError = result->error;
            separationModelInstalled = result->separationModel;
            voicesLoading = false;
            sendChangeMessage();
        });
    });
}

VocalBuilderProcessor::VoiceScan VocalBuilderProcessor::scanVoices() const
{
    VoiceScan scan;
    auto exe = EngineJob::findEngine();
    if (! exe.existsAsFile())
    {
        scan.error = "The Vocal Builder engine is not installed. Run the installer again.";
        return scan;
    }
    juce::ChildProcess p;
    juce::StringArray cmd { exe.getFullPathName() };
    cmd.addArray (baseArgs ("info"));
#if VB_ALLOW_TEST_VOICES // automated tests only; never defined in the shipped plugin
    cmd.add ("--include-test-voices");
#endif
    if (! p.start (cmd, juce::ChildProcess::wantStdOut))
    {
        scan.error = "The processing engine could not be started.";
        return scan;
    }
    const auto out = p.readAllProcessOutput();
    p.waitForProcessToFinish (5000);
    for (auto& line : juce::StringArray::fromLines (out))
        if (line.startsWith ("@result "))
        {
            auto j = juce::JSON::parse (line.substring (8));
            scan.separationModel = j["separation_model"];
            if (auto* arr = j["voices"].getArray())
                for (auto& v : *arr)
                {
                    VoiceEntry e;
                    e.id = v["id"].toString();
                    e.name = v["name"].toString();
                    e.description = v["description"].toString();
                    e.licence = v["licence"].toString();
                    e.hasIndex = v["has_index"];
                    if (auto* l = v["languages"].getArray())
                        for (auto& s : *l)
                            e.languages.add (s.toString());
                    if (auto* t = v["tags"].getArray())
                        for (auto& s : *t)
                            e.tags.add (s.toString());
                    scan.voices.add (e);
                }
        }
        else if (line.startsWith ("@error "))
            scan.error = line.substring (7).fromFirstOccurrenceOf (" ", false, false);
    if (scan.voices.isEmpty() && scan.error.isEmpty())
        scan.error = "No singing voice is installed yet.";
    return scan;
}

void VocalBuilderProcessor::startJob (const juce::StringArray& args, const juce::String& label, std::function<void (const juce::var&)> onResult)
{
    if (! licence.canProcess())
    {
        lastError = "Activate your licence to process audio (Licence button, top right).";
        sendChangeMessage();
        return;
    }
    lastError = lastInfo = {};
    const bool ok = job.start (EngineJob::findEngine(), args, label, [this, onResult] (const EngineMessage& m) {
        if (m.kind == EngineMessage::Kind::result && onResult)
            onResult (m.json);
        else if (m.kind == EngineMessage::Kind::error)
            lastError = m.code == "CANCELLED" ? juce::String() : m.text;
        if (m.kind == EngineMessage::Kind::error && m.code == "CANCELLED")
            lastInfo = m.text;
        sendChangeMessage();
    });
    if (! ok)
        lastError = "Wait for the current step to finish, or cancel it.";
    sendChangeMessage();
}

juce::String VocalBuilderProcessor::importFile (const juce::File& f, bool isolated)
{
    if (job.isRunning())
        return "Wait for the current step to finish, or cancel it.";
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext != ".wav" && ext != ".mp3")
        return "Unsupported file type. Use a WAV or MP3 file.";
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate <= 0)
        return "This file could not be read. It may be damaged or not real audio.";
    const double dur = (double) r->lengthInSamples / r->sampleRate;
    if (dur > 30 * 60)
        return "Files longer than 30 minutes are not supported.";

    stopPlayback();
    {
        const juce::SpinLock::ScopedLockType sl (previewLock);
        retired = std::move (preview);
    }
    retired.reset();
    auto old = session.dir;
    session = Session::createNew();
    old.deleteRecursively();
    session.source = f;
    session.isolatedVocal = isolated;
    session.duration = dur;
    session.sourceRate = r->sampleRate;
    session.sourceChannels = (int) r->numChannels;
    session.regionStart = dur > 40 ? dur * 0.3 : 0.0;
    session.regionEnd = dur > 40 ? session.regionStart + 20.0 : dur;
    session.regionFull = dur <= 40;
    session.save();
    sourceThumb.setSource (new juce::FileInputSource (f));
    previewConv = -1;
    lastError = lastInfo = {};
    sendChangeMessage();
    return {};
}

void VocalBuilderProcessor::setRegion (double start, double end)
{
    session.regionStart = juce::jlimit (0.0, session.duration, juce::jmin (start, end));
    session.regionEnd = juce::jlimit (0.0, session.duration, juce::jmax (start, end));
    session.regionFull = session.regionStart <= 0.01 && session.regionEnd >= session.duration - 0.01;
    sendChangeMessage();
}

void VocalBuilderProcessor::processSection (bool fullSong)
{
    if (fullSong)
    {
        session.regionStart = 0;
        session.regionEnd = session.duration;
        session.regionFull = true;
    }
    if (session.regionEnd - session.regionStart < 0.5)
    {
        lastError = "Select at least 0.5 seconds.";
        sendChangeMessage();
        return;
    }
    stopPlayback();
    session.segmentKey = Session::keyFor (session.regionStart, session.regionEnd, session.regionFull);
    session.extracted = false;
    session.conversions.clear();
    previewConv = -1;
    auto dir = session.segmentDir();
    dir.createDirectory();

    juce::StringArray args;
    if (session.isolatedVocal)
        args = { "copy", "--in", session.source.getFullPathName(), "--out", session.vocalsFile().getFullPathName() };
    else
    {
        args = baseArgs ("separate");
        args.addArray ({ "--in", session.source.getFullPathName(), "--out-dir", dir.getFullPathName() });
    }
    if (! session.regionFull)
        args.addArray ({ "--start", juce::String (session.regionStart, 3), "--end", juce::String (session.regionEnd, 3) });

    startJob (args, session.isolatedVocal ? "Preparing vocal" : "Extracting vocal", [this] (const juce::var& r) {
        session.extracted = true;
        session.rmsVocal = r["vocals"]["rms_db"];
        session.hasInstrumental = r["instrumental"].isObject();
        session.rmsInstrumental = session.hasInstrumental ? (double) r["instrumental"]["rms_db"] : -100.0;
        if (r["seconds"].isDouble())
            lastInfo = "Processed " + juce::String ((double) r["audio_seconds"], 1) + " s of audio in " + juce::String ((double) r["seconds"], 1) + " s.";
        session.save();
        loadPreviewAsync();
    });
}

void VocalBuilderProcessor::convert (const VoiceEntry& voice, double pitch, double indexRate, double protect, double dynamics)
{
    if (! session.extracted)
        return;
    stopPlayback();
    Conversion c;
    c.id = juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()).substring (0, 10);
    c.voiceId = voice.id;
    c.voiceName = voice.name;
    c.pitch = pitch;
    c.indexRate = indexRate;
    c.protect = protect;
    c.dynamics = dynamics;
    c.file = session.segmentDir().getChildFile ("conv_" + c.id + ".wav");
    auto args = baseArgs ("convert");
#if VB_ALLOW_TEST_VOICES
    args.add ("--include-test-voices");
#endif
    args.addArray ({ "--voice", voice.id, "--in", session.vocalsFile().getFullPathName(), "--out", c.file.getFullPathName(),
                     "--pitch", juce::String (pitch), "--index-rate", juce::String (indexRate), "--protect", juce::String (protect),
                     "--dynamics", juce::String (dynamics) });
    startJob (args, "Converting to " + voice.name, [this, c] (const juce::var& r) mutable {
        c.rmsDb = r["converted"]["rms_db"];
        session.conversions.add (c);
        previewConv = session.conversions.size() - 1;
        if (r["seconds"].isDouble())
            lastInfo = "Converted " + juce::String ((double) r["audio_seconds"], 1) + " s in " + juce::String ((double) r["seconds"], 1) + " s.";
        session.save();
        loadPreviewAsync();
    });
}

void VocalBuilderProcessor::exportAudio (const juce::String& source, int sampleRate, const juce::String& bits, bool align,
                                         std::function<void (juce::File, juce::String)> done)
{
    juce::File in;
    juce::String voiceName;
    if (source == "vocals")
        in = session.vocalsFile();
    else if (source == "instrumental")
        in = session.instrumentalFile();
    else
        for (auto& c : session.conversions)
            if (c.id == source)
            {
                in = c.file;
                voiceName = c.voiceName;
            }
    if (! in.existsAsFile())
        return done ({}, "That audio has not been created yet.");

    auto clean = [] (juce::String s) { return s.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-").substring (0, 60); };
    juce::String song = clean (session.source.getFileNameWithoutExtension().replaceCharacter (' ', '-'));
    if (song.isEmpty())
        song = "Song";
    juce::String name = source == "vocals" ? song + "_Extracted_Vocal"
                      : source == "instrumental" ? song + "_Instrumental"
                                                 : song + "_" + clean (voiceName.replaceCharacter (' ', '-')) + "_Converted_Dry";
    double lead = 0;
    if (! session.regionFull)
    {
        if (align)
        {
            lead = session.regionStart;
            name += "_Aligned";
        }
        else
        {
            const int ms = juce::roundToInt (session.regionStart * 1000.0);
            name += "_Region_from-" + juce::String::formatted ("%dm%06.3fs", ms / 60000, (ms % 60000) / 1000.0).replaceCharacter ('.', '-');
        }
    }
    session.exportsDir().createDirectory();
    auto out = session.exportsDir().getChildFile (name + ".wav");
    juce::StringArray args { "export", "--in", in.getFullPathName(), "--out", out.getFullPathName(), "--rate", juce::String (sampleRate), "--bits", bits };
    if (lead > 0)
        args.addArray ({ "--lead", juce::String (lead, 6), "--total", juce::String (session.duration, 6) });
    startJob (args, "Exporting WAV", [this, out, done] (const juce::var& r) {
        juce::String note = r["note"].toString();
        done (out, note);
    });
}

void VocalBuilderProcessor::cancelJob() { job.cancel(); }

// ------------------------------------------------------------------------------- preview
void VocalBuilderProcessor::selectPreviewConversion (int index)
{
    previewConv = index;
    loadPreviewAsync();
}

void VocalBuilderProcessor::setPlaying (bool p) { playing = p; }

void VocalBuilderProcessor::stopPlayback()
{
    playing = false;
    playPos = 0;
}

void VocalBuilderProcessor::seekPreview (double fraction)
{
    const juce::SpinLock::ScopedLockType sl (previewLock);
    if (preview != nullptr)
        playPos = (juce::int64) (juce::jlimit (0.0, 1.0, fraction) * preview->length);
}

double VocalBuilderProcessor::previewPositionSeconds() const { return (double) playPos.load() / hostRate.load(); }

double VocalBuilderProcessor::previewLengthSeconds() const
{
    const juce::SpinLock::ScopedLockType sl (const_cast<juce::SpinLock&> (previewLock));
    return preview ? preview->length / hostRate.load() : 0.0;
}

static bool readResampled (juce::AudioFormatManager& fm, const juce::File& f, double targetRate, juce::AudioBuffer<float>& out)
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr)
        return false;
    juce::AudioBuffer<float> src ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&src, 0, (int) r->lengthInSamples, 0, true, true);
    const double ratio = r->sampleRate / targetRate;
    const int outLen = (int) std::llround ((double) src.getNumSamples() / ratio);
    out.setSize (src.getNumChannels(), outLen);
    for (int c = 0; c < src.getNumChannels(); ++c)
    {
        juce::WindowedSincInterpolator interp;
        interp.process (ratio, src.getReadPointer (c), out.getWritePointer (c), outLen);
    }
    return true;
}

void VocalBuilderProcessor::loadPreviewAsync()
{
    if (! session.extracted)
        return;
    const auto vocals = session.vocalsFile();
    const auto inst = session.hasInstrumental ? session.instrumentalFile() : juce::File();
    juce::File conv;
    float gainB = 1.0f, db = 0.0f;
    if (juce::isPositiveAndBelow (previewConv, session.conversions.size()))
    {
        const auto& c = session.conversions.getReference (previewConv);
        conv = c.file;
        db = (float) juce::jlimit (-12.0, 12.0, session.rmsVocal - c.rmsDb);
        gainB = juce::Decibels::decibelsToGain (db);
    }
    lastMatchDb = db;
    previewThumbA.setSource (new juce::FileInputSource (vocals));
    if (conv.existsAsFile())
        previewThumbB.setSource (new juce::FileInputSource (conv));
    else
        previewThumbB.clear();

    const double rate = hostRate.load();
    auto flag = alive;
    loader.addJob ([this, flag, vocals, inst, conv, gainB, rate] {
        auto set = std::make_unique<PreviewSet>();
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (! readResampled (fm, vocals, rate, set->a))
            return;
        set->length = set->a.getNumSamples();
        if (conv.existsAsFile() && readResampled (fm, conv, rate, set->b))
        {
            set->hasB = true;
            set->gainB = gainB;
        }
        if (inst.existsAsFile() && readResampled (fm, inst, rate, set->inst))
            set->hasInst = true;
        juce::MessageManager::callAsync ([this, flag, s = std::shared_ptr<PreviewSet> (set.release())] () mutable {
            if (! flag->load())
                return;
            auto owned = std::make_unique<PreviewSet> (std::move (*s));
            {
                const juce::SpinLock::ScopedLockType sl (previewLock);
                std::swap (preview, owned);
                if (playPos.load() >= preview->length)
                    playPos = 0;
            }
            owned.reset(); // previous set freed here, on the message thread
            sendChangeMessage();
        });
    });
}
} // namespace vb

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vb::VocalBuilderProcessor(); }
