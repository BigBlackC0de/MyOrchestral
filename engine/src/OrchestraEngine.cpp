#include "moe/OrchestraEngine.h"

#include <algorithm>
#include <cmath>

namespace moe
{

namespace
{
/** Layers that may sound simultaneously for one note. Four covers a dynamic
    crossfade plus a noise/attack layer, which is as deep as real banks go. */
constexpr int kMaxLayersPerNote = 4;

/** One streaming slot per potentially streaming voice would be wasteful; most
    voices play short samples that never leave the resident head. */
constexpr int kStreamSlotsPerVoicePool = 3;   // divisor, not multiplier
}

//==============================================================================
void Section::prepare (double newSampleRate, int maximumBlockSize)
{
    stageProcessor.prepare (newSampleRate, maximumBlockSize);
    selectionState.reset();
    peakLevel.store (0.0f, std::memory_order_relaxed);
}

void Section::setInstrument (bank::InstrumentPtr newInstrument)
{
    instrument = std::move (newInstrument);

    router.setInstrument (instrument.get());
    selectionState.reset();

    if (instrument != nullptr)
    {
        if (! instrument->name.empty())
            setDefaultName (instrument->name);

        humanizerInstance.setSettings (humanize::Humanizer::defaultsFor (instrument->family));

        // Percussion should not glide; everything else benefits from it.
        auto legatoSettings = legatoDetector.getSettings();
        legatoSettings.enabled = instrument->family != Family::percussion;
        legatoSettings.portamentoAmount =
            instrument->family == Family::strings ? 0.85f : 0.45f;
        legatoDetector.setSettings (legatoSettings);
    }
}

void Section::setGainDb (float db) noexcept
{
    gainDb     = db;
    linearGain = db <= -60.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
    stageProcessor.setGain (linearGain);
}

//==============================================================================
OrchestraEngine::OrchestraEngine()
{
    for (int i = 0; i < kMaxSections; ++i)
    {
        sections[static_cast<std::size_t> (i)].setMidiChannel (i);
        sections[static_cast<std::size_t> (i)].setDefaultName ("Section " + std::to_string (i + 1));
    }
}

OrchestraEngine::~OrchestraEngine()
{
    releaseResources();
}

float OrchestraEngine::decibelsToGain (float db) noexcept
{
    return db <= -100.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
}

void OrchestraEngine::prepare (double newSampleRate, int maximumBlockSize, int maxVoices)
{
    sampleRate   = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    maxBlockSize = std::max (16, maximumBlockSize);

    voices.prepare (sampleRate, maxBlockSize, maxVoices);

    const auto settings = StreamingSettings::forProfile (streamingProfile);
    streamer.prepare (std::max (8, maxVoices / kStreamSlotsPerVoicePool), 2, settings);
    voices.setStreamManager (&streamer);

    convolution.prepare (sampleRate, maxBlockSize);

    for (auto& channel : channels)
        channel.reset();

    for (auto& section : sections)
        section.prepare (sampleRate, maxBlockSize);

    const auto blockFloats = static_cast<std::size_t> (maxBlockSize);
    sectionBusStorage.assign (blockFloats * 2, 0.0f);
    reverbBusStorage.assign (blockFloats * 2, 0.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        sectionBus[static_cast<std::size_t> (ch)] =
            sectionBusStorage.data() + static_cast<std::size_t> (ch) * blockFloats;
        reverbBus[static_cast<std::size_t> (ch)] =
            reverbBusStorage.data() + static_cast<std::size_t> (ch) * blockFloats;
    }

    setMasterGainDb (masterGainDb);
}

void OrchestraEngine::releaseResources()
{
    voices.allNotesOff (true);
    voices.setStreamManager (nullptr);
    streamer.shutdown();
}

space::StagePosition OrchestraEngine::setSectionInstrument (int index, bank::InstrumentPtr instrument)
{
    if (index < 0 || index >= kMaxSections)
        return {};

    auto& section = sections[static_cast<std::size_t> (index)];
    section.setInstrument (instrument);

    if (instrument == nullptr)
        return section.stage().getPosition();

    // Count the desks of this family already seated, so the second violins take
    // the second string seat rather than landing on the firsts.
    int seat = 0;
    for (int other = 0; other < kMaxSections; ++other)
    {
        if (other == index)
            continue;

        const auto& sibling = sections[static_cast<std::size_t> (other)].getInstrument();
        if (sibling != nullptr && sibling->family == instrument->family)
            ++seat;
    }

    const auto position = space::StagePosition::defaultFor (instrument->family, seat);
    section.stage().setPosition (position);
    return position;
}

void OrchestraEngine::setStreamingProfile (StreamingProfile profile)
{
    if (profile == streamingProfile)
        return;

    streamingProfile = profile;

    // Restarting the streamer drops every in-flight slot, so stop the voices
    // that reference them first.
    voices.allNotesOff (true);

    const auto settings = StreamingSettings::forProfile (streamingProfile);
    streamer.prepare (std::max (8, voices.getNumVoices() / kStreamSlotsPerVoicePool), 2, settings);
    voices.setStreamManager (&streamer);
}

void OrchestraEngine::setMasterGainDb (float db) noexcept
{
    masterGainDb = db;
    masterGain   = decibelsToGain (db);
}

int OrchestraEngine::getLatencySamples() const noexcept
{
    return convolution.hasImpulseResponse() ? convolution.getLatencySamples() : 0;
}

bool OrchestraEngine::anySectionSoloed() const noexcept
{
    for (const auto& section : sections)
        if (section.isSoloed())
            return true;

    return false;
}

//==============================================================================
void OrchestraEngine::noteOn (int channel, int note, int velocity)
{
    if (channel < 0 || channel >= kNumMidiChannels || note < 0 || note > 127)
        return;

    if (velocity <= 0)
    {
        noteOff (channel, note);
        return;
    }

    auto& channelState = channels[static_cast<std::size_t> (channel)];

    for (int sectionIndex = 0; sectionIndex < static_cast<int> (sections.size()); ++sectionIndex)
    {
        auto& section = sections[static_cast<std::size_t> (sectionIndex)];

        if (! section.listensTo (channel) || section.getInstrument() == nullptr)
            continue;

        const auto& instrument = *section.getInstrument();

        // A keyswitch selects an articulation and must not sound.
        if (section.keyswitches().handleNoteOn (note))
            continue;

        // Legato is analysed against the state *before* this note is registered.
        const auto legato = section.legato().analyse (channelState, note);

        bank::SelectionContext context;
        context.note          = note;
        context.velocity      = velocity;
        context.articulation  = section.keyswitches().getArticulation();
        context.trigger       = legato.isLegato ? bank::TriggerMode::legato
                                                 : bank::TriggerMode::attack;
        context.lastKeyswitch = section.keyswitches().getLastKeyswitchNote();
        context.ccValues      = channelState.controllerArray();

        bank::SelectedRegion selected[kMaxLayersPerNote];
        int numSelected = bank::selectRegions (instrument, context, section.selection(),
                                                selected, kMaxLayersPerNote);

        // Banks that provide no dedicated legato regions still play legato: fall
        // back to normal attack regions and let portamento carry the transition.
        if (numSelected == 0 && context.trigger == bank::TriggerMode::legato)
        {
            context.trigger = bank::TriggerMode::attack;
            numSelected = bank::selectRegions (instrument, context, section.selection(),
                                                selected, kMaxLayersPerNote);
        }

        if (numSelected == 0)
            continue;

        const float dynamics  = channelState.getNormalisedController (perf::cc::modulation);
        const float expression = channelState.getNormalisedController (perf::cc::expression);

        for (int layer = 0; layer < numSelected; ++layer)
        {
            const auto& region = instrument.regions[
                static_cast<std::size_t> (selected[layer].regionIndex)];

            const auto variation = section.humanizer().next();

            voice::VoiceStartInfo info;
            info.instrument    = &instrument;
            info.regionIndex   = selected[layer].regionIndex;
            info.midiNote      = note;
            info.midiChannel   = channel;
            info.velocity      = velocity;
            info.sectionId     = sectionIndex;
            info.selectionGain = selected[layer].gain;

            info.attackScale  = section.getAttackScale();
            info.releaseScale = section.getReleaseScale();

            if (legato.isLegato)
            {
                info.portamentoSemitones = legato.portamentoSemitones;
                info.portamentoSeconds   = legato.portamentoSeconds;
                info.attackScale        *= legato.attackScale;
            }

            info.humanisePitchCents   = variation.pitchCents;
            info.humaniseGainDb       = variation.gainDb;
            info.humaniseDelaySeconds = variation.delaySeconds;

            // An exclusive group silences its own previous notes before the new
            // one starts, which is how mute groups and cutting articulations work.
            if (region.offBy != 0)
                voices.silenceGroup (sectionIndex, region.offBy);

            if (auto* startedVoice = voices.startVoice (info))
            {
                startedVoice->setDynamics (dynamics);
                startedVoice->setExpression (expression);
                startedVoice->setSectionGain (1.0f);
                startedVoice->setPitchBendSemitones (channelState.getPitchBend());
            }
        }

        // Retire the note being left behind so the line stays monophonic through
        // the transition instead of piling notes up.
        if (legato.isLegato && legato.previousNote != kNoNote)
            voices.releaseNote (sectionIndex, legato.previousNote,
                                section.legato().getSettings().previousNoteRelease);
    }

    channelState.noteOn (note);
}

void OrchestraEngine::noteOff (int channel, int note)
{
    if (channel < 0 || channel >= kNumMidiChannels || note < 0 || note > 127)
        return;

    auto& channelState = channels[static_cast<std::size_t> (channel)];
    channelState.noteOff (note);

    for (auto& section : sections)
        if (section.listensTo (channel) && section.getInstrument() != nullptr)
            section.keyswitches().handleNoteOff (note);

    voices.noteOff (channel, note, channelState.isSustainDown());
}

void OrchestraEngine::controller (int channel, int number, int value)
{
    if (channel < 0 || channel >= kNumMidiChannels)
        return;

    auto& channelState = channels[static_cast<std::size_t> (channel)];
    const bool sustainWasDown = channelState.isSustainDown();

    channelState.setController (number, value);

    if (number == perf::cc::sustainPedal && sustainWasDown && ! channelState.isSustainDown())
        voices.releaseSustained (channel);

    const float normalised = static_cast<float> (value) / 127.0f;

    for (int sectionIndex = 0; sectionIndex < static_cast<int> (sections.size()); ++sectionIndex)
    {
        auto& section = sections[static_cast<std::size_t> (sectionIndex)];
        if (! section.listensTo (channel))
            continue;

        section.keyswitches().handleController (number, value);

        if (number == perf::cc::modulation)
            voices.setDynamics (sectionIndex, normalised);
        else if (number == perf::cc::expression)
            voices.setExpression (sectionIndex, normalised);
    }
}

void OrchestraEngine::pitchBend (int channel, float semitones)
{
    if (channel < 0 || channel >= kNumMidiChannels)
        return;

    channels[static_cast<std::size_t> (channel)].setPitchBend (semitones);

    for (int sectionIndex = 0; sectionIndex < static_cast<int> (sections.size()); ++sectionIndex)
        if (sections[static_cast<std::size_t> (sectionIndex)].listensTo (channel))
            voices.setPitchBend (sectionIndex, semitones);
}

void OrchestraEngine::channelPressure (int channel, float value)
{
    if (channel < 0 || channel >= kNumMidiChannels)
        return;

    channels[static_cast<std::size_t> (channel)].setChannelPressure (value);

    // Aftertouch doubles the dynamics control for players whose keyboard has it,
    // which keeps expression available without a spare hand for the mod wheel.
    for (int sectionIndex = 0; sectionIndex < static_cast<int> (sections.size()); ++sectionIndex)
    {
        auto& section = sections[static_cast<std::size_t> (sectionIndex)];
        if (! section.listensTo (channel))
            continue;

        const auto& state = channels[static_cast<std::size_t> (channel)];
        const float dynamics = std::max (state.getNormalisedController (perf::cc::modulation),
                                          value);
        voices.setDynamics (sectionIndex, dynamics);
    }
}

void OrchestraEngine::allNotesOff (bool immediately)
{
    voices.allNotesOff (immediately);

    for (auto& channel : channels)
        channel.reset();
}

//==============================================================================
void OrchestraEngine::process (float* const* output, int numSamples) noexcept
{
    if (output == nullptr || numSamples <= 0)
        return;

    // The host may hand over a block larger than the one we prepared for.
    int offset = 0;
    while (offset < numSamples)
    {
        const int chunk = std::min (maxBlockSize, numSamples - offset);

        float* chunkPointers[2] = { output[0] + offset, output[1] + offset };
        processBlock (chunkPointers, chunk);

        offset += chunk;
    }
}

void OrchestraEngine::processBlock (float* const* output, int numSamples) noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill_n (output[ch], numSamples, 0.0f);
        std::fill_n (reverbBus[static_cast<std::size_t> (ch)], numSamples, 0.0f);
    }

    const bool soloActive = anySectionSoloed();

    for (int sectionIndex = 0; sectionIndex < static_cast<int> (sections.size()); ++sectionIndex)
    {
        auto& section = sections[static_cast<std::size_t> (sectionIndex)];

        if (section.getInstrument() == nullptr)
            continue;

        const bool audible = soloActive ? section.isSoloed() : ! section.isMuted();

        if (! audible)
        {
            // Still render, then discard: muting must not desynchronise voices or
            // leave a note frozen mid-attack when the mute is lifted.
            for (int ch = 0; ch < 2; ++ch)
                std::fill_n (sectionBus[static_cast<std::size_t> (ch)], numSamples, 0.0f);

            voices.renderSection (sectionIndex, sectionBus.data(), numSamples);
            section.setPeakLevel (0.0f);
            continue;
        }

        for (int ch = 0; ch < 2; ++ch)
            std::fill_n (sectionBus[static_cast<std::size_t> (ch)], numSamples, 0.0f);

        voices.renderSection (sectionIndex, sectionBus.data(), numSamples);
        section.stage().process (sectionBus.data(), reverbBus.data(), numSamples);

        float peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* source = sectionBus[static_cast<std::size_t> (ch)];
            float*       target = output[ch];

            for (int i = 0; i < numSamples; ++i)
            {
                target[i] += source[i];
                peak = std::max (peak, std::abs (source[i]));
            }
        }

        section.setPeakLevel (peak);
    }

    if (convolution.hasImpulseResponse() && reverbMix > 0.0f)
    {
        convolution.process (reverbBus.data(), numSamples);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* wet = reverbBus[static_cast<std::size_t> (ch)];
            float*       target = output[ch];

            for (int i = 0; i < numSamples; ++i)
                target[i] += wet[i] * reverbMix;
        }
    }

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            output[ch][i] *= masterGain;
}

} // namespace moe
