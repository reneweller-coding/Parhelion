"""Fit the piano's voicing (PianoSpec, PianoDesign.h) to a reference piano (30.09.2026).

    python Tools/pianoref/fit.py ref.wav [--probe build/msvc/Tools/pianoprobe/Release/parh_pianoprobe.exe] [--evals 320]
                                 [--work dir] [--show key=value ...]

ref.wav plays notes_mid.py's notes (see compare.py for how to render it with Pianoteq). Every evaluation renders the same
notes with parh_pianoprobe and the knobs of the moment and measures both files the same way (compare.py's measures):
  - level: the rms of the levels against C4 at velocity 96, over the keyboard and the four velocities, dB;
  - bright, bright2: the centroids at 50..300 ms and 0.5..1 s, C2 .. C6, rms of log2(ours / ref);
  - vel: how the brightness grows from velocity 32 to 124, C3 .. C5 (log2 ratios);
  - early, late: the two decays, C2 .. C5, rms of dB/s;
  - knock, release: their means' differences, dB;
  - band: the octave spectra 63 Hz .. 8 kHz of 0.05..0.5 s per register (low, middle, high), each note normalised to its
    energy, mean absolute difference, dB;
  - body: 45..180 Hz in 0..60 ms and 60..400 ms at C4, G4, C5 against the note, rms, dB.
The objective weighs them into one number (evaluate()) and Nelder-Mead searches the design's knobs (NAMES), started at
the design's defaults of 30.09.2026 (the Steinway fit). Only numbers are written; the audio stays where it was rendered.
--show renders and prints one set of knobs note by note instead of fitting.
"""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from scipy import optimize, signal

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from compare import measure, onset, read_wav  # noqa: E402
from notes_mid import notes  # noqa: E402

ROOT = HERE.parent.parent
OCT = [63, 125, 250, 500, 1000, 2000, 4000, 8000]
REG = {"low": (24, 43), "mid": (48, 72), "high": (79, 96)}


def table(x, sr):
    rows = [(p, v, measure(x, sr, t, p, h)) for (t, p, v, h) in notes()]
    c4 = next(m["level"] for p, v, m in rows if p == 60 and v == 96)
    for p, v, m in rows:
        m["rel"] = m["level"] - c4
    return rows


def bands(x, sr):
    out = {}
    for (t0, p, v, h) in notes():
        if v not in (64, 96):
            continue
        mono = 0.5 * (x[:, 0] + x[:, 1])
        t = onset(mono, sr, t0)
        seg = mono[int((t + 0.05) * sr):int((t + 0.5) * sr)]
        s = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 32768)) ** 2
        f = np.fft.rfftfreq(32768, 1 / sr)
        tot = s[(f > 40) & (f < 16000)].sum()
        e = [10 * np.log10(s[(f >= c / np.sqrt(2)) & (f < c * np.sqrt(2))].sum() / tot + 1e-12) for c in OCT]
        reg = next(k for k, (lo, hi) in REG.items() if lo <= p <= hi)
        out.setdefault(reg, []).append(e)
    return {k: np.mean(v, 0) for k, v in out.items()}


def lowband(x, sr):
    sos = signal.butter(4, [45, 180], "bandpass", fs=sr, output="sos")
    out = []
    for p in (60, 67, 72):
        t0 = next(t for (t, pp, v, h) in notes() if pp == p and v == 96)
        mono = 0.5 * (x[:, 0] + x[:, 1])
        t = onset(mono, sr, t0)
        low = signal.sosfilt(sos, mono[int((t - 0.05) * sr):int((t + 1.2) * sr)])
        tot = np.mean(mono[int(t * sr):int((t + 0.5) * sr)] ** 2)
        off = int(0.05 * sr)
        out += [10 * np.log10(np.mean(low[off + int(a * sr):off + int(b * sr)] ** 2) / tot + 1e-20) for a, b in ((0, .06), (.06, .4))]
    return np.array(out)


def parts(rows, ref, xb, xr_bands, xl, xr_low):
    def pick(key, cond):
        a = np.array([m[key] for (p, v, m) in rows if cond(p, v)])
        b = np.array([m[key] for (p, v, m) in ref if cond(p, v)])
        return a, b

    out = {}
    a, b = pick("rel", lambda p, v: True)
    out["level"] = float(np.sqrt(np.mean((a - b) ** 2)))
    for key, name in (("c1", "bright"), ("c2", "bright2")):
        a, b = pick(key, lambda p, v: 36 <= p <= 84)
        out[name] = float(np.sqrt(np.mean(np.log2(a / b) ** 2)))
    for key, name in (("decay_early", "early"), ("decay_late", "late")):
        a, b = pick(key, lambda p, v: 36 <= p <= 72)
        out[name] = float(np.sqrt(np.nanmean((a - b) ** 2)))
    for key, name in (("knock", "knock"), ("release_drop", "release")):
        a, b = pick(key, lambda p, v: 36 <= p <= 84)
        out[name] = float(np.mean(a - b))

    def vel(rs):
        r = []
        for p in (48, 55, 60, 64, 67, 72):
            lo = next(m["c1"] for (pp, v, m) in rs if pp == p and v == 32)
            hi = next(m["c1"] for (pp, v, m) in rs if pp == p and v == 124)
            r.append(np.log2(hi / lo))
        return np.array(r)
    out["vel"] = float(np.sqrt(np.mean((vel(rows) - vel(ref)) ** 2)))
    out["band"] = float(np.mean([np.mean(np.abs(xb[k] - xr_bands[k])) for k in REG]))
    out["body"] = float(np.sqrt(np.mean((xl - xr_low) ** 2)))
    return out


