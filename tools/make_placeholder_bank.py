#!/usr/bin/env python3
"""Generate a playable placeholder string bank (WAV + SFZ).

Why this exists
---------------
A sampler is useless without samples, and the real ones are a separate
decision (see docs/00-audit-samples.md). This script synthesises a bank that
is structurally identical to a real one -- several articulations, two dynamic
layers with a CC1 crossfade, two round-robins per note, keyswitches, loop
points, streamed sustains -- so the whole chain can be played and tested on
day one.

It does not sound like an orchestra. It sounds like a synthesiser imitating a
bowed string, which is exactly what it is. Its job is to make every code path
audible so that swapping in a real library is the only remaining step.

The synthesis is a simple excitation/resonance model: a harmonic series whose
slope follows dynamics, plus filtered noise for bow friction, plus slow
vibrato. Percussive articulations reuse it with different envelopes.

Usage
-----
    python3 tools/make_placeholder_bank.py --output banks/placeholder-strings
    python3 tools/make_placeholder_bank.py --quick        # fewer notes, faster

NumPy is used when available and makes this ~30x faster; the script runs
without it.
"""

from __future__ import annotations

import argparse
import math
import os
import random
import struct
import sys
from dataclasses import dataclass

try:
    import numpy as np
except ImportError:  # pragma: no cover - the fallback path is the point
    np = None


SAMPLE_RATE = 44100


# --------------------------------------------------------------------------
# Articulation definitions
# --------------------------------------------------------------------------

@dataclass(frozen=True)
class Articulation:
    name: str
    keyswitch: int
    duration: float          # seconds of rendered audio
    attack: float            # seconds
    decay_to: float          # level the body settles at, relative to the peak
    decay_time: float
    noise: float             # bow/finger noise amount
    tremolo_hz: float        # 0 disables
    looped: bool
    brightness: float        # harmonic slope multiplier
    release: float           # SFZ ampeg_release


ARTICULATIONS = [
    Articulation("sustain",   24, 3.00, 0.090, 0.85, 0.60, 0.030, 0.0,  True,  1.00, 0.35),
    Articulation("legato",    25, 3.00, 0.180, 0.90, 0.80, 0.020, 0.0,  True,  0.92, 0.35),
    Articulation("staccato",  26, 0.55, 0.012, 0.00, 0.30, 0.060, 0.0,  False, 1.15, 0.12),
    Articulation("spiccato",  27, 0.32, 0.006, 0.00, 0.16, 0.090, 0.0,  False, 1.30, 0.08),
    Articulation("pizzicato", 28, 1.10, 0.003, 0.00, 0.70, 0.040, 0.0,  False, 1.45, 0.20),
    Articulation("tremolo",   29, 3.00, 0.060, 0.90, 0.40, 0.110, 7.5,  True,  1.10, 0.30),
    Articulation("marcato",   30, 1.40, 0.010, 0.55, 0.35, 0.070, 0.0,  False, 1.25, 0.25),
]


# Two dynamic layers, crossfaded by CC1 at playback time.
LAYERS = [
    ("soft", 1, 63, 0.30, 0.55),    # name, lovel, hivel, amplitude, brightness
    ("loud", 64, 127, 0.85, 1.00),
]

ROUND_ROBINS = 2


# --------------------------------------------------------------------------
# Synthesis
# --------------------------------------------------------------------------

def midi_to_hz(note: int) -> float:
    return 440.0 * (2.0 ** ((note - 69) / 12.0))


def harmonic_amplitudes(brightness: float, count: int, rng: random.Random) -> list[float]:
    """A 1/n series tilted by `brightness`, with slight per-note irregularity.

    Real strings do not have textbook harmonic amplitudes, and identical ratios
    across every note is one of the things that makes synthesis sound synthetic.
    """
    amplitudes = []
    for h in range(1, count + 1):
        base = 1.0 / (h ** (1.9 - brightness * 0.7))
        amplitudes.append(base * rng.uniform(0.82, 1.18))
    return amplitudes


