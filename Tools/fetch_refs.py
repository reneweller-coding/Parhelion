"""Fetches the reference recordings of Tools/ref_sets.txt (PLAN 13.4): the audio track only, with yt-dlp.

A row names a URL or a search. A search looks at the first eight uploads YouTube offers and takes the one whose length
fits the profile's mixes (an extended or original mix, not a radio edit, not an hour-long set) and whose title holds the
most words of the query; it prefers "original", "extended", "dream version", and avoids "radio", "remix" (unless the
query asks for it), "live", "hour", "cover". What it chose is written to Tools/ref_lock.txt, so the measurement can be
repeated on the same uploads.

The files go to a folder outside the repository (default %TEMP%/parhelion_refs, or --dir), named after the video id, and
stay there. Nothing of the audio enters the repository; the measurement (Tools/analyze_ref.py) keeps statistics only.

    python Tools/fetch_refs.py [--dir D:/somewhere] [--only uplifting]

@note After Totality `Tools/fetch_refs.py` at 4d3c0d2 (29.09.2026); the search is Parhelion's.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOCK = HERE / "ref_lock.txt"

# Seconds an upload of each profile may last: extended and original mixes (Dok. 6, table "Varianten pro Sub-Genre").
LENGTHS = {"uplifting": (300, 780), "progressive": (300, 900), "dream": (240, 660), "acid": (240, 780), "deep": (240, 780)}
AVOID = ("radio", "live", "hour", "cover", "piano version", "slowed", "reverb", "8d", "nightcore", "megamix", "mixed by",
         "tutorial", "remake", "sped up", "instrumental cover", "karaoke", "reaction")


def default_dir() -> Path:
    return Path(os.environ.get("TEMP", tempfile.gettempdir())) / "parhelion_refs"


def read_sets(path: Path) -> list[dict]:
    """The rows of ref_sets.txt, with the video ids of ref_lock.txt where a search was resolved."""
    lock = {}
    if LOCK.exists():
        for line in LOCK.read_text(encoding="utf-8").splitlines():
            parts = [p.strip() for p in line.split("|")]
            if len(parts) >= 2 and not line.startswith("#"):
                lock[parts[0]] = parts[1]
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 4:
            continue
        profile, name, tag, where = parts
        name = name.lstrip("+ ").strip()
        bpm, key = None, None
        if tag != "-":
            t = tag.split()
            bpm = float(t[0])
            key = t[1] if len(t) > 1 else None
        vid = lock.get(name)
        if vid is None and where.startswith("http"):
            vid = where.split("v=")[-1]
        rows.append({"profile": profile, "name": name, "bpm": bpm, "key": key, "where": where, "id": vid})
    return rows


def local_file(folder: Path, vid: str | None) -> Path | None:
    if vid is None:
        return None
    for p in folder.glob(vid + ".*"):
        if p.suffix not in (".part", ".ytdl", ".json"):
            return p
    return None


def resolve(row: dict) -> tuple[str, str, int] | None:
    """The upload a search row stands for: (id, title, seconds)."""
    query = row["where"][len("search:"):]
    out = subprocess.run(["yt-dlp", "--no-warnings", "--flat-playlist", "--print", "%(id)s\t%(duration)s\t%(title)s",
                          "ytsearch8:" + query], capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    lo, hi = LENGTHS.get(row["profile"], (240, 900))
    words = [w for w in re.findall(r"[a-z0-9]+", query.lower()) if len(w) > 2 and w not in ("original", "mix", "version")]
    best, best_score = None, -1e9
    for i, line in enumerate(out.splitlines()):
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        vid, dur, title = parts[0], parts[1], parts[2]
        try:
            sec = int(float(dur))
        except ValueError:
            continue
        t = title.lower()
        score = sum(1.0 for w in words if w in t) - 0.15 * i
        if not (lo <= sec <= hi):
            score -= 10.0
        if any(a in t for a in AVOID):
            score -= 5.0
        if "remix" in t and "remix" not in query.lower() and "mix)" not in query.lower():
            score -= 3.0
        for good in ("original", "extended", "dream version", "12\"", "12 inch", "full"):
            if good in t:
                score += 1.0
        if score > best_score:
            best, best_score = (vid, title, sec), score
    return best if best_score > -5.0 else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=Path, default=default_dir())
    ap.add_argument("--only", default="", help="a profile (uplifting, progressive, dream, acid, deep)")
    args = ap.parse_args()
    args.dir.mkdir(parents=True, exist_ok=True)
    rows = [r for r in read_sets(HERE / "ref_sets.txt") if not args.only or r["profile"] == args.only]
    lock_lines = LOCK.read_text(encoding="utf-8").splitlines() if LOCK.exists() else [
        "# The uploads fetch_refs.py chose for the searches of ref_sets.txt: name | video id | seconds | title"]
    failed = 0
    for r in rows:
        if local_file(args.dir, r["id"]) is not None:
            print(f"have  {r['name']}")
            continue
        if r["id"] is None:
            got = resolve(r)
            if got is None:
                print(f"  NOT FOUND {r['name']}", file=sys.stderr)
                failed += 1
                continue
            r["id"] = got[0]
            lock_lines.append(f"{r['name']} | {got[0]} | {got[2]} | {got[1]}")
            LOCK.write_text("\n".join(lock_lines) + "\n", encoding="utf-8", newline="\n")
            print(f"found {r['name']}: {got[1]} ({got[2] // 60}:{got[2] % 60:02d})", flush=True)
        url = "https://www.youtube.com/watch?v=" + r["id"]
        cmd = ["yt-dlp", "--no-warnings", "--no-playlist", "-f", "bestaudio", "-o", str(args.dir / "%(id)s.%(ext)s"), url]
        if subprocess.run(cmd, capture_output=True).returncode != 0 or local_file(args.dir, r["id"]) is None:
            print(f"  FAILED {url}", file=sys.stderr)
            failed += 1
    print(f"{len(rows) - failed} of {len(rows)} present in {args.dir}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
