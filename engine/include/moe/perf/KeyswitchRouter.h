#pragma once

#include "moe/Types.h"
#include "moe/bank/Instrument.h"

#include <algorithm>
#include <array>

namespace moe::perf
{

/** Turns keyswitch presses into articulation changes.

    Two mechanisms coexist, because banks use both:
      - **note keyswitches** — a note in a dedicated range selects an
        articulation and produces no sound;
      - **CC keyswitches** — a controller value range does the same.

    Latching by default: pressing a keyswitch changes the articulation until
    another one is pressed, which is what lets a player switch mid-phrase without
    holding anything down. Momentary mode reverts on release, which some players
    prefer for one-off accents. */
class KeyswitchRouter
{
public:
    enum class Mode { latching, momentary };

    void setInstrument (const bank::Instrument* newInstrument) noexcept
    {
        instrument = newInstrument;
        reset();
    }

    void setMode (Mode newMode) noexcept { mode = newMode; }

    /** Routes an articulation change to a CC instead of (or as well as) notes.
        `-1` disables. The CC range 0..127 is split evenly across the bank's
        available articulations. */
    void setControllerSwitch (int ccNumber) noexcept { switchCc = ccNumber; }

    void reset() noexcept
    {
        lastKeyswitchNote = instrument != nullptr ? instrument->defaultKeyswitch : kNoNote;
        current = Articulation::sustain;
        previous = current;

        if (instrument != nullptr && lastKeyswitchNote >= 0)
            if (const auto* ks = instrument->keyswitchForNote (lastKeyswitchNote))
                current = ks->articulation;
    }

    /** @returns true when the note was consumed as a keyswitch and must not sound */
    bool handleNoteOn (int note) noexcept
    {
        if (instrument == nullptr || ! instrument->isKeyswitch (note))
            return false;

        const auto* ks = instrument->keyswitchForNote (note);
        if (ks == nullptr)
            return false;

        previous          = current;
        current           = ks->articulation;
        lastKeyswitchNote = note;
        return true;
    }

    /** @returns true when the note was a keyswitch release */
    bool handleNoteOff (int note) noexcept
    {
        if (instrument == nullptr || ! instrument->isKeyswitch (note))
            return false;

        if (mode == Mode::momentary && note == lastKeyswitchNote)
            current = previous;

        return true;
    }

    void handleController (int number, int value) noexcept
    {
        if (instrument == nullptr || switchCc < 0 || number != switchCc)
            return;

        const auto& available = instrument->availableArticulations();
        if (available.empty())
            return;

        const auto index = static_cast<std::size_t> (
            static_cast<float> (value) / 128.0f * static_cast<float> (available.size()));

        current = available[std::min (index, available.size() - 1)];
    }

    /** Overrides the articulation from the UI, bypassing keyswitches. */
    void setArticulation (Articulation articulation) noexcept
    {
        previous = current;
        current  = articulation;

        // Keep `lastKeyswitchNote` consistent so `sw_last`-gated regions follow
        // a UI change as well as a played keyswitch.
        lastKeyswitchNote = kNoNote;

        if (instrument != nullptr)
            for (const auto& ks : instrument->keyswitches)
                if (ks.articulation == articulation)
                {
                    lastKeyswitchNote = ks.note;
                    break;
                }
    }

    Articulation getArticulation() const noexcept { return current; }
    int getLastKeyswitchNote() const noexcept { return lastKeyswitchNote; }

private:
    const bank::Instrument* instrument = nullptr;

    Mode         mode              = Mode::latching;
    Articulation current           = Articulation::sustain;
    Articulation previous          = Articulation::sustain;
    int          lastKeyswitchNote = kNoNote;
    int          switchCc          = -1;
};

} // namespace moe::perf
