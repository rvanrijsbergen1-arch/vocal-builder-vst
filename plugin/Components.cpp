#include "Components.h"

namespace vb
{
VbLookAndFeel::VbLookAndFeel()
{
    using namespace colours;
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::TextButton::buttonColourId, panel2);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff101217));
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, panel2);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accent.withAlpha (0.6f));
    setColour (juce::Slider::backgroundColourId, line);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, line);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff101217));
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, muted);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101217));
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::ProgressBar::backgroundColourId, line);
    setColour (juce::ScrollBar::thumbColourId, line.brighter (0.2f));
}

void VbLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto col = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : c;
    if (! b.isEnabled())
        col = col.withMultipliedAlpha (0.45f);
    else if (down)
        col = col.brighter (0.15f);
    else if (over)
        col = col.brighter (0.07f);
    g.setColour (col);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);
}

// ------------------------------------------------------------------------------------------
namespace
{
class HelpContent : public juce::Component
{
public:
    HelpContent (const juce::String& t, const juce::String& b) : title (t), body (b)
    {
        juce::AttributedString as;
        as.append (title + "\n", juce::FontOptions (14.0f, juce::Font::bold), colours::text);
        as.append (body, juce::FontOptions (13.5f), colours::text.withAlpha (0.92f));
        as.setWordWrap (juce::AttributedString::byWord);
        layout.createLayout (as, 300.0f);
        setSize (316, (int) layout.getHeight() + 16);
        text = as;
    }
    void paint (juce::Graphics& g) override { layout.draw (g, getLocalBounds().reduced (8).toFloat()); }

private:
    juce::String title, body;
    juce::AttributedString text;
    juce::TextLayout layout;
};
} // namespace

InfoButton::InfoButton (juce::String t, juce::String b) : juce::Button ("info"), helpTitle (std::move (t)), helpBody (std::move (b))
{
    setTitle ("More information: " + helpTitle);
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void InfoButton::setHelp (juce::String t, juce::String b)
{
    helpTitle = std::move (t);
    helpBody = std::move (b);
}

void InfoButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (18.0f, 18.0f);
    g.setColour (over ? colours::accent2.withAlpha (0.2f) : juce::Colours::transparentBlack);
    g.fillEllipse (r);
    g.setColour (over ? colours::accent2 : colours::muted);
    g.drawEllipse (r, 1.2f);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold | juce::Font::italic));
    g.drawText ("i", r, juce::Justification::centred);
}

void InfoButton::clicked()
{
    auto content = std::make_unique<HelpContent> (helpTitle, helpBody);
    auto* top = getTopLevelComponent();
    auto area = top->getLocalArea (this, getLocalBounds());
    juce::CallOutBox::launchAsynchronously (std::move (content), area, top);
}

// ------------------------------------------------------------------------------------------
WaveformView::WaveformView (juce::AudioThumbnail& t) : thumb (t)
{
    thumb.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::IBeamCursor);
    setTitle ("Waveform. Drag to select a section.");
}

WaveformView::~WaveformView() { thumb.removeChangeListener (this); }

void WaveformView::setRange (double t, double s, double e)
{
    total = t;
    start = s;
    end = e;
    repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff101217));
    g.fillRoundedRectangle (r, 6.0f);
    if (total <= 0 || thumb.getTotalLength() <= 0)
    {
        g.setColour (colours::muted);
        g.drawText ("Drop a WAV or MP3 here, or click Open Audio File", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const float xa = (float) (start / total) * r.getWidth(), xb = (float) (end / total) * r.getWidth();
    g.setColour (colours::accent.withAlpha (0.13f));
    g.fillRect (juce::Rectangle<float> (xa, 0, xb - xa, r.getHeight()));
    g.setColour (juce::Colour (0xff4a5266));
    thumb.drawChannels (g, getLocalBounds().reduced (0, 6), 0.0, total, 1.0f);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<int> ((int) xa, 0, (int) (xb - xa) + 1, getHeight()));
        g.setColour (juce::Colour (0xffff6a9c));
        thumb.drawChannels (g, getLocalBounds().reduced (0, 6), 0.0, total, 1.0f);
    }
    g.setColour (colours::accent);
    g.fillRect (xa, 0.0f, 2.0f, r.getHeight());
    g.fillRect (xb - 2.0f, 0.0f, 2.0f, r.getHeight());
}

