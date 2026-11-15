#pragma once
#include <JuceHeader.h>
#include <functional>

namespace vb
{
namespace colours
{
const juce::Colour bg { 0xff0e0f13 }, panel { 0xff16181f }, panel2 { 0xff1d2029 }, line { 0xff2a2e3a },
    text { 0xffe9eaf0 }, muted { 0xff9298a8 }, accent { 0xffff3d7f }, accent2 { 0xff35d0ff }, ok { 0xff3ddc97 },
    warn { 0xffffb547 }, bad { 0xffff5d5d };
}

class VbLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VbLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
};

// Small round "i" button. Click or tap opens a short explanation beside it; clicking anywhere
// else closes it. Opening help never touches settings or playback.
class InfoButton : public juce::Button
{
public:
    InfoButton (juce::String title, juce::String body);
    void setHelp (juce::String title, juce::String body);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    using juce::Button::clicked;
    void clicked() override;

private:
    juce::String helpTitle, helpBody;
};

// Waveform of the imported file with a draggable section selection.
class WaveformView : public juce::Component, private juce::ChangeListener
{
public:
    explicit WaveformView (juce::AudioThumbnail& t);
    ~WaveformView() override;
    void setRange (double total, double start, double end);
    std::function<void (double, double)> onRegionChanged;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    double xToTime (float x) const;
    juce::AudioThumbnail& thumb;
    double total = 0, start = 0, end = 0, dragFrom = -1;
};

// Original (top) and converted (bottom) waveforms with playhead; click to seek.
class PreviewView : public juce::Component, private juce::ChangeListener
{
public:
    PreviewView (juce::AudioThumbnail& a, juce::AudioThumbnail& b);
    ~PreviewView() override;
    void setPlayhead (double fraction, bool bActive);
    std::function<void (double)> onSeek;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    juce::AudioThumbnail &ta, &tb;
    double playhead = 0;
    bool showB = false;
};

// Drag this into the FL Studio playlist to drop the exported WAV.
class DragFileButton : public juce::TextButton
{
public:
    DragFileButton();
    juce::File file;
    void mouseDrag (const juce::MouseEvent& e) override;

private:
    bool dragging = false;
};

// A section header with a step number.
class SectionHeader : public juce::Component
{
public:
    SectionHeader (int number, juce::String title);
    void paint (juce::Graphics&) override;
    void setActive (bool a, bool done);

private:
    int num;
    juce::String title;
    bool active = false, isDone = false;
};
} // namespace vb