def total(p):
    return (p["level"] / 3.0 + p["band"] / 3.0 + p["vel"] / 0.15 + p["early"] / 4.0 + p["late"] / 3.0 + abs(p["release"]) / 5.0
            + abs(p["knock"]) / 3.0 + p["bright"] / 0.3 + p["body"] / 4.0)


NAMES = ["felt", "hard-velocity", "coupling", "radiation", "radiation-end", "voice-bass", "voice-treble", "damper", "damper-length",
         "knock", "body-corner", "high-bank", "board-loss"]
DEFAULTS = {"felt": 0.7, "hard-velocity": 0.646, "coupling": 0.296, "radiation": 372.7, "radiation-end": 4505.0, "voice-bass": 3.64,
            "voice-treble": 7.54, "damper": 11.0, "damper-length": 0.2, "knock": 100.2, "body-corner": 50.9, "high-bank": 7.95,
            "board-loss": 0.539}
LOGS = {"felt", "coupling", "radiation", "radiation-end", "knock", "body-corner", "board-loss"}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ref")
    ap.add_argument("--probe", default=str(ROOT / "build" / "msvc" / "Tools" / "pianoprobe" / "Release" / "parh_pianoprobe.exe"))
    ap.add_argument("--evals", type=int, default=320)
    ap.add_argument("--work", default=str(ROOT / "work" / "pianoref"))
    ap.add_argument("--show", nargs="*")
    a = ap.parse_args()
    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    xr, sr = read_wav(a.ref)
    ref = (table(xr, sr), bands(xr, sr), lowband(xr, sr))
    note_list = ",".join("%d:%.3f:%.1f" % (p, v / 127.0, h) for _, p, v, h in notes())

    def evaluate(kw):
        args = [a.probe, "--notes", note_list, "--lead", "0.5", "--gap", "2", "--out", str(work / "fit.wav")]
        for k, v in kw.items():
            args += ["--" + k, "%.6g" % v]
        subprocess.run(args, capture_output=True, check=True)
        x, _ = read_wav(str(work / "fit.wav"))
        rows = table(x, sr)
        p = parts(rows, ref[0], bands(x, sr), ref[1], lowband(x, sr), ref[2])
        return total(p), p, rows

    if a.show is not None:
        kw = dict(DEFAULTS)
        for s in a.show:
            k, v = s.split("=")
            kw[k] = float(v)
        t, p, rows = evaluate(kw)
        for key in ("rel", "c1", "decay_early", "decay_late", "knock", "release_drop"):
            print("%-12s ref  " % key + " ".join("%5.1f" % m[key] for (pp, v, m) in ref[0] if v == 96))
            print("%-12s ours " % "" + " ".join("%5.1f" % m[key] for (pp, v, m) in rows if v == 96))
        print("total %.2f  " % t + "  ".join("%s %.2f" % (k, v) for k, v in p.items()))
        return 0

    def unpack(z):
        return {k: float(np.exp(z[i])) if k in LOGS else float(z[i]) for i, k in enumerate(NAMES)}

    z0 = np.array([np.log(DEFAULTS[k]) if k in LOGS else DEFAULTS[k] for k in NAMES])
    step = np.array([0.4 if k in LOGS else {"hard-velocity": 0.4, "voice-bass": 3.0, "voice-treble": 3.0, "damper": 2.0,
                                             "damper-length": 0.2, "high-bank": 3.0}[k] for k in NAMES])
    log = open(work / "fit_log.jsonl", "a")
    best = [1e9, None]

    def objective(z):
        kw = unpack(z)
        t, p, _ = evaluate(kw)
        log.write(json.dumps({"t": time.time(), "total": t, "parts": p, "params": kw}) + "\n")
        log.flush()
        if t < best[0]:
            best[0], best[1] = t, kw
            print("%.3f  %s" % (t, " ".join("%s=%.4g" % (k, v) for k, v in kw.items())), flush=True)
        return t

    simplex = [z0] + [z0 + np.eye(len(z0))[i] * step[i] for i in range(len(z0))]
    optimize.minimize(objective, z0, method="Nelder-Mead", options={"maxfev": a.evals, "initial_simplex": np.array(simplex)})
    print("best %.3f %s" % (best[0], json.dumps(best[1])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
