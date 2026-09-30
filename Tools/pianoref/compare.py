"""Parhelion's piano against a reference piano, note by note (30.09.2026; the user: "Du kannst es gerne mit Pianoteq
vergleichen und daran abstimmen. Das ist als VST und Standalone hier auf dem Rechner installiert").

    python Tools/pianoref/notes_mid.py notes.mid
    "Pianoteq 8.exe" --headless --preset "NY Steinway Model D" --midi notes.mid --wav ref.wav --rate 48000 --bit-depth 24
    parh_pianoprobe --notes "<notes_mid.py --list>" --lead 0.5 --gap 2 --instrument 0 --out ours.wav
    python Tools/pianoref/compare.py ref.wav ours.wav [--json out.json]

Both files play the notes of notes_mid.py on the same timing. Per note it measures, the same way on both:
  - level: the RMS of the first half second, in dB against the file's own C4 at velocity 96 (the curve over the keyboard
    and over the velocity, not the absolute loudness, which the mix sets anyway);
  - brightness: the spectral centroid in four windows (0..50 ms, 50..300 ms, 0.5..1 s, 2..3 s) as a multiple of f0;
  - decay: the level's slope in dB/s early (0.3..1 s) and late (1.5..2.9 s): the two-stage decay of coupled strings;
  - partials: the amplitudes of partials 1 .. 12 against the fundamental at 0.1..0.6 s, their decay in dB/s between
    0.1..0.6 s and 1.5..2.5 s, and the inharmonicity B fitted to the peaks;
  - the knock: the energy under 0.7 f0 and above the partials in the first 40 ms against the note's (the hammer's and
    the action's thump);
  - release: how far the level falls in the 0.3 s after the key is let go;
  - width: the side against the mid in the first second.
Only numbers are written (the reference's audio stays outside the repository, as the reference tracks do).
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from notes_mid import GAP, HOLD, notes  # noqa: E402


def read_wav(path):
    raw = open(path, "rb").read()
    pos, data, ch, sr, bits, fmt = 12, None, 2, 48000, 16, 1
    while pos + 8 <= len(raw):
        cid, size = raw[pos:pos + 4], struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            fmt, ch, sr = struct.unpack("<HHI", raw[pos + 8:pos + 16])
            bits = struct.unpack("<H", raw[pos + 22:pos + 24])[0]
        if cid == b"data":
            data = raw[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    if fmt == 3 or (fmt == 0xFFFE and bits == 32):
        v = np.frombuffer(data, dtype=np.float32).astype(np.float64)
    elif bits == 24:
        b = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = (((b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)) << 8) >> 8) / 2.0 ** 23
    else:
        v = np.frombuffer(data, dtype=np.int16) / 32768.0
    return v.reshape(-1, ch), sr


def db(x):
    return 10.0 * math.log10(max(float(x), 1e-20))


def onset(mono, sr, t0):
    """The note's onset near its scheduled time: where the level first reaches a tenth of its first 0.2 s's peak."""
    a = max(0, int((t0 - 0.05) * sr))
    seg = np.abs(mono[a:a + int(0.3 * sr)])
    if not len(seg) or seg.max() <= 0:
        return t0
    return (a + int(np.argmax(seg >= 0.1 * seg.max()))) / sr


def spectrum(x, sr, a, b, n=65536):
    seg = x[int(a * sr):int(b * sr)]
    if len(seg) < 16:
        return None, None
    w = np.hanning(len(seg))
    s = np.abs(np.fft.rfft(seg * w, n)) ** 2
    return np.fft.rfftfreq(n, 1.0 / sr), s


def partials(f, s, f0, count=12):
    """Peak frequencies and powers of partials 1..count, searched with a growing stretch; B fitted to them."""
    got = []
    stretch = 1.0
    for k in range(1, count + 1):
        centre = k * f0 * stretch
        lo, hi = centre * (1 - 0.025), centre * (1 + 0.025 + 0.0006 * k * k)
        sel = (f >= lo) & (f <= hi)
        if not sel.any():
            break
        i = np.argmax(np.where(sel, s, 0.0))
        got.append((k, f[i], s[i]))
        stretch = f[i] / (k * f0) if k >= 2 else stretch
    # f_k = k f0 sqrt(1 + B k^2): (f_k / k)^2 = f0^2 (1 + B k^2), a line in k^2
    if len(got) >= 4:
        k2 = np.array([g[0] ** 2 for g in got], dtype=float)
        y = np.array([(g[1] / g[0]) ** 2 for g in got])
        slope, icpt = np.polyfit(k2, y, 1)
        B = slope / icpt if icpt > 0 else float("nan")
    else:
        B = float("nan")
    return got, B


def measure(x, sr, t0, pitch, hold):
    mono = 0.5 * (x[:, 0] + x[:, 1])
    side = 0.5 * (x[:, 0] - x[:, 1])
    t = onset(mono, sr, t0)
    f0 = 440.0 * 2 ** ((pitch - 69) / 12.0)
    r = {}
    first = mono[int(t * sr):int((t + 0.5) * sr)]
    r["level"] = db(np.mean(first ** 2))
    for name, a, b in (("c0", 0.0, 0.05), ("c1", 0.05, 0.3), ("c2", 0.5, 1.0), ("c3", 2.0, 2.9)):
        f, s = spectrum(mono, sr, t + a, t + b, 16384)
        band = (f > 0.5 * f0) & (f < 16000)
        r[name] = float((f[band] * s[band]).sum() / max(s[band].sum(), 1e-30) / f0)
    # The level's slope, from 50 ms windows.
    hop = int(0.05 * sr)
    env = np.array([db(np.mean(mono[int(t * sr) + i * hop:int(t * sr) + (i + 1) * hop] ** 2)) for i in range(int(hold / 0.05))])
    tt = np.arange(len(env)) * 0.05
    for name, a, b in (("decay_early", 0.3, 1.0), ("decay_late", 1.5, min(2.9, hold - 0.1))):
        sel = (tt >= a) & (tt <= b)
        r[name] = float(np.polyfit(tt[sel], env[sel], 1)[0]) if sel.sum() >= 3 else float("nan")
    # Partials.
    f, s = spectrum(mono, sr, t + 0.1, t + 0.6)
    got, B = partials(f, s, f0)
    r["B"] = B
    p1 = got[0][2] if got else 1e-30
    r["partials_db"] = [round(db(g[2] / p1), 1) for g in got]
    f2, s2 = spectrum(mono, sr, t + 1.5, t + 2.5)
    decays = []
    for k, fk, pk in got:
        sel = (f2 >= fk * 0.995) & (f2 <= fk * 1.005)
        later = s2[sel].max() if sel.any() else 1e-30
        decays.append(round((db(later) - db(pk)) / 1.4, 1))
    r["partial_decay"] = decays
    # The knock: under 0.7 f0 and between the partials in the first 40 ms, against the whole first 40 ms.
    fk, sk = spectrum(mono, sr, t, t + 0.04, 16384)
    mask = np.ones_like(fk, dtype=bool)
    for k, fp, _ in got:
        mask &= np.abs(fk - fp) > max(0.08 * f0, 15.0)
    low = fk < 0.7 * f0
    r["knock"] = db(sk[(mask | low) & (fk > 20)].sum() / max(sk[fk > 20].sum(), 1e-30))
    # Release and width.
    rel = int((t + hold) * sr)
    before = db(np.mean(mono[rel - int(0.1 * sr):rel] ** 2))
    after = db(np.mean(mono[rel + int(0.25 * sr):rel + int(0.35 * sr)] ** 2))
    r["release_drop"] = before - after
    r["width"] = db(np.mean(side[int(t * sr):int((t + 1.0) * sr)] ** 2) / max(np.mean(mono[int(t * sr):int((t + 1.0) * sr)] ** 2), 1e-30))
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ref")
    ap.add_argument("ours")
    ap.add_argument("--json")
    a = ap.parse_args()
    xr, sr = read_wav(a.ref)
    xo, so = read_wav(a.ours)
    assert sr == so
    rows = []
    for t0, p, v, h in notes():
        rows.append({"pitch": p, "vel": v, "ref": measure(xr, sr, t0, p, h), "ours": measure(xo, sr, t0, p, h)})
    # Levels against each file's own C4 at velocity 96.
    for side in ("ref", "ours"):
        c4 = next(r[side]["level"] for r in rows if r["pitch"] == 60 and r["vel"] == 96)
        for r in rows:
            r[side]["rel_level"] = r[side]["level"] - c4
    print("%5s %4s | %7s %7s | %5s %5s %5s %5s | %6s %6s | %6s %6s | %8s %8s | %5s %5s | %5s %5s"
          % ("pitch", "vel", "lvl ref", "ours", "cen0", "cen1", "cen2", "cen3", "dEarly", "ours", "dLate", "ours",
             "B ref", "ours", "knock", "ours", "rel", "ours"))
    for r in rows:
        q, o = r["ref"], r["ours"]
        print("%5d %4d | %7.1f %7.1f | %5.1f %5.1f %5.1f %5.1f | %6.1f %6.1f | %6.1f %6.1f | %8.1e %8.1e | %5.1f %5.1f | %5.0f %5.0f"
              % (r["pitch"], r["vel"], q["rel_level"], o["rel_level"], q["c1"], o["c1"], q["c2"], o["c2"],
                 q["decay_early"], o["decay_early"], q["decay_late"], o["decay_late"], q["B"], o["B"], q["knock"], o["knock"],
                 q["release_drop"], o["release_drop"]))
    print("\npartials 1..12 at 0.1..0.6 s, dB against the fundamental (velocity 96):")
    for r in rows:
        if r["vel"] != 96:
            continue
        print("%3d ref  %s" % (r["pitch"], " ".join("%5.0f" % v for v in r["ref"]["partials_db"])))
        print("    ours %s" % " ".join("%5.0f" % v for v in r["ours"]["partials_db"]))
    print("\npartial decay 1..12, dB/s between 0.35 and 2 s (velocity 96):")
    for r in rows:
        if r["vel"] != 96:
            continue
        print("%3d ref  %s" % (r["pitch"], " ".join("%5.1f" % v for v in r["ref"]["partial_decay"])))
        print("    ours %s" % " ".join("%5.1f" % v for v in r["ours"]["partial_decay"]))
    print("\nwidth (side against mid, dB), velocity 96:")
    print("  ref  " + " ".join("%5.1f" % r["ref"]["width"] for r in rows if r["vel"] == 96))
    print("  ours " + " ".join("%5.1f" % r["ours"]["width"] for r in rows if r["vel"] == 96))
    if a.json:
        Path(a.json).write_text(json.dumps(rows, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
