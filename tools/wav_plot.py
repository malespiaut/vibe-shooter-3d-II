#!/usr/bin/env python3
"""Waveform + spectrogram PNG of a 16-bit PCM WAV (evidence for audio PRs).

    tools/wav_plot.py in.wav out.png [--width 1200] [--seconds N]

Top: min/max waveform of the left+right mix per column. Bottom: log-magnitude
spectrogram, 0..5.5 kHz on a linear axis (low notes at the bottom), with a
tick every 5 s. Pillow and numpy only."""
import argparse
import wave

import numpy as np
from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("wav")
    ap.add_argument("png")
    ap.add_argument("--width", type=int, default=1200)
    ap.add_argument("--seconds", type=float, default=0, help="only the first N s")
    a = ap.parse_args()

    with wave.open(a.wav, "rb") as w:
        assert w.getsampwidth() == 2, "16-bit WAV expected"
        rate, ch = w.getframerate(), w.getnchannels()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    x = x.reshape(-1, ch).mean(axis=1) / 32768.0
    if a.seconds:
        x = x[: int(a.seconds * rate)]

    W, WH, SH = a.width, 160, 320
    img = Image.new("RGB", (W, WH + SH + 18), (16, 16, 24))
    d = ImageDraw.Draw(img)

    # Waveform.
    cols = np.array_split(x, W)
    mid = WH // 2
    for i, c in enumerate(cols):
        if len(c):
            lo, hi = c.min(), c.max()
            d.line([(i, mid - hi * mid), (i, mid - lo * mid)], fill=(120, 200, 255))
    d.line([(0, mid), (W, mid)], fill=(60, 60, 80))

    # Spectrogram: one 2048-point Hann FFT per column.
    n = 2048
    fmax = 5500
    bins = int(fmax * n / rate)
    win = np.hanning(n)
    spec = np.zeros((bins, W))
    hop = max(1, (len(x) - n) // W)
    for i in range(W):
        seg = x[i * hop : i * hop + n]
        if len(seg) < n:
            break
        spec[:, i] = np.abs(np.fft.rfft(seg * win))[:bins]
    db = 20 * np.log10(spec + 1e-6)
    db = np.clip((db - (db.max() - 70)) / 70, 0, 1)
    rows = np.linspace(bins - 1, 0, SH).astype(int)
    v = db[rows, :]
    rgb = np.stack([v ** 0.7 * 255, v ** 1.5 * 220, (1 - v) * v * 4 * 120 + v * 40], axis=-1)
    img.paste(Image.fromarray(rgb.astype(np.uint8)), (0, WH))

    secs = len(x) / rate
    for s in range(0, int(secs) + 1, 5):
        px = int(s / secs * (W - 1))
        d.line([(px, WH + SH), (px, WH + SH + 5)], fill=(200, 200, 200))
        d.text((px + 2, WH + SH + 4), f"{s}s", fill=(200, 200, 200))
    img.save(a.png)
    print(f"wav_plot: {a.wav}: {secs:.2f} s, {rate} Hz -> {a.png} ({img.width}x{img.height})")


if __name__ == "__main__":
    main()
