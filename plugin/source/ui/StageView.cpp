#include "StageView.h"

namespace moe::plugin::ui
{

namespace
{
constexpr float kMinDistance = 1.0f;
constexpr float kMaxDistance = 30.0f;
constexpr float kDotRadius   = 13.0f;
}

StageView::StageView (MyOrchestralProcessor& p)
    : processor (p)
{
    setWantsKeyboardFocus (false);
    startTimerHz (24);
}

StageView::~StageView()
{
    stopTimer();
}

void StageView::setSelectedSection (int section)
{
    selectedSection = section;
    repaint();
}

juce::Rectangle<float> StageView::stageArea() const
{
    return getLocalBounds().toFloat().reduced (24.0f, 20.0f);
}

juce::Point<float> StageView::positionToPoint (float lateral, float distance) const
{
    const auto area = stageArea();

    // The stage narrows towards the back, which matches both a real hall and
    // the way a seating chart is usually drawn.
    const float depth = juce::jmap (juce::jlimit (kMinDistance, kMaxDistance, distance),
                                    kMinDistance, kMaxDistance, 1.0f, 0.0f);

    const float halfWidth = juce::jmap (depth, 0.0f, 1.0f,
                                        area.getWidth() * 0.30f, area.getWidth() * 0.5f);

    return { area.getCentreX() + lateral * halfWidth,
             area.getY() + depth * (area.getHeight() - kDotRadius * 2.0f) + kDotRadius };
}

void StageView::pointToPosition (juce::Point<float> point, float& lateral, float& distance) const
{
    const auto area = stageArea();

    const float depth = juce::jlimit (0.0f, 1.0f,
                                      (point.y - area.getY() - kDotRadius)
                                          / juce::jmax (1.0f, area.getHeight() - kDotRadius * 2.0f));

    distance = juce::jmap (depth, 0.0f, 1.0f, kMaxDistance, kMinDistance);

    const float halfWidth = juce::jmap (depth, 0.0f, 1.0f,
                                        area.getWidth() * 0.30f, area.getWidth() * 0.5f);

    lateral = juce::jlimit (-1.0f, 1.0f, (point.x - area.getCentreX())
                                             / juce::jmax (1.0f, halfWidth));
}

int StageView::sectionAt (juce::Point<float> point) const
{
    // Front to back, so a section nearer the listener wins an overlap.
    int   best         = -1;
    float bestDistance = kDotRadius * 1.6f;

    for (int index = 0; index < params::numSections; ++index)
    {
        const auto& section = processor.getEngine().getSection (index);
        if (section.getInstrument() == nullptr)
            continue;

        const auto& position = section.stage().getPosition();
        const auto  centre   = positionToPoint (position.lateral, position.distance);
        const float distance = centre.getDistanceFrom (point);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            best         = index;
        }
    }

    return best;
}

