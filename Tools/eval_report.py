"""eval_report.py -- Parhelion's evaluation (PLAN 13.5, Dok. 9 and 10): what the composer planned and what the audio
shows, per track, against the references of its style.

usage:
  python Tools/eval_report.py --plan track.json --wav track.wav [--plan ... --wav ...] --out docs/eval/report.md
  python Tools/eval_report.py --plan set.json --wav set.wav --out docs/eval/set.md      (a set: every track on its own time)

The plan JSON is parh_render's --plan-json (style, key, tempo, every section with its bar and second). Per track:

  sections   the novelty curve of the audio (Foote, "Automatic audio segmentation using a measure of audio novelty",
             ICME 2000: a self-similarity matrix of per-beat spectra, a checkerboard kernel of four bars) against the
             planned section boundaries -- precision, recall and F1 within a bar; and the share of the boundaries the audio
             shows that lie on a multiple of 8 bars (Dok. 10's proxy for a DJ's use)
  drops      every planned drop heard: the level rises by 3 dB at least from the two bars before it to the two after
  gap        the main breakdown's loudest 3 s under the drop's (analyze_ref.py), against the style's references
  key        the key the audio shows (analyze_ref.py's Krumhansl-Kessler estimate) against the planned one; the relative
             major or minor counts as agreeing (the same scale), a fifth either way is counted apart (a Camelot neighbour:
             the estimate's commonest error, and a DJ's compatible key)
  corridor   analyze_ref.py's measures against the style's references (their range), as Tools/calibrate.py

FAD or CLAP against the references are left out (Dok. 9: they say nothing about the drop's timing or a DJ's use).
The report is Markdown (German, like the PLAN).
"""
from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import analyze_ref as ar  # noqa: E402
import calibrate as cal  # noqa: E402

KEYS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
IONIAN = 5
SR = 22050


def beat_features(x, sr, bpm, start_s, beats):
    """Per beat: the log energies of 24 bands (log-spaced 40 Hz .. 10 kHz) of the beat's window."""
    spb = 60.0 / bpm
    edges = np.geomspace(40.0, 10000.0, 25)
    feats = []
    n = int(spb * sr)
    win = np.hanning(n)
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    band = [(freqs >= edges[i]) & (freqs < edges[i + 1]) for i in range(24)]
    for b in range(beats):
        a = int((start_s + b * spb) * sr)
        seg = x[a:a + n]
        if len(seg) < n:
            seg = np.pad(seg, (0, n - len(seg)))
        spec = np.abs(np.fft.rfft(seg * win)) ** 2
        feats.append([math.log10(spec[m].sum() + 1e-10) for m in band])
    return np.array(feats)


def novelty(feats, half=16):
    """Foote's novelty: a Gaussian-tapered checkerboard kernel of 2 * half beats along the self-similarity matrix."""
    f = feats - feats.mean(axis=0)
    f /= np.linalg.norm(f, axis=1, keepdims=True) + 1e-9
    S = f @ f.T
    k = np.arange(-half, half)
    g = np.exp(-(((k + 0.5) / (half * 0.5)) ** 2))
    kern = np.outer(g, g) * np.outer(np.sign(k + 0.5), np.sign(k + 0.5))
    n = len(S)
    nov = np.zeros(n)
    for i in range(half, n - half):
        nov[i] = (S[i - half:i + half, i - half:i + half] * kern).sum()
    return np.maximum(nov, 0.0)


def peaks(nov, bpm, min_gap_beats=16):
    """The novelty's local maxima above its mean plus half a standard deviation, at least four bars apart."""
    thr = nov.mean() + 0.5 * nov.std()
    out = []
    for i in range(1, len(nov) - 1):
        if nov[i] > thr and nov[i] >= nov[i - 1] and nov[i] >= nov[i + 1]:
            if out and i - out[-1] < min_gap_beats:
                if nov[i] > nov[out[-1]]:
                    out[-1] = i
                continue
            out.append(i)
    return out


def rms_db(x):
    return 10.0 * math.log10(float(np.mean(x ** 2)) + 1e-12)


def key_name(key, scale):
    return KEYS[key % 12] + ("" if scale == IONIAN else "m")


def same_scale(a, b):
    """Whether two key names share a scale (the same, or the relative major/minor)."""
    def pcs(k):
        minor = k.endswith("m")
        t = KEYS.index(k[:-1] if minor else k)
        steps = [0, 2, 3, 5, 7, 8, 10] if minor else [0, 2, 4, 5, 7, 9, 11]
        return frozenset((t + s) % 12 for s in steps)
    try:
        return pcs(a) == pcs(b)
    except ValueError:
        return False


def fifth_apart(a, b):
    """Whether two key names are a fifth apart in the same mode (Camelot neighbours)."""
    try:
        ma, mb = a.endswith("m"), b.endswith("m")
        ta, tb = KEYS.index(a[:-1] if ma else a), KEYS.index(b[:-1] if mb else b)
    except ValueError:
        return False
    return ma == mb and (ta - tb) % 12 in (5, 7)


