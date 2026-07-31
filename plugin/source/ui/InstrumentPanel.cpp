#include "InstrumentPanel.h"

namespace moe::plugin::ui
{

namespace
{
bool isBlackKey (int note)
{
    switch (note % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}
} // namespace

//==============================================================================
KeyswitchStrip::KeyswitchStrip (MyOrchestralProcessor& p)
    : processor (p)
{
    setTooltip ("Keyswitches. Click one to arm its articulation.");
    startTimerHz (15);
}

KeyswitchStrip::~KeyswitchStrip()
{
    stopTimer();
}

void KeyswitchStrip::setSection (int section)
{
    sectionIndex = section;

    lowestNote  = 12;
    highestNote = 36;

    if (const auto& instrument = processor.getEngine().getSection (section).getInstrument())
    {
        if (! instrument->keyswitches.empty())
        {
            lowestNote  = instrument->keyswitches.front().note;
            highestNote = instrument->keyswitches.back().note;

            // Pad out so a bank with one or two keyswitches still draws a strip
            // rather than a single enormous key.
            const int span = highestNote - lowestNote;
            if (span < 11)
            {
                const int padding = (12 - span) / 2;
                lowestNote  = juce::jmax (0, lowestNote - padding);
                highestNote = juce::jmin (127, highestNote + padding);
            }
        }
    }

    repaint();
}

juce::Rectangle<float> KeyswitchStrip::boundsForKey (int note) const
{
    const int count = juce::jmax (1, highestNote - lowestNote + 1);
    const float width = static_cast<float> (getWidth()) / static_cast<float> (count);

    return { static_cast<float> (note - lowestNote) * width, 0.0f,
             width, static_cast<float> (getHeight()) };
}

void KeyswitchStrip::paint (juce::Graphics& g)
{
    const auto& section = processor.getEngine().getSection (sectionIndex);
    const auto& instrument = section.getInstrument();

    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

    if (instrument == nullptr || instrument->keyswitches.empty())
    {
        g.setColour (colours::textFaint);
        g.setFont (OrchestralLookAndFeel::font (11.0f));
        g.drawText (instrument == nullptr ? "No bank loaded"
                                          : "This bank declares no keyswitches",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int active = section.keyswitches().getLastKeyswitchNote();

    for (int note = lowestNote; note <= highestNote; ++note)
    {
        const auto keyBounds = boundsForKey (note).reduced (0.5f, 0.0f);
        const auto* keyswitch = instrument->keyswitchForNote (note);

        juce::Colour fill = isBlackKey (note) ? colours::background : colours::panelRaised;

        if (keyswitch != nullptr)
            fill = note == active ? colours::accent : colours::accentDim.withAlpha (0.55f);

        g.setColour (fill);
        g.fillRoundedRectangle (keyBounds, 2.0f);

        g.setColour (colours::outline.withAlpha (0.6f));
        g.drawRoundedRectangle (keyBounds, 2.0f, 0.8f);

        if (keyswitch != nullptr && keyBounds.getWidth() > 22.0f)
        {
            g.setColour (note == active ? juce::Colours::black.withAlpha (0.8f) : colours::text);
            g.setFont (OrchestralLookAndFeel::font (9.0f, note == active));
            g.drawFittedText (juce::String (keyswitch->label),
                              keyBounds.toNearestInt().reduced (2, 2),
                              juce::Justification::centredBottom, 2);
        }
    }
}

void KeyswitchStrip::mouseDown (const juce::MouseEvent& event)
{
    const auto& instrument = processor.getEngine().getSection (sectionIndex).getInstrument();
    if (instrument == nullptr)
        return;

    for (const auto& keyswitch : instrument->keyswitches)
        if (boundsForKey (keyswitch.note).contains (event.position))
        {
            processor.setSectionArticulation (sectionIndex, keyswitch.articulation);
            repaint();
            return;
        }
}

void KeyswitchStrip::timerCallback()
{
    repaint();
}

//==============================================================================
InstrumentPanel::InstrumentPanel (MyOrchestralProcessor& p)
    : processor (p), keyswitchStrip (p)
{
    bankLabel.setFont (OrchestralLookAndFeel::font (15.0f, true));
    bankLabel.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (bankLabel);

    loadButton.onClick = [this] { chooseBank(); };
    addAndMakeVisible (loadButton);

    clearButton.onClick = [this]
    {
        processor.clearSection (sectionIndex);
        refreshFromEngine();
        if (onInstrumentChanged)
            onInstrumentChanged();
    };
    addAndMakeVisible (clearButton);

    midiChannelLabel.setFont (OrchestralLookAndFeel::font (11.0f));
    midiChannelLabel.setColour (juce::Label::textColourId, colours::textDim);
    midiChannelLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (midiChannelLabel);

    midiChannelBox.addItem ("Omni", 1);
    for (int channel = 1; channel <= 16; ++channel)
        midiChannelBox.addItem (juce::String (channel), channel + 1);

    midiChannelBox.setTooltip ("Which MIDI channel this section listens to");
    midiChannelBox.onChange = [this]
    {
        processor.setSectionMidiChannel (sectionIndex, midiChannelBox.getSelectedId() - 2);
    };
    addAndMakeVisible (midiChannelBox);

    addAndMakeVisible (keyswitchStrip);

    auto setUpKnob = [this] (LabelledKnob& knob, const juce::String& name,
                             const juce::String& tooltip, juce::Colour colour)
    {
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 15);
        knob.slider.setColour (juce::Slider::trackColourId, colour);
        knob.slider.setTooltip (tooltip);
        addAndMakeVisible (knob.slider);

        knob.label.setText (name, juce::dontSendNotification);
        knob.label.setFont (OrchestralLookAndFeel::font (10.0f));
        knob.label.setColour (juce::Label::textColourId, colours::textDim);
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);
    };

    setUpKnob (attackKnob,   "Attack",
               "Scales the bank's attack times. Below 1 tightens, above 1 softens.",
               colours::accent);
    setUpKnob (releaseKnob,  "Release",
               "Scales the bank's release times.", colours::accent);
    setUpKnob (distanceKnob, "Distance",
               "How far back this section sits, in metres.", colours::spatial);
    setUpKnob (widthKnob,    "Width",
               "How wide the section spreads across the image.", colours::spatial);
    setUpKnob (sendKnob,     "Hall",
               "How much of this section goes to the convolution reverb.", colours::spatial);

    setSection (0);
    startTimerHz (8);
}

InstrumentPanel::~InstrumentPanel()
{
    stopTimer();
}

void InstrumentPanel::setSection (int section)
{
    sectionIndex = juce::jlimit (0, params::numSections - 1, section);

    auto& state = processor.getParameters();

    // Attachments are rebuilt because they bind to a specific parameter, and the
    // panel follows whichever section is selected.
    attackAttachment.reset();
    releaseAttachment.reset();
    distanceAttachment.reset();
    widthAttachment.reset();
    sendAttachment.reset();

    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::sectionAttack (sectionIndex), attackKnob.slider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::sectionRelease (sectionIndex), releaseKnob.slider);
    distanceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::sectionDistance (sectionIndex), distanceKnob.slider);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::sectionWidth (sectionIndex), widthKnob.slider);
    sendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::sectionSend (sectionIndex), sendKnob.slider);

    keyswitchStrip.setSection (sectionIndex);
    refreshFromEngine();
}

