#!/usr/bin/env python3
"""Sound-effect synthesiser (docs/design/audio.md, "Sound effects").

    tools/sfx.py assets/src/sfx/*.json -o assets/gen/sfx [--wav previews/]

Each `<name>.json` becomes `<dir>/<name>.snd` (SND1, 22050 Hz signed 8-bit
mono, written through libbast via tools/bast.py). `--wav` also writes 16-bit
WAV previews of exactly the same samples (int8 << 8) for listening/evidence.

Parameter file
--------------
    {
      "desc": "one line: how it sounds",
      "seed": 7,                    # noise seed (xorshift32), deterministic
      "lowpass": 6000,              # optional master one-pole lowpass, Hz
      "crush": 6,                   # optional master bit depth (1..8)
      "voices": [ { ...voice... }, ... ]   # layered, summed
    }

Voice keys (all optional except "wave"):
    wave        square | saw | triangle | noise
    start       offset in seconds from the start of the effect (layering)
    freq        Hz at the start (noise: sample-and-hold rate, "pitch" of the
                noise; default 22050 = white)
    freq_end    Hz at the end of the voice: exponential pitch sweep
    duty        square duty cycle 0..1 (default 0.5)
    arp         list of semitone offsets stepped through every `arp_step` s
                (on top of the sweep); e.g. [0, 4, 7, 12] rising arpeggio
    arp_step    seconds per arpeggio note (default 0.06)
    vibrato     [rate Hz, depth semitones]
    tremolo     [rate Hz, depth 0..1]   amplitude LFO
    attack, decay, sustain (level 0..1), hold, release   ADSR, seconds
    gain        linear voice gain before mixing (default 1)
    lowpass     one-pole lowpass cutoff Hz; `lowpass_end` sweeps it
    crush       bit depth for this voice (1..8); `downsample` holds every
                N-th sample (sample-rate reduction)

Loudness target (the same for every effect): the mix is peak-normalised so
the loudest sample is exactly +-PEAK (= 112 of 127, -1.1 dBFS), leaving the
game mixer headroom and making every effect equally hot; relative loudness
in game is set by the mixer's per-sound volume, not by the files. A 2 ms
fade-out at the end avoids a click.

Floating point is fine here: this is an offline asset tool. Output depends
only on the parameter file (noise uses its own xorshift32, not numpy's RNG),
so two runs are byte-identical.
"""
import argparse
import json
import math
import os
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402

RATE = bast.SND_RATE
PEAK = 112
WAVES = ("square", "saw", "triangle", "noise")
VOICE_KEYS = {"wave", "start", "freq", "freq_end", "duty", "arp", "arp_step", "vibrato",
              "tremolo", "attack", "decay", "sustain", "hold", "release", "gain",
              "lowpass", "lowpass_end", "crush", "downsample"}
TOP_KEYS = {"desc", "seed", "lowpass", "crush", "voices"}


class SfxError(Exception):
    pass


def xorshift_noise(seed, n):
    """n values in [-1, 1) from xorshift32; seed 0 is remapped."""
    x = (seed * 2654435761 + 0x9E3779B9) & 0xFFFFFFFF or 1
    out = np.empty(n, dtype=np.float64)
    for i in range(n):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out[i] = x / 2147483648.0 - 1.0
    return out


def envelope(v, n):
    a = v.get("attack", 0.002)
    d = v.get("decay", 0.0)
    s = v.get("sustain", 1.0)
    h = v.get("hold", 0.0)
    r = v.get("release", 0.05)
    t = np.arange(n) / RATE
    e = np.zeros(n)
    e = np.where(t < a, t / a if a > 0 else 1.0, e)
    t1 = t - a
    m = (t1 >= 0) & (t1 < d)
    e = np.where(m, 1.0 - (1.0 - s) * (t1 / d if d > 0 else 1.0), e)
    t2 = t1 - d
    e = np.where((t2 >= 0) & (t2 < h), s, e)
    t3 = t2 - h
    m = t3 >= 0
    e = np.where(m, s * np.clip(1.0 - t3 / r if r > 0 else 0.0, 0.0, 1.0), e)
    return e


def voice_length(v):
    return (v.get("attack", 0.002) + v.get("decay", 0.0) + v.get("hold", 0.0)
            + v.get("release", 0.05))


def one_pole(x, f0, f1=None):
    """One-pole lowpass with cutoff swept exponentially f0 -> f1."""
    n = len(x)
    f1 = f0 if f1 is None else f1
    fc = f0 * (f1 / f0) ** (np.arange(n) / max(n - 1, 1))
    a = 1.0 - np.exp(-2.0 * math.pi * np.minimum(fc, RATE / 2) / RATE)
    y = np.empty(n)
    acc = 0.0
    for i in range(n):
        acc += a[i] * (x[i] - acc)
        y[i] = acc
    return y


