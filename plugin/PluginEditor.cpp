#include "PluginEditor.h"

namespace vb
{
namespace help
{
// Help texts describe exactly what the bundled engine does.
const char* open = "Loads a WAV or MP3. Your original file is only read, never changed. Drag a file onto the plugin or click Open.";
const char* isolated = "Tick this if the file is already just a vocal (an acapella or a vocal bounce). Vocal/instrumental separation is skipped; Lead vocal only and Remove reverb still apply.";
const char* section = "Drag across the waveform to choose a section, for example a chorus. Test a short section first, then run the full song with the same settings.";
const char* extract = "Splits the mix into a vocal and an instrumental on this computer with an MDX-Net vocal model (no internet). The converted voice follows the melody found in this vocal, so a clean extraction matters most: listen with A in Preview first.";
const char* leadOnly = "Removes backing vocals, doubles and harmonies so only the lead vocal is converted. Recommended: stacked voices confuse the melody detection and make the result sound off-key. The backing vocals stay available as a separate export.";
const char* dereverb = "Removes reverb and echo from the vocal before conversion. Turn it on when the vocal is wet; a dry vocal gives a cleaner, more in-tune conversion. Takes extra processing time.";
const char* tune = "Pulls the converted melody toward exact semitones (pitch correction). 0% keeps the singer's own pitch; 50-70% tightens slightly off notes; 100% is hard, robotic tuning.";
const char* voice = "The singing voice the vocal is converted into. Voices come with Vocal Builder, or add your own trained voice with Add voice (pick its voice.json).";
const char* pitch = "Moves the melody up or down in semitones before conversion, so it fits the voice's range. Example: +12 for a male vocal into a higher voice. The exported vocal is then in the new key.";
const char* character = "How much of the voice's own recorded sound replaces the source's timbre. Higher sounds more like the voice but can blur words; try 40-60%.";
const char* protect = "Keeps breaths and consonants like s, t and k closer to the original, which helps pronunciation. Lower values protect more; 33% is a good start.";
const char* dynamics = "100% lets the voice model set the loudness; lower values follow the original singer's loudness movement more closely.";
const char* convert = "Changes the timbre of the existing vocal into the chosen voice. Lyrics, melody, timing and phrasing come from the original performance. It is a transformation, not a new recording or a cleared cover.";
const char* ab = "A plays the original vocal, B the converted one. Both run in sync, so switching keeps the same position.";
const char* match = "Turns the converted vocal up or down to the original's level during preview only, so the louder one doesn't simply sound better. Exports are not affected.";
const char* inst = "Plays the extracted instrumental underneath, to hear how the vocal sits in the track.";
const char* mute = "Silences the audio coming into this mixer track while you preview, so you only hear the plugin.";
const char* exportHelp = "Writes a dry WAV (no added effects) for FL Studio. Add your own reverb and delay in the mixer.";
const char* timeline = "Aligned adds silence before the section so the file lines up when placed at the very start of the song. Section only starts exactly at the section; its start time is in the file name.";
const char* rate = "Match your FL Studio project (usually 44.1 or 48 kHz). A higher rate than the source changes the format only; it does not add detail.";
const char* bits = "24-bit is the standard for stems. Choose 32-bit float if the vocal peaks above 0 dBFS so nothing is clipped.";
const char* rights = "Changing the voice does not clear the rights in the original recording or in the song's composition and lyrics. Clear those before releasing.";
} // namespace help

// ==========================================================================================
class Content : public juce::Component
{
public:
    explicit Content (VocalBuilderProcessor& p) : proc (p), wave (p.sourceThumb), previewView (p.previewThumbA, p.previewThumbB)
    {
        for (auto* c : std::initializer_list<juce::Component*> {
                 &h1, &h2, &h3, &h4, &h5, &h6, &openButton, &isolated, &fileInfo, &wave, &startLabel, &endLabel, &startEdit,
                 &endEdit, &fullSelect, &processSection, &processFull, &leadOnlyToggle, &dereverbToggle, &iLeadOnly, &iDereverb, &tuneL, &tune, &iTune, &extractNote, &voiceBox, &addVoice, &voiceInfo, &pitch, &character,
                 &protect, &dynamics, &pitchL, &characterL, &protectL, &dynamicsL, &convertButton, &convertNote, &takeBox,
                 &previewView, &play, &stop, &aButton, &bButton, &loop, &match, &withInst, &muteInput, &volume, &volumeL,
                 &timeLabel, &exportSource, &exportRate, &exportBits, &exportTimeline, &exportButton, &dragButton,
                 &exportResult, &rightsNote, &iOpen, &iIsolated, &iSection, &iExtract, &iVoice, &iPitch, &iCharacter,
                 &iProtect, &iDynamics, &iConvert, &iAB, &iMatch, &iInst, &iMute, &iExport, &iTimeline, &iRate, &iBits, &iRights })
            addAndMakeVisible (c);

        for (auto* l : { &fileInfo, &extractNote, &voiceInfo, &convertNote, &timeLabel, &exportResult, &rightsNote })
        {
            l->setColour (juce::Label::textColourId, colours::muted);
            l->setFont (juce::FontOptions (13.0f));
            l->setJustificationType (juce::Justification::topLeft);
        }
        rightsNote.setText (help::rights, juce::dontSendNotification);

        openButton.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser> ("Open audio", juce::File(), "*.wav;*.mp3");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
                if (fc.getResult().existsAsFile())
                    importFile (fc.getResult());
            });
        };
        wave.onRegionChanged = [this] (double s, double e) { proc.setRegion (s, e); };
        auto onEdit = [this] { proc.setRegion (startEdit.getText().getDoubleValue(), endEdit.getText().getDoubleValue()); };
        startEdit.onReturnKey = endEdit.onReturnKey = onEdit;
        startEdit.onFocusLost = endEdit.onFocusLost = onEdit;
        for (auto* e : { &startEdit, &endEdit })
            e->setInputRestrictions (9, "0123456789.");
        fullSelect.onClick = [this] { proc.setRegion (0, proc.session.duration); };
        leadOnlyToggle.setToggleState (p.leadOnly.load(), juce::dontSendNotification);
        dereverbToggle.setToggleState (p.dereverb.load(), juce::dontSendNotification);
        leadOnlyToggle.onClick = [this] { proc.leadOnly = leadOnlyToggle.getToggleState(); };
        dereverbToggle.onClick = [this] { proc.dereverb = dereverbToggle.getToggleState(); };
        processSection.onClick = [this] { proc.processSection (false); };
        processFull.onClick = [this] { proc.processSection (true); };

        voiceBox.onChange = [this] { refresh(); };
        addVoice.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser> ("Choose the voice.json of a voice package", juce::File(), "voice.json");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
                if (! fc.getResult().existsAsFile())
                    return;
                auto err = proc.addVoiceFromFolder (fc.getResult());
                if (err.isNotEmpty())
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't add voice", err);
            });
        };
        auto setupSlider = [] (juce::Slider& s, double lo, double hi, double step, double def, const juce::String& suffix) {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
            s.setRange (lo, hi, step);
            s.setValue (def, juce::dontSendNotification);
            s.setTextValueSuffix (suffix);
            s.setDoubleClickReturnValue (true, def);
        };
        setupSlider (pitch, -24, 24, 1, 0, " st");
        setupSlider (character, 0, 100, 1, 50, " %");
        setupSlider (protect, 0, 50, 1, 33, " %");
        setupSlider (dynamics, 0, 100, 1, 100, " %");
        setupSlider (tune, 0, 100, 1, 0, " %");
        setupSlider (volume, 0, 100, 1, 80, " %");
        volume.setValue (p.volume.load() * 100.0, juce::dontSendNotification);
        volume.onValueChange = [this] { proc.volume = (float) volume.getValue() / 100.0f; };
        convertButton.onClick = [this] {
            const int i = voiceBox.getSelectedItemIndex();
            if (juce::isPositiveAndBelow (i, proc.voices.size()))
            {
                const auto& v = proc.voices.getReference (i);
                proc.convert (v, pitch.getValue(), v.hasIndex ? character.getValue() / 100.0 : 0.0, protect.getValue() / 100.0, dynamics.getValue() / 100.0, tune.getValue() / 100.0);
            }
        };

        takeBox.onChange = [this] { proc.selectPreviewConversion (takeBox.getSelectedItemIndex()); };
        play.onClick = [this] { proc.setPlaying (! proc.isPlaying()); };
        stop.onClick = [this] { proc.stopPlayback(); };
        aButton.setClickingTogglesState (false);
        bButton.setClickingTogglesState (false);
        aButton.onClick = [this] { proc.playB = false; };
        bButton.onClick = [this] { proc.playB = true; };
        bButton.setColour (juce::TextButton::buttonOnColourId, colours::accent2);
        bButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        loop.setToggleState (p.loop.load(), juce::dontSendNotification);
        match.setToggleState (p.matchLoudness.load(), juce::dontSendNotification);
        muteInput.setToggleState (p.muteInputWhilePreviewing.load(), juce::dontSendNotification);
        loop.onClick = [this] { proc.loop = loop.getToggleState(); };
        match.onClick = [this] { proc.matchLoudness = match.getToggleState(); };
        withInst.onClick = [this] { proc.withInstrumental = withInst.getToggleState(); };
        muteInput.onClick = [this] { proc.muteInputWhilePreviewing = muteInput.getToggleState(); };
        previewView.onSeek = [this] (double f) { proc.seekPreview (f); };

        exportRate.addItemList ({ "44.1 kHz", "48 kHz" }, 1);
        exportRate.setSelectedItemIndex (std::abs (p.session.sourceRate - 48000.0) < 1 ? 1 : 0, juce::dontSendNotification);
        exportBits.addItemList ({ "WAV 24-bit PCM", "WAV 16-bit PCM", "WAV 32-bit float" }, 1);
        exportBits.setSelectedItemIndex (0, juce::dontSendNotification);
        exportTimeline.addItemList ({ "Aligned to song start", "Section only" }, 1);
        exportTimeline.setSelectedItemIndex (0, juce::dontSendNotification);
        exportButton.setColour (juce::TextButton::buttonColourId, colours::accent);
        exportButton.onClick = [this] { doExport(); };
        dragButton.setEnabled (false);
        for (auto* b : { &processSection, &convertButton })
            b->setColour (juce::TextButton::buttonColourId, colours::accent);

        setSize (900, 1480);
        refresh();
    }

    void importFile (const juce::File& f)
    {
        auto err = proc.importFile (f, isolated.getToggleState());
        if (err.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Can't open file", err);
    }

    void refresh()
    {
        const auto& s = proc.session;
        const bool hasFile = s.source.getFullPathName().isNotEmpty() && s.duration > 0;
        const bool busy = proc.job.isRunning();
        fileInfo.setText (hasFile ? s.source.getFileName() + "   " + timeStr (s.duration) + "   " + juce::String (s.sourceRate / 1000.0, 1) + " kHz   "
                                        + (s.sourceChannels == 1 ? "Mono" : "Stereo") + "   " + (s.isolatedVocal ? "Isolated vocal" : "Full mix")
                                  : "No file loaded.",
                          juce::dontSendNotification);
        wave.setRange (s.duration, s.regionStart, s.regionEnd);
        if (! startEdit.hasKeyboardFocus (true))
            startEdit.setText (juce::String (s.regionStart, 2), false);
        if (! endEdit.hasKeyboardFocus (true))
            endEdit.setText (juce::String (s.regionEnd, 2), false);
        processSection.setButtonText (s.isolatedVocal ? "Use vocal: section" : "Extract vocal: section");
        processFull.setButtonText (s.isolatedVocal ? "Use vocal: full song" : "Extract vocal: full song");
        for (auto* b : { &processSection, &processFull, &fullSelect })
            b->setEnabled (hasFile && ! busy);
        isolated.setEnabled (! busy);
        leadOnlyToggle.setEnabled (! busy && proc.leadBackingAvailable);
        dereverbToggle.setEnabled (! busy && proc.dereverbAvailable);

        juce::String ex;
        if (! hasFile)
            ex = "Open a file first.";
        else if (s.isolatedVocal)
            ex = "Isolated vocal: vocal/instrumental separation is skipped.";
        else if (! proc.separationModelInstalled && ! proc.voicesLoading)
            ex = "The separation model is missing. Reinstall Vocal Builder.";
        else if (! proc.mdxInstalled && ! proc.voicesLoading)
            ex = "Using the older Spleeter model (lower quality). Reinstall Vocal Builder to get the MDX-Net models.";
        else
            ex = "Returns the vocal and the instrumental" + juce::String (leadOnlyToggle.getToggleState() ? ", with backing vocals split off." : ".");
        if (s.extracted)
            ex += "\nReady: " + juce::String (s.regionFull ? "full song" : "section " + timeStr (s.regionStart) + " - " + timeStr (s.regionEnd))
                  + (s.separationEngine.isNotEmpty() ? " (" + s.separationEngine + ")" : juce::String()) + ". Listen with A in Preview before converting.";
        extractNote.setText (ex, juce::dontSendNotification);

        // Voices
        const int prevSel = voiceBox.getSelectedItemIndex();
        voiceBox.clear (juce::dontSendNotification);
        for (int i = 0; i < proc.voices.size(); ++i)
            voiceBox.addItem (proc.voices[i].name, i + 1);
        if (proc.voices.size() > 0)
            voiceBox.setSelectedItemIndex (juce::jlimit (0, proc.voices.size() - 1, prevSel), juce::dontSendNotification);
        voiceBox.setTextWhenNothingSelected (proc.voicesLoading ? "Looking for installed voices..." : "No voice installed");
        const int vi = voiceBox.getSelectedItemIndex();
        const VoiceEntry* v = juce::isPositiveAndBelow (vi, proc.voices.size()) ? &proc.voices.getReference (vi) : nullptr;
        if (v != nullptr)
            voiceInfo.setText (v->description + (v->tags.isEmpty() ? "" : "\nStyle: " + v->tags.joinIntoString (", "))
                                   + "\nLanguages tested: " + (v->languages.isEmpty() ? "not specified" : v->languages.joinIntoString (", "))
                                   + "\nLicence: " + v->licence,
                               juce::dontSendNotification);
        else
            voiceInfo.setText (proc.voicesLoading ? "" : proc.voicesError + "\nVoice conversion is unavailable until a trained, licensed voice is installed.", juce::dontSendNotification);
        voiceInfo.setColour (juce::Label::textColourId, v ? colours::muted : colours::warn);
        const bool idx = v != nullptr && v->hasIndex;
        for (auto* c : std::initializer_list<juce::Component*> { &character, &characterL, &iCharacter, &protect, &protectL, &iProtect })
            c->setVisible (idx); // only shown when the voice supports them
        convertButton.setEnabled (v != nullptr && s.extracted && ! busy);
        convertNote.setText (! s.extracted ? "Extract or load a vocal first." : v == nullptr ? "Install a voice to convert." : "Converts the vocal shown in Preview A.", juce::dontSendNotification);

        // Takes
        const int prevTake = proc.previewConversionIndex();
        takeBox.clear (juce::dontSendNotification);
        for (int i = 0; i < s.conversions.size(); ++i)
            takeBox.addItem (juce::String (i + 1) + ". " + s.conversions[i].voiceName + "  (" + (s.conversions[i].pitch >= 0 ? "+" : "") + juce::String ((int) s.conversions[i].pitch) + " st)", i + 1);
        if (prevTake >= 0)
            takeBox.setSelectedItemIndex (prevTake, juce::dontSendNotification);
        takeBox.setTextWhenNothingSelected ("No converted take yet");
        for (auto* c : std::initializer_list<juce::Component*> { &play, &stop, &aButton, &loop, &match, &muteInput, &volume })
            c->setEnabled (s.extracted);
        bButton.setEnabled (! s.conversions.isEmpty());
        withInst.setEnabled (s.hasInstrumental);

        // Export sources
        const auto prevSrc = exportSource.getText();
        exportSource.clear (juce::dontSendNotification);
        exportIds.clear();
        for (auto& c : s.conversions)
        {
            exportIds.add (c.id);
            exportSource.addItem ("Converted vocal - " + c.voiceName + " (dry)", exportIds.size());
        }
        if (s.extracted)
        {
            exportIds.add ("vocals");
            exportSource.addItem (s.leadOnly ? "Lead vocal (original singer)" : s.isolatedVocal ? "Original vocal" : "Extracted original vocal", exportIds.size());
        }
        if (s.hasBacking)
        {
            exportIds.add ("backing");
            exportSource.addItem ("Backing vocals (original)", exportIds.size());
        }
        if (s.hasInstrumental)
        {
            exportIds.add ("instrumental");
            exportSource.addItem ("Instrumental", exportIds.size());
        }
        exportSource.setSelectedItemIndex (juce::jmax (0, proc.previewConversionIndex()), juce::dontSendNotification);
        if (prevSrc.isNotEmpty())
            for (int i = 0; i < exportSource.getNumItems(); ++i)
                if (exportSource.getItemText (i) == prevSrc)
                    exportSource.setSelectedItemIndex (i, juce::dontSendNotification);
        exportTimeline.setVisible (s.extracted && ! s.regionFull);
        iTimeline.setVisible (exportTimeline.isVisible());
        exportButton.setEnabled (s.extracted && ! busy);

        h1.setActive (! hasFile, hasFile);
        h2.setActive (hasFile && ! s.extracted, s.extracted);
        h3.setActive (s.extracted && v == nullptr, v != nullptr && s.extracted);
        h4.setActive (s.extracted && s.conversions.isEmpty(), ! s.conversions.isEmpty());
        h5.setActive (! s.conversions.isEmpty(), false);
        h6.setActive (! s.conversions.isEmpty(), lastExport.existsAsFile());
    }

    void tick()
    {
        const double len = proc.previewLengthSeconds();
        timeLabel.setText (timeStr (proc.previewPositionSeconds()) + " / " + timeStr (len)
                               + (proc.matchLoudness.load() && proc.previewConversionIndex() >= 0 ? "    B level-matched " + juce::String (proc.matchDb(), 1) + " dB" : ""),
                           juce::dontSendNotification);
        previewView.setPlayhead (len > 0 ? proc.previewPositionSeconds() / len : 0.0, proc.playB.load());
        play.setButtonText (proc.isPlaying() ? "Pause" : "Play");
        aButton.setToggleState (! proc.playB.load(), juce::dontSendNotification);
        bButton.setToggleState (proc.playB.load(), juce::dontSendNotification);
    }

    void doExport()
    {
        const int i = exportSource.getSelectedItemIndex();
        if (! juce::isPositiveAndBelow (i, exportIds.size()))
            return;
        const int rate = exportRate.getSelectedItemIndex() == 1 ? 48000 : 44100;
        const juce::String bits = exportBits.getSelectedItemIndex() == 1 ? "16" : exportBits.getSelectedItemIndex() == 2 ? "32f" : "24";
        const bool aligned = exportTimeline.getSelectedItemIndex() == 0;
        proc.exportAudio (exportIds[i], rate, bits, aligned, [this, sp = juce::Component::SafePointer<Content> (this)] (juce::File out, juce::String note) {
            if (sp == nullptr)
                return;
            lastExport = out;
            dragButton.file = out;
            dragButton.setEnabled (out.existsAsFile());
            const auto& s = proc.session;
            juce::String where = s.regionFull ? "Full length, starts at 0:00."
                                              : exportTimeline.getSelectedItemIndex() == 0 ? "Place it at the very start of the song (bar 1). The section begins at " + juce::String (s.regionStart, 3) + " s."
                                                                                            : "Place it at " + juce::String (s.regionStart, 3) + " s in the song.";
            exportResult.setText (out.getFileName() + "\n" + where + (note.isNotEmpty() ? "\n" + note : ""), juce::dontSendNotification);
            chooser = std::make_unique<juce::FileChooser> ("Save WAV", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (out.getFileName()), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, out] (const juce::FileChooser& fc) {
                auto dest = fc.getResult();
                if (dest != juce::File() && out.copyFileTo (dest.withFileExtension ("wav")))
                {
                    lastExport = dest.withFileExtension ("wav");
                    dragButton.file = lastExport;
                    exportResult.setText ("Saved: " + lastExport.getFullPathName() + "\n" + exportResult.getText().fromFirstOccurrenceOf ("\n", false, false), juce::dontSendNotification);
                }
            });
            refresh();
        });
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 14);
        constexpr int gap = 8;
        auto row = [&] (int h = 30) { auto x = r.removeFromTop (h); r.removeFromTop (gap); return x; };
        auto withInfo = [] (juce::Rectangle<int>& line, juce::Component& c, int w, InfoButton& i) {
            c.setBounds (line.removeFromLeft (w));
            i.setBounds (line.removeFromLeft (28));
            line.removeFromLeft (8);
        };

        h1.setBounds (row (28));
        auto l = row();
        openButton.setBounds (l.removeFromLeft (170));
        iOpen.setBounds (l.removeFromLeft (28));
        l.removeFromLeft (14);
        withInfo (l, isolated, 340, iIsolated);
        fileInfo.setBounds (row (22));
        wave.setBounds (row (120));
        l = row();
        startLabel.setBounds (l.removeFromLeft (44));
        startEdit.setBounds (l.removeFromLeft (80));
        l.removeFromLeft (10);
        endLabel.setBounds (l.removeFromLeft (36));
        endEdit.setBounds (l.removeFromLeft (80));
        l.removeFromLeft (10);
        fullSelect.setBounds (l.removeFromLeft (150));
        iSection.setBounds (l.removeFromLeft (28));
        r.removeFromTop (8);

        h2.setBounds (row (28));
        l = row();
        processSection.setBounds (l.removeFromLeft (200));
        l.removeFromLeft (8);
        processFull.setBounds (l.removeFromLeft (200));
        iExtract.setBounds (l.removeFromLeft (28));
        l = row();
        withInfo (l, leadOnlyToggle, 300, iLeadOnly);
        withInfo (l, dereverbToggle, 200, iDereverb);
        extractNote.setBounds (row (40));
        r.removeFromTop (8);

        h3.setBounds (row (28));
        l = row();
        withInfo (l, voiceBox, 320, iVoice);
        addVoice.setBounds (l.removeFromLeft (130));
        voiceInfo.setBounds (row (66));
        r.removeFromTop (8);

        h4.setBounds (row (28));
        auto sliderRow = [&] (juce::Label& lab, juce::Slider& s, InfoButton& i) {
            if (! s.isVisible())
                return;
            auto x = row();
            lab.setBounds (x.removeFromLeft (170));
            i.setBounds (x.removeFromRight (28));
            s.setBounds (x);
        };
        sliderRow (pitchL, pitch, iPitch);
        sliderRow (characterL, character, iCharacter);
        sliderRow (protectL, protect, iProtect);
        sliderRow (dynamicsL, dynamics, iDynamics);
        sliderRow (tuneL, tune, iTune);
        l = row();
        convertButton.setBounds (l.removeFromLeft (200));
        iConvert.setBounds (l.removeFromLeft (28));
        l.removeFromLeft (8);
        convertNote.setBounds (l);
        r.removeFromTop (8);

        h5.setBounds (row (28));
        l = row();
        takeBox.setBounds (l.removeFromLeft (320));
        previewView.setBounds (row (110));
        l = row (34);
        play.setBounds (l.removeFromLeft (90));
        l.removeFromLeft (6);
        stop.setBounds (l.removeFromLeft (70));
        l.removeFromLeft (14);
        aButton.setBounds (l.removeFromLeft (150));
        bButton.setBounds (l.removeFromLeft (130));
        iAB.setBounds (l.removeFromLeft (28));
        l.removeFromLeft (8);
        timeLabel.setBounds (l);
        l = row();
        loop.setBounds (l.removeFromLeft (120));
        withInfo (l, match, 140, iMatch);
        withInfo (l, withInst, 170, iInst);
        withInfo (l, muteInput, 160, iMute);
        l = row();
        volumeL.setBounds (l.removeFromLeft (70));
        volume.setBounds (l.removeFromLeft (260));
        r.removeFromTop (8);

        h6.setBounds (row (28));
        auto exportRow = [&] (juce::Component& c, InfoButton* i) {
            auto x = row();
            c.setBounds (x.removeFromLeft (360));
            if (i != nullptr)
                i->setBounds (x.removeFromLeft (28));
        };
        exportRow (exportSource, &iExport);
        exportRow (exportRate, &iRate);
        exportRow (exportBits, &iBits);
        if (exportTimeline.isVisible())
            exportRow (exportTimeline, &iTimeline);
        l = row (38);
        exportButton.setBounds (l.removeFromLeft (180));
        l.removeFromLeft (10);
        dragButton.setBounds (l.removeFromLeft (180));
        exportResult.setBounds (row (54));
        l = row (36);
        iRights.setBounds (l.removeFromLeft (28));
        rightsNote.setBounds (l);
    }

