"""Parhelion's factory sets (17.10.2026): 256 DJ sets to begin a night with -> Core/src/FactorySetsData.inc.

Parhelion's sets either keep a style all night (set.journey Stay) or follow the arc along the style ladder Deep,
Progressive, Dream House, Uplifting, Acid (Wander) -- then the arc alone decides the styles: a warm-up from Deep to
Progressive, a closing from Uplifting back to Progressive, a sunrise from Progressive to Uplifting with the orchestra at
the end, a peak in Uplifting and Acid, a journey through all five. So the bank has

- for each of the five styles every arc, eight sets an arc, kept in that style (200), now and then leaning towards
  another style (compose.morph_to) -- and
- the sets across the styles, eleven an arc, and one journey of twelve hours (56).

Each set: its length from those its arc is played at, the blend (32 or 64 bars), a track's time (trance tracks are
extended mixes, five to eight minutes), the humanize, its seed; a name of its own (two words of light and sky, never
twice). The draws are seeded, so the bank is the same every time.

How long each set plays is measured by composing it, which the render app does for every row of its --catalog: build,
ask it, and run the script again with its answer -- the lengths go into the table, so the app and Heliacal list the bank
at once.

    python Tools/sets/make_sets.py
    bin/msvc/ParhelionSets.exe --catalog > work/sets_catalog.json
    python Tools/sets/make_sets.py --lengths work/sets_catalog.json
"""
import json
import pathlib
import random
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "Core" / "src" / "FactorySetsData.inc"

STYLES = ["Uplifting", "Progressive", "Dream House", "Acid", "Deep"]
ACROSS = "Across the styles"
# The arcs (DramaturgyNames) with the lengths they are played at (minutes), the share of 64-bar blends, a track's time.
ARCS = {
    "Warm-up": dict(lengths=[60, 90, 120], long_blend=0.6, track=(6.0, 8.0), across="from Deep to Progressive"),
    "Peak": dict(lengths=[60, 90, 120], long_blend=0.3, track=(5.0, 6.5), across="Uplifting and Acid"),
    "Closing": dict(lengths=[60, 90, 120], long_blend=0.5, track=(5.5, 7.5), across="from Uplifting to Progressive"),
    "Sunrise": dict(lengths=[90, 120, 180], long_blend=0.6, track=(6.0, 8.0), across="from Progressive to Uplifting"),
    "Journey": dict(lengths=[120, 180, 240], long_blend=0.5, track=(5.5, 7.5), across="through all five styles"),
}
KEEP_PER_ARC = 8
ACROSS_PER_ARC = 11

FIRST = ["Silver", "Golden", "Crystal", "Azure", "Solar", "Lunar", "Amber", "Radiant", "Emerald", "Celestial",
         "Velvet", "Distant", "Endless", "Ocean", "Midnight", "Morning", "Sapphire", "Opal", "Coral", "Ivory",
         "Northern", "Southern", "Eternal", "Shimmering", "Glass", "Cloud", "Star", "Sky", "Moon", "Ember", "Halcyon",
         "Bright", "Pale", "Rising", "Floating", "Weightless", "Infinite", "Polar", "Tidal", "Summer", "Winter",
         "Gleaming", "Luminous", "Open", "Higher", "Blue", "First", "Last"]
SECOND = ["Horizon", "Skies", "Tides", "Dreams", "Light", "Shores", "Hearts", "Waves", "Echoes", "Voyage", "Lagoon",
          "Haven", "Paradise", "Embrace", "Reverie", "Elysium", "Symphony", "Promise", "Hymn", "Bloom", "Daybreak",
          "Afterglow", "Sunrise", "Twilight", "Wings", "Odyssey", "Mirage", "Nebula", "Coast", "Island", "Garden",
          "Flight", "Spirit", "Memory", "Euphoria", "Serenade", "Rain", "Rapture", "Kingdom", "Cascade", "Harbour",
          "Beacon", "Path", "Season", "Glow", "Breeze", "Oasis", "Meridian"]


def length_text(minutes):
    """A set's length as people say it."""
    if minutes < 60 or minutes % 60:
        return f"{minutes} min"
    h = minutes // 60
    return "1 hour" if h == 1 else f"{h} hours"


def names(rng, count):
    """Two words each, never the same word twice in a name, never a name twice."""
    out, seen = [], set()
    while len(out) < count:
        a, b = rng.choice(FIRST), rng.choice(SECOND)
        if a == b or a + " " + b in seen:
            continue
        seen.add(a + " " + b)
        out.append(a + " " + b)
    return out


def sets():
    """The 256 sets: (name, section, arc, note, text)."""
    rng = random.Random(17102027)
    plan = []
    for style in STYLES:
        for arc, a in ARCS.items():
            for k in range(KEEP_PER_ARC):
                plan.append((style, arc, a["lengths"][k % 3], False))
    for arc, a in ARCS.items():
        for k in range(ACROSS_PER_ARC):
            plan.append((ACROSS, arc, a["lengths"][k % 3], True))
    plan.append((ACROSS, "Journey", 720, True))   # a journey of twelve hours
    out = []
    for (section, arc, minutes, wander), name in zip(plan, names(rng, len(plan))):
        a = ARCS[arc]
        style = section if not wander else rng.choice(STYLES)   # across the styles the arc decides; the knob stays
        knobs = [
            ("compose.style", style),
            ("set.dramaturgy", arc),
            ("set.journey", "Wander" if wander else "Stay"),
            ("set.minutes", f"{minutes}"),
            ("set.track_minutes", f"{rng.uniform(*a['track']):.2f}"),
            ("set.blend", "64 bars" if rng.random() < a["long_blend"] else "32 bars"),
            ("compose.humanize", f"{rng.uniform(4.0, 12.0):.1f}"),
        ]
        lean = ""
        if not wander and rng.random() < 0.3:
            other = rng.choice([s for s in STYLES if s != style])
            knobs.append(("compose.morph_to", other))
            knobs.append(("compose.morph", f"{rng.uniform(0.15, 0.45):.2f}"))
            lean = f", leaning towards {other}"
        seed = rng.randrange(1, 2 ** 31)
        text = ["# Parhelion set", f"seed={seed}", "minutes=0", f"set={minutes}"]
        text += [f"param {k}={v}" for k, v in knobs]
        note = length_text(minutes) + (f", {a['across']}" if wander else f", {style} all night{lean}")
        out.append((name, section, arc, note, "\n".join(text) + "\n"))
    return out


def c_string(s):
    """A C string literal, a line of the set to a line of the source."""
    lines = s.split("\n")
    parts = [line.replace("\\", "\\\\").replace('"', '\\"') + "\\n" for line in lines[:-1]]
    return "\n".join(f'      "{p}"' for p in parts)


def main():
    rows = sets()
    lengths = {}
    if "--lengths" in sys.argv:   # the render app's catalog: the factory sets' rows with their lengths
        catalog = json.load(open(sys.argv[sys.argv.index("--lengths") + 1], encoding="utf-8"))
        lengths = {i["name"]: i["length"] for i in catalog["items"] if i["kind"] == "Factory set"}
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// The factory's DJ sets (FactorySets.h), written by Tools/sets/make_sets.py -- not by hand.\n")
        for name, section, arc, note, text in rows:
            f.write(f'    {{ "{name}", "{section}", "{arc}", "{note}",\n{c_string(text)},\n'
                    f'      {lengths.get(name, 0.0):.1f} }},\n')
    measured = sum(1 for row in rows if row[0] in lengths)
    print(f"{len(rows)} sets, {measured} with their lengths -> {OUT}")


if __name__ == "__main__":
    main()
