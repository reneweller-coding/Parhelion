"""Builds the melodic statistics of Parhelion from the local trance MIDI corpus (PLAN 6.6, stage A).

Reads the royalty-free EMP packs of the VORTEX bundle under a root folder (M:/Midi by default) -- melodies (the lead),
pianos (the piano's line), acid (the 303), basslines -- estimates each file's key, puts its top line on a sixteenth grid
and counts, per role:

  * transitions of order 0, 1 and 2 over scale steps: a note's diatonic step above the minor tonic (-7 .. +14, the
    natural minor's seven degrees per octave; a chromatic note goes to the nearer degree), so that a melody in the
    relative major, which the key profile cannot tell from its minor, counts the same;
  * rhythm: the chance of an onset on each of the 32 sixteenths of two bars given whether the step before had one, and
    note lengths in sixteenths;
  * articulation (the 303): accent (a velocity clearly above the loop's median) and slide (a note overlapping the next)
    per sixteenth of the bar.

The output is Core/src/compose/CorpusTables.cpp: counts only -- statistics of step successions and positions, from which
no loop can be read back. The MIDI files never leave this machine.

The 5452 fan transcriptions of famous tracks in the same bundle are *not* counted (PLAN 16.2): they serve the
memorisation check alone (memorisation.py).

    python Tools/corpus/build_corpus.py [--root M:/Midi] [--out Core/src/compose/CorpusTables.cpp] [--report]

@note The MIDI reader is copied from Phosphene `Tools/corpus/build_corpus.py` at 76f7100 (29.09.2026).
"""
import argparse
import math
import os
import struct
import sys
from collections import Counter, defaultdict

BUNDLE = "VORTEX ULTIMATE TRANCE BUNDLE/Vortex Ultimate Trance Bundle/Vortex Ultimate Trance Midi Pack ( 6000  Midis )"
PACKS = {"lead": "EMP - Trance Melodies 500 Midi Pack", "piano": "EMP - Trance Pianos 100 Midi Pack",
         "acid": "EMP - Trance Acid 100 Midi Pack", "bass": "EMP - Trance Basslines 200 Midi Pack"}
ROLES = ["lead", "piano", "acid", "bass"]
STEP_MIN, STEP_MAX = -7, 14
ALPHA = STEP_MAX - STEP_MIN + 1          # 22 scale steps
AEOLIAN = [0, 2, 3, 5, 7, 8, 10]
KK_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
KK_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]