private:
    static juce::String timeStr (double t)
    {
        const int m = (int) (t / 60);
        return juce::String (m) + ":" + juce::String (t - m * 60, 2).paddedLeft ('0', 5);
    }

    VocalBuilderProcessor& proc;
    SectionHeader h1 { 1, "Drop Track" }, h2 { 2, "Extract Vocal" }, h3 { 3, "Choose Voice" }, h4 { 4, "Convert" }, h5 { 5, "Preview" }, h6 { 6, "Export WAV" };
    juce::TextButton openButton { "Open Audio File..." }, fullSelect { "Select full song" }, processSection, processFull,
        convertButton { "Convert" }, addVoice { "Add voice..." }, play { "Play" }, stop { "Stop" }, aButton { "A  Original vocal" }, bButton { "B  Converted" },
        exportButton { "Export WAV..." };
    juce::ToggleButton isolated { "This file is an isolated vocal (skip extraction)" }, loop { "Loop section" }, match { "Match loudness" },
        withInst { "Play with instrumental" }, muteInput { "Mute track input" };
    juce::Label fileInfo, startLabel { {}, "Start" }, endLabel { {}, "End" }, extractNote, voiceInfo, convertNote, timeLabel, exportResult,
        rightsNote, pitchL { {}, "Pitch Shift" }, characterL { {}, "Voice Character" }, protectL { {}, "Consonant Protection" },
        dynamicsL { {}, "Dynamics" }, volumeL { {}, "Volume" }, tuneL { {}, "Pitch Correction" };
    juce::TextEditor startEdit, endEdit;
    WaveformView wave;
    PreviewView previewView;
    juce::ComboBox voiceBox, takeBox, exportSource, exportRate, exportBits, exportTimeline;
    juce::Slider pitch, character, protect, dynamics, volume, tune;
    juce::ToggleButton leadOnlyToggle { "Lead vocal only (remove backing/doubles)" }, dereverbToggle { "Remove reverb/echo" };
    DragFileButton dragButton;
    juce::StringArray exportIds;
    juce::File lastExport;
    std::unique_ptr<juce::FileChooser> chooser;
    InfoButton iOpen { "Open", help::open }, iIsolated { "Isolated vocal", help::isolated }, iSection { "Section", help::section },
        iExtract { "Extract Vocal", help::extract }, iVoice { "Voice", help::voice }, iPitch { "Pitch Shift", help::pitch },
        iCharacter { "Voice Character", help::character }, iProtect { "Consonant Protection", help::protect },
        iDynamics { "Dynamics", help::dynamics }, iLeadOnly { "Lead vocal only", help::leadOnly }, iDereverb { "Remove reverb/echo", help::dereverb },
        iTune { "Pitch Correction", help::tune }, iConvert { "Convert", help::convert }, iAB { "A/B comparison", help::ab },
        iMatch { "Match loudness", help::match }, iInst { "Play with instrumental", help::inst }, iMute { "Mute track input", help::mute },
        iExport { "Export", help::exportHelp }, iTimeline { "Timeline position", help::timeline }, iRate { "Sample rate", help::rate },
        iBits { "Format", help::bits }, iRights { "Rights", help::rights };
};

