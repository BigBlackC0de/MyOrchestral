#include "Parameters.h"

namespace moe::plugin::params
{

namespace
{
/** Bumping this tells the host that parameter meanings changed. */
constexpr int parameterVersion = 1;

juce::String idFor (const char* suffix, int section)
{
    return "sec" + juce::String (section) + "_" + suffix;
}

juce::String labelFor (const char* suffix, int section)
{
    return "S" + juce::String (section + 1) + " " + suffix;
}
} // namespace

juce::String sectionGain (int section)     { return idFor ("gain", section); }
juce::String sectionPan (int section)      { return idFor ("pan", section); }
juce::String sectionDistance (int section) { return idFor ("distance", section); }
juce::String sectionWidth (int section)    { return idFor ("width", section); }
juce::String sectionSend (int section)     { return idFor ("send", section); }
juce::String sectionMute (int section)     { return idFor ("mute", section); }
juce::String sectionAttack (int section)   { return idFor ("attack", section); }
juce::String sectionRelease (int section)  { return idFor ("release", section); }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { masterGain, parameterVersion }, "Master",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { reverbMix, parameterVersion }, "Hall",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.28f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { humaniseDepth, parameterVersion }, "Humanise",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { legatoAmount, parameterVersion }, "Legato",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.75f));

    for (int section = 0; section < numSections; ++section)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sectionGain (section), parameterVersion },
            labelFor ("Gain", section),
            juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sectionPan (section), parameterVersion },
            labelFor ("Pan", section),
            juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));

        // Distance in metres: the unit is meaningful, so expose it as one
        // rather than as an abstract 0..1 "depth".
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sectionDistance (section), parameterVersion },
            labelFor ("Distance", section),
            juce::NormalisableRange<float> (1.0f, 30.0f, 0.1f), 8.0f,
            juce::AudioParameterFloatAttributes().withLabel ("m")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sectionWidth (section), parameterVersion },
            labelFor ("Width", section),
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.6f));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sectionSend (section), parameterVersion },
            labelFor ("Send", section),
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.25f));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { sectionMute (section), parameterVersion },
            labelFor ("Mute", section), false));

        // Multipliers on whatever the bank recorded, not absolute times: an
        // absolute attack would flatten the difference between a spiccato and a
        // sustain, which is precisely what the bank is there to express.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sectionAttack (section), parameterVersion },
            labelFor ("Attack", section),
            juce::NormalisableRange<float> (0.25f, 8.0f, 0.01f, 0.4f), 1.0f,
            juce::AudioParameterFloatAttributes().withLabel ("x")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { sectionRelease (section), parameterVersion },
            labelFor ("Release", section),
            juce::NormalisableRange<float> (0.1f, 8.0f, 0.01f, 0.4f), 1.0f,
            juce::AudioParameterFloatAttributes().withLabel ("x")));
    }

    return layout;
}

} // namespace moe::plugin::params
