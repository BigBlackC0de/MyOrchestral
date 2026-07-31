#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace moe::plugin
{

namespace
{
/** Sub-block size used to place MIDI events accurately inside a buffer.
    The engine handles any block length, so this is purely about timing
    resolution: 32 samples is well under a millisecond at any sample rate. */
constexpr int kMinimumSubBlock = 32;
}

MyOrchestralProcessor::MyOrchestralProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", params::createLayout())
{
    masterGainParameter = parameters.getRawParameterValue (params::masterGain);
    reverbMixParameter  = parameters.getRawParameterValue (params::reverbMix);
    humaniseParameter   = parameters.getRawParameterValue (params::humaniseDepth);
    legatoParameter     = parameters.getRawParameterValue (params::legatoAmount);

    for (int section = 0; section < params::numSections; ++section)
    {
        auto& cached = sectionParameters[static_cast<std::size_t> (section)];
        cached.gain     = parameters.getRawParameterValue (params::sectionGain (section));
        cached.pan      = parameters.getRawParameterValue (params::sectionPan (section));
        cached.distance = parameters.getRawParameterValue (params::sectionDistance (section));
        cached.width    = parameters.getRawParameterValue (params::sectionWidth (section));
        cached.send     = parameters.getRawParameterValue (params::sectionSend (section));
        cached.mute     = parameters.getRawParameterValue (params::sectionMute (section));
        cached.attack   = parameters.getRawParameterValue (params::sectionAttack (section));
        cached.release  = parameters.getRawParameterValue (params::sectionRelease (section));

        juce::ValueTree node (state::sectionNode);
        node.setProperty (state::index, section, nullptr);
        node.setProperty (state::midiChannel, section, nullptr);
        node.setProperty (state::sectionName, "Section " + juce::String (section + 1), nullptr);
        extraState.appendChild (node, nullptr);
    }

    loaderPool = std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions()
                                                          .withNumberOfThreads (1)
                                                          .withThreadName ("MyOrchestral bank loader"));
}

MyOrchestralProcessor::~MyOrchestralProcessor()
{
    bankLoader.cancel();
    loaderPool.reset();
}

//==============================================================================
void MyOrchestralProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    currentSampleRate = sampleRate;

    const int maxVoices = extraState.hasProperty (state::maxVoices)
                              ? static_cast<int> (extraState.getProperty (state::maxVoices))
                              : kDefaultMaxVoices;

    engine.prepare (sampleRate, maximumExpectedSamplesPerBlock, maxVoices);

    setLatencySamples (engine.getLatencySamples());
    pushParametersToEngine();
}

void MyOrchestralProcessor::releaseResources()
{
    engine.releaseResources();
}

bool MyOrchestralProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo out only. A multi-output build (one bus per section) is a natural
    // extension but changes how the mixer and the stage view are used, so it is
    // deliberately not a V1 feature.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

double MyOrchestralProcessor::getTailLengthSeconds() const
{
    // Long enough for a hall tail plus a slow string release.
    return 8.0;
}

//==============================================================================
void MyOrchestralProcessor::pushParametersToEngine()
{
    engine.setMasterGainDb (masterGainParameter->load());
    engine.setReverbMix (reverbMixParameter->load());

    const float humanise = humaniseParameter->load();
    const float legato   = legatoParameter->load();

    for (int index = 0; index < params::numSections; ++index)
    {
        const auto& cached  = sectionParameters[static_cast<std::size_t> (index)];
        auto&       section = engine.getSection (index);

        section.setGainDb (cached.gain->load());
        section.setMuted (cached.mute->load() > 0.5f);
        section.setEnvelopeScales (cached.attack->load(), cached.release->load());

        auto position = section.stage().getPosition();
        position.lateral  = cached.pan->load();
        position.distance = cached.distance->load();
        position.width    = cached.width->load();
        section.stage().setPosition (position);
        section.stage().setReverbSend (cached.send->load());

        auto humanSettings = section.humanizer().getSettings();
        humanSettings.depth = humanise;
        section.humanizer().setSettings (humanSettings);

        auto legatoSettings = section.legato().getSettings();
        legatoSettings.portamentoAmount = legato;
        legatoSettings.enabled = legato > 0.0f
                                 && (section.getInstrument() == nullptr
                                     || section.getInstrument()->family != Family::percussion);
        section.legato().setSettings (legatoSettings);
    }
}

void MyOrchestralProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    // Clear any channels the host gave us beyond our own output.
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    pushParametersToEngine();

    float* channels[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };

    // Walk the MIDI buffer and the audio buffer together so events land close to
    // where the host put them, rather than all at the start of the block.
    int position = 0;
    auto iterator = midi.begin();

    while (position < numSamples)
    {
        int nextEventTime = numSamples;

        // Apply every event due at or before this position.
        while (iterator != midi.end())
        {
            const auto metadata = *iterator;

            if (metadata.samplePosition > position)
            {
                nextEventTime = juce::jlimit (position, numSamples, metadata.samplePosition);
                break;
            }

            const auto message = metadata.getMessage();
            const int  channel = message.getChannel() - 1;   // JUCE is 1-based

            if (channel >= 0)
            {
                if (message.isNoteOn())
                    engine.noteOn (channel, message.getNoteNumber(), message.getVelocity());
                else if (message.isNoteOff())
                    engine.noteOff (channel, message.getNoteNumber());
                else if (message.isController())
                    engine.controller (channel, message.getControllerNumber(),
                                       message.getControllerValue());
                else if (message.isPitchWheel())
                    engine.pitchBend (channel,
                                      (message.getPitchWheelValue() - 8192) / 8192.0f * 2.0f);
                else if (message.isChannelPressure())
                    engine.channelPressure (channel, message.getChannelPressureValue() / 127.0f);
                else if (message.isAftertouch())
                    engine.channelPressure (channel, message.getAfterTouchValue() / 127.0f);
                else if (message.isAllNotesOff() || message.isAllSoundOff())
                    engine.allNotesOff (message.isAllSoundOff());
            }

            ++iterator;
        }

        // Round up to a minimum chunk so a dense MIDI stream cannot degenerate
        // into hundreds of one-sample render calls.
        const int chunk = juce::jlimit (1, numSamples - position,
                                        juce::jmax (nextEventTime - position, kMinimumSubBlock));

        float* chunkChannels[2] = { channels[0] + position, channels[1] + position };
        engine.process (chunkChannels, chunk);

        position += chunk;
    }

    // Copy to any extra output channels a host may have handed us.
    for (int channel = 2; channel < buffer.getNumChannels(); ++channel)
        buffer.copyFrom (channel, 0, buffer, channel % 2, 0, numSamples);
}

//==============================================================================
void MyOrchestralProcessor::loadBankAsync (int section,
                                           const juce::File& sfzFile,
                                           std::function<void (bool, juce::String)> onFinished)
{
    if (section < 0 || section >= params::numSections || ! sfzFile.existsAsFile())
    {
        if (onFinished)
            onFinished (false, "File not found");
        return;
    }

    loadingSection.store (section);
    loadProgress.store (0.0f);

    const auto path = sfzFile.getFullPathName();

    loaderPool->addJob ([this, section, path, onFinished]
    {
        bank::BankLoader::Options options;
        options.streaming = StreamingSettings::forProfile (engine.getStreamingProfile());

        auto result = bankLoader.loadSfzFile (path.toStdString(), options);

        loadProgress.store (1.0f);

        juce::StringArray warnings;
        for (const auto& warning : result.warnings)
            warnings.add (juce::String (warning));

        {
            const juce::ScopedLock lock (warningLock);
            lastWarnings = warnings;
        }

        const bool success = result.ok();
        auto instrument = result.instrument;
        const juce::String message = success
                                         ? juce::String (instrument->regions.size()) + " regions"
                                         : juce::String (result.error);

        // Installing the bank and touching the ValueTree both belong on the
        // message thread; the engine swaps it in behind a shared_ptr, so no
        // audio-thread coordination is needed.
        juce::MessageManager::callAsync ([this, section, path, success, message,
                                          instrument, onFinished]
        {
            if (success)
            {
                engine.getSection (section).setInstrument (instrument);

                auto node = extraState.getChild (section);
                if (node.isValid())
                {
                    node.setProperty (state::bankPath, path, nullptr);
                    node.setProperty (state::sectionName,
                                      juce::String (instrument->name), nullptr);
                }
            }

            loadingSection.store (-1);

            if (onFinished)
                onFinished (success, message);
        });
    });
}

void MyOrchestralProcessor::clearSection (int section)
{
    if (section < 0 || section >= params::numSections)
        return;

    engine.getSection (section).setInstrument (nullptr);

    auto node = extraState.getChild (section);
    if (node.isValid())
    {
        node.setProperty (state::bankPath, "", nullptr);
        node.setProperty (state::sectionName, "Section " + juce::String (section + 1), nullptr);
    }
}

