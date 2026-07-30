#pragma once

#include "moe/Types.h"

#include <array>
#include <cstdint>

namespace moe::perf
{

/** Continuous controllers the engine reacts to by name. */
namespace cc
{
    inline constexpr int modulation   = 1;    ///< dynamics — the orchestral fader
    inline constexpr int breath       = 2;
    inline constexpr int expression   = 11;
    inline constexpr int sustainPedal = 64;
    inline constexpr int vibrato      = 21;
}

/** Per-channel MIDI state: what is held, and where every controller sits.

    Kept separate from the voice pool because articulation choice, legato
    detection and dynamics all need this view and none of them should have to
    walk the voices to reconstruct it. */
class ChannelState
{
public:
    void reset() noexcept
    {
        heldNotes.fill (false);
        controllers.fill (0);
        controllers[cc::modulation] = 100;
        controllers[cc::expression] = 127;
        noteOnOrder.fill (0);
        orderCounter    = 0;
        numHeld         = 0;
        pitchBend       = 0.0f;
        channelPressure = 0.0f;
        sustainDown     = false;
    }

    void noteOn (int note) noexcept
    {
        if (note < 0 || note >= kNumMidiNotes)
            return;

        if (! heldNotes[static_cast<std::size_t> (note)])
            ++numHeld;

        heldNotes[static_cast<std::size_t> (note)]   = true;
        noteOnOrder[static_cast<std::size_t> (note)] = ++orderCounter;
    }

    void noteOff (int note) noexcept
    {
        if (note < 0 || note >= kNumMidiNotes)
            return;

        if (heldNotes[static_cast<std::size_t> (note)])
            --numHeld;

        heldNotes[static_cast<std::size_t> (note)] = false;
    }

    void setController (int number, int value) noexcept
    {
        if (number < 0 || number >= kNumMidiCcs)
            return;

        controllers[static_cast<std::size_t> (number)] = value;

        if (number == cc::sustainPedal)
            sustainDown = value >= 64;
    }

    int  getController (int number) const noexcept
    {
        return (number >= 0 && number < kNumMidiCcs)
                   ? controllers[static_cast<std::size_t> (number)] : 0;
    }

    float getNormalisedController (int number) const noexcept
    {
        return static_cast<float> (getController (number)) / 127.0f;
    }

    bool isNoteHeld (int note) const noexcept
    {
        return note >= 0 && note < kNumMidiNotes && heldNotes[static_cast<std::size_t> (note)];
    }

    int  getNumHeldNotes() const noexcept { return numHeld; }
    bool isSustainDown()  const noexcept  { return sustainDown; }

    void  setPitchBend (float semitones) noexcept { pitchBend = semitones; }
    float getPitchBend() const noexcept           { return pitchBend; }

    void  setChannelPressure (float value) noexcept { channelPressure = value; }
    float getChannelPressure() const noexcept       { return channelPressure; }

    /** Most recently pressed note that is still held, excluding `exclude`.
        Returns kNoNote when there is none — this is what legato detection asks. */
    int mostRecentHeldNote (int exclude = kNoNote) const noexcept
    {
        int          best      = kNoNote;
        std::uint32_t bestOrder = 0;

        for (int note = 0; note < kNumMidiNotes; ++note)
        {
            if (note == exclude || ! heldNotes[static_cast<std::size_t> (note)])
                continue;

            if (noteOnOrder[static_cast<std::size_t> (note)] > bestOrder)
            {
                bestOrder = noteOnOrder[static_cast<std::size_t> (note)];
                best      = note;
            }
        }

        return best;
    }

    const int* controllerArray() const noexcept { return controllers.data(); }

private:
    std::array<bool, kNumMidiNotes>          heldNotes {};
    std::array<std::uint32_t, kNumMidiNotes> noteOnOrder {};
    std::array<int, kNumMidiCcs>             controllers {};

    std::uint32_t orderCounter    = 0;
    int           numHeld         = 0;
    float         pitchBend       = 0.0f;
    float         channelPressure = 0.0f;
    bool          sustainDown     = false;
};

} // namespace moe::perf