def render_numpy(freq, articulation, amplitude, brightness, seed):
    rng = random.Random(seed)
    length = int(articulation.duration * SAMPLE_RATE)
    time = np.arange(length, dtype=np.float64) / SAMPLE_RATE

    harmonic_count = max(4, min(48, int(SAMPLE_RATE * 0.45 / freq)))
    amplitudes = harmonic_amplitudes(brightness * articulation.brightness,
                                     harmonic_count, rng)

    # Vibrato: slow, shallow, and phase-offset per sample so unison notes drift.
    vibrato = 1.0 + 0.0035 * np.sin(2.0 * math.pi * 5.2 * time + rng.uniform(0, 6.28))
    phase = 2.0 * math.pi * freq * np.cumsum(vibrato) / SAMPLE_RATE

    body = np.zeros(length, dtype=np.float64)
    for index, harmonic_amplitude in enumerate(amplitudes, start=1):
        # Higher harmonics decay faster, as they do on a real string.
        decay = np.exp(-time * (0.25 + index * 0.09) * (0.3 if articulation.looped else 1.6))
        body += harmonic_amplitude * decay * np.sin(phase * index + rng.uniform(0, 6.28))

    peak = np.max(np.abs(body))
    if peak > 0:
        body /= peak

    if articulation.noise > 0:
        noise = np.random.default_rng(seed).standard_normal(length)
        # Two passes of a short moving average stand in for a bow-noise filter.
        kernel = np.ones(12) / 12.0
        noise = np.convolve(noise, kernel, mode="same")
        noise = np.convolve(noise, kernel, mode="same")
        body += noise * articulation.noise

    envelope = build_envelope_numpy(length, articulation)
    signal = body * envelope * amplitude

    if articulation.tremolo_hz > 0:
        signal *= 0.55 + 0.45 * np.sin(2.0 * math.pi * articulation.tremolo_hz * time)

    return signal


def build_envelope_numpy(length, articulation):
    time = np.arange(length, dtype=np.float64) / SAMPLE_RATE
    envelope = np.ones(length, dtype=np.float64)

    attack_samples = max(1, int(articulation.attack * SAMPLE_RATE))
    ramp = np.linspace(0.0, 1.0, attack_samples)
    envelope[:attack_samples] = ramp ** 1.6

    decay_samples = max(1, int(articulation.decay_time * SAMPLE_RATE))
    decay_end = min(length, attack_samples + decay_samples)
    if decay_end > attack_samples:
        span = decay_end - attack_samples
        curve = np.exp(-np.linspace(0.0, 4.0, span))
        envelope[attack_samples:decay_end] = (articulation.decay_to
                                              + (1.0 - articulation.decay_to) * curve)
        envelope[decay_end:] = articulation.decay_to

    if not articulation.looped:
        # Short articulations must reach silence on their own.
        tail = max(1, int(0.05 * SAMPLE_RATE))
        envelope[-tail:] *= np.linspace(1.0, 0.0, tail)
        envelope *= np.exp(-time * (2.2 / max(articulation.duration, 0.1)))

    return envelope


def render_pure_python(freq, articulation, amplitude, brightness, seed):
    """Fallback used when NumPy is missing. Same model, cruder and slower."""
    rng = random.Random(seed)
    length = int(articulation.duration * SAMPLE_RATE)

    harmonic_count = max(3, min(16, int(SAMPLE_RATE * 0.45 / freq)))
    amplitudes = harmonic_amplitudes(brightness * articulation.brightness,
                                     harmonic_count, rng)

    # One cycle is synthesised then tiled, which is what makes this tractable
    # without NumPy. The pitch is quantised to a whole number of samples per
    # cycle; a few cents of error is irrelevant for a placeholder.
    period = max(4, int(round(SAMPLE_RATE / freq)))
    phases = [rng.uniform(0, 2 * math.pi) for _ in amplitudes]

    cycle = []
    for n in range(period):
        value = 0.0
        for index, harmonic_amplitude in enumerate(amplitudes, start=1):
            value += harmonic_amplitude * math.sin(
                2.0 * math.pi * index * n / period + phases[index - 1])
        cycle.append(value)

    peak = max(abs(v) for v in cycle) or 1.0
    cycle = [v / peak for v in cycle]

    repeats = length // period + 1
    body = (cycle * repeats)[:length]

    signal = [0.0] * length
    attack_samples = max(1, int(articulation.attack * SAMPLE_RATE))
    decay_samples = max(1, int(articulation.decay_time * SAMPLE_RATE))
    noise_amount = articulation.noise
    tremolo = articulation.tremolo_hz

    previous_noise = 0.0
    for i in range(length):
        if i < attack_samples:
            envelope = (i / attack_samples) ** 1.6
        elif i < attack_samples + decay_samples:
            t = (i - attack_samples) / decay_samples
            envelope = articulation.decay_to + (1.0 - articulation.decay_to) * math.exp(-4.0 * t)
        else:
            envelope = articulation.decay_to

        if not articulation.looped:
            envelope *= math.exp(-(i / SAMPLE_RATE) * (2.2 / max(articulation.duration, 0.1)))

        value = body[i] * envelope

        if noise_amount > 0:
            previous_noise = 0.85 * previous_noise + 0.15 * rng.uniform(-1.0, 1.0)
            value += previous_noise * noise_amount * envelope

        if tremolo > 0:
            value *= 0.55 + 0.45 * math.sin(2.0 * math.pi * tremolo * i / SAMPLE_RATE)

        signal[i] = value * amplitude

    if not articulation.looped:
        tail = max(1, int(0.05 * SAMPLE_RATE))
        for i in range(tail):
            signal[length - tail + i] *= 1.0 - i / tail

    return signal


