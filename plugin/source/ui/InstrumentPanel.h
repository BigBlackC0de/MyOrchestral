#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Theme.h"

namespace moe::plugin::ui
{

/** A keyboard strip showing which notes are keyswitches and which one is armed.

    Keyswitches are the single most confusing part of an orchestral library —
    they are invisible, silent, and vary per bank. Drawing them, live, with the
    active one lit, removes most of that confusion. Clicking one arms it. */
class KeyswitchStrip final : public juce::Component,
                             public  juce::SettableTooltipClient,
                             private juce::Timer
{
public:
    explicit KeyswitchStrip (MyOrchestralProcessor&);
    ~KeyswitchStrip() override;

    void setSection (int section);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> boundsForKey (int note) const;

    MyOrchestralProcessor& processor;
    int sectionIndex = 0;
    int lowestNote   = 12;
    int highestNote  = 36;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyswitchStrip)
};

/** Everything about the currently selected section: its bank, its
    articulations, its envelope and its place in the room. */
class InstrumentPanel final : public juce::Component,
                              private juce::Timer
{
public:
    explicit InstrumentPanel (MyOrchestralProcessor&);
    ~InstrumentPanel() override;

    void setSection (int section);
    void refreshFromEngine();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Fired when a bank load changes what the section is, so the rest of the
        editor can repaint. */
    std::function<void()> onInstrumentChanged;

private:
    void timerCallback() override;
    void rebuildArticulationButtons();
    void chooseBank();

    MyOrchestralProcessor& processor;
    int sectionIndex = 0;

    juce::Label      bankLabel;
    juce::TextButton loadButton  { "Load bank..." };
    juce::TextButton clearButton { "Clear" };
    juce::ComboBox   midiChannelBox;
    juce::Label      midiChannelLabel { {}, "MIDI" };

    juce::OwnedArray<juce::TextButton> articulationButtons;
    std::vector<Articulation>          articulationOrder;

    KeyswitchStrip keyswitchStrip;

    struct LabelledKnob
    {
        juce::Slider slider;
        juce::Label  label;
    };

    LabelledKnob attackKnob, releaseKnob, distanceKnob, widthKnob, sendKnob;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> distanceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sendAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String statusText;
    juce::String detailText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPanel)
};

} // namespace moe::plugin::ui