void StageView::paint (juce::Graphics& g)
{
    const auto area = stageArea();

    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    // ---- the hall --------------------------------------------------------
    juce::Path stage;
    const auto frontLeft  = positionToPoint (-1.0f, kMinDistance);
    const auto frontRight = positionToPoint (1.0f, kMinDistance);
    const auto backLeft   = positionToPoint (-1.0f, kMaxDistance);
    const auto backRight  = positionToPoint (1.0f, kMaxDistance);

    stage.startNewSubPath (frontLeft);
    stage.lineTo (frontRight);
    stage.lineTo (backRight);
    stage.lineTo (backLeft);
    stage.closeSubPath();

    g.setColour (colours::panelRaised.withAlpha (0.55f));
    g.fillPath (stage);
    g.setColour (colours::outline);
    g.strokePath (stage, juce::PathStrokeType (1.0f));

    // Distance rings, labelled — the depth axis is meaningless without a scale.
    g.setFont (OrchestralLookAndFeel::font (10.0f));
    for (const float metres : { 5.0f, 10.0f, 15.0f, 20.0f, 25.0f })
    {
        const auto left  = positionToPoint (-1.0f, metres);
        const auto right = positionToPoint (1.0f, metres);

        g.setColour (colours::outline.withAlpha (0.5f));
        g.drawLine (left.x, left.y, right.x, right.y, 1.0f);

        g.setColour (colours::textFaint);
        g.drawText (juce::String (static_cast<int> (metres)) + " m",
                    juce::Rectangle<float> (area.getX() - 22.0f, left.y - 7.0f, 22.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    // The listener.
    const auto listener = juce::Point<float> (area.getCentreX(), area.getBottom() + 6.0f);
    g.setColour (colours::textFaint);
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (listener));
    g.setFont (OrchestralLookAndFeel::font (10.0f));
    g.drawText ("listener", juce::Rectangle<float> (listener.x - 40.0f, listener.y - 24.0f, 80.0f, 14.0f),
                juce::Justification::centred);

    // ---- the sections ----------------------------------------------------
    for (int index = 0; index < params::numSections; ++index)
    {
        const auto& section = processor.getEngine().getSection (index);
        if (section.getInstrument() == nullptr)
            continue;

        const auto& position = section.stage().getPosition();
        const auto  centre   = positionToPoint (position.lateral, position.distance);

        const auto family = static_cast<int> (section.getInstrument()->family);
        auto       colour = colours::forFamily (family);

        if (section.isMuted())
            colour = colour.withSaturation (0.1f).withAlpha (0.45f);

        // Width is drawn as a horizontal spread, so the knob has a visible meaning.
        const float spread = position.width * 46.0f;
        if (spread > 1.0f)
        {
            g.setColour (colour.withAlpha (0.16f));
            g.fillRoundedRectangle (juce::Rectangle<float> (spread * 2.0f, kDotRadius * 1.5f)
                                        .withCentre (centre), kDotRadius * 0.75f);
        }

        // The meter ring: a section that is playing lights up.
        const float level = meterLevels[static_cast<std::size_t> (index)];
        if (level > 0.001f)
        {
            g.setColour (colours::meter.withAlpha (juce::jlimit (0.0f, 0.85f, level * 2.2f)));
            g.drawEllipse (juce::Rectangle<float> (kDotRadius * 2.0f + 8.0f, kDotRadius * 2.0f + 8.0f)
                               .withCentre (centre), 2.0f);
        }

        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (kDotRadius * 2.0f, kDotRadius * 2.0f).withCentre (centre));

        if (index == selectedSection || index == hoveredSection)
        {
            g.setColour (index == selectedSection ? colours::text : colours::textDim);
            g.drawEllipse (juce::Rectangle<float> (kDotRadius * 2.0f + 4.0f, kDotRadius * 2.0f + 4.0f)
                               .withCentre (centre), 1.5f);
        }

        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.setFont (OrchestralLookAndFeel::font (11.0f, true));
        g.drawText (juce::String (index + 1),
                    juce::Rectangle<float> (kDotRadius * 2.0f, kDotRadius * 2.0f).withCentre (centre),
                    juce::Justification::centred);

        g.setColour (colours::textDim);
        g.setFont (OrchestralLookAndFeel::font (10.0f));
        g.drawText (section.getName(),
                    juce::Rectangle<float> (centre.x - 55.0f, centre.y + kDotRadius + 1.0f, 110.0f, 12.0f),
                    juce::Justification::centred);
    }

    if (processor.getEngine().getSection (selectedSection).getInstrument() == nullptr)
    {
        g.setColour (colours::textFaint);
        g.setFont (OrchestralLookAndFeel::font (13.0f));
        g.drawText ("Load a bank to place a section on the stage",
                    getLocalBounds(), juce::Justification::centred);
    }
}

void StageView::resized() {}

void StageView::mouseDown (const juce::MouseEvent& event)
{
    const int hit = sectionAt (event.position);

    if (hit >= 0)
    {
        draggedSection  = hit;
        selectedSection = hit;

        if (onSectionSelected)
            onSectionSelected (hit);

        repaint();
    }
}

void StageView::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedSection < 0)
        return;

    float lateral = 0.0f, distance = 8.0f;
    pointToPosition (event.position, lateral, distance);

    // Write through the parameters rather than the engine, so the move is
    // recorded by the host's automation and undo like any other control.
    auto& state = processor.getParameters();

    if (auto* panParameter = state.getParameter (params::sectionPan (draggedSection)))
        panParameter->setValueNotifyingHost (
            panParameter->convertTo0to1 (lateral));

    if (auto* distanceParameter = state.getParameter (params::sectionDistance (draggedSection)))
        distanceParameter->setValueNotifyingHost (
            distanceParameter->convertTo0to1 (distance));

    repaint();
}

void StageView::mouseUp (const juce::MouseEvent&)
{
    draggedSection = -1;
}

void StageView::mouseMove (const juce::MouseEvent& event)
{
    const int hit = sectionAt (event.position);

    if (hit != hoveredSection)
    {
        hoveredSection = hit;
        setMouseCursor (hit >= 0 ? juce::MouseCursor::DraggingHandCursor
                                 : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void StageView::timerCallback()
{
    bool changed = false;

    for (int index = 0; index < params::numSections; ++index)
    {
        const float level = processor.getEngine().getSection (index).getPeakLevel();
        auto&       meter = meterLevels[static_cast<std::size_t> (index)];

        // Fast attack, slow release — a meter that falls at the signal's rate is
        // unreadable at 24 fps.
        const float updated = level > meter ? level : meter * 0.82f;

        if (std::abs (updated - meter) > 0.001f)
        {
            meter   = updated;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

} // namespace moe::plugin::ui
