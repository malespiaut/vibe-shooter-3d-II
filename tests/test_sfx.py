#!/usr/bin/env python3
"""ctest `sfx_determinism`: tools/sfx.py renders every assets/src/sfx/*.json
twice into separate directories; the .snd files must be byte-identical, load
through libbast, and hit the loudness target. Also checks the SND wrapper in
tools/bast.py and that bad parameter files are rejected. Plain asserts.
"""
import filecmp
import glob
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)
import bast  # noqa: E402
import sfx  # noqa: E402

PARAMS = sorted(glob.glob(os.path.join(ROOT, "assets", "src", "sfx", "*.json")))


def run(*args):
    p = subprocess.run([sys.executable, os.path.join(TOOLS, "sfx.py")] + list(args),
                       capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def test_wrapper_roundtrip(d):
    pcm = bytes(range(256))
    p = os.path.join(d, "w.snd")
    bast.Snd(pcm).save(p)
    with open(p, "rb") as f:
        raw = f.read()
    assert raw[:12] == b"SND1" + (22050).to_bytes(4, "little") + (256).to_bytes(4, "little")
    assert raw[12:] == pcm
    assert bast.Snd.load(p).samples == pcm
    with open(p, "wb") as f:
        f.write(b"SNDX" + raw[4:])
    try:
        bast.Snd.load(p)
        assert False, "bad magic accepted"
    except bast.BastError as e:
        assert "SND1" in str(e)


def test_determinism(d):
    assert PARAMS, "no assets/src/sfx/*.json"
    a, b = os.path.join(d, "a"), os.path.join(d, "b")
    for out in (a, b):
        rc, log = run("-q", *PARAMS, "-o", out)
        assert rc == 0, log
    names = sorted(os.listdir(a))
    assert names == sorted(os.path.basename(p)[:-5] + ".snd" for p in PARAMS), names
    for n in names:
        assert filecmp.cmp(os.path.join(a, n), os.path.join(b, n), shallow=False), n
        s = bast.Snd.load(os.path.join(a, n))
        peak = max(abs(v - 256 if v > 127 else v) for v in s.samples)
        assert peak == sfx.PEAK, (n, peak)
        assert 0 < len(s) <= 2 * 22050, (n, len(s))  # short effects


def test_params_documented():
    for p in PARAMS:
        with open(p) as f:
            j = json.load(f)
        assert j.get("desc"), p


def test_rejects(d):
    bad = os.path.join(d, "bad.json")
    for params in ({"voices": []}, {"voices": [{"wave": "sine"}]},
                   {"voices": [{"wave": "square", "frq": 3}]}, {"volume": 1, "voices": []}):
        with open(bad, "w") as f:
            json.dump(params, f)
        rc, log = run(bad, "-o", os.path.join(d, "bad"))
        assert rc == 1 and "bad.json" in log, (params, rc, log)


def main():
    with tempfile.TemporaryDirectory() as d:
        test_wrapper_roundtrip(d)
        test_determinism(d)
        test_params_documented()
        test_rejects(d)
    print("sfx_determinism: ok (%d effects)" % len(PARAMS))


if __name__ == "__main__":
    main()
