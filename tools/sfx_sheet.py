#!/usr/bin/env python3
"""Waveform + spectrogram contact sheet of SND files, for evidence/review.

    tools/sfx_sheet.py assets/gen/sfx/*.snd -o sheet.png [--cols 2]

Each cell: name and duration, the waveform (int8, +-127 band, the +-PEAK
lines dashed) and a log-magnitude spectrogram (0..11025 Hz, linear axis, 60 dB
range below each sound's peak).
All cells share one time scale (pixels per second) so lengths compare.
Pillow and numpy only.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bast  # noqa: E402

RATE = bast.SND_RATE
W, WAVE_H, SPEC_H, LABEL_H = 520, 70, 90, 14
PX_PER_S = 400  # 1.3 s fits in a cell


def spectrogram(x, width, height):
    n_fft, out = 512, np.zeros((height, width))
    win = np.hanning(n_fft)
    pad = np.concatenate([np.zeros(n_fft // 2), x, np.zeros(n_fft)])
    hop = RATE / PX_PER_S
    cols = min(width, int(len(x) / hop) + 1)
    for c in range(cols):
        s = int(c * hop)
        mag = np.abs(np.fft.rfft(pad[s:s + n_fft] * win))[: n_fft // 2]
        db = 20 * np.log10(mag + 1e-3)
        # resample the 256 bins onto `height` rows, low frequencies at the bottom
        rows = np.interp(np.linspace(0, len(db) - 1, height), np.arange(len(db)), db)
        out[:, c] = rows[::-1]
    top = out[:, :cols].max() if cols else 0.0
    out = np.clip((out - (top - 60.0)) / 60.0, 0, 1)  # 60 dB range below the peak
    return out, cols


def cell(name, pcm):
    img = Image.new("RGB", (W, LABEL_H + WAVE_H + SPEC_H + 6), (16, 16, 20))
    d = ImageDraw.Draw(img)
    d.text((4, 1), "%s  %.3f s  %d smp" % (name, len(pcm) / RATE, len(pcm)), fill=(230, 230, 230))
    y0, mid = LABEL_H, LABEL_H + WAVE_H // 2
    d.line([(0, mid), (W, mid)], fill=(60, 60, 70))
    for lv in (112, -112):
        yy = mid - lv * (WAVE_H // 2) / 128
        for xx in range(0, W, 8):
            d.point((xx, yy), fill=(90, 60, 60))
    x = pcm.astype(np.float64)
    spp = RATE / PX_PER_S
    for c in range(W):
        seg = x[int(c * spp):int((c + 1) * spp)]
        if not len(seg):
            break
        lo, hi = seg.min(), seg.max()
        d.line([(c, mid - hi * (WAVE_H // 2) / 128), (c, mid - lo * (WAVE_H // 2) / 128)],
               fill=(120, 220, 140))
    spec, cols = spectrogram(x / 128.0, W, SPEC_H)
    v = (spec * 255).astype(np.uint8)
    vi = v.astype(np.int32)
    rgb = np.stack([np.minimum(255, vi * 3 // 2), vi * vi // 255, vi // 3 + 20],
                   axis=-1).astype(np.uint8)
    rgb[:, cols:] = 16
    img.paste(Image.fromarray(rgb, "RGB"), (0, y0 + WAVE_H + 4))
    return img


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("snd", nargs="+")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--cols", type=int, default=2)
    a = ap.parse_args(argv)
    cells = []
    for p in a.snd:
        s = bast.Snd.load(p)
        pcm = np.frombuffer(s.samples, dtype=np.int8)
        cells.append(cell(os.path.splitext(os.path.basename(p))[0], pcm))
    cw, ch = cells[0].size
    rows = (len(cells) + a.cols - 1) // a.cols
    sheet = Image.new("RGB", (a.cols * (cw + 4) + 4, rows * (ch + 4) + 4), (40, 40, 48))
    for i, c in enumerate(cells):
        sheet.paste(c, (4 + (i % a.cols) * (cw + 4), 4 + (i // a.cols) * (ch + 4)))
    sheet.save(a.out)
    print("%s: %d sounds, %dx%d" % (a.out, len(cells), sheet.size[0], sheet.size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
