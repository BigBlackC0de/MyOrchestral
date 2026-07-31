#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/InstrumentPanel.h"
#include "ui/SectionList.h"
#include "ui/StageView.h"
#include "ui/Theme.h"

namespace moe::plugin
{

/** The plugin window.

    Layout follows how the tool is actually used: the mixer is always on screen
    because a composer needs to reach every desk while playing, the selected
    section's detail sits top right, and the stage occupies the bottom right
    where its spatial meaning is obvious next to the mixer's levels. */
class MyOrchestralEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit MyOrchestralEditor (MyOrchestralProcessor&);
    ~MyOrchestralEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectSection (int section);
    void chooseImpulseResponse();

    MyOrchestralProcessor& processor;

    ui::OrchestralLookAndFeel lookAndFeel;
    juce::TooltipWindow       tooltips { this, 600 };

    ui::SectionList     sectionList;
    ui::InstrumentPanel instrumentPanel;
    ui::StageView       stageView;

    // ---- top bar ----------------------------------------------------------
    juce::Slider     masterSlider;
    juce::Label      masterLabel   { {}, "Master" };
    juce::Slider     hallSlider;
    juce::Label      hallLabel     { {}, "Hall" };
    juce::Slider     humaniseSlider;
    juce::Label      humaniseLabel { {}, "Humanise" };
    juce::Slider     legatoSlider;
    juce::Label      legatoLabel   { {}, "Legato" };

    juce::TextButton impulseButton { "Load IR..." };
    juce::ComboBox   streamingBox;
    juce::Label      streamingLabel { {}, "Quality" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hallAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humaniseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> legatoAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String statusLine;
    int          selectedSection = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyOrchestralEditor)
};

} // namespace moe::plugin
