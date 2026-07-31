#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "Theme.h"

namespace moe::plugin::ui
{

/** The seating plan, drawn from the audience's point of view.

    Sections are dots on a stage; dragging one changes its distance and lateral
    position, which is what the engine's spatialiser actually consumes. This is
    the point of the view: panning and depth are one gesture, in the units a
    composer thinks in ("second violins, a bit further back and to the left"),
    instead of two abstract sliders.

    Meters ride on the dots so it is obvious which section is playing. */
class StageView final : public juce::Component,
                        private juce::Timer
{
public:
    explicit StageView (MyOrchestralProcessor&);
    ~StageView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    /** Called when the user clicks a section, so the editor can follow. */
    std::function<void (int section)> onSectionSelected;

    void setSelectedSection (int section);

private:
    void timerCallback() override;

    /** Maps a stage position (lateral -1..1, distance 1..30 m) to the view. */
    juce::Point<float> positionToPoint (float lateral, float distance) const;
    void               pointToPosition (juce::Point<float>, float& lateral, float& distance) const;

    int sectionAt (juce::Point<float>) const;
    juce::Rectangle<float> stageArea() const;

    MyOrchestralProcessor& processor;

    int selectedSection = 0;
    int hoveredSection  = -1;
    int draggedSection  = -1;

    std::array<float, params::numSections> meterLevels {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageView)
};

} // namespace moe::plugin::ui