# --------------------------------------------------------------------------
# WAV writing
# --------------------------------------------------------------------------

def write_wav(path, samples, loop_start=None, loop_end=None, root_note=None):
    """16-bit mono WAV, with an optional smpl chunk for loop points."""
    if np is not None and isinstance(samples, np.ndarray):
        clipped = np.clip(samples, -0.98, 0.98)
        data = (clipped * 32767.0).astype("<i2").tobytes()
        frames = len(clipped)
    else:
        values = [max(-32000, min(32000, int(v * 32767.0))) for v in samples]
        data = struct.pack("<%dh" % len(values), *values)
        frames = len(values)

    chunks = []

    fmt = struct.pack("<HHIIHH", 1, 1, SAMPLE_RATE, SAMPLE_RATE * 2, 2, 16)
    chunks.append(b"fmt " + struct.pack("<I", len(fmt)) + fmt)
    chunks.append(b"data" + struct.pack("<I", len(data)) + data)

    if loop_start is not None and loop_end is not None and root_note is not None:
        smpl = struct.pack(
            "<9I", 0, 0, int(1e9 / SAMPLE_RATE), root_note, 0, 0, 0, 1, 0)
        smpl += struct.pack("<6I", 0, 0, loop_start, loop_end, 0, 0)
        chunks.append(b"smpl" + struct.pack("<I", len(smpl)) + smpl)

    body = b"".join(chunks)

    with open(path, "wb") as handle:
        handle.write(b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body)

    return frames


# --------------------------------------------------------------------------
# Bank generation
# --------------------------------------------------------------------------

def generate(output_dir, notes, verbose=True):
    os.makedirs(output_dir, exist_ok=True)
    render = render_numpy if np is not None else render_pure_python

    entries = []
    total = len(notes) * len(ARTICULATIONS) * len(LAYERS) * ROUND_ROBINS
    done = 0

    for note in notes:
        freq = midi_to_hz(note)

        for articulation in ARTICULATIONS:
            for layer_name, lovel, hivel, amplitude, brightness in LAYERS:
                for rr in range(1, ROUND_ROBINS + 1):
                    filename = f"{articulation.name}_{note}_{layer_name}_rr{rr}.wav"
                    path = os.path.join(output_dir, filename)

                    # A distinct seed per file is what produces the round-robin
                    # variation: same model, different phases and noise.
                    seed = note * 1000 + articulation.keyswitch * 10 + rr * 3 + lovel

                    signal = render(freq, articulation, amplitude, brightness, seed)

                    loop_start = loop_end = root = None
                    if articulation.looped:
                        # Loop the second half, avoiding the attack transient.
                        loop_start = int(len(signal) * 0.45)
                        loop_end = len(signal) - 1
                        root = note

                    frames = write_wav(path, signal, loop_start, loop_end, root)

                    entries.append({
                        "file": filename,
                        "note": note,
                        "articulation": articulation,
                        "lovel": lovel,
                        "hivel": hivel,
                        "layer": layer_name,
                        "rr": rr,
                        "frames": frames,
                        "loop_start": loop_start,
                        "loop_end": loop_end,
                    })

                    done += 1
                    if verbose and done % 20 == 0:
                        print(f"  {done}/{total} samples", end="\r", flush=True)

    if verbose:
        print(f"  {total}/{total} samples")

    return entries