def crush(x, bits, downsample=1):
    if downsample and downsample > 1:
        idx = (np.arange(len(x)) // downsample) * downsample
        x = x[idx]
    if bits and bits < 8:
        q = float(1 << (bits - 1))
        x = np.round(x * q) / q
    return x


def render_voice(v, seed, vi):
    unknown = set(v) - VOICE_KEYS
    if unknown:
        raise SfxError("unknown voice keys %s" % sorted(unknown))
    w = v.get("wave")
    if w not in WAVES:
        raise SfxError("voice %d: wave must be one of %s" % (vi, WAVES))
    n = max(1, int(round(voice_length(v) * RATE)))
    t = np.arange(n) / RATE
    f0 = float(v.get("freq", RATE if w == "noise" else 440.0))
    f1 = float(v.get("freq_end", f0))
    if f0 <= 0 or f1 <= 0:
        raise SfxError("voice %d: freq must be > 0" % vi)
    freq = f0 * (f1 / f0) ** (np.arange(n) / max(n - 1, 1))
    if "arp" in v:
        step = max(1, int(round(v.get("arp_step", 0.06) * RATE)))
        arp = np.asarray(v["arp"], dtype=np.float64)
        semis = arp[np.minimum(np.arange(n) // step, len(arp) - 1)]
        freq = freq * 2.0 ** (semis / 12.0)
    if "vibrato" in v:
        rate_hz, depth = v["vibrato"]
        freq = freq * 2.0 ** (depth * np.sin(2 * math.pi * rate_hz * t) / 12.0)
    phase = np.cumsum(freq / RATE)  # in cycles
    if w == "square":
        x = np.where((phase % 1.0) < v.get("duty", 0.5), 1.0, -1.0)
    elif w == "saw":
        x = 2.0 * (phase % 1.0) - 1.0
    elif w == "triangle":
        x = 4.0 * np.abs((phase % 1.0) - 0.5) - 1.0
    else:  # noise: a new random value each time the phase crosses an integer
        cell = np.floor(phase).astype(np.int64)
        cell -= cell[0]
        x = xorshift_noise(seed * 101 + vi, int(cell[-1]) + 1)[cell]
    x = x * envelope(v, n)
    if "tremolo" in v:
        rate_hz, depth = v["tremolo"]
        x = x * (1.0 - depth * 0.5 * (1.0 + np.sin(2 * math.pi * rate_hz * t)))
    if "lowpass" in v:
        x = one_pole(x, float(v["lowpass"]), float(v.get("lowpass_end", v["lowpass"])))
    x = crush(x, v.get("crush"), v.get("downsample", 1))
    return x * float(v.get("gain", 1.0))


def synth(params):
    """Parameter dict -> int8 numpy array (peak-normalised to +-PEAK)."""
    unknown = set(params) - TOP_KEYS
    if unknown:
        raise SfxError("unknown keys %s" % sorted(unknown))
    voices = params.get("voices") or []
    if not voices:
        raise SfxError("no voices")
    seed = int(params.get("seed", 0))
    parts = []
    for vi, v in enumerate(voices):
        start = int(round(float(v.get("start", 0.0)) * RATE))
        parts.append((start, render_voice(v, seed, vi)))
    total = max(s + len(x) for s, x in parts)
    mix = np.zeros(total)
    for s, x in parts:
        mix[s:s + len(x)] += x
    if "lowpass" in params:
        mix = one_pole(mix, float(params["lowpass"]))
    if "crush" in params:
        mix = crush(mix, params["crush"])
    fade = min(total, int(0.002 * RATE))
    if fade:
        mix[-fade:] *= np.linspace(1.0, 0.0, fade)
    peak = np.max(np.abs(mix))
    if peak <= 0:
        raise SfxError("effect is silent")
    out = np.round(mix * (PEAK / peak))
    return np.clip(out, -PEAK, PEAK).astype(np.int8)


def write_wav(path, pcm8):
    pcm16 = (pcm8.astype(np.int16) * 256).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm16.tobytes())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("params", nargs="+", help="assets/src/sfx/<name>.json")
    ap.add_argument("-o", "--out", required=True, help="output directory for .snd")
    ap.add_argument("--wav", help="also write 16-bit WAV previews here")
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    if a.wav:
        os.makedirs(a.wav, exist_ok=True)
    for path in a.params:
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            with open(path) as f:
                params = json.load(f)
            pcm = synth(params)
        except (OSError, ValueError, SfxError) as e:
            print("sfx: %s: %s" % (path, e), file=sys.stderr)
            return 1
        bast.Snd(pcm.tobytes()).save(os.path.join(a.out, name + ".snd"))
        if a.wav:
            write_wav(os.path.join(a.wav, name + ".wav"), pcm)
        if not a.quiet:
            print("%-16s %5d samples %5.3f s  peak %3d  rms %5.1f" % (
                name, len(pcm), len(pcm) / RATE, int(np.max(np.abs(pcm.astype(np.int16)))),
                float(np.sqrt(np.mean(pcm.astype(np.float64) ** 2)))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
