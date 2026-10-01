"""The memorisation check of Parhelion's melodies (PLAN 6.6): no generated line may repeat a known track's.

The 5452 fan transcriptions of famous trance tracks in the VORTEX bundle are pitch-for-pitch copies of other people's
compositions. Parhelion learns nothing from them (its models count the royalty-free EMP packs only, build_corpus.py),
but a generator can still arrive at a known motif by chance or by rule. So every generated window of two bars is
compared against every window of the transcriptions:

  * **The window.** Two bars (32 sixteenths) of a monophonic line (the top voice of one track): the onsets relative to the
    window's start and each note's interval to the one before (the first note: 0). The comparison is therefore
    transposition-invariant and exact in rhythm. A window counts with at least five notes and three distinct pitches --
    fewer is a figure every genre shares (a held chord tone, an octave jump), not a melody.
  * **The corpus side** takes windows at every beat of every track of every transcription (a transcription may start
    on a pickup), the generated side at every bar.
  * **A hit** is an identical window. The generated lines are rendered by parh_render --midi over many seeds and styles;
    the gate is zero hits for the lead (and the piano, which plays the lead's part in Dream House). The arp and the
    pluck are reported against a reference -- the EMP packs' own lines measured the same way -- because a broken
    triad over two bars coincides with some transcription by construction and is nobody's melody.

``--emit`` writes the transcriptions' windows as a Bloom filter of their hashes into Core/src/compose/MemoFilter.cpp:
the engine tests every lead window it writes and draws again on a hit (Memo.h), so the gate holds for every seed,
not only for the ones rendered here. A Bloom filter of 64-bit hashes cannot be read back into notes.

    python Tools/corpus/memorisation.py --emit                   # build the filter
    python Tools/corpus/memorisation.py --check work/memo/*.mid   # count hits in rendered tracks
    python Tools/corpus/memorisation.py --reference              # the EMP packs against the transcriptions
"""
import argparse
import glob
import os
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_corpus as bc                                           # noqa: E402

FAMOUS = "5500 Trance Midi Famous Artist"
MIN_NOTES, MIN_PITCHES = 5, 3
BLOOM_BITS_PER_ITEM = 12
BLOOM_K = 8
FNV_OFFSET, FNV_PRIME, MASK = 0xCBF29CE484222325, 0x100000001B3, (1 << 64) - 1

PROBE = ((0, 0), (4, 2), (8, 1), (12, -3), (16, 5))     # the self-test's window (Memo.h)

# The generated MIDI's channels (Core/src/Midi.cpp): poly instances 3 .. 8 in PolyInstance order.
GEN_CHANNELS = {"lead": 3, "counter": 4, "pluck": 5, "arp": 6, "pad": 7, "stab": 8, "acid": 2, "bass": 1}


def read_tracks(path):
    """(ppq, [notes per track and channel]) with notes (start, end, pitch); drums excluded."""
    d = open(path, "rb").read()
    if d[:4] != b"MThd":
        return None, []
    _, ntrk, div = struct.unpack(">HHH", d[8:14])
    if div & 0x8000:
        return None, []
    i = 8 + struct.unpack(">I", d[4:8])[0]
    out = []
    for _ in range(ntrk):
        if d[i:i + 4] != b"MTrk":
            break
        ln = struct.unpack(">I", d[i + 4:i + 8])[0]
        j, end, t, run = i + 8, i + 8 + ln, 0, None
        open_notes, notes = {}, defaultdict(list)
        while j < end:
            dt, j = bc.read_vlq(d, j)
            t += dt
            st = d[j]
            if st == 0xFF:
                l, j2 = bc.read_vlq(d, j + 2)
                j = j2 + l
                continue
            if st in (0xF0, 0xF7):
                l, j2 = bc.read_vlq(d, j + 1)
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
                open_notes.setdefault((ch, a), []).append(t)
            elif hi == 0x80 or (hi == 0x90 and b == 0):
                if open_notes.get((ch, a)):
                    s = open_notes[(ch, a)].pop(0)
                    notes[ch].append((s, t, a))
        for ch in sorted(notes):
            out.append((ch, sorted(notes[ch])))
        i = end
    return div, out


def top_voice(ppq, notes):
    """{sixteenth: pitch} of the highest note starting on each sixteenth."""
    step = ppq / 4.0
    line = {}
    for s, e, p in notes:
        k = int(round(s / step))
        if p > line.get(k, -1):
            line[k] = p
    return line


def window_key(line, start):
    """The window of two bars from sixteenth ``start``: ((onset, interval), ...) or None if it is too thin."""
    ks = [k for k in range(start, start + 32) if k in line]
    if len(ks) < MIN_NOTES or len({line[k] for k in ks}) < MIN_PITCHES:
        return None
    key, prev = [], None
    for k in ks:
        key.append((k - start, 0 if prev is None else max(-127, min(127, line[k] - prev))))
        prev = line[k]
    return tuple(key)