def write_sfz(output_dir, entries, notes):
    """Emit the SFZ mapping.

    Key ranges are stretched halfway to the neighbouring sampled note, so the
    whole keyboard plays even though only every few semitones was rendered --
    the same trick every real library uses, just with wider gaps.
    """
    boundaries = {}
    for index, note in enumerate(notes):
        low = notes[index - 1] if index > 0 else note - 6
        high = notes[index + 1] if index + 1 < len(notes) else note + 6
        boundaries[note] = (int(math.ceil((low + note) / 2)) if index > 0 else note - 5,
                            int(math.floor((note + high) / 2)) if index + 1 < len(notes) else note + 5)

    lines = [
        "// Placeholder string bank, generated by tools/make_placeholder_bank.py",
        "//",
        "// This is a synthesised stand-in, not a recording. Its purpose is to make",
        "// every part of the engine audible before a real library is chosen.",
        "// See docs/00-audit-samples.md.",
        "",
        "<control>",
        "default_path=",
        "moe_name=Placeholder Strings",
        "",
        "<global>",
        "amp_veltrack=68",
        "",
    ]

    for articulation in ARTICULATIONS:
        lines.append(f"// ---- {articulation.name} (keyswitch {articulation.keyswitch}) ----")
        lines.append("<group>")
        lines.append(
            f"sw_lokey={ARTICULATIONS[0].keyswitch} sw_hikey={ARTICULATIONS[-1].keyswitch} "
            f"sw_last={articulation.keyswitch} sw_default={ARTICULATIONS[0].keyswitch} "
            f"sw_label={articulation.name}")
        lines.append(f"ampeg_attack={articulation.attack:.3f} "
                     f"ampeg_release={articulation.release:.3f}")

        if articulation.looped:
            lines.append("loop_mode=loop_sustain")
        else:
            lines.append("loop_mode=no_loop")

        lines.append("")

        for entry in entries:
            if entry["articulation"] is not articulation:
                continue

            low, high = boundaries[entry["note"]]

            # CC1 crossfade between the two dynamic layers. This is the control
            # that carries orchestral expression, so the placeholder wires it up
            # exactly as a real bank would.
            if entry["layer"] == "soft":
                crossfade = "xfout_locc1=48 xfout_hicc1=110"
            else:
                crossfade = "xfin_locc1=40 xfin_hicc1=104"

            region = (f"<region> sample={entry['file']} "
                      f"lokey={low} hikey={high} pitch_keycenter={entry['note']} "
                      f"lovel={entry['lovel']} hivel={entry['hivel']} "
                      f"seq_length={ROUND_ROBINS} seq_position={entry['rr']} "
                      f"{crossfade}")

            if entry["loop_start"] is not None:
                region += f" loop_start={entry['loop_start']} loop_end={entry['loop_end']}"

            # A touch of built-in variation on top of the engine's humanisation.
            region += " pitch_random=4 amp_random=0.8"

            lines.append(region)

        lines.append("")

    path = os.path.join(output_dir, "placeholder-strings.sfz")
    with open(path, "w") as handle:
        handle.write("\n".join(lines) + "\n")

    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", default="banks/placeholder-strings",
                        help="directory to write the bank into")
    parser.add_argument("--low", type=int, default=36, help="lowest sampled MIDI note")
    parser.add_argument("--high", type=int, default=84, help="highest sampled MIDI note")
    parser.add_argument("--step", type=int, default=4,
                        help="semitones between sampled notes (smaller = bigger, better)")
    parser.add_argument("--quick", action="store_true",
                        help="fewer notes, for a fast first run")
    args = parser.parse_args()

    if args.quick:
        args.step = 12

    notes = list(range(args.low, args.high + 1, args.step))

    print(f"Generating placeholder bank into {args.output}")
    print(f"  notes ......... {len(notes)} ({args.low}..{args.high} step {args.step})")
    print(f"  articulations . {len(ARTICULATIONS)}")
    print(f"  layers ........ {len(LAYERS)} x {ROUND_ROBINS} round-robins")
    print(f"  backend ....... {'numpy' if np is not None else 'pure python (slow)'}")

    if np is None:
        print("\n  NumPy is not installed. This will take a few minutes.")
        print("  `pip install numpy` makes it roughly 30x faster.\n")

    entries = generate(args.output, notes)
    sfz_path = write_sfz(args.output, entries, notes)

    total_bytes = sum(os.path.getsize(os.path.join(args.output, e["file"])) for e in entries)

    print(f"\nDone: {len(entries)} samples, {total_bytes / 1e6:.1f} MB")
    print(f"SFZ: {sfz_path}")
    print("\nLoad that .sfz in the plugin, then play. Keyswitches:")
    for articulation in ARTICULATIONS:
        print(f"  {articulation.keyswitch:3d}  {articulation.name}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
