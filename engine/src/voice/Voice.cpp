#include "moe/voice/Voice.h"

#include "moe/dsp/Interpolation.h"

#include <algorithm>
#include <cmath>

namespace moe::voice
{

namespace
{

constexpr float kPi = 3.14159265358979f;

float decibelsToGain (float db) noexcept
{
    return db <= -100.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
}

/** Constant-power pan. SFZ `pan` runs -100..+100. */
void panGains (float pan, float& left, float& right) noexcept
{
    const float normalised = std::clamp (pan / 100.0f, -1.0f, 1.0f);
    const float angle      = (normalised + 1.0f) * 0.25f * kPi;   // 0..pi/2

    left  = std::cos (angle);
    right = std::sin (angle);
}

} // namespace

void Voice::prepare (double newSampleRate, int /*blockSize*/)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    envelope.prepare (sampleRate);
    toneFilter.prepare (sampleRate);

    portamento.reset (sampleRate, 0.05);
    dynamicsGain.reset (sampleRate, 0.02);
    expressionGain.reset (sampleRate, 0.02);
    sectionGain.reset (sampleRate, 0.02);

    portamento.setCurrentAndTarget (0.0f);
    dynamicsGain.setCurrentAndTarget (1.0f);
    expressionGain.setCurrentAndTarget (1.0f);
    sectionGain.setCurrentAndTarget (1.0f);

    reset();
}

void Voice::start (const VoiceStartInfo& info, stream::StreamManager* manager)
{
    if (info.instrument == nullptr || info.regionIndex < 0)
        return;

    instrument = info.instrument;
    region     = &instrument->regions[static_cast<std::size_t> (info.regionIndex)];

    if (region->sampleId < 0 || region->sampleId >= static_cast<int> (instrument->samples.size()))
    {
        instrument = nullptr;
        region     = nullptr;
        return;
    }

    sample        = &instrument->samples[static_cast<std::size_t> (region->sampleId)];
    streamManager = manager;

    midiNote       = info.midiNote;
    midiChannel    = info.midiChannel;
    velocity       = info.velocity;
    sectionId      = info.sectionId;
    exclusiveGroup = region->group;
    offByGroup     = region->offBy;
    sustained      = false;

    // ---- playback window ---------------------------------------------------
    sourcePosition = std::clamp<SampleIndex> (region->offset, 0, sample->info.numFrames);
    endFrame       = region->endFrame >= 0
                         ? std::min (region->endFrame, sample->info.numFrames)
                         : sample->info.numFrames;

    const bool wantsLoop = region->loopMode == bank::LoopMode::loopContinuous
                           || region->loopMode == bank::LoopMode::loopSustain;

    stream::LoopConfig loop;
    loop.enabled = wantsLoop && region->loopEnd > region->loopStart;
    loop.start   = region->loopStart;
    loop.end     = std::min (region->loopEnd, endFrame);

    // Streaming is only needed when playback will actually leave the resident
    // head — a short sample, or a loop that fits in RAM, never touches the disk.
    const bool loopFitsInPreload = loop.enabled && loop.end <= sample->preloadFrames;
    useStream = ! sample->isFullyResident() && ! loopFitsInPreload
                && endFrame > sample->preloadFrames;

    streamSlot = nullptr;
    if (useStream && streamManager != nullptr)
    {
        streamSlot = streamManager->acquireSlot (sample->path, sample->preloadFrames,
                                                 endFrame, loop);
        if (streamSlot != nullptr)
            streamManager->notifyStreamThread();
    }

    sourceEnded   = false;
    stagingFilled = 0;
    stagingRead   = 0;
    phase         = 0.0;

    for (auto& channel : window)
        channel.fill (0.0f);

    // ---- pitch -------------------------------------------------------------
    const float keytrack   = region->pitchKeytrack / 100.0f;
    const float semitones  = static_cast<float> (midiNote - region->rootKey) * keytrack
                             + static_cast<float> (region->transpose)
                             + (region->tuneCents + info.humanisePitchCents) / 100.0f;

    const double resampling = sample->info.sampleRate > 0.0
                                  ? sample->info.sampleRate / sampleRate
                                  : 1.0;

    baseRatio = std::pow (2.0, static_cast<double> (semitones) / 12.0) * resampling;

    pitchBendSemitones = 0.0f;
    portamento.reset (sampleRate, std::max (0.005, static_cast<double> (info.portamentoSeconds) / 3.0));
    portamento.setCurrentAndTarget (info.portamentoSemitones);
    portamento.setTarget (0.0f);

    updatePitchRatio();

    // ---- amplitude ---------------------------------------------------------
    staticGain = info.selectionGain
                 * decibelsToGain (region->volumeDb + info.humaniseGainDb);

    panGains (region->pan, gainLeft, gainRight);

    Envelope::Parameters envelopeParameters;
    envelopeParameters.delay   = 0.0f;   // handled by delayCounter so it is exact
    envelopeParameters.attack  = std::max (0.0005f, region->ampegAttack * info.attackScale);
    envelopeParameters.hold    = region->ampegHold;
    envelopeParameters.decay   = region->ampegDecay;
    envelopeParameters.sustain = std::clamp (region->ampegSustain, 0.0f, 1.0f);
    envelopeParameters.release = std::max (0.005f, region->ampegRelease);

    envelope.start (envelopeParameters);

    toneFilter.reset();
    toneFilter.setCutoff (18000.0f);

    delayCounter = static_cast<int> ((region->delaySeconds + info.humaniseDelaySeconds)
                                     * static_cast<float> (sampleRate));

    active = true;
}

