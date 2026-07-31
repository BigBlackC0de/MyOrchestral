#include "Theme.h"

namespace moe::plugin::ui
{

juce::Colour colours::forFamily (int family)
{
    // Order matches moe::Family.
    static const juce::Colour palette[] = {
        juce::Colour (0xffc98a4b),   // strings   — warm amber
        juce::Colour (0xffb85c5c),   // brass     — red
        juce::Colour (0xff6f9e6a),   // woodwinds — green
        juce::Colour (0xff8a7fb0),   // percussion— violet
        juce::Colour (0xff5c8fb8),   // choir     — blue
        juce::Colour (0xff6d7484),   // other     — grey
    };

    const int count = static_cast<int> (sizeof (palette) / sizeof (palette[0]));
    return palette[juce::jlimit (0, count - 1, family)];
}

//==============================================================================
juce::Font OrchestralLookAndFeel::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions()
                           .withHeight (height)
                           .withStyle (bold ? "Bold" : "Regular"));
}

OrchestralLookAndFeel::OrchestralLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::DocumentWindow::textColourId,        colours::text);

    setColour (juce::Label::textColourId,                 colours::text);
    setColour (juce::Slider::textBoxTextColourId,         colours::text);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId,   colours::panel);
    setColour (juce::Slider::thumbColourId,               colours::accent);
    setColour (juce::Slider::trackColourId,               colours::accent);
    setColour (juce::Slider::backgroundColourId,          colours::outline);

    setColour (juce::TextButton::buttonColourId,          colours::panelRaised);
    setColour (juce::TextButton::buttonOnColourId,        colours::accentDim);
    setColour (juce::TextButton::textColourOffId,         colours::textDim);
    setColour (juce::TextButton::textColourOnId,          colours::text);

    setColour (juce::ComboBox::backgroundColourId,        colours::panelRaised);
    setColour (juce::ComboBox::textColourId,              colours::text);
    setColour (juce::ComboBox::outlineColourId,           colours::outline);
    setColour (juce::ComboBox::arrowColourId,             colours::textDim);

    setColour (juce::PopupMenu::backgroundColourId,       colours::panel);
    setColour (juce::PopupMenu::textColourId,             colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId,  juce::Colours::white);

    setColour (juce::TooltipWindow::backgroundColourId,   colours::panelRaised);
    setColour (juce::TooltipWindow::textColourId,         colours::text);
    setColour (juce::TooltipWindow::outlineColourId,      colours::outline);

    setColour (juce::ScrollBar::thumbColourId,            colours::outline);
}

void OrchestralLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPosProportional,
                                              float rotaryStartAngle, float rotaryEndAngle,
                                              juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float centreX = bounds.getCentreX();
    const float centreY = bounds.getCentreY();
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const float thickness = juce::jmax (2.0f, radius * 0.16f);

    const auto trackColour = slider.findColour (juce::Slider::backgroundColourId);
    const auto fillColour  = slider.findColour (juce::Slider::trackColourId);

    juce::Path track;
    track.addCentredArc (centreX, centreY, radius - thickness, radius - thickness,
                         0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (trackColour);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    if (sliderPosProportional > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centreX, centreY, radius - thickness, radius - thickness,
                             0.0f, rotaryStartAngle, angle, true);
        g.setColour (slider.isEnabled() ? fillColour : fillColour.withAlpha (0.4f));
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    // A short radial tick reads as a position far faster than the arc alone.
    juce::Path pointer;
    const float pointerLength = radius * 0.42f;
    pointer.addRectangle (-1.0f, -radius + thickness * 0.5f, 2.0f, pointerLength);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centreX, centreY));

    g.setColour (colours::text);
    g.fillPath (pointer);
}

void OrchestralLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float, float,
                                              juce::Slider::SliderStyle style,
                                              juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const bool vertical = style == juce::Slider::LinearVertical
                          || style == juce::Slider::LinearBarVertical;

    const float thickness = 4.0f;

    auto track = vertical
                     ? juce::Rectangle<float> (bounds.getCentreX() - thickness * 0.5f, bounds.getY(),
                                                thickness, bounds.getHeight())
                     : juce::Rectangle<float> (bounds.getX(), bounds.getCentreY() - thickness * 0.5f,
                                                bounds.getWidth(), thickness);

    g.setColour (slider.findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (track, thickness * 0.5f);

    auto filled = track;
    if (vertical)
        filled = filled.withTop (sliderPos);
    else
        filled = filled.withRight (sliderPos);

    g.setColour (slider.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (filled, thickness * 0.5f);

    const float thumbRadius = 5.0f;
    const auto thumbCentre = vertical
                                 ? juce::Point<float> (bounds.getCentreX(), sliderPos)
                                 : juce::Point<float> (sliderPos, bounds.getCentreY());

    g.setColour (colours::text);
    g.fillEllipse (juce::Rectangle<float> (thumbRadius * 2.0f, thumbRadius * 2.0f)
                       .withCentre (thumbCentre));
}

void OrchestralLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour& backgroundColour,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

    auto fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.brighter (0.25f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter (0.12f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (button.getToggleState() ? colours::accent.withAlpha (0.8f) : colours::outline);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
}

void OrchestralLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                            bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId));

    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0),
                      juce::Justification::centred, 1);
}

void OrchestralLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                          int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    juce::Path arrow;
    const float centreY = bounds.getCentreY();
    const float right   = bounds.getRight() - 10.0f;
    arrow.startNewSubPath (right - 4.0f, centreY - 2.0f);
    arrow.lineTo (right, centreY + 2.5f);
    arrow.lineTo (right + 4.0f, centreY - 2.0f);

    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.4f));
}

juce::Font OrchestralLookAndFeel::getLabelFont (juce::Label& label)
{
    return font (juce::jlimit (11.0f, 15.0f, static_cast<float> (label.getHeight()) * 0.62f));
}

juce::Font OrchestralLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (juce::jlimit (11.0f, 14.0f, static_cast<float> (buttonHeight) * 0.5f));
}

juce::Font OrchestralLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return font (13.0f);
}

void OrchestralLookAndFeel::drawHeading (juce::Graphics& g, juce::Rectangle<int> area,
                                         const juce::String& text)
{
    g.setFont (font (11.0f, true));
    g.setColour (colours::textFaint);
    g.drawText (text.toUpperCase(), area, juce::Justification::centredLeft);

    const int textWidth = juce::roundToInt (font (11.0f, true).getStringWidthFloat (text.toUpperCase()));
    const auto line = area.withTrimmedLeft (textWidth + 10)
                          .withHeight (1)
                          .withY (area.getCentreY());

    if (line.getWidth() > 0)
    {
        g.setColour (colours::outline);
        g.fillRect (line);
    }
}

} // namespace moe::plugin::ui