void InstrumentPanel::refreshFromEngine()
{
    const auto& section = processor.getEngine().getSection (sectionIndex);
    const auto& instrument = section.getInstrument();

    bankLabel.setText (instrument != nullptr ? juce::String (instrument->name)
                                             : "Section " + juce::String (sectionIndex + 1)
                                                   + " — empty",
                       juce::dontSendNotification);

    midiChannelBox.setSelectedId (section.getMidiChannel() + 2, juce::dontSendNotification);

    if (instrument != nullptr)
    {
        detailText = juce::String (instrument->regions.size()) + " regions, "
                     + juce::String (instrument->samples.size()) + " samples  ·  keys "
                     + juce::MidiMessage::getMidiNoteName (instrument->lowestKey, true, true, 4)
                     + "–"
                     + juce::MidiMessage::getMidiNoteName (instrument->highestKey, true, true, 4)
                     + "  ·  " + juce::String (toName (instrument->family));
    }
    else
    {
        detailText = "Load an SFZ bank to play this section.";
    }

    clearButton.setEnabled (instrument != nullptr);

    rebuildArticulationButtons();
    keyswitchStrip.setSection (sectionIndex);
    resized();
    repaint();
}

void InstrumentPanel::rebuildArticulationButtons()
{
    articulationButtons.clear();
    articulationOrder.clear();

    const auto& instrument = processor.getEngine().getSection (sectionIndex).getInstrument();
    if (instrument == nullptr)
        return;

    for (const auto articulation : instrument->availableArticulations())
    {
        articulationOrder.push_back (articulation);

        auto* button = new juce::TextButton (juce::String (toName (articulation)));
        button->setClickingTogglesState (false);
        button->onClick = [this, articulation]
        {
            processor.setSectionArticulation (sectionIndex, articulation);
            repaint();
        };

        articulationButtons.add (button);
        addAndMakeVisible (button);
    }
}