def read_vlq(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def read_midi(path):
    """(ppq, notes) with notes (start, end, pitch, velocity), drums excluded; running status and SysEx handled."""
    d = open(path, "rb").read()
    if d[:4] != b"MThd":
        return None, []
    _, ntrk, div = struct.unpack(">HHH", d[8:14])
    if div & 0x8000:
        return None, []
    i = 8 + struct.unpack(">I", d[4:8])[0]
    notes = []
    for _ in range(ntrk):
        if d[i:i + 4] != b"MTrk":
            break
        ln = struct.unpack(">I", d[i + 4:i + 8])[0]
        j, end, t, run = i + 8, i + 8 + ln, 0, None
        open_notes = {}
        while j < end:
            dt, j = read_vlq(d, j)
            t += dt
            st = d[j]
            if st == 0xFF:
                l, j2 = read_vlq(d, j + 2)
                j = j2 + l
                continue
            if st in (0xF0, 0xF7):
                l, j2 = read_vlq(d, j + 1)
                j = j2 + l
                run = None
                continue
            if st & 0x80:
                if st >= 0xF0:
                    break
                run = st
                j += 1
            if run is None:
                break
            hi, ch = run & 0xF0, run & 0x0F
            n = 1 if hi in (0xC0, 0xD0) else 2
            if j + n > end:
                break
            a = d[j]
            b = d[j + 1] if n == 2 else 0
            j += n
            if ch == 9:
                continue
            if hi == 0x90 and b > 0:
                open_notes.setdefault(a, []).append((t, b))
            elif hi == 0x80 or (hi == 0x90 and b == 0):
                if open_notes.get(a):
                    s, v = open_notes[a].pop(0)
                    notes.append((s, t, a, v))
        i = end
    return div, sorted(notes)


def minor_tonic(notes):
    """The minor tonic: Krumhansl-Kessler over duration-weighted pitch classes; a major key gives its relative minor."""
    hist = [0.0] * 12
    for s, e, p, v in notes:
        hist[p % 12] += max(1, e - s)

    def corr(prof, k):
        xs = [hist[(k + i) % 12] for i in range(12)]
        mx, my = sum(xs) / 12, sum(prof) / 12
        num = sum((x - mx) * (y - my) for x, y in zip(xs, prof))
        den = math.sqrt(sum((x - mx) ** 2 for x in xs) * sum((y - my) ** 2 for y in prof)) or 1.0
        return num / den
    best = max([(corr(KK_MINOR, k), k, False) for k in range(12)] + [(corr(KK_MAJOR, k), k, True) for k in range(12)])
    return (best[1] + 9) % 12 if best[2] else best[1]


def step_of(pitch, tonic, ref_octave):
    """The diatonic step of a pitch above the minor tonic (the octave of ref_octave is step 0); chromatic to the nearer."""
    rel = pitch - (tonic + 12 * ref_octave)
    octave, pc = divmod(rel, 12)
    # The nearer degree; a tie goes to the lower one (harmonic minor's raised seventh stays the seventh).
    best = min(range(7), key=lambda d: (abs(AEOLIAN[d] - pc), -AEOLIAN[d] if AEOLIAN[d] < pc else AEOLIAN[d]))
    return 7 * octave + best


def top_line(ppq, notes):
    """The top voice on a sixteenth grid: (step, pitch, velocity, length, slide) per onset step."""
    step = ppq / 4.0
    by = defaultdict(list)
    for s, e, p, v in notes:
        by[int(round(s / step))].append((p, v, s, e))
    onsets = sorted(by)
    out = {}
    for idx, k in enumerate(onsets):
        p, v, s, e = max(by[k])
        nxt = onsets[idx + 1] if idx + 1 < len(onsets) else None
        out[k] = (p, v, max(1, int(round((e - s) / step))), nxt is not None and e > nxt * step + 1)
    return out


def analyse(root):
    stats = {r: {"t0": Counter(), "t1": Counter(), "t2": Counter(), "on": [[0, 0] for _ in range(64)],
                 "len": Counter(), "acc": [[0, 0] for _ in range(16)], "sld": [[0, 0] for _ in range(16)], "files": 0,
                 "notes": 0} for r in ROLES}
    for role in ROLES:
        folder = os.path.join(root, BUNDLE, PACKS[role])
        for name in sorted(os.listdir(folder)):
            if not name.lower().endswith(".mid"):
                continue
            ppq, notes = read_midi(os.path.join(folder, name))
            if not ppq or len(notes) < 4:
                continue
            tonic = minor_tonic(notes)
            line = top_line(ppq, notes)
            if not line:
                continue
            total = max(line) + 1
            bars = max(1, int(math.ceil(total / 16.0)))
            # The octave the line centres on is step 0 .. 6.
            mean = sum(p for p, _, _, _ in line.values()) / len(line)
            ref = int(round((mean - tonic) / 12.0 - 0.3))
            st = stats[role]
            st["files"] += 1
            seq = []
            vels = sorted(v for _, v, _, _ in line.values())
            med = vels[len(vels) // 2]
            for k in range(bars * 16):
                on = k in line
                prev = (k - 1) in line
                st["on"][k % 32 + (32 if prev else 0)][1 if on else 0] += 1
                if not on:
                    continue
                p, v, ln, slide = line[k]
                s = max(STEP_MIN, min(STEP_MAX, step_of(p, tonic, ref)))
                seq.append(s - STEP_MIN)
                st["len"][min(ln, 16)] += 1
                st["acc"][k % 16][1 if v > med + 10 else 0] += 1
                st["sld"][k % 16][1 if slide else 0] += 1
            st["notes"] += len(seq)
            for i, x in enumerate(seq):
                st["t0"][x] += 1
                if i >= 1:
                    st["t1"][(seq[i - 1], x)] += 1
                if i >= 2:
                    st["t2"][(seq[i - 2], seq[i - 1], x)] += 1
    return stats


def write_tables(stats, out):
    lines = ["/**", " * @file CorpusTables.cpp",
             " * @brief Counts of the local trance MIDI corpus (Tools/corpus/build_corpus.py, generated -- do not edit): per role",
             " *        the scale-step successions of order 0, 1 and 2, the onsets on two bars' sixteenths, the note lengths, the",
             " *        303's accents and slides. Statistics only; no loop can be read back from them.", " */",
             '#include "parh/compose/Corpus.h"', "", "namespace parh {", ""]
    for role in ROLES:
        st = stats[role]
        R = role.capitalize()
        lines.append(f"// {role}: {st['files']} files, {st['notes']} notes")
        lines.append(f"const uint16_t k{R}T0[kCorpusAlpha] = {{ " + ", ".join(str(min(65535, st['t0'][i])) for i in range(ALPHA)) + " };")
        rows = []
        for a in range(ALPHA):
            rows.append("    { " + ", ".join(str(min(65535, st['t1'][(a, b)])) for b in range(ALPHA)) + " },")
        lines.append(f"const uint16_t k{R}T1[kCorpusAlpha][kCorpusAlpha] = {{")
        lines += rows
        lines.append("};")
        # Order 2 as a sparse list: (a, b, c, count).
        t2 = sorted(st["t2"].items())
        lines.append(f"const CorpusTriple k{R}T2[] = {{")
        for (a, b, c), n in t2:
            lines.append(f"    {{ {a}, {b}, {c}, {min(65535, n)} }},")
        lines.append("};")
        lines.append(f"const int k{R}T2Count = {len(t2)};")
        lines.append(f"const uint16_t k{R}Onset[64][2] = {{ " + ", ".join("{ %d, %d }" % (min(65535, a), min(65535, b)) for a, b in st["on"]) + " };")
        lines.append(f"const uint16_t k{R}Length[17] = {{ " + ", ".join(str(min(65535, st['len'][i])) for i in range(17)) + " };")
        lines.append(f"const uint16_t k{R}Accent[16][2] = {{ " + ", ".join("{ %d, %d }" % tuple(x) for x in st["acc"]) + " };")
        lines.append(f"const uint16_t k{R}Slide[16][2] = {{ " + ", ".join("{ %d, %d }" % tuple(x) for x in st["sld"]) + " };")
        lines.append("")
    lines.append("const CorpusRole kCorpusRoles[kCorpusRoleCount] = {")
    for role in ROLES:
        R = role.capitalize()
        lines.append(f"    {{ k{R}T0, k{R}T1, k{R}T2, k{R}T2Count, k{R}Onset, k{R}Length, k{R}Accent, k{R}Slide }},")
    lines.append("};")
    lines.append("")
    lines.append("} // namespace parh")
    open(out, "w", newline="\n", encoding="utf-8").write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="M:/Midi")
    ap.add_argument("--out", default="Core/src/compose/CorpusTables.cpp")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    stats = analyse(a.root)
    for role in ROLES:
        st = stats[role]
        on = sum(b for _, b in st["on"]) / max(1, sum(a + b for a, b in st["on"]))
        print(f"{role:6s} {st['files']:4d} files {st['notes']:6d} notes  onset share {on:.2f}  "
              f"lengths {dict(sorted(st['len'].most_common(5)))}")
        if a.report:
            steps = sorted(st["t0"].items(), key=lambda kv: -kv[1])[:8]
            print("        most used steps (0 = tonic):", [(s + STEP_MIN, n) for s, n in steps])
    write_tables(stats, a.out)
    print("wrote", a.out)


if __name__ == "__main__":
    sys.exit(main())