// ==========================================================================================
class LicencePanel : public juce::Component
{
public:
    explicit LicencePanel (LicenceManager& l) : lm (l)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &state, &keyLabel, &keyEdit, &applyKey, &machine, &requestLabel, &requestEdit,
                                                                 &copyRequest, &codeLabel, &codeEdit, &activate, &remove, &devNote, &message })
            addAndMakeVisible (c);
        for (auto* e : { &keyEdit, &requestEdit, &codeEdit })
        {
            e->setMultiLine (true);
            e->setReturnKeyStartsNewLine (false);
            e->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        }
        requestEdit.setReadOnly (true);
        devNote.setColour (juce::Label::textColourId, colours::warn);
        devNote.setText (LicenceManager::usingDevelopmentKey() ? "Development build: licences are signed with a test key. Not for sale." : "", juce::dontSendNotification);
        applyKey.onClick = [this] { show (lm.enterKey (keyEdit.getText())); };
        activate.onClick = [this] { show (lm.enterActivation (codeEdit.getText())); };
        copyRequest.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (requestEdit.getText()); message.setText ("Copied. E-mail it to the seller to receive your activation code.", juce::dontSendNotification); };
        remove.onClick = [this] { lm.removeLicence(); show ({}); };
        setSize (560, 470);
        show ({});
    }

    void show (const juce::String& err)
    {
        state.setText (lm.statusText(), juce::dontSendNotification);
        state.setColour (juce::Label::textColourId, lm.canProcess() ? colours::ok : colours::warn);
        machine.setText ("This computer's ID: " + lm.machineId(), juce::dontSendNotification);
        const bool needAct = lm.state() == LicenceManager::State::needsActivation || lm.state() == LicenceManager::State::activationInvalid;
        requestEdit.setText (needAct ? lm.activationRequest() : juce::String(), false);
        for (auto* c : std::initializer_list<juce::Component*> { &requestLabel, &requestEdit, &copyRequest, &codeLabel, &codeEdit, &activate })
            c->setEnabled (needAct);
        message.setText (err, juce::dontSendNotification);
        message.setColour (juce::Label::textColourId, err.isEmpty() ? colours::muted : colours::bad);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        state.setBounds (r.removeFromTop (26));
        devNote.setBounds (r.removeFromTop (22));
        keyLabel.setBounds (r.removeFromTop (22));
        keyEdit.setBounds (r.removeFromTop (54));
        r.removeFromTop (4);
        applyKey.setBounds (r.removeFromTop (28).removeFromLeft (160));
        r.removeFromTop (10);
        machine.setBounds (r.removeFromTop (22));
        requestLabel.setBounds (r.removeFromTop (22));
        auto rr = r.removeFromTop (54);
        copyRequest.setBounds (rr.removeFromRight (90).withHeight (28));
        requestEdit.setBounds (rr.withTrimmedRight (8));
        r.removeFromTop (8);
        codeLabel.setBounds (r.removeFromTop (22));
        codeEdit.setBounds (r.removeFromTop (54));
        r.removeFromTop (4);
        auto b = r.removeFromTop (28);
        activate.setBounds (b.removeFromLeft (160));
        remove.setBounds (b.removeFromRight (140));
        r.removeFromTop (6);
        message.setBounds (r);
    }