def evaluate(track, wav, offset_s, refs):
    bpm = track["bpm"]
    spb = 60.0 / bpm
    beats = int(track["bars"]) * 4
    start = track["start"] - offset_s
    dur = beats * spb
    x = ar.decode(wav, SR, mono=True, start=max(0.0, start), dur=dur)
    out = {"style": track["style"], "camelot": track["camelot"], "bars": track["bars"], "bpm": bpm}
    # Sections: novelty against the plan.
    feats = beat_features(x, SR, bpm, 0.0, beats)
    nov = novelty(feats)
    found = peaks(nov, bpm)
    planned = [4 * s["bar"] for s in track["sections"][1:]]
    hit_found = [f for f in found if any(abs(f - p) <= 4 for p in planned)]
    hit_planned = [p for p in planned if any(abs(f - p) <= 4 for f in found)]
    prec = len(hit_found) / max(1, len(found))
    rec = len(hit_planned) / max(1, len(planned))
    out["precision"], out["recall"] = prec, rec
    out["f1"] = 2 * prec * rec / max(1e-9, prec + rec)
    on8 = [f for f in found if round(f / 4.0) % 8 == 0]
    out["on8"] = len(on8) / max(1, len(found))
    out["boundaries"] = (len(found), len(planned))
    # Drops heard.
    drops = [s for s in track["sections"] if s["kind"] == "Drop"]
    heard = 0
    for s in drops:
        b = 4 * s["bar"]
        a0, a1 = int((b - 8) * spb * SR), int(b * spb * SR)
        c0, c1 = a1, int((b + 8) * spb * SR)
        if a0 >= 0 and c1 <= len(x) and rms_db(x[c0:c1]) - rms_db(x[a0:a1]) >= 3.0:
            heard += 1
    out["drops"] = (heard, len(drops))
    # The measures of the references on the track's own audio (cut out when it is part of a set).
    with tempfile.TemporaryDirectory() as tmp:
        seg = Path(tmp) / "track.wav"
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", f"{max(0.0, start):.3f}", "-t", f"{dur:.3f}", "-i", str(wav), str(seg)], check=True)
        m = ar.measure(str(seg), bpm)
    out["gap_lu"] = m.get("gap_lu")
    audio_key = m.get("key", "")
    out["key_plan"] = key_name(track["key"], track["scale"])
    out["key_audio"] = audio_key
    out["key_ok"] = audio_key == out["key_plan"] or same_scale(audio_key, out["key_plan"])
    out["key_fifth"] = not out["key_ok"] and fifth_apart(audio_key, out["key_plan"])
    prof = cal.STYLES.get(track["style"], None)
    inside, total, outside = 0, 0, []
    for k in cal.MEASURES:
        vals = [r[k] for r in refs if r.get("profile") == prof and isinstance(r.get(k), (int, float)) and r[k] == r[k]]
        v = m.get(k)
        if not vals or not isinstance(v, (int, float)) or v != v:
            continue
        total += 1
        if min(vals) <= v <= max(vals):
            inside += 1
        else:
            outside.append(k)
    out["corridor"] = (inside, total, outside)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plan", action="append", required=True)
    ap.add_argument("--wav", action="append", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")   # (the console's code page has no ✗)
    refs = json.loads((HERE / "ref_stats.json").read_text(encoding="utf-8"))["rows"]
    lines = ["# Evaluationsbericht", "", "Parhelion, `Tools/eval_report.py` (PLAN 13.5): der Plan gegen das Audio, je Track.", "",
             "| Track | Stil | Tonart Plan/Audio | Grenzen gefunden/geplant | P | R | F1 | auf 8 Takten | Drops gehört | Abstand LU | Korridor |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    rows = []
    for plan_path, wav in zip(a.plan, a.wav):
        plan = json.loads(Path(plan_path).read_text(encoding="utf-8"))
        for i, track in enumerate(plan["tracks"]):
            r = evaluate(track, wav, 0.0, refs)
            rows.append(r)
            name = f"{Path(wav).stem}" + (f" T{i + 1}" if plan.get("set") else "")
            gap = f"{r['gap_lu']:.1f}" if isinstance(r["gap_lu"], (int, float)) and r["gap_lu"] == r["gap_lu"] else "-"
            lines.append(f"| {name} | {r['style']} | {r['key_plan']} / {r['key_audio']}{'' if r['key_ok'] else ' ≈' if r['key_fifth'] else ' ✗'} | "
                         f"{r['boundaries'][0]}/{r['boundaries'][1]} | {r['precision']:.2f} | {r['recall']:.2f} | {r['f1']:.2f} | "
                         f"{100 * r['on8']:.0f} % | {r['drops'][0]}/{r['drops'][1]} | {gap} | {r['corridor'][0]}/{r['corridor'][1]} |")
            print(lines[-1], flush=True)
    if rows:
        lines += ["", "## Zusammenfassung", ""]
        f1 = np.mean([r["f1"] for r in rows])
        on8 = np.mean([r["on8"] for r in rows])
        drops = sum(r["drops"][0] for r in rows), sum(r["drops"][1] for r in rows)
        keys = sum(1 for r in rows if r["key_ok"])
        lines.append(f"- Sektionsgrenzen (Neuheitskurve gegen den Plan, ±1 Takt): F1 im Mittel {f1:.2f}")
        lines.append(f"- Grenzen auf Vielfachen von 8 Takten: {100 * on8:.0f} %")
        lines.append(f"- Drops hörbar (+3 dB): {drops[0]} von {drops[1]}")
        fifths = sum(1 for r in rows if r["key_fifth"])
        lines.append(f"- Tonart auf dem Audio wie geplant (oder die Parallele): {keys} von {len(rows)}; eine Quinte daneben "
                     f"(Camelot-Nachbar, ≈): {fifths}")
        outs = {}
        for r in rows:
            for k in r["corridor"][2]:
                outs[k] = outs.get(k, 0) + 1
        if outs:
            lines.append("- Außerhalb des Korridors der Referenzen: " + ", ".join(f"{k} ({n}×)" for k, n in sorted(outs.items(), key=lambda kv: -kv[1])))
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print("report:", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