bool MyOrchestralProcessor::loadImpulseResponse (const juce::File& irFile)
{
    if (! irFile.existsAsFile())
        return false;

    const bool loaded = engine.reverb().loadImpulseResponseFile (irFile.getFullPathName().toStdString());

    if (loaded)
    {
        extraState.setProperty (state::impulsePath, irFile.getFullPathName(), nullptr);
        setLatencySamples (engine.getLatencySamples());
    }

    return loaded;
}

juce::File MyOrchestralProcessor::getImpulseResponseFile() const
{
    return juce::File (extraState.getProperty (state::impulsePath).toString());
}

juce::File MyOrchestralProcessor::getSectionBankFile (int section) const
{
    const auto node = extraState.getChild (section);
    return node.isValid() ? juce::File (node.getProperty (state::bankPath).toString())
                          : juce::File();
}

void MyOrchestralProcessor::setSectionMidiChannel (int section, int channel)
{
    if (section < 0 || section >= params::numSections)
        return;

    engine.getSection (section).setMidiChannel (channel);

    if (auto node = extraState.getChild (section); node.isValid())
        node.setProperty (state::midiChannel, channel, nullptr);
}

void MyOrchestralProcessor::setSectionArticulation (int section, Articulation articulation)
{
    if (section < 0 || section >= params::numSections)
        return;

    engine.getSection (section).keyswitches().setArticulation (articulation);

    if (auto node = extraState.getChild (section); node.isValid())
        node.setProperty (state::articulation, static_cast<int> (articulation), nullptr);
}

void MyOrchestralProcessor::setSectionSoloed (int section, bool soloed)
{
    if (section < 0 || section >= params::numSections)
        return;

    engine.getSection (section).setSoloed (soloed);

    if (auto node = extraState.getChild (section); node.isValid())
        node.setProperty (state::soloed, soloed, nullptr);
}

void MyOrchestralProcessor::setStreamingProfile (StreamingProfile profile)
{
    engine.setStreamingProfile (profile);
    extraState.setProperty (state::streamingProfile, static_cast<int> (profile), nullptr);
}

juce::StringArray MyOrchestralProcessor::getLastLoadWarnings() const
{
    const juce::ScopedLock lock (warningLock);
    return lastWarnings;
}

//==============================================================================
void MyOrchestralProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    juce::ValueTree combined ("MyOrchestral");
    combined.appendChild (parameters.copyState(), nullptr);
    combined.appendChild (extraState.createCopy(), nullptr);

    if (auto xml = combined.createXml())
        copyXmlToBinary (*xml, destination);
}

void MyOrchestralProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto combined = juce::ValueTree::fromXml (*xml);
    if (! combined.isValid())
        return;

    if (const auto parameterState = combined.getChildWithName (parameters.state.getType());
        parameterState.isValid())
        parameters.replaceState (parameterState);

    const auto restored = combined.getChildWithName (state::tree);
    if (! restored.isValid())
        return;

    extraState = restored.createCopy();

    if (extraState.hasProperty (state::streamingProfile))
        engine.setStreamingProfile (static_cast<StreamingProfile> (
            static_cast<int> (extraState.getProperty (state::streamingProfile))));

    if (const juce::File ir (extraState.getProperty (state::impulsePath).toString());
        ir.existsAsFile())
        engine.reverb().loadImpulseResponseFile (ir.getFullPathName().toStdString());

    // Banks are referenced by path, not embedded: a session restore reloads
    // them, and a missing library is reported rather than silently ignored.
    for (int section = 0; section < params::numSections; ++section)
    {
        const auto node = extraState.getChild (section);
        if (! node.isValid())
            continue;

        engine.getSection (section).setMidiChannel (
            node.hasProperty (state::midiChannel)
                ? static_cast<int> (node.getProperty (state::midiChannel))
                : section);

        engine.getSection (section).setSoloed (
            static_cast<bool> (node.getProperty (state::soloed, false)));

        if (const juce::File bankFile (node.getProperty (state::bankPath).toString());
            bankFile.existsAsFile())
        {
            const auto articulation = node.hasProperty (state::articulation)
                                          ? static_cast<Articulation> (
                                                static_cast<int> (node.getProperty (state::articulation)))
                                          : Articulation::sustain;

            loadBankAsync (section, bankFile, [this, section, articulation] (bool ok, juce::String)
            {
                if (ok)
                    engine.getSection (section).keyswitches().setArticulation (articulation);
            });
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* MyOrchestralProcessor::createEditor()
{
    return new MyOrchestralEditor (*this);
}

} // namespace moe::plugin

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new moe::plugin::MyOrchestralProcessor();
}
