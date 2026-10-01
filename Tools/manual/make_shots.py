"""Parhelion -- the manual's screenshots: every tab as a whole page, from the standalone, muted.

    python Tools/manual/make_shots.py [path\\to\\Parhelion.exe]   -> docs/screenshots/tab_NN.png

Each tab is its own run of the standalone in the screenshot mode: the seed 4242, the playhead at beat 760 (bar 191, in the main drop),
and PARH_SHOT_FULL, which grows the window until nothing of the page in front scrolls, so every knob is in the picture.
The runs are muted (PARH_SHOT forces it). Besides: docs/screenshots/set.png, the Arrange tab of a 40-minute set,
docs/screenshots/zoom.png, the Arrange tab zoomed in on a main drop (PARH_SHOT_ZOOM), and
docs/screenshot.png, the Arrange tab at the window's usual size, the picture on the project's front page. After
Totality's make_shots.py (itself after Ephemeris').
"""
import os
import subprocess
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
# (name, tab, small tab or None): the pages in the order of the tabs (01.10.2026: a tab of several modules a picture per
# small tab); chapters.txt numbers its pictures by this list.
PAGES = [("Set", 0, None), ("Arrange", 1, None),
         ("Kick", 2, 0), ("Sub", 2, 1), ("Bass", 2, 2), ("Acid", 2, 3), ("Pump", 2, 4), ("Drums", 3, None), ("Synths", 4, None),
         ("Piano", 5, 0), ("Cloud", 5, 1), ("Strings", 6, 0), ("Choir", 6, 1), ("Brass", 6, 2), ("Timpani", 6, 3),
         ("SFX", 7, 0), ("Sends", 7, 1), ("Console", 8, 0), ("Buses and Master", 8, 1), ("Decks", 8, 2),
         ("Perform", 9, None), ("Export", 10, None), ("Style", 11, None)]
ARRANGE = 1


def shot(exe, page, path, full, extra=None):
    name, tab, small = PAGES[page]
    env = dict(os.environ, PARH_MUTE="1", PARH_SEED="4242", PARH_PLAY="1", PARH_SHOT_AT="760", PARH_TAB=str(tab), PARH_SHOT=path)
    env.pop("PARH_SET", None)
    env.pop("PARH_SUBTAB", None)
    if small is not None:
        env["PARH_SUBTAB"] = str(small)
    if full:
        env["PARH_SHOT_FULL"] = "1"
        env["FAMILY_NO_SECTIONS"] = "1"   # every page whole in its picture, its sections at once (Frame.h, 01.10.2026)
    else:
        env.pop("PARH_SHOT_FULL", None)
        env.pop("FAMILY_NO_SECTIONS", None)
    env.update(extra or {})
    if os.path.exists(path):
        os.remove(path)
    subprocess.run([exe], env=env, timeout=600, check=False)
    if not os.path.exists(path):
        sys.exit("no picture of page %d (%s)" % (page, name))
    print("%-16s %s" % (name, os.path.relpath(path, ROOT)))


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "bin", "msvc", "Parhelion.exe")
    out = os.path.join(ROOT, "docs", "screenshots")
    os.makedirs(out, exist_ok=True)
    for page in range(len(PAGES)):
        shot(exe, page, os.path.join(out, "tab_%02d.png" % page), True)
    shot(exe, ARRANGE, os.path.join(out, "set.png"), False, {"PARH_SET": "40", "PARH_SEED": "5", "PARH_SHOT_AT": "1400"})
    shot(exe, ARRANGE, os.path.join(out, "zoom.png"), False, {"PARH_SHOT_ZOOM": "720:848"})   # (the main drop, zoomed: 01.10.2026)
    shot(exe, ARRANGE, os.path.join(ROOT, "docs", "screenshot.png"), False)


if __name__ == "__main__":
    main()