private:
    LicenceManager& lm;
    juce::Label state, keyLabel { {}, "1. Licence key (from your purchase e-mail)" }, machine, requestLabel { {}, "2. Activation request - send this to the seller" },
        codeLabel { {}, "3. Activation code (from the seller)" }, devNote, message;
    juce::TextEditor keyEdit, requestEdit, codeEdit;
    juce::TextButton applyKey { "Apply key" }, copyRequest { "Copy" }, activate { "Activate" }, remove { "Remove licence" };
};

// ==========================================================================================
VocalBuilderEditor::VocalBuilderEditor (VocalBuilderProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    content = std::make_unique<Content> (p);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    title.setText ("Vocal Builder", juce::dontSendNotification);
    title.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    subtitle.setText ("v" VB_VERSION "  -  local AI vocal extraction and voice conversion, no internet needed", juce::dontSendNotification);
    subtitle.setColour (juce::Label::textColourId, colours::muted);
    for (auto* c : std::initializer_list<juce::Component*> { &title, &subtitle, &licenceButton, &progress, &status, &cancelButton })
        addAndMakeVisible (c);
    licenceButton.onClick = [this] { openLicenceDialog(); };
    cancelButton.onClick = [this] { proc.cancelJob(); };
    status.setColour (juce::Label::textColourId, colours::muted);
    progress.setPercentageDisplay (false);
    proc.addChangeListener (this);
    proc.refreshVoicesAsync();
    setResizable (true, true);
    setResizeLimits (760, 520, 1400, 1400);
    setSize (940, 760);
    startTimerHz (20);
    changeListenerCallback (nullptr);
}

