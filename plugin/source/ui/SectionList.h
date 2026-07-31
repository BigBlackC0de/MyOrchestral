#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Theme.h"

namespace moe::plugin::ui
{

/** One mixer row: name, mute/solo, level, meter. */
class SectionRow final : public juce::Component
{
public:
    SectionRow (MyOrchestralProcessor&, int sectionIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void setSelected (bool);
    void refresh();

    std::function<void (int)> onSelected;

private:
    MyOrchestralProcessor& processor;
    const int              sectionIndex;

    juce::Slider     gainSlider;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  gainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>  muteAttachment;

    bool  selected   = false;
    float meterLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionRow)
};

/** The full mixer: every section, always visible.

    A composer working on a template needs to see and reach all sixteen desks
    without navigating, which is why this is a permanent left-hand column rather
    than a tab. */
class SectionList final : public juce::Component,
                          private juce::Timer
{
public:
    explicit SectionList (MyOrchestralProcessor&);
    ~SectionList() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setSelectedSection (int);
    int  getSelectedSection() const noexcept { return selectedSection; }

    std::function<void (int)> onSectionSelected;

private:
    void timerCallback() override;

    MyOrchestralProcessor& processor;
    juce::OwnedArray<SectionRow> rows;
    juce::Viewport viewport;
    juce::Component rowHolder;

    int selectedSection = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionList)
};

} // namespace moe::plugin::ui
