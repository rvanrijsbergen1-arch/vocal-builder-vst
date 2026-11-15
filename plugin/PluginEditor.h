#pragma once
#include "Components.h"
#include "PluginProcessor.h"

namespace vb
{
class Content;

class VocalBuilderEditor : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    explicit VocalBuilderEditor (VocalBuilderProcessor&);
    ~VocalBuilderEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void openLicenceDialog();

    VocalBuilderProcessor& proc;
    VbLookAndFeel lnf;
    juce::TextButton licenceButton;
    juce::Label title, subtitle;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    double progressValue = 0.0;
    bool licenceWasActive = false;
    juce::ProgressBar progress { progressValue };
    juce::Label status;
    juce::TextButton cancelButton { "Cancel" };
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalBuilderEditor)
};
} // namespace vb
