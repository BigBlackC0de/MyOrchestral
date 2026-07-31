#include "SectionList.h"

namespace moe::plugin::ui
{

namespace
{
constexpr int kRowHeight = 44;
}

//==============================================================================
SectionRow::SectionRow (MyOrchestralProcessor& p, int index)
    : processor (p), sectionIndex (index)
{
    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    gainSlider.setTooltip ("Level for this section");
    addAndMakeVisible (gainSlider);

    muteButton.setClickingTogglesState (true);
    muteButton.setTooltip ("Mute");
    muteButton.setColour (juce::TextButton::buttonOnColourId, colours::mute);
    addAndMakeVisible (muteButton);

    soloButton.setClickingTogglesState (true);
    soloButton.setTooltip ("Solo");
    soloButton.setColour (juce::TextButton::buttonOnColourId, colours::solo);
    soloButton.onClick = [this]
    {
        processor.setSectionSoloed (sectionIndex, soloButton.getToggleState());
    };
    addAndMakeVisible (soloButton);

    auto& state = processor.getParameters();
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::sectionGain (sectionIndex), gainSlider);
    muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, params::sectionMute (sectionIndex), muteButton);
}

void SectionRow::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void SectionRow::refresh()
{
    const float level = processor.getEngine().getSection (sectionIndex).getPeakLevel();
    const float updated = level > meterLevel ? level : meterLevel * 0.80f;

    if (std::abs (updated - meterLevel) > 0.002f)
    {
        meterLevel = updated;
        repaint();
    }

    soloButton.setToggleState (processor.getEngine().getSection (sectionIndex).isSoloed(),
                               juce::dontSendNotification);
}

void SectionRow::mouseDown (const juce::MouseEvent&)
{
    if (onSelected)
        onSelected (sectionIndex);
}

void SectionRow::paint (juce::Graphics& g)
{
    const auto& section = processor.getEngine().getSection (sectionIndex);
    const bool  loaded  = section.getInstrument() != nullptr;

    auto bounds = getLocalBounds().reduced (2, 1);

    g.setColour (selected ? colours::panelRaised : colours::panel);
    g.fillRoundedRectangle (bounds.toFloat(), 3.0f);

    if (selected)
    {
        g.setColour (colours::accent.withAlpha (0.65f));
        g.fillRoundedRectangle (bounds.removeFromLeft (3).toFloat(), 1.5f);
    }
    else
    {
        bounds.removeFromLeft (3);
    }

    // Family stripe: identifies the desk at a glance, matching the stage view.
    const auto family = loaded ? static_cast<int> (section.getInstrument()->family) : 5;
    g.setColour (loaded ? colours::forFamily (family) : colours::outline);
    g.fillRect (bounds.removeFromLeft (3).reduced (0, 6));

    bounds.removeFromLeft (5);

    auto textArea = bounds.removeFromTop (17);

    g.setFont (OrchestralLookAndFeel::font (12.0f, loaded));
    g.setColour (loaded ? colours::text : colours::textFaint);
    g.drawText (juce::String (sectionIndex + 1) + "  " + section.getName(),
                textArea.withTrimmedRight (58), juce::Justification::centredLeft);

    if (loaded)
    {
        // The active articulation is the single most useful thing to see while
        // playing, so it lives in the mixer row rather than only in the editor.
        g.setFont (OrchestralLookAndFeel::font (10.0f));
        g.setColour (colours::accent.withAlpha (0.9f));
        g.drawText (juce::String (toName (section.keyswitches().getArticulation())),
                    textArea.removeFromRight (58), juce::Justification::centredRight);
    }

    // Meter, under the fader.
    const auto meterArea = getLocalBounds().removeFromBottom (3).reduced (14, 0)
                                            .withTrimmedRight (44);
    g.setColour (colours::outline.withAlpha (0.6f));
    g.fillRect (meterArea);

    if (meterLevel > 0.001f)
    {
        const float normalised = juce::jlimit (0.0f, 1.0f,
                                                juce::Decibels::gainToDecibels (meterLevel, -48.0f) / 48.0f + 1.0f);

        g.setColour (meterLevel > 0.9f ? colours::meterHot : colours::meter);
        g.fillRect (meterArea.withWidth (juce::roundToInt (meterArea.getWidth() * normalised)));
    }
}

void SectionRow::resized()
{
    auto bounds = getLocalBounds().reduced (13, 2);
    bounds.removeFromTop (17);
    bounds.removeFromBottom (4);

    auto buttons = bounds.removeFromRight (44);
    soloButton.setBounds (buttons.removeFromRight (20).reduced (1));
    buttons.removeFromRight (2);
    muteButton.setBounds (buttons.removeFromRight (20).reduced (1));

    gainSlider.setBounds (bounds.withTrimmedRight (4));
}

//==============================================================================
SectionList::SectionList (MyOrchestralProcessor& p)
    : processor (p)
{
    for (int index = 0; index < params::numSections; ++index)
    {
        auto* row = new SectionRow (processor, index);
        row->onSelected = [this] (int section)
        {
            setSelectedSection (section);
            if (onSectionSelected)
                onSectionSelected (section);
        };

        rows.add (row);
        rowHolder.addAndMakeVisible (row);
    }

    rowHolder.setSize (100, params::numSections * kRowHeight);

    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    setSelectedSection (0);
    startTimerHz (20);
}

SectionList::~SectionList()
{
    stopTimer();
}

void SectionList::setSelectedSection (int section)
{
    selectedSection = juce::jlimit (0, params::numSections - 1, section);

    for (int index = 0; index < rows.size(); ++index)
        rows[index]->setSelected (index == selectedSection);
}

void SectionList::paint (juce::Graphics& g)
{
    g.setColour (colours::background);
    g.fillAll();

    OrchestralLookAndFeel::drawHeading (g, getLocalBounds().removeFromTop (22).reduced (8, 4),
                                        "Orchestra");
}

void SectionList::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (22);

    viewport.setBounds (bounds);

    const int rowWidth = viewport.getMaximumVisibleWidth();
    rowHolder.setSize (rowWidth, params::numSections * kRowHeight);

    for (int index = 0; index < rows.size(); ++index)
        rows[index]->setBounds (0, index * kRowHeight, rowWidth, kRowHeight);
}

void SectionList::timerCallback()
{
    for (auto* row : rows)
        row->refresh();
}

} // namespace moe::plugin::ui