void Voice::noteOff()
{
    if (! active)
        return;

    // A one-shot ignores note-off by definition: that is what makes a cymbal
    // ring on after the key is lifted.
    if (region != nullptr && region->loopMode == bank::LoopMode::oneShot)
        return;

    if (streamSlot != nullptr)
        streamSlot->releaseLoop();

    envelope.noteOff();
}

void Voice::fadeOutAndStop (float seconds)
{
    if (! active)
        return;

    if (streamSlot != nullptr)
        streamSlot->releaseLoop();

    envelope.fastRelease (seconds);
}

void Voice::reset()
{
    if (streamSlot != nullptr)
    {
        streamSlot->release();
        streamSlot = nullptr;
    }

    active        = false;
    sustained     = false;
    sourceEnded   = false;
    stagingFilled = 0;
    stagingRead   = 0;
    instrument    = nullptr;
    region        = nullptr;
    sample        = nullptr;
}

void Voice::setDynamics (float value) noexcept
{
    const float clamped = std::clamp (value, 0.0f, 1.0f);

    // Level: a musically useful range rather than a straight fade to silence.
    // -18 dB at CC1=0 keeps pp audible while leaving room for a real crescendo.
    dynamicsGain.setTarget (decibelsToGain (-18.0f * (1.0f - clamped)));

    // Brightness: 2 kHz at pp opening to fully transparent at ff.
    toneFilter.setCutoff (2000.0f * std::pow (9.0f, clamped));
}

void Voice::setExpression (float value) noexcept
{
    expressionGain.setTarget (std::clamp (value, 0.0f, 1.0f));
}

void Voice::setSectionGain (float linearGain) noexcept
{
    sectionGain.setTarget (std::max (0.0f, linearGain));
}

void Voice::setPitchBendSemitones (float semitones) noexcept
{
    pitchBendSemitones = semitones;
}

void Voice::updatePitchRatio()
{
    const double offset = static_cast<double> (portamento.getCurrent())
                          + static_cast<double> (pitchBendSemitones);

    currentRatio = baseRatio * std::pow (2.0, offset / 12.0);
}

