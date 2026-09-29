"""Calibration of the style profiles against the reference tracks (PLAN 13.4, Phase 4c).

Renders tracks of every style with parh_render (or takes them from out/calib if they are there), measures them with
exactly the analysis the references went through (analyze_ref.measure), and sets every measure against the range of
the style's references: the minimum, the median and the maximum of the recordings in Tools/ref_stats.json. A measure
outside the range is marked; the acceptance of Phase 4c is a track of every style inside its references' corridor on
the measures that describe the genre (tempo, length, breakdown share and distance, loudness and its range, the balance
of the bands, the width, the pump).

    python Tools/calibrate.py                    # three seeds of every style
    python Tools/calibrate.py --seeds 7 --only Deep
    python Tools/calibrate.py --force            # render again
    python Tools/calibrate.py --force --jobs 6   # six renders at a time

Only statistics are printed; the rendered audio stays in out/calib (not in the repository).
"""
import argparse
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import analyze_ref as ar   # noqa: E402

STYLES = {"Uplifting": "uplifting", "Progressive": "progressive", "Dream House": "dream", "Acid": "acid", "Deep": "deep"}
# The measures that describe the genre and that the generator decides; the rest (the key, the kick's pitch against it)
# are drawn per track and not to be matched. The side of the band under 120 Hz is left out on purpose: Parhelion's low
# end is mono by the rule of the mix (Dok. 7, master.mono_below), and the references' -14 to -37 dB there come partly
# from their lossy transcodes (a joint-stereo codec's side channel).
MEASURES = ["bpm", "minutes", "lufs", "lra", "loud20", "breakdown_share", "breakdown_bars", "gap_lu", "b20_60", "b60_150",
            "b150_400", "b400_2k", "b2k_5k", "b5k_16k", "centroid", "side_hi_db", "correlation", "pump_mid",
            "bass_offbeat"]


def render(style, seed, out, force):
    wav = out / f"{style.replace(' ', '')}_{seed}.wav"
    if wav.exists() and not force:
        return wav
    exe = ROOT / "build" / "Tools" / "render" / "Release" / "parh_render.exe"
    if not exe.exists():
        exe = ROOT / "build" / "Tools" / "render" / "parh_render"
    with open(wav.with_suffix(".log"), "w", encoding="utf-8", newline="\n") as log:
        subprocess.run([str(exe), "--seed", str(seed), "--style", style, "--out", str(wav)], check=True, stdout=log,
                       stderr=subprocess.STDOUT)
    return wav


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seeds", default="1,2,3")
    ap.add_argument("--only", default="")
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--jobs", type=int, default=1, help="renders at a time")
    a = ap.parse_args()
    stats = json.loads((HERE / "ref_stats.json").read_text(encoding="utf-8"))
    out = ROOT / "out" / "calib"
    out.mkdir(parents=True, exist_ok=True)
    seeds = [int(s) for s in a.seeds.split(",") if s]
    summary = {}
    # Every render first (as many at a time as asked), then the measures style by style.
    todo = [(style, seed) for style in STYLES if not a.only or a.only == style for seed in seeds]
    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        wavs = dict(zip(todo, pool.map(lambda job: render(job[0], job[1], out, a.force), todo)))
    for style, prof in STYLES.items():
        if a.only and a.only != style:
            continue
        refs = [r for r in stats["rows"] if r["profile"] == prof]
        rows = []
        for seed in seeds:
            rows.append(ar.measure(str(wavs[(style, seed)])))
        print(f"\n== {style}: {len(rows)} tracks against {len(refs)} references")
        print(f"{'measure':16s} {'ref min':>9s} {'median':>9s} {'max':>9s}   " + " ".join(f"{'s' + str(s):>9s}" for s in seeds))
        inside = 0
        for m in MEASURES:
            vals = [r[m] for r in refs if isinstance(r.get(m), (int, float)) and r[m] == r[m]]
            if not vals:
                continue
            lo, hi = min(vals), max(vals)
            med = sorted(vals)[len(vals) // 2]
            cells = []
            ok_all = True
            for r in rows:
                v = r.get(m)
                ok = isinstance(v, (int, float)) and v == v and lo <= v <= hi
                ok_all = ok_all and ok
                cells.append(f"{v:>8.3g}{' ' if ok else '!'}" if isinstance(v, (int, float)) else f"{'-':>9s}")
            inside += ok_all
            print(f"{m:16s} {lo:9.3g} {med:9.3g} {hi:9.3g}   " + " ".join(cells))
        summary[style] = (inside, len(MEASURES))
        print(f"inside the references' range on every track: {inside} of {len(MEASURES)} measures")
    print("\n" + ", ".join(f"{s} {i}/{n}" for s, (i, n) in summary.items()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