void InstrumentPanel::chooseBank()
{
    const auto startingPoint = processor.getSectionBankFile (sectionIndex).existsAsFile()
                                   ? processor.getSectionBankFile (sectionIndex).getParentDirectory()
                                   : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    fileChooser = std::make_unique<juce::FileChooser> ("Load an SFZ bank",
                                                       startingPoint, "*.sfz");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (! file.existsAsFile())
            return;

        statusText = "Loading " + file.getFileName() + "...";
        repaint();

        processor.loadBankAsync (sectionIndex, file, [this] (bool success, juce::String message)
        {
            statusText = success ? juce::String() : "Load failed: " + message;

            refreshFromEngine();

            if (onInstrumentChanged)
                onInstrumentChanged();
        });
    });
}

void InstrumentPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    auto bounds = getLocalBounds().reduced (14, 10);

    bounds.removeFromTop (30);   // bank label
    bounds.removeFromTop (30);   // buttons

    g.setFont (OrchestralLookAndFeel::font (11.0f));
    g.setColour (colours::textDim);
    g.drawText (statusText.isNotEmpty() ? statusText : detailText,
                bounds.removeFromTop (18), juce::Justification::centredLeft);

    bounds.removeFromTop (6);
    OrchestralLookAndFeel::drawHeading (g, bounds.removeFromTop (16), "Articulation");

    // Mark the active articulation. Doing it in paint rather than with toggle
    // state keeps a keyswitch played on the keyboard and a click in the UI
    // showing identically.
    const auto active = processor.getEngine().getSection (sectionIndex)
                            .keyswitches().getArticulation();

    for (int index = 0; index < articulationButtons.size(); ++index)
    {
        if (static_cast<std::size_t> (index) >= articulationOrder.size())
            break;

        if (articulationOrder[static_cast<std::size_t> (index)] != active)
            continue;

        const auto area = articulationButtons[index]->getBounds().toFloat().expanded (1.5f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (area, 4.0f, 1.6f);
    }
}

void InstrumentPanel::resized()
{
    auto bounds = getLocalBounds().reduced (14, 10);

    bankLabel.setBounds (bounds.removeFromTop (30));

    auto buttonRow = bounds.removeFromTop (26);
    loadButton.setBounds (buttonRow.removeFromLeft (110));
    buttonRow.removeFromLeft (6);
    clearButton.setBounds (buttonRow.removeFromLeft (64));

    midiChannelBox.setBounds (buttonRow.removeFromRight (74));
    midiChannelLabel.setBounds (buttonRow.removeFromRight (38));

    bounds.removeFromTop (4);
    bounds.removeFromTop (18);    // detail text, drawn in paint
    bounds.removeFromTop (6);
    bounds.removeFromTop (16);    // "Articulation" heading

    // Articulation grid: four per row, wrapping.
    constexpr int buttonHeight = 26;
    constexpr int columns      = 4;
    constexpr int spacing      = 5;

    const int buttonWidth = (bounds.getWidth() - spacing * (columns - 1)) / columns;
    const int rows = (articulationButtons.size() + columns - 1) / columns;

    for (int index = 0; index < articulationButtons.size(); ++index)
    {
        const int row    = index / columns;
        const int column = index % columns;

        articulationButtons[index]->setBounds (
            bounds.getX() + column * (buttonWidth + spacing),
            bounds.getY() + row * (buttonHeight + spacing),
            buttonWidth, buttonHeight);
    }

    bounds.removeFromTop (juce::jmax (0, rows * (buttonHeight + spacing)) + 8);

    keyswitchStrip.setBounds (bounds.removeFromTop (44));
    bounds.removeFromTop (12);

    // Knob row.
    auto knobRow = bounds.removeFromTop (86);
    const int knobWidth = knobRow.getWidth() / 5;

    for (LabelledKnob* knob : { &attackKnob, &releaseKnob, &distanceKnob, &widthKnob, &sendKnob })
    {
        auto cell = knobRow.removeFromLeft (knobWidth);
        knob->label.setBounds (cell.removeFromTop (14));
        knob->slider.setBounds (cell.reduced (4, 0));
    }
}

void InstrumentPanel::timerCallback()
{
    // The articulation can change from a played keyswitch, so the highlight has
    // to follow the engine rather than only the UI.
    repaint();
}

} // namespace moe::plugin::ui
