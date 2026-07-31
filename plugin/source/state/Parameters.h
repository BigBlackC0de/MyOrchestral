#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "moe/OrchestraEngine.h"

namespace moe::plugin
{

/** Parameter identifiers and layout.

    Everything a user might automate lives here as an
    `AudioProcessorValueTreeState` parameter. Everything else — bank paths,
    the loaded impulse response, MIDI channel assignments — lives in a separate
    ValueTree, because a DAW cannot usefully automate a file path and exposing
    one as a parameter only clutters the automation list.

    The engine never reads these directly: the processor copies them into plain
    values once per block. That keeps the engine JUCE-free and pays the atomic
    loads once per block instead of once per sample. */
namespace params
{
    inline constexpr int numSections = OrchestraEngine::kMaxSections;

    // ---- global ----------------------------------------------------------
    inline constexpr const char* masterGain   = "master_gain";
    inline constexpr const char* reverbMix    = "reverb_mix";
    inline constexpr const char* humaniseDepth = "humanise_depth";
    inline constexpr const char* legatoAmount = "legato_amount";

    // ---- per section -----------------------------------------------------
    juce::String sectionGain (int section);
    juce::String sectionPan (int section);
    juce::String sectionDistance (int section);
    juce::String sectionWidth (int section);
    juce::String sectionSend (int section);
    juce::String sectionMute (int section);
    juce::String sectionAttack (int section);
    juce::String sectionRelease (int section);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}

/** Non-automatable state, serialised alongside the parameters. */
namespace state
{
    inline constexpr const char* tree            = "MyOrchestralState";
    inline constexpr const char* sectionNode     = "Section";
    inline constexpr const char* index           = "index";
    inline constexpr const char* bankPath        = "bankPath";
    inline constexpr const char* sectionName     = "name";
    inline constexpr const char* midiChannel     = "midiChannel";
    inline constexpr const char* articulation    = "articulation";
    inline constexpr const char* soloed          = "soloed";

    inline constexpr const char* impulsePath     = "impulsePath";
    inline constexpr const char* streamingProfile = "streamingProfile";
    inline constexpr const char* maxVoices       = "maxVoices";
}

} // namespace moe::plugin