VocalBuilderEditor::~VocalBuilderEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void VocalBuilderEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::bg);
    g.setColour (juce::Colour (0xff111319));
    g.fillRect (getLocalBounds().removeFromTop (56));
    g.fillRect (getLocalBounds().removeFromBottom (46));
}

void VocalBuilderEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (56).reduced (16, 8);
    licenceButton.setBounds (top.removeFromRight (250).reduced (0, 4));
    title.setBounds (top.removeFromTop (22));
    subtitle.setBounds (top);
    auto bottom = r.removeFromBottom (46).reduced (16, 9);
    cancelButton.setBounds (bottom.removeFromRight (90));
    bottom.removeFromRight (10);
    progress.setBounds (bottom.removeFromLeft (220));
    bottom.removeFromLeft (10);
    status.setBounds (bottom);
    viewport.setBounds (r);
    content->setSize (juce::jmax (760, r.getWidth() - viewport.getScrollBarThickness()), content->getHeight());
}

void VocalBuilderEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    content->refresh();
    content->resized();
    licenceButton.setButtonText (proc.licence.canProcess() ? "Licence: active" : "Activate licence");
    licenceButton.setColour (juce::TextButton::buttonColourId, proc.licence.canProcess() ? colours::panel2 : colours::accent);
}

void VocalBuilderEditor::timerCallback()
{
    content->tick();
    if (proc.licence.canProcess() != licenceWasActive)
    {
        licenceWasActive = proc.licence.canProcess();
        changeListenerCallback (nullptr);
    }
    const bool busy = proc.job.isRunning();
    cancelButton.setEnabled (busy);
    if (busy)
    {
        const double p = proc.job.currentProgress();
        progressValue = p < 0 ? -1.0 : p;
        status.setText (proc.job.currentLabel() + ": " + proc.job.currentMessage(), juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, colours::text);
    }
    else
    {
        progressValue = 0.0;
        if (proc.lastError.isNotEmpty())
        {
            status.setText (proc.lastError, juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, colours::bad);
        }
        else
        {
            status.setText (proc.lastInfo.isNotEmpty() ? proc.lastInfo : (proc.licence.canProcess() ? "Ready." : "Activate your licence to process audio."), juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, colours::muted);
        }
    }
}

bool VocalBuilderEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1 && (files[0].endsWithIgnoreCase (".wav") || files[0].endsWithIgnoreCase (".mp3"));
}

void VocalBuilderEditor::filesDropped (const juce::StringArray& files, int, int)
{
    content->importFile (juce::File (files[0]));
}

void VocalBuilderEditor::openLicenceDialog()
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new LicencePanel (proc.licence));
    o.dialogTitle = "Vocal Builder licence";
    o.dialogBackgroundColour = colours::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = this;
    o.launchAsync();
}
} // namespace vb
