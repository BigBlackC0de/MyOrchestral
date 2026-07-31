#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace moe::plugin::ui
{

/** The palette.

    Dark, low-saturation, warm — the interface should recede so the ear leads.
    Accents are used sparingly and always mean something: amber for the active
    articulation, a single cool tone for anything spatial. */
namespace colours
{
    inline const juce::Colour background   { 0xff14161a };
    inline const juce::Colour panel        { 0xff1c1f26 };
    inline const juce::Colour panelRaised  { 0xff242832 };
    inline const juce::Colour outline      { 0xff2f343f };

    inline const juce::Colour text         { 0xffd8dbe2 };
    inline const juce::Colour textDim      { 0xff8b91a0 };
    inline const juce::Colour textFaint    { 0xff5b6070 };

    inline const juce::Colour accent       { 0xffd9a441 };   ///< amber: active, selected
    inline const juce::Colour accentDim    { 0xff8a6a2c };
    inline const juce::Colour spatial      { 0xff5c8fb8 };   ///< cool: distance, width, hall
    inline const juce::Colour meter        { 0xff6fbf87 };
    inline const juce::Colour meterHot     { 0xffd4694a };
    inline const juce::Colour mute         { 0xffb85c5c };
    inline const juce::Colour solo         { 0xffd9a441 };

    /** One colour per instrument family, used on the stage view and the mixer so
        a section can be identified without reading its name. */
    juce::Colour forFamily (int family);
}

/** Shared look and feel: flat surfaces, thin arcs, no gradients or bevels.
    Everything here exists to make dense information readable at a glance. */
class OrchestralLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    OrchestralLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    /** Small caps-ish section heading, used all over the editor. */
    static void drawHeading (juce::Graphics&, juce::Rectangle<int> area, const juce::String& text);

    static juce::Font font (float height, bool bold = false);
};

} // namespace moe::plugin::ui
