#include "PluginEditor.h"

namespace moe::plugin
{

namespace
{
constexpr int kTopBarHeight  = 66;
constexpr int kMixerWidth    = 288;
constexpr int kStatusHeight  = 22;
}

MyOrchestralEditor::MyOrchestralEditor (MyOrchestralProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      sectionList (p),
      instrumentPanel (p),
      stageView (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (sectionList);
    addAndMakeVisible (instrumentPanel);
    addAndMakeVisible (stageView);

    sectionList.onSectionSelected = [this] (int section) { selectSection (section); };
    stageView.onSectionSelected   = [this] (int section) { selectSection (section); };
    instrumentPanel.onInstrumentChanged = [this]
    {
        sectionList.repaint();
        stageView.repaint();
    };

    auto setUpGlobalKnob = [this] (juce::Slider& slider, juce::Label& label,
                                   const juce::String& tooltip, juce::Colour colour)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 14);
        slider.setColour (juce::Slider::trackColourId, colour);
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);

        label.setFont (ui::OrchestralLookAndFeel::font (10.0f));
        label.setColour (juce::Label::textColourId, ui::colours::textDim);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    };

    setUpGlobalKnob (masterSlider, masterLabel, "Overall output level",
                     ui::colours::accent);
    setUpGlobalKnob (hallSlider, hallLabel,
                     "How much of the convolution reverb reaches the output",
                     ui::colours::spatial);
    setUpGlobalKnob (humaniseSlider, humaniseLabel,
                     "Depth of the per-note pitch, timing and level variation. "
                     "Zero makes every repeat identical.",
                     ui::colours::accent);
    setUpGlobalKnob (legatoSlider, legatoLabel,
                     "How much of a legato interval is glided. Higher suits strings, "
                     "lower suits brass and winds.",
                     ui::colours::accent);

    auto& state = processor.getParameters();
    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::masterGain, masterSlider);
    hallAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::reverbMix, hallSlider);
    humaniseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::humaniseDepth, humaniseSlider);
    legatoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::legatoAmount, legatoSlider);

    impulseButton.setTooltip ("Load a concert-hall impulse response (WAV)");
    impulseButton.onClick = [this] { chooseImpulseResponse(); };
    addAndMakeVisible (impulseButton);

    streamingLabel.setFont (ui::OrchestralLookAndFeel::font (10.0f));
    streamingLabel.setColour (juce::Label::textColourId, ui::colours::textDim);
    streamingLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (streamingLabel);

    streamingBox.addItem ("Live", 1);
    streamingBox.addItem ("Balanced", 2);
    streamingBox.addItem ("Render", 3);
    streamingBox.setSelectedId (static_cast<int> (processor.getStreamingProfile()) + 1,
                                juce::dontSendNotification);
    streamingBox.setTooltip ("Live: smallest buffers, lowest latency, most disk traffic.\n"
                             "Render: largest buffers, safest for bouncing.");
    streamingBox.onChange = [this]
    {
        processor.setStreamingProfile (
            static_cast<StreamingProfile> (streamingBox.getSelectedId() - 1));
    };
    addAndMakeVisible (streamingBox);

    selectSection (0);

    setResizable (true, true);
    setResizeLimits (960, 620, 2200, 1500);
    setSize (1180, 760);

    startTimerHz (6);
}

MyOrchestralEditor::~MyOrchestralEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MyOrchestralEditor::selectSection (int section)
{
    selectedSection = juce::jlimit (0, params::numSections - 1, section);

    sectionList.setSelectedSection (selectedSection);
    stageView.setSelectedSection (selectedSection);
    instrumentPanel.setSection (selectedSection);
}

void MyOrchestralEditor::chooseImpulseResponse()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load an impulse response",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (! file.existsAsFile())
            return;

        statusLine = processor.loadImpulseResponse (file)
                         ? "Hall: " + file.getFileName()
                         : "Could not read " + file.getFileName();
        repaint();
    });
}

void MyOrchestralEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::colours::background);

    // ---- top bar ---------------------------------------------------------
    auto topBar = getLocalBounds().removeFromTop (kTopBarHeight);

    g.setColour (ui::colours::panel);
    g.fillRect (topBar);
    g.setColour (ui::colours::outline);
    g.fillRect (topBar.removeFromBottom (1));

    // Title block: the two lines are stacked explicitly rather than nudged with
    // offsets, which is what made them collide.
    auto titleArea = topBar.withTrimmedLeft (16).withWidth (216).reduced (0, 10);

    g.setColour (ui::colours::text);
    g.setFont (ui::OrchestralLookAndFeel::font (19.0f, true));
    g.drawText ("MyOrchestral", titleArea.removeFromTop (24),
                juce::Justification::bottomLeft);

    g.setColour (ui::colours::textFaint);
    g.setFont (ui::OrchestralLookAndFeel::font (10.0f));
    g.drawText ("symphonic sampler", titleArea.removeFromTop (14),
                juce::Justification::topLeft);

    // ---- status bar ------------------------------------------------------
    auto status = getLocalBounds().removeFromBottom (kStatusHeight);

    g.setColour (ui::colours::panel);
    g.fillRect (status);
    g.setColour (ui::colours::outline);
    g.fillRect (status.removeFromTop (1));

    auto& engine = processor.getEngine();

    const juce::String diagnostics =
        juce::String (engine.getNumActiveVoices()) + " voices   ·   "
        + juce::String (engine.getNumActiveStreams()) + " streams   ·   "
        + juce::String (engine.getStreamUnderruns()) + " underruns   ·   "
        + juce::String (engine.getVoiceStealCount()) + " steals";

    g.setFont (ui::OrchestralLookAndFeel::font (11.0f));
    g.setColour (ui::colours::textFaint);
    g.drawText (diagnostics, status.withTrimmedLeft (12), juce::Justification::centredLeft);

    if (processor.isLoading())
    {
        g.setColour (ui::colours::accent);
        g.drawText ("Loading section " + juce::String (processor.getLoadingSection() + 1) + "...",
                    status.withTrimmedRight (12), juce::Justification::centredRight);
    }
    else if (statusLine.isNotEmpty())
    {
        g.setColour (ui::colours::textDim);
        g.drawText (statusLine, status.withTrimmedRight (12), juce::Justification::centredRight);
    }
}

void MyOrchestralEditor::resized()
{
    auto bounds = getLocalBounds();

    // ---- top bar ---------------------------------------------------------
    auto topBar = bounds.removeFromTop (kTopBarHeight).reduced (10, 6);
    topBar.removeFromLeft (230);   // title, drawn in paint

    auto placeKnob = [&topBar] (juce::Slider& slider, juce::Label& label)
    {
        auto cell = topBar.removeFromLeft (66);
        label.setBounds (cell.removeFromTop (12));
        slider.setBounds (cell);
        topBar.removeFromLeft (4);
    };

    placeKnob (masterSlider, masterLabel);
    placeKnob (hallSlider, hallLabel);
    placeKnob (humaniseSlider, humaniseLabel);
    placeKnob (legatoSlider, legatoLabel);

    auto rightControls = topBar.removeFromRight (210);
    auto qualityCell   = rightControls.removeFromRight (100);
    streamingLabel.setBounds (qualityCell.removeFromTop (12));
    streamingBox.setBounds (qualityCell.reduced (0, 3));

    rightControls.removeFromRight (8);
    impulseButton.setBounds (rightControls.removeFromRight (94).reduced (0, 10));

    // ---- status bar ------------------------------------------------------
    bounds.removeFromBottom (kStatusHeight);

    // ---- main area -------------------------------------------------------
    bounds.reduce (8, 8);

    sectionList.setBounds (bounds.removeFromLeft (kMixerWidth));
    bounds.removeFromLeft (8);

    // The instrument panel needs a fixed-ish height for its knob row; the stage
    // takes whatever is left, since it degrades gracefully at any size.
    const int panelHeight = juce::jlimit (300, 380, bounds.getHeight() / 2);

    instrumentPanel.setBounds (bounds.removeFromTop (panelHeight));
    bounds.removeFromTop (8);
    stageView.setBounds (bounds);
}

void MyOrchestralEditor::timerCallback()
{
    // Only the status bar changes at this rate; the views run their own timers.
    repaint (getLocalBounds().removeFromBottom (kStatusHeight));
}

} // namespace moe::plugin