double WaveformView::xToTime (float x) const { return juce::jlimit (0.0, total, (double) x / getWidth() * total); }

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (total > 0)
        dragFrom = xToTime ((float) e.x);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0)
        return;
    const double t = xToTime ((float) e.x);
    if (std::abs (t - dragFrom) > 0.2)
    {
        start = juce::jmin (t, dragFrom);
        end = juce::jmax (t, dragFrom);
        repaint();
        if (onRegionChanged)
            onRegionChanged (start, end);
    }
}

void WaveformView::mouseUp (const juce::MouseEvent&) { dragFrom = -1; }

// ------------------------------------------------------------------------------------------
PreviewView::PreviewView (juce::AudioThumbnail& a, juce::AudioThumbnail& b) : ta (a), tb (b)
{
    ta.addChangeListener (this);
    tb.addChangeListener (this);
    setTitle ("Original (top) and converted (bottom). Click to move the playhead.");
}

PreviewView::~PreviewView()
{
    ta.removeChangeListener (this);
    tb.removeChangeListener (this);
}

void PreviewView::setPlayhead (double f, bool b)
{
    if (std::abs (f - playhead) > 1e-4 || b != showB)
    {
        playhead = f;
        showB = b;
        repaint();
    }
}

void PreviewView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (juce::Colour (0xff101217));
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    auto top = r.removeFromTop (r.getHeight() / 2).reduced (0, 3), bottom = r.reduced (0, 3);
    if (ta.getTotalLength() > 0)
    {
        g.setColour (colours::accent.withAlpha (showB ? 0.4f : 1.0f));
        ta.drawChannels (g, top, 0.0, ta.getTotalLength(), 1.0f);
    }
    if (tb.getTotalLength() > 0)
    {
        g.setColour (colours::accent2.withAlpha (showB ? 1.0f : 0.4f));
        tb.drawChannels (g, bottom, 0.0, tb.getTotalLength(), 1.0f);
    }
    else
    {
        g.setColour (colours::muted);
        g.drawText ("Converted vocal appears here", bottom, juce::Justification::centred);
    }
    g.setColour (juce::Colours::white);
    g.fillRect ((float) (playhead * getWidth()), 0.0f, 1.5f, (float) getHeight());
}

void PreviewView::mouseDown (const juce::MouseEvent& e)
{
    if (onSeek)
        onSeek ((double) e.x / juce::jmax (1, getWidth()));
}

// ------------------------------------------------------------------------------------------
DragFileButton::DragFileButton() : juce::TextButton ("Drag into FL Studio")
{
    setTooltip ("Drag this button into the FL Studio playlist to drop the exported WAV.");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragFileButton::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && file.existsAsFile() && e.getDistanceFromDragStart() > 6)
    {
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [this] { dragging = false; });
    }
}

// ------------------------------------------------------------------------------------------
SectionHeader::SectionHeader (int n, juce::String t) : num (n), title (std::move (t)) {}

void SectionHeader::setActive (bool a, bool d)
{
    active = a;
    isDone = d;
    repaint();
}

void SectionHeader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto badge = r.removeFromLeft (24).withSizeKeepingCentre (22, 22).toFloat();
    g.setColour (isDone ? colours::ok.withAlpha (0.25f) : active ? colours::accent.withAlpha (0.3f) : colours::panel2);
    g.fillRoundedRectangle (badge, 6.0f);
    g.setColour (isDone ? colours::ok : active ? colours::accent : colours::line);
    g.drawRoundedRectangle (badge, 6.0f, 1.0f);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (isDone ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93")) : juce::String (num), badge, juce::Justification::centred);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (title, r.withTrimmedLeft (8), juce::Justification::centredLeft);
}
} // namespace vb