def key_hash(key):
    """FNV-1a 64 over the bytes (onset, interval + 128) -- the same as MemoFilter.cpp."""
    h = FNV_OFFSET
    for on, iv in key:
        for byte in (on & 0xFF, (iv + 128) & 0xFF):
            h ^= byte
            h = (h * FNV_PRIME) & MASK
    return h


def windows(line, every):
    if not line:
        return
    last = max(line)
    for start in range(0, last + 1, every):
        k = window_key(line, start)
        if k is not None:
            yield start, k


def corpus_windows(root):
    """Every window of every track of every transcription (at every beat)."""
    folder = os.path.join(root, bc.BUNDLE, FAMOUS)
    keys = set()
    files = 0
    for dirpath, _, names in os.walk(folder):
        for name in names:
            if not name.lower().endswith(".mid"):
                continue
            try:
                ppq, tracks = read_tracks(os.path.join(dirpath, name))
            except Exception:
                continue
            if not ppq:
                continue
            files += 1
            for _, notes in tracks:
                for _, k in windows(top_voice(ppq, notes), 4):
                    keys.add(k)
    return files, keys


def emp_lines(root, pack):
    folder = os.path.join(root, bc.BUNDLE, pack)
    for name in sorted(os.listdir(folder)):
        if name.lower().endswith(".mid"):
            ppq, tracks = read_tracks(os.path.join(folder, name))
            if ppq:
                for _, notes in tracks:
                    yield name, top_voice(ppq, notes)


def emit(keys, out):
    n = len(keys)
    bits = max(1 << 16, n * BLOOM_BITS_PER_ITEM)
    bits = 1 << (bits - 1).bit_length()                  # a power of two: the index is a mask
    words = [0] * (bits // 64)
    for k in keys:
        h = key_hash(k)
        h1, h2 = h & 0xFFFFFFFF, (h >> 32) | 1
        for i in range(BLOOM_K):
            b = (h1 + i * h2) & (bits - 1)
            words[b >> 6] |= 1 << (b & 63)
    lines = ["/**", " * @file MemoFilter.cpp",
             " * @brief The Bloom filter of the transcriptions' two-bar windows (Tools/corpus/memorisation.py --emit,",
             " *        generated -- do not edit). Hashes only; no window can be read back from them.", " */",
             '#include "parh/compose/Memo.h"', "", "namespace parh {", "",
             f"/// {n} windows, {bits} bits, {BLOOM_K} hashes",
             f"const int kMemoBloomBits = {bits};", f"const int kMemoBloomK = {BLOOM_K};   ///< hashes per window",
             "const uint64_t kMemoProbeHash = 0x%016Xull;" % key_hash(PROBE),
             "/// The filter's bits, 64 a word.",
             f"const uint64_t kMemoBloom[{len(words)}] = {{"]
    for i in range(0, len(words), 4):
        lines.append("    " + ", ".join("0x%016Xull" % w for w in words[i:i + 4]) + ",")
    lines += ["};", "", "} // namespace parh", ""]
    open(out, "w", newline="\n", encoding="utf-8").write("\n".join(lines))
    print(f"wrote {out}: {n} windows, {bits // 8 // 1024} KiB")


def check(files, keys, every=16):
    hits = defaultdict(lambda: [0, 0])
    examples = []
    for f in files:
        ppq, tracks = read_tracks(f)
        if not ppq:
            continue
        for ch, notes in tracks:
            role = next((r for r, c in GEN_CHANNELS.items() if c == ch), None)
            if role is None:
                continue
            for start, k in windows(top_voice(ppq, notes), every):
                hits[role][1] += 1
                if k in keys:
                    hits[role][0] += 1
                    if len(examples) < 10:
                        examples.append((os.path.basename(f), role, start // 16))
    for role, (h, t) in sorted(hits.items()):
        print(f"{role:8s} {h:6d} hits in {t:7d} windows ({100.0 * h / max(1, t):.2f} %)")
    for e in examples:
        print("  hit:", *e)
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="M:/Midi")
    ap.add_argument("--emit", action="store_true")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "..", "..", "Core", "src", "compose", "MemoFilter.cpp"))
    ap.add_argument("--check", nargs="*")
    ap.add_argument("--reference", action="store_true")
    a = ap.parse_args()
    files, keys = corpus_windows(a.root)
    print(f"transcriptions: {files} files, {len(keys)} distinct windows")
    if a.emit:
        emit(keys, os.path.normpath(a.out))
    if a.reference:
        for role in ("lead", "piano"):
            h = t = 0
            for _, line in emp_lines(a.root, bc.PACKS[role]):
                for _, k in windows(line, 16):
                    t += 1
                    h += k in keys
            print(f"EMP {role:6s} {h:6d} hits in {t:7d} windows ({100.0 * h / max(1, t):.2f} %)")
    if a.check:
        paths = [p for g in a.check for p in glob.glob(g)]
        hits = check(paths, keys)
        if hits.get("lead", [0])[0] > 0:
            sys.exit(1)


if __name__ == "__main__":
    main()
