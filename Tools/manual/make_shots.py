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
TABS = ["Set", "Arrange", "Low End", "Drums", "Synths", "Keys", "Orchestra", "Effects", "Mixer", "Perform", "Export", "Style"]


def shot(exe, tab, path, full, extra=None):
    env = dict(os.environ, PARH_MUTE="1", PARH_SEED="4242", PARH_PLAY="1", PARH_SHOT_AT="760", PARH_TAB=str(tab), PARH_SHOT=path)
    env.pop("PARH_SET", None)
    if full:
        env["PARH_SHOT_FULL"] = "1"
    else:
        env.pop("PARH_SHOT_FULL", None)
    env.update(extra or {})
    if os.path.exists(path):
        os.remove(path)
    subprocess.run([exe], env=env, timeout=600, check=False)
    if not os.path.exists(path):
        sys.exit("no picture of tab %d (%s)" % (tab, TABS[tab]))
    print("%-10s %s" % (TABS[tab], os.path.relpath(path, ROOT)))


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "Plugin", "Parhelion_artefacts", "Release", "Standalone", "Parhelion.exe")
    out = os.path.join(ROOT, "docs", "screenshots")
    os.makedirs(out, exist_ok=True)
    for tab in range(len(TABS)):
        shot(exe, tab, os.path.join(out, "tab_%02d.png" % tab), True)
    shot(exe, 1, os.path.join(out, "set.png"), False, {"PARH_SET": "40", "PARH_SEED": "5", "PARH_SHOT_AT": "1400"})
    shot(exe, 1, os.path.join(out, "zoom.png"), False, {"PARH_SHOT_ZOOM": "720:848"})   # (the main drop, zoomed: 01.10.2026)
    shot(exe, 1, os.path.join(ROOT, "docs", "screenshot.png"), False)


if __name__ == "__main__":
    main()