bool Voice::refillStaging()
{
    stagingRead   = 0;
    stagingFilled = 0;

    if (sample == nullptr)
        return false;

    // Phase 1 — the resident head.
    if (sourcePosition < sample->preloadFrames)
    {
        const bool loopInPreload = region->loopMode != bank::LoopMode::noLoop
                                   && region->loopMode != bank::LoopMode::oneShot
                                   && region->loopEnd > region->loopStart
                                   && region->loopEnd <= sample->preloadFrames
                                   && ! envelope.isReleasing();

        const SampleIndex boundary = loopInPreload
                                         ? region->loopEnd
                                         : std::min (sample->preloadFrames, endFrame);

        SampleIndex remaining = boundary - sourcePosition;

        if (remaining <= 0)
        {
            if (loopInPreload)
            {
                sourcePosition = region->loopStart;
                remaining      = boundary - sourcePosition;
            }
            else
            {
                remaining = 0;
            }
        }

        const auto count = static_cast<int> (std::min<SampleIndex> (remaining, kStagingFrames));

        if (count > 0)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const int sourceChannel = std::min (ch, sample->info.numChannels - 1);
                const float* source = sample->channel (sourceChannel);

                if (source == nullptr)
                {
                    std::fill_n (staging[static_cast<std::size_t> (ch)].data(), count, 0.0f);
                    continue;
                }

                std::copy (source + sourcePosition, source + sourcePosition + count,
                           staging[static_cast<std::size_t> (ch)].data());
            }

            sourcePosition += count;
            stagingFilled = count;
            return true;
        }
    }

    // Phase 2 — the streamed tail.
    if (streamSlot != nullptr)
    {
        // While the slot is still opening, produce silence rather than ending the
        // note: the head buffer normally covers the open latency, and a moment of
        // silence beats a truncated note.
        if (streamSlot->isPending())
        {
            for (auto& channel : staging)
                channel.fill (0.0f);

            stagingFilled = 64;
            return true;
        }

        float* pointers[2] = { staging[0].data(), staging[1].data() };
        const auto got = static_cast<int> (streamSlot->read (pointers, 2, kStagingFrames));

        if (got > 0)
        {
            stagingFilled = got;
            return true;
        }

        if (! streamSlot->isExhausted())
        {
            // Starved: a short patch of silence keeps the voice alive while the
            // streaming thread catches up.
            for (auto& channel : staging)
                std::fill_n (channel.data(), 64, 0.0f);

            stagingFilled = 64;
            return true;
        }
    }

    sourceEnded = true;
    return false;
}

float Voice::pullFrame (int channel)
{
    return staging[static_cast<std::size_t> (channel)][static_cast<std::size_t> (stagingRead)];
}

void Voice::advanceSourceWindow()
{
    if (stagingRead >= stagingFilled && ! refillStaging())
    {
        // Shift zeros in so the window empties smoothly instead of holding a
        // constant value, which would read as a click.
        for (auto& channel : window)
        {
            channel[0] = channel[1];
            channel[1] = channel[2];
            channel[2] = channel[3];
            channel[3] = 0.0f;
        }
        return;
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        auto& channel = window[static_cast<std::size_t> (ch)];
        channel[0] = channel[1];
        channel[1] = channel[2];
        channel[2] = channel[3];
        channel[3] = pullFrame (ch);
    }

    ++stagingRead;
}

bool Voice::renderAdd (float* const* output, int numSamples)
{
    if (! active || region == nullptr || sample == nullptr)
        return false;

    for (int i = 0; i < numSamples; ++i)
    {
        if (delayCounter > 0)
        {
            --delayCounter;
            continue;
        }

        // Advance the source by `currentRatio` frames, which may be more or less
        // than one output frame.
        phase += currentRatio;
        while (phase >= 1.0)
        {
            advanceSourceWindow();
            phase -= 1.0;
        }

        const auto fraction = static_cast<float> (phase);

        const float left = dsp::hermite (fraction, window[0][0], window[0][1],
                                          window[0][2], window[0][3]);
        const float right = dsp::hermite (fraction, window[1][0], window[1][1],
                                           window[1][2], window[1][3]);

        const float envelopeLevel = envelope.next();

        portamento.next();
        updatePitchRatio();

        const float gain = staticGain * envelopeLevel
                           * dynamicsGain.next()
                           * expressionGain.next()
                           * sectionGain.next();

        output[0][i] += toneFilter.processChannel (0, left)  * gain * gainLeft;
        output[1][i] += toneFilter.processChannel (1, right) * gain * gainRight;

        if (! envelope.isActive())
        {
            reset();
            return false;
        }
    }

    // The source running out ends the note even if the envelope has not: a
    // sample that has finished has nothing left to give.
    if (sourceEnded && stagingRead >= stagingFilled)
    {
        reset();
        return false;
    }

    return true;
}

} // namespace moe::voice
