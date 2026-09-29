"""Parhelion's factory banks (Phase 5b, PLAN 12): 16 groups x 64 presets for each of eighteen sound modules.

The user (29.09.2026): "Bitte achte auch wieder auf die Presets (wie bei den anderen Synth, möglichst 1024 pro Modul)
sowie genügend Modulationsmöglichkeiten in hinreichender Komplexität"; "Die Presets sollen beim Abspielen natürlich auch
entsprechend angezeigt werden." The scheme is the siblings' (Ephemeris, Totality, Phosphene):

- a bank has sixteen groups (its submenus), each an eight by eight grid;
- the rows are the bank's eight adjectives from dark to bright, the columns the group's eight nouns, so every name is
  two words, unique within its bank, and says where on the grid the sound lies;
- a group gives each knob it cares about a range and an axis: 'A' the row's (mostly the brightness), 'B' the column's
  (mostly the shape), 'R' a seeded draw, 'C' the constant lo; a range may be a list ("list:2,5,6"), one value drawn or
  stepped along the axis (a filter model, a family of risers); the rest keep their defaults;
- its modulation recipes each come with a chance, take the next free LFO ('@') and slot of the matrix ('#'); a source
  of -1 means "that LFO";
- its weights in the five styles (Uplifting, Progressive, Dream House, Acid, Deep) steer the composer's choice; a kit
  group names the lane roles it is made for.

This file is the source; `python Tools/presets/gen_bank.py` writes Core/src/PresetBankData.inl from it and checks the
counts and the names. The sounds follow PLAN 2.6 and 5 (the trance recipes) and Dok. 7's windows.
"""

# ---------------------------------------------------------------- the matrix's vocabulary (Params.cpp, Modulation.h)
LFO, MENV, FENV, VEL, KEY, RND, WHEEL, PRESS, ENERGY = -1, 5, 6, 7, 8, 9, 10, 11, 12       # sources
SINE, TRI, SAWUP, SAWDN, SQUARE, SH, SMOOTH = range(7)                                   # LFO shapes
FREE, BARS4, BARS2, BAR1, HALF, QUARTER, EIGHTH, SIXTEENTH, QT, ET = range(10)           # LFO syncs
# targets, by each engine's own list
P_PITCH, P_O2, P_PW, P_TP, P_FMI, P_CUT, P_RES, P_FMODE, P_LVL, P_PAN, P_DET = range(1, 12)          # poly
S_PITCH, S_PW, S_CUT, S_RES, S_ENV, S_DRIVE, S_LVL, S_PAN = range(1, 9)                            # bass, 303
PN_PITCH, PN_HARD, PN_LVL, PN_PAN = range(1, 5)                                                     # piano
ST_PITCH, ST_PRESS, ST_SPEED, ST_POS, ST_VIB, ST_VIBRATE, ST_LVL, ST_PAN = range(1, 9)            # strings
CH_PITCH, CH_VOWEL, CH_TENSION, CH_BREATH, CH_VIB, CH_FORMANT, CH_LVL, CH_PAN = range(1, 9)       # choir
BR_PITCH, BR_BREATH, BR_BLARE, BR_VIB, BR_LVL, BR_PAN = range(1, 7)                                # brass
TI_PITCH, TI_HARD, TI_STRIKE, TI_DECAY, TI_LVL, TI_PAN = range(1, 7)                               # timpani
# filter models: the mono synth's list (Filters.h), the poly voice's (0 its own state-variable filter, then the same + 1)
M_MOOG, M_PROPHET, M_JUNO, M_SEM, M_XPANDER, M_DIODE, M_KORG, M_POLIVOKS, M_WASP = range(9)
PF_OWN, PF_MOOG, PF_PROPHET, PF_JUNO, PF_SEM, PF_XPANDER, PF_DIODE, PF_KORG, PF_POLIVOKS, PF_WASP = range(10)
SUPERSAW, VA, FM, WT = range(4)                                                           # poly.osc
T_CLASSIC, T_VOCAL, T_GLASS, T_PWM, T_SYNC, T_FSAW = range(6)                             # built-in tables
# kit roles (PercRole)
CH, RH, OH, RIDE, CLAP, CLAPG, SNARE, RIM, SHAKER, TOM, CONGA, NOISE, CRASH, TAMB = range(14)


def L(*values):
    """A list to draw from (or to step through along the axis)."""
    return "list:" + ",".join(str(v) for v in values)


def lfo(chance, dst, lo, hi, shape=SINE, sync=FREE, rate=(0.2, 1.0), retrig=0, fade=None, src=LFO):
    """A recipe: one LFO (or another source) on one target, the amount drawn from [lo, hi]."""
    knobs = [("mx#_src", src, src, "C"), ("mx#_dst", dst, dst, "C"), ("mx#_amount", lo, hi, "R")]
    if src == LFO:
        knobs += [("lfo@_shape", shape, shape, "C"), ("lfo@_sync", sync, sync, "C")]
        if sync == FREE:
            knobs.append(("lfo@_rate", rate[0], rate[1], "R"))
        if retrig:
            knobs.append(("lfo@_retrig", 1, 1, "C"))
        if fade:
            knobs.append(("lfo@_fade", fade[0], fade[1], "R"))
    return (chance, knobs)


def menv(chance, dst, lo, hi, decay=(100, 600), attack=(0.1, 5), sustain=0.0):
    """A recipe: the modulation envelope on one target."""
    return (chance, [("menv_attack", attack[0], attack[1], "R"), ("menv_decay", decay[0], decay[1], "B"),
                     ("menv_sustain", sustain, sustain, "C"),
                     ("mx#_src", MENV, MENV, "C"), ("mx#_dst", dst, dst, "C"), ("mx#_amount", lo, hi, "R")])


def src(chance, source, dst, lo, hi):
    """A recipe: a fixed source (velocity, key, random, the wheel, the pressure, the energy) on one target."""
    return (chance, [("mx#_src", source, source, "C"), ("mx#_dst", dst, dst, "C"), ("mx#_amount", lo, hi, "R")])


def G(name, style, nouns, knobs, mods=(), roles=None):
    """A group: its name, its weights in the five styles, its eight nouns ("a, b, ..."), its knobs, its recipes."""
    return dict(name=name, style=style, nouns=[n.strip() for n in nouns.split(",")], knobs=list(knobs),
                mods=list(mods), roles=roles)


# Style weights (Uplifting, Progressive, Dream House, Acid, Deep).
UP, PROG, DREAM, ACID, DEEP = (1, .3, .3, .1, .1), (.4, 1, .4, .2, .5), (.5, .5, 1, .1, .5), (.1, .3, .1, 1, .2), (.2, .5, .5, .1, 1)
ALL = (.6, .6, .6, .6, .6)   # (the all-rounders: at home everywhere, first nowhere -- the choice cubes the weights)
CINE = (1, .3, .6, .05, .4)

# ================================================================= the kick
KICK_ADJ = ["Muffled", "Padded", "Round", "Solid", "Punchy", "Tight", "Crisp", "Cutting"]
def kk(engine, end, start, pdecay, adecay, drive, click, extra=()):
    return [("engine", engine, engine, "C"), ("pitch_end", end[0], end[1], "R"), ("pitch_start", start[0], start[1], "R"),
            ("pitch_decay", pdecay[0], pdecay[1], "B"), ("amp_decay", adecay[0], adecay[1], "B"), ("drive", drive[0], drive[1], "R"),
            ("tone", 2500, 12000, "A"), ("click_level", click[0], click[1], "A"), ("click_tone", 2000, 7000, "A")] + list(extra)
KICK = dict(label="kick", module="Kick", instance=0, adj=KICK_ADJ, groups=[
    G("Anthem 909", UP, "Beacon, Summit, Horizon, Skyline, Crest, Pinnacle, Rampart, Citadel",
      kk(2, (48, 56), (240, 380), (14, 22), (320, 460), (.25, .45), (.2, .5), [("punch", .4, .65, "R"), ("top_level", -18, -10, "R")])),
    G("Rolling Progressive", PROG, "Current, Tide, Stream, Drift, Swell, Undertow, Channel, Estuary",
      kk(2, (46, 54), (200, 320), (16, 24), (340, 480), (.2, .35), (.1, .3), [("punch", .3, .5, "R"), ("dip", -5, -2, "R")])),
    G("Dream Soft", DREAM, "Pillow, Cushion, Feather, Down, Velvet, Cotton, Fleece, Satin",
      kk(0, (44, 52), (150, 260), (18, 26), (300, 420), (.1, .25), (.0, .15), [("top_level", -36, -22, "R"), ("punch", .2, .35, "R")])),
    G("Acid Warehouse", ACID, "Hangar, Depot, Loading Bay, Freight, Gantry, Crane, Silo, Boiler",
      kk(2, (48, 58), (260, 420), (12, 18), (260, 380), (.45, .75), (.3, .6), [("top_drive", .5, .85, "R"), ("clip", 0, 1, "R")])),
    G("Deep Dub", DEEP, "Lagoon, Harbour, Reef, Mooring, Jetty, Shoal, Inlet, Cove",
      kk(0, (42, 50), (140, 240), (20, 28), (380, 560), (.1, .25), (.0, .15), [("top_level", -40, -26, "R")])),
    G("Hard Trance", (.6, .1, .05, .7, .05), "Hammer, Anvil, Forge, Ram, Piston, Press, Stamp, Rivet",
      kk(2, (50, 60), (300, 600), (10, 16), (240, 340), (.55, .85), (.35, .65), [("clip", 1, 1, "C"), ("top_drive", .6, .9, "R")])),
    G("Tech Trance", (.5, .6, .1, .6, .2), "Circuit, Relay, Switch, Diode, Pulse, Signal, Clock, Latch",
      kk(2, (48, 56), (220, 360), (12, 18), (260, 360), (.35, .55), (.25, .5), [("punch", .5, .7, "R")])),
    G("Classic Resonator", (.6, .6, .3, .3, .3), "Bell Jar, Kettle, Cistern, Barrel, Drum Shell, Vat, Tank, Urn",
      kk(1, (46, 56), (200, 340), (14, 20), (300, 440), (.25, .45), (.15, .4))),
    G("Big Room Thump", (.7, .3, .1, .3, .1), "Arena, Stadium, Dome, Amphitheatre, Coliseum, Bowl, Pavilion, Forum",
      kk(2, (46, 54), (220, 340), (16, 24), (380, 520), (.35, .55), (.2, .45), [("top_level", -14, -8, "R"), ("punch", .5, .75, "R")])),
    G("Balearic Warm", (.4, .6, .8, .1, .6), "Terrace, Veranda, Cabana, Grotto Beach, Sunset, Ibiza Bay, Siesta, Horizon Line",
      kk(0, (44, 52), (160, 280), (18, 26), (320, 460), (.15, .3), (.05, .2))),
    G("Tribal Toms", (.3, .8, .3, .3, .6), "Ritual, Totem, Canyon, Mesa, Butte, Arroyo, Sierra, Pueblo",
      kk(1, (48, 58), (220, 360), (18, 28), (260, 380), (.2, .4), (.1, .3), [("top_level", -20, -12, "R")])),
    G("Short Club", ALL, "Tick, Knock, Tap, Rap, Snap, Clack, Dab, Nudge",
      kk(2, (50, 58), (240, 380), (12, 16), (200, 300), (.25, .45), (.25, .5))),
    G("Sub Heavy", (.5, .5, .4, .3, .8), "Bedrock, Mantle, Basalt, Magma, Strata, Core, Bedstone, Deepwell",
      kk(0, (40, 48), (140, 240), (20, 30), (420, 620), (.15, .3), (.05, .2), [("dip", -8, -3, "B")])),
    G("Punchy Top", (.8, .6, .2, .5, .2), "Needle, Pin, Spike, Barb, Stylus, Tack, Awl, Nail",
      kk(2, (48, 56), (260, 420), (12, 18), (280, 400), (.3, .5), (.4, .7), [("click_decay", 3, 10, "B"), ("top_level", -14, -8, "R")])),
    G("Tape Vintage", (.5, .7, .6, .3, .6), "Reel, Spool, Cassette, Capstan, Tapehead, Splice, Leader, Hiss",
      kk(0, (44, 54), (180, 300), (16, 24), (300, 440), (.3, .55), (.1, .3), [("tone", 1500, 6000, "A")])),
    G("Cinematic Boom", (.5, .2, .4, .05, .4), "Titan, Colossus, Monolith, Behemoth, Leviathan, Goliath, Atlas, Juggernaut",
      kk(1, (40, 50), (160, 300), (22, 34), (500, 800), (.3, .5), (.1, .3), [("top_level", -20, -12, "R")])),
])

# ================================================================= the sub
SUB_ADJ = ["Buried", "Dark", "Deep", "Warm", "Full", "Firm", "Present", "Bright"]
def sb(att, dec, sus, rel, lp, drive, octave=(0, 0)):
    return [("attack", att[0], att[1], "R"), ("decay", dec[0], dec[1], "B"), ("sustain", sus[0], sus[1], "R"),
            ("release", rel[0], rel[1], "B"), ("low_pass", lp[0], lp[1], "A"), ("drive", drive[0], drive[1], "A"),
            ("octave", octave[0], octave[1], "C")]
SUB = dict(label="sub", module="Sub", instance=0, adj=SUB_ADJ, groups=[
    G("Pure Offbeat", UP, "Pulse, Beat, Throb, Surge, Heave, Push, Bounce, Lift", sb((.5, 2), (50, 110), (0, .1), (30, 60), (80, 160), (0, .15))),
    G("Rolling Round", PROG, "Wheel, Barrel, Drum, Roller, Spindle, Axle, Hub, Rim", sb((.5, 3), (80, 160), (.2, .5), (40, 90), (70, 140), (0, .2))),
    G("Long Hold", (.4, .6, .6, .3, .8), "Anchor, Keel, Ballast, Plinth, Pillar, Footing, Bedrock, Mooring", sb((1, 6), (200, 600), (.6, .9), (80, 200), (60, 120), (0, .1))),
    G("Tight Pluck", (.7, .5, .3, .6, .2), "Pick, Flick, Plink, Pluck, Twang, Pinch, Snip, Clip", sb((.5, 1.5), (30, 70), (0, 0), (20, 40), (90, 180), (0, .2))),
    G("Driven Warm", (.6, .5, .3, .7, .3), "Ember, Coal, Kiln, Hearth, Furnace, Brazier, Cinder, Glow", sb((.5, 2), (60, 140), (.1, .4), (30, 80), (100, 220), (.3, .7))),
    G("Soft Swell", DREAM, "Breath, Sigh, Tide Pool, Wave, Billow, Ripple, Murmur, Hush", sb((6, 25), (120, 300), (.4, .8), (60, 180), (60, 120), (0, .1))),
    G("Octave Up", (.6, .6, .4, .4, .3), "Step, Rung, Stair, Ladder, Ascent, Climb, Riser, Landing", sb((.5, 2), (60, 140), (.1, .4), (30, 80), (140, 260), (0, .2), (1, 1))),
    G("Sub Octave", (.3, .5, .4, .2, .9), "Cavern, Grotto, Abyss, Trench, Chasm, Pit, Hollow, Vault", sb((1, 4), (100, 300), (.3, .7), (60, 150), (40, 90), (0, .1), (-1, -1))),
    G("Punch Short", (.8, .6, .3, .5, .2), "Jab, Punch, Strike, Hit, Thrust, Blow, Knock, Bump", sb((.5, 1), (40, 80), (0, .05), (20, 40), (100, 200), (.1, .35))),
    G("Deep Drone", DEEP, "Hum, Drone, Rumble, Murmur Low, Groan, Thrum, Purr, Growl", sb((4, 20), (400, 1200), (.8, 1), (150, 400), (50, 100), (0, .15))),
    G("Saturated", (.5, .4, .2, .8, .2), "Grit, Sand, Gravel, Rasp, File, Burr, Scrape, Grind", sb((.5, 2), (60, 120), (.2, .5), (30, 70), (120, 260), (.5, .9))),
    G("Rounded Classic", ALL, "Globe, Orb, Sphere, Pebble, Marble, Bead, Pearl, Button", sb((1, 3), (60, 120), (.1, .3), (30, 70), (90, 150), (.05, .2))),
    G("Glide Soft", (.3, .6, .7, .2, .7), "Slide, Glide, Skate, Sail, Coast, Float, Waft, Drift Low", sb((3, 12), (100, 250), (.4, .7), (60, 150), (70, 130), (0, .1))),
    G("Gated Stab", (.7, .5, .2, .5, .2), "Gate, Shutter, Valve, Latch Low, Sluice, Hatch, Door, Lock", sb((.5, 1), (30, 60), (0, 0), (15, 30), (100, 200), (.1, .3))),
    G("Warm Bloom", (.5, .6, .8, .1, .6), "Bloom, Petal, Blossom, Bud, Pollen, Nectar, Stem, Leaf", sb((2, 8), (150, 400), (.5, .8), (80, 200), (80, 150), (.05, .2))),
    G("Cinematic Low", CINE, "Tremor, Quake, Rift, Fault, Faultline, Aftershock, Epicentre, Seism", sb((4, 20), (300, 900), (.6, .9), (200, 600), (50, 100), (.1, .3), (-1, 0))),
])

# ================================================================= a lane of the kit
PERC_ADJ = ["Dusty", "Muted", "Soft", "Warm", "Clean", "Crisp", "Sharp", "Sparkling"]
def pc(engine, pitch, decay, cutoff, flt, extra=()):
    return [("engine", engine, engine, "C"), ("pitch", pitch[0], pitch[1], "R"), ("decay", decay[0], decay[1], "B"),
            ("cutoff", cutoff[0], cutoff[1], "A"), ("filter", flt, flt, "C")] + list(extra)
PERC = dict(label="perc", module="Perc", instance=0, adj=PERC_ADJ, groups=[
    G("Tight Hats", ALL, "Tick, Tock, Chip, Flick, Spark, Glint, Blip, Dot",
      pc(1, (6000, 10000), (20, 60), (7000, 16000), 2, [("noise", .2, .5, "R"), ("noise_decay", 15, 50, "B"), ("resonance", .1, .3, "R")]),
      [src(.4, VEL, 0, 0, 0)], roles=[CH, RH]),
    G("Silky Hats", (.7, 1, .8, .5, .8), "Silk, Thread, Filament, Fibre, Strand, Wisp, Hair, Lace",
      pc(0, (8000, 12000), (30, 90), (8000, 18000), 2, [("noise", .6, 1, "R"), ("noise_decay", 30, 80, "B"), ("noise_type", 0, 0, "C")]), roles=[CH, RH]),
    G("909 Open", UP, "Hiss, Sizzle, Fizz, Crackle, Spritz, Sprinkle, Spray, Foam",
      pc(1, (5000, 9000), (180, 420), (6000, 15000), 2, [("noise", .4, .7, "R"), ("noise_decay", 150, 400, "B"), ("noise_type", 1, 1, "C")]), roles=[OH]),
    G("Airy Open", (.6, .8, .9, .3, .8), "Breeze, Gust, Draught, Zephyr, Air, Waft, Current Air, Updraft",
      pc(0, (7000, 11000), (220, 600), (7000, 18000), 2, [("noise", .8, 1, "R"), ("noise_decay", 200, 600, "B")]), roles=[OH]),
    G("Bell Ride", (.8, .7, .5, .6, .4), "Bell, Chime, Gong, Carillon, Tocsin, Knell, Peal, Clang",
      pc(1, (3000, 6000), (400, 1200), (6000, 16000), 1, [("metal_scale", .8, 1.5, "R"), ("noise", .2, .4, "R")]), roles=[RIDE]),
    G("Wash Ride", (.8, .6, .6, .3, .6), "Wash, Surf, Spume, Spindrift, Breaker, Roller Wave, Comber, Whitecap",
      pc(1, (4000, 8000), (600, 1800), (7000, 18000), 2, [("noise", .5, .8, "R"), ("noise_decay", 500, 1500, "B")]), roles=[RIDE, CRASH]),
    G("Anthem Clap", UP, "Applause, Ovation, Cheer, Salute, Bravo, Encore, Hail, Huzzah",
      pc(0, (1000, 2000), (150, 300), (2000, 9000), 1, [("bursts", 3, 4, "R"), ("burst_spacing", 8, 14, "R"), ("noise", 1, 1, "C"), ("noise_decay", 150, 300, "B")]),
      roles=[CLAP, CLAPG]),
    G("Room Clap", (.6, .8, .6, .4, .6), "Hall Clap, Studio, Booth, Chamber, Parlour, Salon, Loft, Attic",
      pc(0, (900, 1800), (250, 500), (1800, 7000), 1, [("bursts", 2, 4, "R"), ("burst_spacing", 10, 20, "R"), ("noise", 1, 1, "C"), ("noise_decay", 250, 500, "B")]),
      roles=[CLAP, CLAPG]),
    G("Roll Snare", (1, .6, .4, .5, .3), "Rattle, Roll, Rumble Snare, Clatter, Patter, Drumroll, Flurry, Barrage",
      pc(2, (180, 260), (120, 260), (3000, 12000), 0, [("noise", .5, .8, "R"), ("noise_decay", 100, 250, "B"), ("mode_set", 0, 0, "C")]), roles=[SNARE]),
    G("Crisp Snare", (.8, .8, .4, .6, .4), "Crack, Snap Snare, Whip, Lash, Slap, Smack, Clack Snare, Rap Snare",
      pc(2, (200, 320), (80, 180), (4000, 16000), 0, [("noise", .4, .7, "R"), ("noise_decay", 80, 180, "B"), ("drive", .1, .4, "R")]), roles=[SNARE, RIM]),
    G("Shaker Loop", PROG, "Seed, Grain, Sand Shaker, Rice, Pebble Shaker, Maraca, Rattlepod, Gourd",
      pc(0, (6000, 10000), (40, 100), (5000, 14000), 1, [("noise", 1, 1, "C"), ("noise_decay", 40, 100, "B")]), roles=[SHAKER, TAMB]),
    G("Tambourine Jingle", (.6, .7, .7, .3, .5), "Jingle, Zill, Sleigh, Tinkle, Jangle, Clink, Ring, Chink",
      pc(1, (5000, 9000), (120, 300), (6000, 16000), 1, [("noise", .3, .6, "R"), ("metal_scale", 1.2, 2, "R")]), roles=[TAMB, SHAKER]),
    G("Congas", (.4, .9, .5, .3, .8), "Tumba, Quinto, Segundo, Bongo, Djembe, Ashiko, Cajon, Udu",
      pc(2, (200, 400), (120, 300), (2000, 8000), 0, [("mode_set", 0, 0, "C"), ("mode_damp", .3, .7, "B"), ("tune", 1, 1, "C")]), roles=[CONGA, TOM]),
    G("Toms", (.8, .6, .4, .4, .5), "Floor Tom, Rack Tom, Tom Roll, Timbale, Surdo, Taiko, Tabla, Frame",
      pc(3, (80, 200), (200, 500), (1500, 6000), 0, [("pitch_amount", 1.5, 3, "R"), ("pitch_decay", 30, 120, "B"), ("tune", 1, 1, "C")]), roles=[TOM, CONGA]),
    G("Crash Cymbal", (1, .5, .5, .3, .4), "Crash, Splash, China, Burst, Blast, Shatter, Cascade, Deluge",
      pc(1, (3500, 7000), (800, 2500), (6000, 18000), 2, [("noise", .6, .9, "R"), ("noise_decay", 800, 2500, "B"), ("noise_type", 1, 1, "C")]), roles=[CRASH, RIDE]),
    G("Noise Sweep", (.7, .6, .5, .6, .6), "Static, Snow, Whitenoise, Pinknoise, Rustle, Hiss Sweep, Radio, Ether",
      pc(0, (2000, 8000), (300, 1500), (2000, 16000), 1, [("noise", 1, 1, "C"), ("noise_decay", 300, 1500, "B"), ("resonance", .2, .6, "R")]),
      [lfo(.7, 0, 0, 0)], roles=[NOISE]),
])
# (The kit's matrix: none -- the lanes carry their patterns' own movement. Recipes naming target 0 are skipped.)
for g in PERC["groups"]:
    g["mods"] = []

# ================================================================= the bass and the 303 (the mono synth)
BASS_ADJ = ["Sub", "Dark", "Round", "Warm", "Punchy", "Tight", "Bright", "Biting"]
def ms(wave, cut, res, env, dec, flt, drive=(.1, .35), amp=((.5, 2), (80, 300), (0, .4), (20, 80)), extra=()):
    a, d, s, r = amp
    return [("wave", wave[0], wave[1], "R"), ("cutoff", cut[0], cut[1], "A"), ("resonance", res[0], res[1], "R"),
            ("env_amount", env[0], env[1], "B"), ("decay", dec[0], dec[1], "B"), ("filter", flt, None, "R"),
            ("drive", drive[0], drive[1], "R"), ("amp_attack", a[0], a[1], "R"), ("amp_decay", d[0], d[1], "B"),
            ("amp_sustain", s[0], s[1], "R"), ("amp_release", r[0], r[1], "R")] + list(extra)
BASS = dict(label="bass", module="Bass", instance=0, adj=BASS_ADJ, groups=[
    G("Offbeat Saw", UP, "Arrow, Dart, Bolt, Quarrel, Javelin, Lance, Spear, Pike",
      ms((0, .15), (250, 900), (.1, .3), (1.5, 3), (60, 160), L(M_JUNO, M_MOOG, M_PROPHET)),
      [src(.5, VEL, S_CUT, .1, .25), lfo(.3, S_CUT, .03, .08, sync=BARS4)]),
    G("Rolling Juno", PROG, "Rolling, Tumble, Spin, Whirl, Twirl, Swirl, Gyre, Vortex",
      ms((0, .3), (300, 1000), (.15, .35), (1.5, 3.5), (50, 130), L(M_JUNO)), [lfo(.5, S_CUT, .04, .1, sync=BAR1), src(.4, VEL, S_CUT, .1, .2)]),
    G("Gallop Pluck", (.9, .6, .4, .3, .2), "Stallion, Mare, Colt, Charger, Steed, Mustang, Courser, Bronco",
      ms((0, .2), (300, 1100), (.15, .4), (2, 4), (40, 110), L(M_PROPHET, M_JUNO, M_MOOG)), [src(.6, VEL, S_CUT, .15, .3)]),
    G("Round Pulse", (.6, .7, .7, .2, .6), "Orb Bass, Globe Bass, Moon, Planet, Comet, Nebula, Pulsar, Quasar",
      ms((.8, 1), (200, 700), (.05, .2), (1, 2.5), (80, 220), L(M_SEM, M_JUNO), extra=[("pulse_width", .3, .5, "B")]),
      [lfo(.5, S_PW, .1, .3, shape=TRI, rate=(.2, 1.5))]),
    G("Moog Growl", (.5, .6, .2, .6, .3), "Bear, Wolf, Lynx, Puma, Jaguar, Panther, Cougar, Ocelot",
      ms((0, .3), (180, 700), (.3, .6), (2, 4), (70, 200), L(M_MOOG), drive=(.35, .7)), [lfo(.5, S_CUT, .05, .15, sync=EIGHTH), src(.4, VEL, S_DRIVE, .1, .3)]),
    G("Warm Progressive", PROG, "Amber, Honey, Caramel, Toffee, Maple, Cider, Copper, Bronze",
      ms((0, .4), (220, 800), (.1, .3), (1, 2.5), (100, 260), L(M_JUNO, M_SEM)), [lfo(.6, S_CUT, .04, .1, sync=BARS2, shape=TRI), src(.4, ENERGY, S_CUT, .1, .3)]),
    G("Dream Soft", DREAM, "Cloud Bass, Mist Bass, Haze, Vapour, Dew, Fogbank, Cirrus, Nimbus",
      ms((0, .5), (180, 600), (.05, .2), (.5, 1.5), (120, 300), L(M_SEM, M_JUNO), drive=(0, .15),
         amp=((2, 10), (200, 600), (.4, .8), (80, 200)))),
    G("Deep Dub Bass", DEEP, "Dubplate, Echo Bass, Skank, Riddim, Version, Bassline, Rockers, Steppers",
      ms((0, .6), (150, 500), (.1, .3), (.5, 2), (150, 400), L(M_MOOG, M_SEM), drive=(.05, .2),
         amp=((1, 5), (200, 700), (.5, .9), (60, 200))), [lfo(.6, S_CUT, .05, .15, sync=BARS2, shape=SMOOTH)]),
    G("Driven Tech", (.5, .6, .1, .7, .2), "Torque, Gear, Cog, Sprocket, Crank, Ratchet, Pinion, Flywheel",
      ms((0, .3), (250, 900), (.2, .45), (2, 4), (50, 140), L(M_KORG, M_PROPHET, M_MOOG), drive=(.45, .8)), [menv(.5, S_PITCH, .05, .12, decay=(20, 80))]),
    G("Acid Bassline", ACID, "Squelch, Gurgle, Burble, Bubble, Slosh, Splash, Swash, Gush",
      ms((0, .2), (200, 800), (.45, .75), (2.5, 4.5), (60, 200), L(M_DIODE)), [lfo(.6, S_CUT, .05, .2, shape=SH, sync=SIXTEENTH), src(.5, VEL, S_RES, .1, .2)]),
    G("SEM Bounce", (.6, .7, .5, .4, .4), "Trampoline, Spring, Coil, Rebound, Hop, Skip, Leap, Vault Bass",
      ms((.2, .7), (300, 1200), (.2, .45), (1.5, 3.5), (40, 120), L(M_SEM)), [menv(.6, S_CUT, .1, .3, decay=(40, 150))]),
    G("Polivoks Bite", (.4, .5, .1, .8, .2), "Fang, Tusk, Molar, Canine, Incisor, Talon, Claw, Beak",
      ms((0, .4), (200, 800), (.4, .7), (2, 4.5), (50, 150), L(M_POLIVOKS), drive=(.3, .6)), [lfo(.5, S_CUT, .05, .15, sync=EIGHTH, shape=SQUARE)]),
    G("Wasp Buzz", (.3, .5, .1, .8, .3), "Hornet, Wasp, Bee, Gnat, Midge, Cicada, Locust, Cricket",
      ms((.3, .8), (300, 1000), (.3, .6), (1.5, 3.5), (60, 160), L(M_WASP), drive=(.3, .6)), [lfo(.6, S_PW, .1, .3, sync=QUARTER)]),
    G("Clean Sub-Saw", ALL, "Plain, Prairie, Steppe, Savanna, Tundra, Plateau, Meadow, Heath",
      ms((0, .1), (300, 900), (0, .15), (.5, 1.5), (100, 250), L(M_JUNO, M_SEM), drive=(0, .1), extra=[("sub_osc", .3, .7, "R")])),
    G("Reso Pluck", (.8, .8, .4, .5, .3), "Pluck Bass, Pick Bass, Fret, Nut, Bridge Bass, Saddle, Tuner, Peg",
      ms((0, .3), (200, 700), (.4, .65), (2.5, 4.5), (30, 90), L(M_PROPHET, M_MOOG, M_XPANDER))),
    G("Cinematic Pulse", CINE, "Heartbeat, Sentinel, Warden, Guardian, Vigil, Watch, Patrol, Herald",
      ms((0, .5), (200, 700), (.1, .3), (1, 2.5), (100, 300), L(M_MOOG, M_JUNO), amp=((1, 4), (200, 500), (.4, .7), (80, 200))),
      [src(.6, ENERGY, S_CUT, .15, .35), lfo(.4, S_LVL, -.2, -.1, sync=SIXTEENTH, shape=SQUARE)]),
])
ACID_ADJ = ["Murky", "Dark", "Rubbery", "Liquid", "Squelchy", "Acidic", "Screaming", "Searing"]
ACID_BANK = dict(label="acid", module="Acid", instance=0, adj=ACID_ADJ, groups=[
    G("Classic 303", ACID, "Silver Box, Bassline, Transistor, Sequencer, Step, Pattern, Slide, Accent",
      ms((0, 0), (200, 1200), (.55, .8), (2.5, 4.5), (80, 300), L(M_DIODE), extra=[("accent", .3, .6, "B")]),
      [src(.5, VEL, S_CUT, .1, .25)]),
    G("Square Acid", ACID, "Checker, Tile, Grid Acid, Block, Cube, Pixel, Mosaic, Lattice",
      ms((1, 1), (200, 1000), (.55, .8), (2.5, 4.5), (80, 300), L(M_DIODE), extra=[("accent", .3, .6, "B")])),
    G("Squelch Machine", ACID, "Swamp, Bog, Marsh, Fen, Mire, Quagmire, Slough, Morass",
      ms((0, .3), (150, 900), (.65, .9), (3, 5.5), (60, 250), L(M_DIODE, M_KORG)), [lfo(.7, S_CUT, .05, .2, shape=SH, sync=SIXTEENTH)]),
    G("Rubber Band", (.2, .5, .1, .9, .2), "Elastic, Bungee, Rubber, Latex, Gum, Putty, Slingshot, Catapult",
      ms((0, .4), (250, 900), (.45, .7), (2, 4), (40, 150), L(M_MOOG, M_DIODE)), [menv(.6, S_PITCH, -.08, -.03, decay=(30, 100))]),
    G("Screamer", (.3, .2, 0, 1, .1), "Siren, Klaxon, Banshee, Howl, Wail, Shriek, Scream, Yowl",
      ms((0, .3), (400, 2000), (.75, .95), (3, 5.5), (100, 350), L(M_DIODE, M_KORG, M_POLIVOKS), drive=(.4, .8)),
      [lfo(.6, S_CUT, .08, .2, sync=QUARTER, shape=TRI)]),
    G("Liquid Acid", (.3, .7, .3, .7, .5), "River, Brook, Creek, Rill, Spring Water, Fountain, Cascade Water, Rapids",
      ms((0, .3), (250, 1000), (.5, .7), (2, 4), (120, 400), L(M_DIODE, M_SEM)), [lfo(.8, S_CUT, .05, .15, sync=BARS2, shape=SMOOTH)]),
    G("Dark Acid", (.1, .5, .05, .9, .5), "Shadow, Umbra, Penumbra, Eclipse, Dusk, Midnight, Nightfall, Gloam",
      ms((0, .3), (120, 500), (.55, .8), (2, 4), (100, 350), L(M_DIODE, M_MOOG), drive=(.3, .6))),
    G("Acid Lead", (.4, .4, .1, .9, .2), "Flare, Signal Flare, Beacon Acid, Tracer, Rocket, Starshell, Fusee, Torch",
      ms((0, .2), (500, 2500), (.6, .85), (2.5, 4.5), (120, 400), L(M_DIODE), amp=((.5, 2), (200, 500), (.3, .6), (40, 120))),
      [lfo(.6, S_PITCH, .03, .06, rate=(4.5, 6), retrig=1, fade=(.2, .6))]),
    G("Tape Acid", (.2, .6, .2, .8, .4), "Dropout, Warble, Wow, Flutter, Crinkle, Stretch, Dub Acid, Fade",
      ms((0, .3), (200, 900), (.5, .75), (2, 4), (80, 300), L(M_DIODE, M_JUNO)), [lfo(.8, S_PITCH, .02, .05, rate=(.3, 1.2), shape=SMOOTH)]),
    G("Hard Acid", (.2, .3, 0, 1, .1), "Iron, Steel, Chrome, Cobalt, Titanium, Tungsten, Carbide, Adamant",
      ms((0, .2), (300, 1500), (.6, .85), (3, 5), (60, 220), L(M_DIODE), drive=(.6, .95)), [src(.6, VEL, S_DRIVE, .1, .3)]),
    G("Resonant Sweep", (.3, .5, .1, .9, .3), "Sweep, Arc, Swing, Pendulum, Crescent, Parabola, Curve, Bend",
      ms((0, .3), (150, 800), (.65, .9), (1.5, 3.5), (150, 500), L(M_DIODE, M_KORG)), [lfo(.9, S_CUT, .15, .35, sync=BARS4, shape=TRI)]),
    G("Bubbling", (.2, .5, .2, .8, .4), "Fizz Acid, Soda, Seltzer, Froth, Lather, Effervesce, Sparkle Acid, Spritz Acid",
      ms((0, .3), (300, 1200), (.6, .85), (1.5, 3.5), (40, 120), L(M_DIODE, M_SEM)), [lfo(.8, S_CUT, .08, .2, shape=SH, sync=SIXTEENTH)]),
    G("Acid Chords", (.3, .5, .2, .7, .3), "Minor, Dorian Acid, Phrygian, Aeolian Acid, Mixolydian, Lydian Acid, Locrian, Modal",
      ms((0, .4), (300, 1000), (.4, .65), (2, 3.5), (100, 300), L(M_MOOG, M_JUNO, M_DIODE))),
    G("Wasp Acid", (.1, .3, 0, .9, .2), "Sting, Venom, Hive, Swarm, Drone Bee, Stinger, Nest, Colony",
      ms((.2, .8), (250, 1000), (.5, .8), (2.5, 4.5), (80, 250), L(M_WASP)), [lfo(.6, S_PW, .1, .3, sync=EIGHTH)]),
    G("Energy Acid", ACID, "Charge, Voltage, Ampere, Current Acid, Discharge, Spark Gap, Arc Lamp, Tesla",
      ms((0, .3), (200, 900), (.55, .85), (2, 4), (80, 300), L(M_DIODE)), [src(.9, ENERGY, S_CUT, .2, .45), src(.6, ENERGY, S_RES, .1, .2)]),
    G("Wheel Acid", ACID, "Lever, Handle, Knob, Dial, Crank Acid, Throttle, Rudder, Tiller",
      ms((0, .3), (200, 900), (.55, .85), (2, 4), (80, 300), L(M_DIODE)), [src(.9, WHEEL, S_CUT, .3, .6), src(.6, PRESS, S_RES, .1, .25)]),
])

# ================================================================= the six polyphonic voices
LEAD_AMP = [("amp_attack", 1, 12, "R"), ("amp_decay", 300, 1200, "R"), ("amp_sustain", 0.7, 1.0, "R"), ("amp_release", 120, 500, "B")]
SHORT_AMP = [("amp_attack", 0.3, 3, "R"), ("amp_decay", 80, 400, "B"), ("amp_sustain", 0.0, 0.25, "R"), ("amp_release", 40, 200, "B")]
PAD_AMP = [("amp_attack", 150, 1500, "B"), ("amp_decay", 800, 3000, "R"), ("amp_sustain", 0.7, 1.0, "R"), ("amp_release", 600, 3000, "B")]
FILT = lambda *m: ("filter_model", L(*m), None, "R")


def saw(det=(.45, .85), cut=(2500, 16000), res=(.05, .3), env=(.5, 2.5), fdec=(150, 900), amp=LEAD_AMP, extra=()):
    return [("osc", SUPERSAW, SUPERSAW, "C"), ("detune", det[0], det[1], "B"), ("mix", .6, .9, "R"), ("cutoff", cut[0], cut[1], "A"),
            ("resonance", res[0], res[1], "R"), ("env_amount", env[0], env[1], "R"), ("filter_decay", fdec[0], fdec[1], "B")] + list(amp) + list(extra)


def va(wave=(0, .4), cut=(800, 8000), res=(.1, .4), env=(1, 3), fdec=(80, 400), amp=SHORT_AMP, extra=()):
    return [("osc", VA, VA, "C"), ("wave", wave[0], wave[1], "R"), ("cutoff", cut[0], cut[1], "A"), ("resonance", res[0], res[1], "R"),
            ("env_amount", env[0], env[1], "R"), ("filter_decay", fdec[0], fdec[1], "B")] + list(amp) + list(extra)


def fm(ratio=(2, 4), index=(1, 4), fdecay=(80, 600), cut=(4000, 16000), amp=SHORT_AMP, extra=()):
    return [("osc", FM, FM, "C"), ("fm_ratio", ratio[0], ratio[1], "R"), ("fm_index", index[0], index[1], "B"),
            ("fm_decay", fdecay[0], fdecay[1], "B"), ("cutoff", cut[0], cut[1], "A")] + list(amp) + list(extra)


def wt(tables, pos=(.1, .9), cut=(3000, 16000), amp=LEAD_AMP, extra=()):
    return [("osc", WT, WT, "C"), ("table", L(*tables), None, "R"), ("position", pos[0], pos[1], "B"), ("cutoff", cut[0], cut[1], "A")] + list(amp) + list(extra)


LEAD = dict(label="lead", module="Poly", instance=0, adj=["Veiled", "Smoky", "Warm", "Golden", "Glowing", "Radiant", "Brilliant", "Blazing"], groups=[
    G("Anthem Supersaw", UP, "Sunrise, Dawn, Daybreak, Aurora, Firmament, Zenith, Solstice, Equinox",
      saw() + [FILT(PF_OWN, PF_JUNO, PF_PROPHET)], [lfo(.6, P_CUT, .04, .12, rate=(.05, .3)), src(.4, VEL, P_CUT, .1, .25), src(.5, ENERGY, P_DET, .1, .3)]),
    G("Euphoric Octaves", UP, "Ascension, Rapture, Elation, Euphoria, Bliss, Jubilee, Triumph, Glory",
      saw(extra=[("osc2", 1, 1, "C"), ("osc2_interval", 5, 5, "C"), ("osc2_mix", .2, .45, "R")]) + [FILT(PF_OWN, PF_JUNO)],
      [lfo(.5, P_CUT, .04, .1, sync=BARS2), lfo(.4, P_PAN, .2, .4, sync=BAR1)]),
    G("Hoover Rave", (.6, .3, .1, .6, .1), "Rave, Warehouse Lead, Hardcore, Breakbeat, Jungle, Oldskool, Anthem Rave, Hoover",
      saw(det=(.7, 1), env=(1, 3)) + [("osc2", 1, 1, "C"), ("osc2_interval", 1, 1, "C"), ("osc2_mix", .3, .6, "R"), ("glide", 40, 160, "B"), FILT(PF_JUNO, PF_PROPHET)],
      [menv(1, P_PITCH, -.35, -.2, decay=(150, 600)), lfo(.5, P_CUT, .05, .12, sync=QUARTER)]),
    G("Progressive Pluck Lead", PROG, "Meridian, Latitude, Longitude, Compass, Bearing, Heading, Azimuth, Vector",
      va(cut=(1500, 9000), amp=LEAD_AMP) + [FILT(PF_PROPHET, PF_MOOG, PF_SEM)], [lfo(.7, P_CUT, .06, .15, sync=BARS2, shape=TRI), src(.4, VEL, P_CUT, .1, .2)]),
    G("Vocal Wavetable", (.6, .6, .7, .2, .5), "Voice, Choirboy, Siren Voice, Muse, Oracle, Sibyl, Nymph, Seraph",
      wt([T_VOCAL, T_FSAW]) + [FILT(PF_OWN, PF_SEM)], [lfo(1, P_TP, .15, .4, shape=SMOOTH, rate=(.2, 1.5)), lfo(.5, P_CUT, .05, .12, sync=HALF)]),
    G("Glass Lead", (.6, .6, .8, .1, .6), "Crystal, Prism, Quartz, Diamond, Sapphire, Opal, Topaz, Beryl",
      wt([T_GLASS]) + [FILT(PF_OWN, PF_XPANDER)], [lfo(.6, P_TP, .1, .25, rate=(.1, .4))]),
    G("FM Bell Lead", (.5, .6, .8, .2, .6), "Chime Lead, Tubular, Celesta, Glockenspiel, Carillon Lead, Gamelan, Vibraphone, Marimba",
      fm(amp=[("amp_attack", .3, 3, "R"), ("amp_decay", 300, 1500, "B"), ("amp_sustain", .3, .7, "R"), ("amp_release", 200, 800, "B")]),
      [menv(.6, P_FMI, .3, .7, decay=(200, 1000)), src(.5, VEL, P_FMI, .2, .5)]),
    G("Sync Scream", (.4, .4, .1, .7, .2), "Comet Tail, Meteor, Bolide, Fireball, Shooting Star, Nova, Supernova, Flare Star",
      wt([T_SYNC], pos=(.1, .5)) + [("pos_env", .3, .8, "B"), ("pos_decay", 100, 800, "B")], [menv(1, P_TP, .3, .6, decay=(150, 900))]),
    G("PWM Classic", (.5, .7, .6, .3, .5), "Jupiter, Saturn, Neptune, Uranus, Mercury, Venus, Mars, Pluto",
      va(wave=(1, 1), cut=(1500, 9000), amp=LEAD_AMP, extra=[("pulse_width", .15, .5, "B")]) + [FILT(PF_SEM, PF_PROPHET, PF_JUNO)],
      [lfo(1, P_PW, .3, .6, shape=TRI, rate=(.3, 2)), lfo(.3, P_CUT, .05, .1, sync=HALF)]),
    G("Echo Lead", (.7, .8, .6, .2, .6), "Echo, Reverb Lead, Canyon Echo, Valley, Gorge, Ravine, Fjord, Glen",
      va(cut=(2000, 9000), amp=LEAD_AMP, extra=[("delay_send", .3, .6, "B"), ("delay_feedback", .4, .7, "B")]) + [FILT(PF_OWN, PF_SEM)],
      [lfo(.6, P_PAN, .3, .6, sync=BAR1), lfo(.4, P_CUT, .05, .12, sync=HALF)]),
    G("Flute Breath", (.4, .5, .8, .1, .7), "Flute, Piccolo, Ocarina, Pan Pipe, Shakuhachi, Recorder, Fife, Whistle",
      fm(ratio=(1, 1), index=(.5, 1.5), amp=[("amp_attack", 20, 120, "B"), ("amp_sustain", .7, .95, "R"), ("amp_release", 150, 500, "R")]) + [FILT(PF_SEM)],
      [lfo(1, P_PITCH, .05, .08, rate=(4, 6), retrig=1, fade=(.5, 1.5)), src(.5, RND, P_CUT, .1, .2), src(.5, PRESS, P_LVL, .1, .3)]),
    G("Trance Gate Lead", UP, "Strobe, Laser, Beam, Ray, Searchlight, Spotlight, Floodlight, Lightning",
      saw() + [FILT(PF_OWN, PF_JUNO)], [lfo(1, P_LVL, -.8, -.5, sync=SIXTEENTH, shape=SQUARE), lfo(.4, P_CUT, .05, .12, sync=BARS4)]),
    G("Detuned Energy", UP, "Surge Lead, Boost, Overdrive, Afterburner, Thruster, Propulsion, Momentum, Velocity",
      saw(det=(.3, .7)) + [FILT(PF_OWN, PF_JUNO, PF_PROPHET)], [src(1, ENERGY, P_DET, .2, .5), src(.8, ENERGY, P_CUT, .1, .25), src(.6, WHEEL, P_CUT, .2, .4)]),
    G("Dream Soft Lead", DREAM, "Reverie, Daydream, Lullaby, Slumber, Idyll, Nocturne, Serenade, Pastorale",
      va(wave=(0, .3), cut=(1200, 6000), res=(.05, .2), amp=LEAD_AMP) + [("glide", 20, 80, "R"), FILT(PF_SEM, PF_JUNO)],
      [lfo(.8, P_PITCH, .04, .07, rate=(4.5, 5.5), fade=(.4, 1.2)), lfo(.5, P_CUT, .04, .1, sync=BARS2)]),
    G("Acid Lead Poly", ACID, "Acid Rain, Toxin, Reagent, Solvent, Catalyst, Titrant, Enzyme, Alkali",
      va(wave=(0, .3), cut=(400, 3000), res=(.55, .85), env=(2.5, 5), fdec=(80, 350), amp=LEAD_AMP) + [FILT(PF_DIODE, PF_MOOG, PF_KORG)],
      [lfo(.5, P_CUT, .05, .15, sync=SIXTEENTH, shape=SH), src(.5, VEL, P_CUT, .15, .35)]),
    G("Cinematic Lead", CINE, "Odyssey, Saga, Epic, Legend, Myth, Chronicle, Anthem, Overture",
      saw(det=(.35, .7), cut=(2000, 12000)) + [FILT(PF_OWN, PF_PROPHET)],
      [lfo(.7, P_PITCH, .04, .07, rate=(4.8, 5.8), fade=(.6, 1.5)), src(.7, ENERGY, P_CUT, .1, .3), src(.5, WHEEL, P_PITCH, .05, .08)]),
])
COUNTER = dict(label="counter", module="Poly", instance=1, adj=["Hushed", "Dim", "Mellow", "Soft", "Clear", "Keen", "Bright", "Piercing"], groups=[
    G("Dry Saw Answer", UP, "Reply, Answer, Echo Voice, Response, Rejoinder, Retort, Riposte, Return",
      saw(det=(.2, .5), cut=(1500, 9000), env=(1.5, 3.5), fdec=(60, 300), amp=SHORT_AMP) + [FILT(PF_OWN, PF_PROPHET, PF_MOOG)], [src(.5, VEL, P_CUT, .15, .3)]),
    G("Rubber Counter", (.3, .6, .2, .7, .3), "Bounce Line, Rebound, Ricochet, Deflect, Carom, Spring Line, Recoil, Kickback",
      va(cut=(500, 3000), res=(.45, .75), env=(2, 4.5), fdec=(60, 250)) + [FILT(PF_DIODE, PF_MOOG, PF_KORG)], [lfo(.4, P_CUT, .05, .12, sync=EIGHTH, shape=SAWDN)]),
    G("FM Pluck Counter", (.5, .6, .7, .2, .6), "Plectrum, Quill, Stylus Pluck, Nib, Point, Tip, Barb Pluck, Prick",
      fm(ratio=(2, 3.5), index=(1, 4), fdecay=(30, 250)), [menv(.7, P_FMI, .3, .8, decay=(40, 250)), src(.4, VEL, P_FMI, .2, .4)]),
    G("Hollow Pulse", (.5, .7, .6, .3, .6), "Hollow, Cavity, Chamber Tone, Alcove, Niche, Recess, Nook, Crevice",
      va(wave=(1, 1), cut=(1200, 7000), extra=[("pulse_width", .1, .35, "B")]) + [FILT(PF_SEM, PF_XPANDER)], [lfo(.6, P_PW, .2, .4, sync=QUARTER, shape=TRI)]),
    G("Chirp", (.5, .5, .3, .5, .3), "Sparrow, Finch, Wren, Robin, Lark, Thrush, Swallow, Starling",
      va(wave=(.4, .9), cut=(2500, 11000), res=(.3, .6)) + [FILT(PF_OWN, PF_KORG)], [menv(1, P_PITCH, .3, .55, decay=(15, 80), attack=(.1, .3))]),
    G("Metal Tick", (.4, .6, .3, .5, .5), "Rivet Tick, Bolt Tick, Washer, Nut Tick, Screw, Pin Tick, Staple, Tack Tick",
      fm(ratio=(3.5, 7), index=(2, 6), fdecay=(20, 150)), [src(.5, RND, P_FMI, .2, .4)]),
    G("Glass Answer", (.5, .6, .8, .1, .6), "Mirror, Lens, Pane, Window, Crystal Ball, Goblet, Vial, Flask",
      wt([T_GLASS], amp=SHORT_AMP), [lfo(.6, P_TP, .1, .25, sync=BAR1)]),
    G("Soft Sine Line", DREAM, "Whisper, Murmur Line, Sigh Line, Breath Line, Hush Line, Rustle Line, Purl, Lilt",
      fm(ratio=(1, 1), index=(0, .5), amp=LEAD_AMP), [lfo(.6, P_PITCH, .03, .06, rate=(4.5, 5.5), fade=(.3, 1))]),
    G("Octave Counter", UP, "Upper, Descant, Soprano Line, Alto Line, Tenor Line, Treble, Overtone, Harmonic",
      saw(det=(.3, .6), amp=SHORT_AMP, extra=[("osc2", 1, 1, "C"), ("osc2_interval", 5, 5, "C"), ("osc2_mix", .2, .4, "R")]), [src(.5, VEL, P_CUT, .1, .25)]),
    G("Delay Answer", PROG, "Relay, Baton, Handoff, Passage, Transfer, Pass, Exchange, Swap",
      va(cut=(1500, 7000), extra=[("delay_send", .35, .65, "B"), ("delay_feedback", .45, .75, "B")]), [lfo(.6, P_PAN, .3, .6, sync=HALF)]),
    G("Vocal Chop", (.6, .6, .7, .3, .4), "Syllable, Vowel Chop, Phoneme, Utterance, Call, Cry, Hail Voice, Shout",
      wt([T_VOCAL], pos=(0, 1), amp=SHORT_AMP), [menv(.8, P_TP, .2, .5, decay=(60, 300)), src(.5, RND, P_TP, .1, .3)]),
    G("Pizzicato Synth", (.6, .6, .5, .2, .4), "Pizzicato, Plucked String, Snap Pizz, Bartok, Col Legno, Harp Pluck, Lute, Mandolin",
      va(wave=(0, .2), cut=(1500, 8000), env=(2, 4), fdec=(40, 150)) + [FILT(PF_PROPHET, PF_SEM)], [src(.6, VEL, P_CUT, .15, .3)]),
    G("Energy Counter", UP, "Spark Line, Ember Line, Kindle, Ignite, Flicker, Flare Line, Blaze Line, Glow Line",
      saw(det=(.2, .5), amp=SHORT_AMP), [src(1, ENERGY, P_CUT, .15, .35), src(.6, ENERGY, P_DET, .1, .3)]),
    G("Acid Counter", ACID, "Etch, Corrode, Erode, Pickle, Anneal, Temper, Quench, Burnish",
      va(cut=(400, 2500), res=(.55, .85), env=(2.5, 4.5)) + [FILT(PF_DIODE)], [lfo(.6, P_CUT, .05, .15, shape=SH, sync=SIXTEENTH)]),
    G("Deep Muted", DEEP, "Felt Line, Wool, Muffle, Damper Line, Mute, Baffle, Padding, Batting",
      va(wave=(0, .5), cut=(500, 2500), res=(.05, .2)) + [FILT(PF_SEM, PF_MOOG)], [lfo(.5, P_CUT, .05, .1, sync=BARS2, shape=SMOOTH)]),
    G("Cinematic Counter", CINE, "Harbinger, Envoy, Emissary, Courier, Messenger, Legate, Nuncio, Ambassador",
      saw(det=(.3, .6), cut=(1500, 9000), amp=LEAD_AMP), [lfo(.6, P_PITCH, .04, .06, rate=(5, 6), fade=(.5, 1.2)), src(.5, ENERGY, P_CUT, .1, .25)]),
])
PLUCK = dict(label="pluck", module="Poly", instance=2, adj=["Muted", "Dusky", "Woody", "Warm", "Snappy", "Crisp", "Glassy", "Sparkling"], groups=[
    G("Trance Pluck", UP, "Starlight, Starbeam, Stardust, Starfall, Starfield, Starburst, Starglow, Starshine",
      saw(det=(.3, .6), cut=(800, 6000), res=(.2, .5), env=(2, 4.5), fdec=(40, 200), amp=SHORT_AMP) + [FILT(PF_PROPHET, PF_MOOG, PF_OWN)],
      [src(.5, VEL, P_CUT, .2, .4), src(.3, RND, P_CUT, .05, .15)]),
    G("Progressive Pluck", PROG, "Pebble Pluck, Stone, Flint, Chert, Obsidian, Agate, Jasper, Onyx",
      va(cut=(600, 5000), res=(.2, .5), env=(2, 4.5), fdec=(40, 180)) + [FILT(PF_PROPHET, PF_SEM, PF_JUNO)],
      [lfo(.8, P_CUT, .08, .2, sync=BARS4, shape=TRI), src(.4, VEL, P_CUT, .1, .3)]),
    G("Dream Keys", DREAM, "Music Box, Celeste Keys, Toy Piano, Kalimba, Mbira, Sanza, Thumb Piano, Lamellophone",
      fm(ratio=(2, 4), index=(1, 3), fdecay=(80, 400), amp=[("amp_attack", .3, 2, "R"), ("amp_decay", 300, 1200, "B"), ("amp_sustain", 0, .2, "R"), ("amp_release", 200, 600, "B")]),
      [menv(.6, P_FMI, .2, .5, decay=(100, 500)), lfo(.5, P_PITCH, .02, .04, rate=(.3, 1), shape=SMOOTH)]),
    G("Marimba Wood", (.4, .6, .8, .1, .7), "Rosewood, Padauk, Teak, Ebony, Mahogany, Cedar, Walnut, Birch",
      fm(ratio=(3.5, 4.5), index=(1, 3), fdecay=(40, 200)), [src(.6, VEL, P_FMI, .2, .4)]),
    G("Harp Glass", (.5, .6, .8, .1, .6), "Harp, Lyre, Zither, Dulcimer, Psaltery, Kithara, Koto, Guqin",
      wt([T_GLASS], amp=SHORT_AMP) + [("pos_env", .2, .5, "B"), ("pos_decay", 100, 600, "B")], [menv(.6, P_TP, .1, .3, decay=(100, 500))]),
    G("Bell Pluck", (.5, .6, .8, .1, .5), "Tinkerbell, Bellbird, Silverbell, Hand Bell, Cowbell Pluck, Sleighbell, Temple Bell, Bell Tree",
      fm(ratio=(2.5, 3.5), index=(2, 5), fdecay=(100, 500)), [menv(.6, P_FMI, .3, .6, decay=(150, 600))]),
    G("Resonant Blip", (.5, .7, .3, .6, .4), "Blip, Bleep, Beep, Ping, Pong, Boop, Bloop, Plip",
      va(wave=(0, .5), cut=(500, 3000), res=(.5, .8), env=(2.5, 5), fdec=(30, 120)) + [FILT(PF_MOOG, PF_DIODE, PF_KORG)], [src(.6, KEY, P_CUT, .1, .2)]),
    G("Rhythmic Chord Pluck", UP, "Chordal, Triad, Seventh, Ninth, Suspension, Inversion, Voicing, Cluster",
      saw(det=(.4, .7), cut=(1000, 7000), env=(2, 4), fdec=(50, 250), amp=SHORT_AMP) + [FILT(PF_OWN, PF_JUNO)], [lfo(.5, P_CUT, .05, .15, sync=BARS2)]),
    G("Gated Pluck", UP, "Shutter Pluck, Blind, Slat, Louvre, Lattice Pluck, Grille, Trellis, Screen",
      saw(det=(.4, .7), cut=(1500, 8000), amp=SHORT_AMP), [lfo(1, P_LVL, -.6, -.3, sync=SIXTEENTH, shape=SQUARE)]),
    G("Delay Pluck", (.7, .8, .6, .2, .6), "Ricochet Pluck, Bounce Pluck, Skip Stone, Hopscotch, Stepping, Ripple Pluck, Relay Pluck, Echo Pluck",
      va(cut=(1000, 6000), extra=[("delay_send", .35, .65, "B"), ("delay_feedback", .45, .75, "B")]), [lfo(.6, P_PAN, .3, .6, sync=HALF)]),
    G("Wavetable Pluck", (.5, .7, .5, .3, .5), "Facet, Pixel Pluck, Fractal, Voxel, Polygon, Vertex, Mesh, Shard",
      wt([T_CLASSIC, T_PWM, T_SYNC], pos=(0, .8), amp=SHORT_AMP) + [("pos_env", .2, .6, "B"), ("pos_decay", 80, 600, "B")], [src(.4, RND, P_TP, .1, .2)]),
    G("Acid Pluck", ACID, "Zinc, Nickel, Tin, Lead Pluck, Antimony, Bismuth, Gallium, Indium",
      va(cut=(300, 2500), res=(.6, .85), env=(3, 5), fdec=(40, 150)) + [FILT(PF_DIODE)], [lfo(.5, P_CUT, .05, .15, shape=SH, sync=SIXTEENTH)]),
    G("Deep Muted Pluck", DEEP, "Moss, Lichen, Fern, Bracken, Sedge, Rush, Reed, Heather",
      va(wave=(0, .6), cut=(400, 2500), res=(.1, .3), env=(1, 3), fdec=(60, 250)) + [FILT(PF_SEM, PF_MOOG)], [lfo(.6, P_CUT, .05, .12, sync=BARS4, shape=SMOOTH)]),
    G("Energy Pluck", UP, "Photon, Electron, Proton, Neutron, Quark, Gluon, Boson, Lepton",
      saw(det=(.3, .6), cut=(800, 6000), amp=SHORT_AMP), [src(1, ENERGY, P_CUT, .15, .35), src(.5, WHEEL, P_RES, .1, .3)]),
    G("Kalimba Drift", (.3, .6, .8, .1, .7), "Rainstick, Raindrop, Droplet, Drizzle, Shower, Sprinkle Drop, Mist Drop, Dewdrop",
      fm(ratio=(4, 6), index=(.5, 2), fdecay=(50, 250)), [lfo(.7, P_PITCH, .02, .04, rate=(.2, .8), shape=SMOOTH), src(.5, RND, P_PAN, .2, .5)]),
    G("Cinematic Pluck", CINE, "Pizzicato Grand, Harpsichord, Clavichord, Spinet, Virginal, Cembalo, Fortepiano, Clavinet",
      va(wave=(0, .3), cut=(1200, 8000), env=(2, 4), fdec=(60, 250)) + [FILT(PF_PROPHET, PF_SEM)], [src(.6, VEL, P_CUT, .15, .3), src(.4, ENERGY, P_CUT, .1, .2)]),
])
ARP = dict(label="arp", module="Poly", instance=3, adj=["Shadowed", "Dim", "Soft", "Warm", "Lively", "Sparkly", "Shimmering", "Dazzling"], groups=[
    G("Classic Sequence", ALL, "Cycle, Loop, Circuit Arp, Orbit, Revolution, Rotation, Round, Lap",
      va(cut=(700, 5000), res=(.25, .55), env=(1.5, 4), fdec=(60, 300)) + [FILT(PF_MOOG, PF_PROPHET, PF_DIODE, PF_OWN)],
      [lfo(.8, P_CUT, .08, .2, sync=BARS2, shape=TRI), src(.4, VEL, P_CUT, .1, .3)]),
    G("Glass Arp", (.6, .6, .8, .1, .6), "Icicle, Frost, Snowflake, Hailstone, Rime, Hoarfrost, Glacier, Floe",
      wt([T_GLASS, T_SYNC], amp=SHORT_AMP), [lfo(.8, P_TP, .15, .35, sync=BAR1, shape=TRI)]),
    G("Wavetable Arp", (.5, .7, .5, .3, .6), "Morph, Shift, Phase, Flux, Transit, Transition, Metamorph, Mutation",
      wt([T_CLASSIC, T_PWM, T_FSAW], pos=(0, .7), amp=SHORT_AMP) + [("pos_env", .2, .6, "B"), ("pos_decay", 80, 600, "B")],
      [lfo(1, P_TP, .2, .4, sync=BARS2), src(.3, RND, P_TP, .1, .2)]),
    G("Ratchet Saw", UP, "Ratchet, Pawl, Escapement, Detent, Click Wheel, Tick Wheel, Stepper, Indexer",
      saw(det=(.3, .6), cut=(1500, 8000), env=(2, 4), fdec=(40, 180), amp=SHORT_AMP) + [FILT(PF_JUNO, PF_PROPHET, PF_OWN)],
      [lfo(.8, P_LVL, -.5, -.3, sync=SIXTEENTH, shape=SQUARE)]),
    G("Bubble Arp", (.3, .5, .4, .7, .5), "Bubble Arp, Globule, Droplet Arp, Sphere Arp, Blob, Bead Arp, Pearl Arp, Orb Arp",
      fm(ratio=(1, 2), index=(.5, 2.5), cut=(1000, 6000)) + [("resonance", .4, .7, "R"), FILT(PF_SEM, PF_KORG, PF_DIODE)],
      [lfo(1, P_CUT, .1, .25, shape=SH, sync=SIXTEENTH), src(.4, KEY, P_CUT, .1, .2)]),
    G("Pan Arp", (.6, .8, .6, .3, .6), "Pendulum Arp, Swing Arp, Seesaw, Teeter, Metronome, Oscillate, Sway, Rock",
      va(wave=(.3, .8), cut=(1500, 8000)) + [FILT(PF_PROPHET, PF_SEM)], [lfo(1, P_PAN, .5, .9, sync=QT, shape=TRI), lfo(.4, P_CUT, .05, .15, sync=BAR1)]),
    G("Dream Arp", DREAM, "Firefly, Glowworm, Lantern, Candle, Taper, Wick, Nightlight, Lamplight",
      fm(ratio=(2, 3), index=(.5, 2), fdecay=(100, 400), amp=SHORT_AMP), [lfo(.6, P_PITCH, .02, .04, rate=(.2, .8), shape=SMOOTH), lfo(.5, P_PAN, .2, .4, sync=BARS2)]),
    G("Progressive Arp", PROG, "Spiral, Helix, Coil Arp, Whorl, Volute, Scroll, Tendril, Curl",
      va(cut=(600, 4000), res=(.3, .6), env=(2, 4)) + [FILT(PF_PROPHET, PF_MOOG)], [lfo(1, P_CUT, .1, .25, sync=BARS4, shape=TRI), src(.5, ENERGY, P_CUT, .1, .3)]),
    G("Acid Arp", ACID, "Acidhouse, Chicago, Detroit, Warehouse Arp, Jack, Trax, Phuture, Mayday",
      va(cut=(300, 2500), res=(.6, .85), env=(3, 5), fdec=(40, 150)) + [FILT(PF_DIODE)], [lfo(.7, P_CUT, .05, .2, shape=SH, sync=SIXTEENTH)]),
    G("Deep Arp", DEEP, "Nautilus, Ammonite, Conch, Whelk, Cowrie, Abalone, Scallop, Periwinkle",
      va(wave=(0, .6), cut=(400, 2500), res=(.15, .35)) + [FILT(PF_SEM, PF_MOOG)], [lfo(.8, P_CUT, .06, .14, sync=BARS4, shape=SMOOTH)]),
    G("FM Glass Arp", (.5, .6, .7, .2, .5), "Filigree, Gossamer, Cobweb, Tracery, Fretwork, Lacework, Meshwork, Needlepoint",
      fm(ratio=(3, 5), index=(1, 3), fdecay=(40, 200)), [src(.5, RND, P_FMI, .1, .3), menv(.5, P_FMI, .2, .5, decay=(40, 200))]),
    G("Octave Arp", UP, "Altitude, Elevation, Apex, Summit Arp, Peak, Acme, Vertex Arp, Crown",
      saw(det=(.3, .6), cut=(1500, 9000), amp=SHORT_AMP, extra=[("osc2", 1, 1, "C"), ("osc2_interval", 5, 5, "C"), ("osc2_mix", .2, .4, "R")]),
      [src(.5, VEL, P_CUT, .1, .25)]),
    G("Delay Arp", (.7, .8, .6, .2, .6), "Cascade Arp, Waterfall, Spillway, Weir, Rapids Arp, Chute, Torrent, Flume",
      va(cut=(1000, 6000), extra=[("delay_send", .35, .65, "B"), ("delay_feedback", .45, .75, "B")]), [lfo(.6, P_PAN, .3, .6, sync=HALF)]),
    G("Energy Arp", UP, "Dynamo, Generator, Turbine Arp, Alternator, Magneto, Rotor, Stator, Armature",
      saw(det=(.3, .6), cut=(800, 6000), amp=SHORT_AMP), [src(1, ENERGY, P_CUT, .15, .35), src(.6, WHEEL, P_CUT, .2, .4)]),
    G("Vocal Arp", (.6, .6, .7, .2, .5), "Chant, Mantra, Hymn, Psalm, Canticle, Anthem Voice, Carol, Plainsong",
      wt([T_VOCAL, T_FSAW], pos=(0, 1), amp=SHORT_AMP), [lfo(.8, P_TP, .2, .4, sync=BAR1), src(.5, RND, P_TP, .1, .3)]),
    G("Cinematic Ostinato", CINE, "Ostinato, Pulse Figure, Motor, Engine Figure, Drive Figure, Loop Figure, Pedal Figure, Moto",
      va(wave=(0, .3), cut=(800, 5000), res=(.15, .35)) + [FILT(PF_PROPHET, PF_MOOG)], [src(.8, ENERGY, P_CUT, .15, .35), lfo(.4, P_CUT, .05, .12, sync=BARS4)]),
])
PAD = dict(label="pad", module="Poly", instance=4, adj=["Deep", "Dark", "Hazy", "Warm", "Lush", "Silken", "Luminous", "Celestial"], groups=[
    G("Supersaw Pad", UP, "Heaven, Paradise, Elysium, Eden, Arcadia, Nirvana, Utopia, Shangri-La",
      saw(det=(.5, .9), cut=(1500, 12000), res=(.05, .2), amp=PAD_AMP) + [FILT(PF_OWN, PF_JUNO, PF_PROPHET)],
      [lfo(.8, P_CUT, .05, .15, sync=BARS4, shape=TRI), lfo(.5, P_PAN, .1, .3, sync=BARS2), src(.5, ENERGY, P_CUT, .1, .3)]),
    G("Warm Analog Pad", (.6, .8, .8, .2, .8), "Hearthside, Fireside, Inglenook, Snug, Den, Cabin, Lodge, Chalet",
      va(wave=(0, .5), cut=(800, 6000), res=(.05, .25), env=(.3, 1.5), amp=PAD_AMP) + [FILT(PF_JUNO, PF_SEM, PF_PROPHET)],
      [lfo(.8, P_CUT, .04, .1, rate=(.05, .2), shape=SMOOTH), lfo(.5, P_PITCH, .02, .04, rate=(.1, .4), shape=SMOOTH)]),
    G("PWM Strings", (.7, .7, .7, .2, .6), "Ensemble, String Machine, Solina, Polymoog, Crumar, Elka, Omni, Arp Strings",
      va(wave=(1, 1), cut=(1500, 8000), amp=PAD_AMP, extra=[("pulse_width", .2, .5, "B")]) + [FILT(PF_SEM, PF_JUNO)],
      [lfo(1, P_PW, .2, .45, shape=TRI, rate=(.3, 1.5)), lfo(.4, P_CUT, .04, .1, sync=BARS2)]),
    G("Glass Pad", (.6, .6, .9, .1, .8), "Stained Glass, Rose Window, Lantern Glass, Skylight, Conservatory, Atrium, Orangery, Greenhouse",
      wt([T_GLASS, T_VOCAL], amp=PAD_AMP), [lfo(1, P_TP, .1, .3, rate=(.05, .2), shape=SMOOTH)]),
    G("Choir Pad", (.8, .5, .8, .1, .6), "Angel, Cherub, Archangel, Choir Loft, Cathedral Pad, Chapel Pad, Nave Pad, Apse Pad",
      wt([T_VOCAL, T_FSAW], pos=(.2, .8), amp=PAD_AMP), [lfo(1, P_TP, .15, .35, rate=(.1, .4), shape=SMOOTH), lfo(.5, P_PAN, .1, .3, sync=BARS4)]),
    G("Evolving Wavetable", (.5, .8, .7, .3, .9), "Genesis, Emergence, Evolution, Unfolding, Growth, Germination, Budding, Flowering",
      wt([T_CLASSIC, T_PWM, T_SYNC, T_FSAW], pos=(0, .5), amp=PAD_AMP) + [("pos_lfo_depth", .2, .45, "B"), ("pos_lfo_beats", 16, 64, "R")],
      [lfo(1, P_TP, .15, .35, sync=BARS4, shape=TRI), menv(.5, P_CUT, .2, .4, decay=(2000, 8000), attack=(500, 3000), sustain=.5)]),
    G("Dream Haze", DREAM, "Haze Pad, Gauze, Veil, Shroud, Mantle Pad, Cloak, Canopy, Awning",
      va(wave=(0, .4), cut=(600, 4000), res=(.05, .2), env=(.2, 1), amp=PAD_AMP) + [("drift", 2, 5, "R"), FILT(PF_SEM, PF_JUNO)],
      [lfo(.8, P_PITCH, .02, .05, rate=(.05, .2), shape=SMOOTH), lfo(.6, P_CUT, .05, .12, rate=(.03, .1), shape=SMOOTH)]),
    G("Deep Drift Pad", DEEP, "Abyssal, Benthic, Pelagic, Hadal, Bathyal, Photic, Neritic, Littoral",
      va(wave=(0, .6), cut=(300, 2500), res=(.1, .3), env=(.2, 1), amp=PAD_AMP) + [("drift", 2, 6, "R"), FILT(PF_SEM, PF_MOOG)],
      [lfo(1, P_CUT, .08, .2, sync=BARS4, shape=SMOOTH), lfo(.7, P_PAN, .2, .5, rate=(.02, .08), shape=SMOOTH)]),
    G("Progressive Pad", PROG, "Tundra Pad, Taiga, Steppe Pad, Prairie Pad, Pampas, Veldt, Outback, Heathland",
      saw(det=(.4, .7), cut=(800, 6000), res=(.1, .3), amp=PAD_AMP) + [FILT(PF_PROPHET, PF_JUNO)],
      [lfo(1, P_CUT, .08, .2, sync=BARS4, shape=TRI), src(.5, ENERGY, P_CUT, .1, .3)]),
    G("Gated Pad", UP, "Gatekeeper, Portcullis, Drawbridge, Postern, Wicket, Turnstile, Barrier, Tollgate",
      saw(det=(.5, .8), cut=(2000, 10000), amp=PAD_AMP), [lfo(1, P_LVL, -.8, -.5, sync=SIXTEENTH, shape=SQUARE), lfo(.4, P_CUT, .05, .12, sync=BARS4)]),
    G("Acid Pad", ACID, "Toxic Haze, Smog, Fumes, Vapour Acid, Miasma, Effluvium, Reek, Exhalation",
      va(wave=(0, .4), cut=(300, 2000), res=(.4, .7), amp=PAD_AMP) + [FILT(PF_DIODE, PF_MOOG)], [lfo(1, P_CUT, .1, .25, sync=BARS2, shape=TRI)]),
    G("Air Pad", (.5, .6, .8, .1, .8), "Stratosphere, Troposphere, Mesosphere, Ionosphere, Exosphere, Thermosphere, Ozone, Jetstream",
      va(wave=(.5, 1), cut=(3000, 14000), res=(0, .15), env=(0, .5), amp=PAD_AMP) + [FILT(PF_OWN, PF_SEM)],
      [lfo(.8, P_CUT, .05, .15, rate=(.03, .1), shape=SMOOTH), lfo(.6, P_PAN, .2, .5, rate=(.03, .1), shape=SMOOTH)]),
    G("FM Glass Pad", (.4, .6, .8, .1, .7), "Nacre, Mother-of-Pearl, Iridescence, Lustre, Sheen, Shimmer Pad, Opalescence, Pearl Pad",
      fm(ratio=(1, 3), index=(.5, 2), fdecay=(500, 2000), amp=PAD_AMP), [lfo(.8, P_FMI, .1, .3, rate=(.05, .2), shape=SMOOTH)]),
    G("Octave Pad", UP, "Cathedral Octave, Organ Pad, Diapason, Principal, Open Diapason, Stopped Flute, Mixture, Plenum",
      saw(det=(.4, .7), amp=PAD_AMP, extra=[("osc2", 1, 1, "C"), ("osc2_interval", 1, 1, "C"), ("osc2_mix", .2, .4, "R")]),
      [lfo(.6, P_CUT, .05, .12, sync=BARS4)]),
    G("Wheel Pad", ALL, "Horizon Pad, Vista, Panorama, Prospect, Outlook, Overlook, Viewpoint, Lookout",
      saw(det=(.4, .8), cut=(1000, 8000), amp=PAD_AMP), [src(1, WHEEL, P_CUT, .3, .6), src(.8, PRESS, P_DET, .2, .4), src(.6, ENERGY, P_LVL, .1, .2)]),
    G("Cinematic Pad", CINE, "Tapestry, Fresco, Mural, Mosaic Pad, Frieze, Triptych, Panorama Epic, Cyclorama",
      saw(det=(.5, .8), cut=(1000, 9000), amp=PAD_AMP) + [FILT(PF_OWN, PF_PROPHET)],
      [src(1, ENERGY, P_CUT, .15, .35), lfo(.6, P_CUT, .05, .12, sync=BARS4, shape=TRI), lfo(.4, P_PAN, .1, .3, sync=BARS2)]),
])
STAB = dict(label="stab", module="Poly", instance=5, adj=["Thick", "Dark", "Round", "Warm", "Punchy", "Snappy", "Bright", "Blinding"], groups=[
    G("Chord Stab", UP, "Hit, Chord Hit, Blow Stab, Impact Stab, Shot, Burst Stab, Jab Stab, Strike Stab",
      saw(det=(.4, .75), cut=(1500, 10000), env=(1.5, 3.5), fdec=(60, 300), amp=SHORT_AMP) + [FILT(PF_OWN, PF_JUNO, PF_PROPHET)], [src(.5, VEL, P_CUT, .1, .3)]),
    G("Organ Stab", (.5, .7, .6, .3, .5), "Hammond, Drawbar, Leslie, Tonewheel, Percussion Organ, Gospel, Rock Organ, Jazz Organ",
      fm(ratio=(2, 2), index=(.5, 2), amp=SHORT_AMP, extra=[("osc2", 3, 3, "C"), ("osc2_interval", 5, 5, "C"), ("osc2_mix", .2, .4, "R")]),
      [lfo(.5, P_PITCH, .03, .05, rate=(5, 6.5))]),
    G("Brass Stab", (.7, .5, .4, .3, .3), "Fanfare Stab, Horn Stab, Trumpet Stab, Bugle, Cornet, Flugel, Sax Stab, Section Stab",
      va(wave=(0, .2), cut=(700, 4000), res=(.1, .3), env=(1.5, 3)) + [("filt_attack", 10, 60, "B"), ("filt_sustain", .2, .5, "R"), FILT(PF_PROPHET, PF_MOOG, PF_SEM)],
      [src(.4, VEL, P_CUT, .1, .25)]),
    G("Dub Stab", DEEP, "Dub Chord, Echo Chord, Delay Chord, Chord Echo, Version Chord, Riddim Chord, Siren Chord, Skank Chord",
      va(wave=(.2, .5), cut=(500, 2500), res=(.3, .6), extra=[("delay_send", .4, .7, "B"), ("delay_feedback", .5, .8, "B")]) + [FILT(PF_SEM, PF_MOOG)],
      [lfo(.6, P_CUT, .05, .15, sync=BARS2, shape=SMOOTH)]),
    G("Rave Stab", (.6, .3, .1, .6, .1), "Rave Hit, Hardcore Stab, Belgian, Mentasm, Dominator, Anasthasia, Alarm, Warning",
      saw(det=(.7, 1), cut=(1000, 6000), res=(.2, .4), amp=SHORT_AMP), [menv(.8, P_PITCH, -.3, -.15, decay=(80, 300))]),
    G("Pluck Stab", PROG, "Stab Pluck, Snip Stab, Nip, Tweak, Pinch Stab, Twist, Pick Stab, Plink Stab",
      va(cut=(800, 5000), res=(.2, .45), env=(2, 4), fdec=(40, 160)) + [FILT(PF_PROPHET, PF_SEM)], [src(.5, VEL, P_CUT, .15, .3)]),
    G("Glass Stab", (.5, .6, .8, .1, .6), "Shatter Stab, Splinter, Fragment, Sliver, Chip Stab, Flake, Crumb, Mote",
      wt([T_GLASS], amp=SHORT_AMP), [src(.5, RND, P_TP, .1, .3)]),
    G("Acid Stab", ACID, "Burn, Scorch, Sear, Singe, Char, Blister, Brand, Cauterise",
      va(cut=(300, 2000), res=(.6, .85), env=(3, 5), fdec=(40, 150)) + [FILT(PF_DIODE, PF_KORG)], [lfo(.5, P_CUT, .05, .15, shape=SH, sync=SIXTEENTH)]),
    G("Dream Stab", DREAM, "Petal Stab, Blossom Stab, Bloom Stab, Pollen Stab, Stamen, Sepal, Corolla, Calyx",
      fm(ratio=(1, 2), index=(.5, 1.5), fdecay=(100, 400)), [lfo(.5, P_PITCH, .02, .04, rate=(.3, 1), shape=SMOOTH)]),
    G("Gated Stab", UP, "Stutter, Glitch, Chop, Slice, Dice, Mince, Shred, Hack",
      saw(det=(.4, .7), cut=(1500, 8000), amp=PAD_AMP), [lfo(1, P_LVL, -.8, -.6, sync=SIXTEENTH, shape=SQUARE)]),
    G("Hollow Stab", (.5, .7, .6, .3, .6), "Void, Hollow Stab, Vacuum, Empty, Gap Stab, Cavity Stab, Shell Stab, Husk",
      va(wave=(1, 1), cut=(1000, 6000), extra=[("pulse_width", .1, .3, "B")]) + [FILT(PF_SEM, PF_XPANDER)], [lfo(.5, P_PW, .15, .3, sync=QUARTER)]),
    G("Filtered Chord", PROG, "Muffled Chord, Veiled Chord, Distant Chord, Far Chord, Faint Chord, Low Chord, Dim Chord, Hazy Chord",
      saw(det=(.3, .6), cut=(400, 3000), res=(.2, .4), amp=SHORT_AMP) + [FILT(PF_MOOG, PF_PROPHET)], [lfo(1, P_CUT, .1, .25, sync=BARS4, shape=TRI)]),
    G("Energy Stab", UP, "Voltage Stab, Jolt, Shock, Zap Stab, Buzz Stab, Crackle Stab, Arc Stab, Surge Stab",
      saw(det=(.4, .7), cut=(1000, 8000), amp=SHORT_AMP), [src(1, ENERGY, P_CUT, .15, .35), src(.5, WHEEL, P_CUT, .2, .4)]),
    G("FM Keys Stab", (.5, .6, .6, .3, .5), "Rhodes, Wurlitzer, Electric Piano, Tine, Reed Piano, Clav, Pianet, EP",
      fm(ratio=(1, 1), index=(.5, 2), fdecay=(200, 800)), [src(.6, VEL, P_FMI, .2, .5), lfo(.5, P_PAN, .2, .4, rate=(3, 5))]),
    G("Deep House Chord", DEEP, "Garage, Loft Chord, Basement, Sub-Level, Crypt Chord, Cellar Chord, Vault Chord, Bunker",
      va(wave=(0, .5), cut=(500, 3000), res=(.1, .3)) + [FILT(PF_SEM, PF_MOOG)], [lfo(.6, P_CUT, .05, .12, sync=BARS2, shape=SMOOTH)]),
    G("Cinematic Stab", CINE, "Thunderclap, Cannon, Salvo, Volley, Broadside, Barrage Stab, Fusillade, Bombard",
      saw(det=(.5, .8), cut=(1000, 8000), amp=SHORT_AMP, extra=[("osc2", 1, 1, "C"), ("osc2_interval", 1, 1, "C"), ("osc2_mix", .3, .5, "R")]),
      [src(.8, ENERGY, P_CUT, .1, .3)]),
])

# ================================================================= the physical piano
PIANO_ADJ = ["Muted", "Mellow", "Soft", "Warm", "Rounded", "Clear", "Bright", "Brilliant"]
def pn(instrument, hard, strike=(-.3, .3), unison=(.5, 1.5), inharm=(.8, 1.2), imped=(.8, 1.2), stretch=(.8, 1.2), cond=(.1, .5), extra=()):
    return [("instrument", instrument, None, "R"), ("hardness", hard[0], hard[1], "A"), ("strike", strike[0], strike[1], "R"),
            ("unison", unison[0], unison[1], "B"), ("inharm", inharm[0], inharm[1], "R"), ("impedance", imped[0], imped[1], "R"),
            ("stretch", stretch[0], stretch[1], "R"), ("condition", cond[0], cond[1], "B")] + list(extra)
PIANO = dict(label="piano", module="Piano", instance=0, adj=PIANO_ADJ, groups=[
    G("Concert Grand", (.8, .5, .8, .1, .5), "Steinway Hall, Bösendorfer Hall, Carnegie, Musikverein, Concertgebouw, Wigmore, Albert Hall, Philharmonie",
      pn(L(0), (.7, 1.8)), [src(.6, VEL, PN_HARD, .1, .3)]),
    G("Baby Grand", (.6, .6, .8, .1, .5), "Salon, Drawing Room, Parlour Grand, Music Room, Studio Grand, Library, Conservatory Grand, Suite",
      pn(L(1), (.7, 1.6)), [src(.5, VEL, PN_HARD, .1, .25)]),
    G("Upright", (.4, .6, .8, .1, .6), "Schoolroom, Pub, Chapel Upright, Rehearsal, Practice Room, Village Hall, Front Room, Tearoom",
      pn(L(2), (.6, 1.5), unison=(1, 2.5)), [src(.5, VEL, PN_HARD, .1, .25)]),
    G("Dream Dull", DREAM, "Felted, Blanket, Muffled Keys, Hushed Keys, Quilted, Padded Keys, Wadded, Swaddled",
      pn(L(3), (.3, .8)), [lfo(.4, PN_PITCH, .02, .04, rate=(.2, .6), shape=SMOOTH)]),
    G("Felt Soft", (.5, .6, .9, .05, .8), "Moleskin, Chamois, Suede, Nubuck, Doeskin, Lambskin, Buckskin, Kidskin",
      pn(L(3, 2), (.25, .6), strike=(.2, .6)), [src(.5, VEL, PN_HARD, .05, .15)]),
    G("Bright Pop", (.7, .7, .5, .2, .3), "Chart, Single, Hook Piano, Chorus Keys, Radio Edit, Pop Grand, Hit Keys, Top Line",
      pn(L(0, 1), (1.4, 3), strike=(-.6, -.1)), [src(.6, VEL, PN_HARD, .15, .35)]),
    G("Honky Tonk", (.3, .5, .4, .2, .3), "Saloon, Barrelhouse, Juke Joint, Speakeasy, Dance Hall, Tavern, Roadhouse, Honky",
      pn(L(2), (.8, 1.6), unison=(3, 6), cond=(1, 2.5))),
    G("Old Upright", (.3, .5, .7, .1, .6), "Attic Keys, Heirloom, Relic, Antique, Keepsake, Memento, Souvenir, Curio",
      pn(L(2, 3), (.6, 1.3), unison=(1.5, 3), cond=(.8, 2)), [lfo(.4, PN_PITCH, .02, .04, rate=(.2, .5), shape=SMOOTH)]),
    G("Tape Keys", (.4, .6, .8, .1, .7), "Lo-Fi, Tape Loop, Dictaphone, Walkman, Reel Keys, Bootleg, Demo Tape, Mixtape",
      pn(L(1, 2, 3), (.5, 1.2)), [lfo(1, PN_PITCH, .03, .06, rate=(.3, 1), shape=SMOOTH), lfo(.5, PN_PITCH, .01, .02, rate=(4, 7))]),
    G("Stretched Bell", (.5, .5, .6, .1, .6), "Bellows Keys, Campanile, Belfry, Steeple, Spire Keys, Minaret, Cupola, Dome Keys",
      pn(L(0, 1), (.8, 1.8), inharm=(1.6, 2.6), stretch=(1.3, 2))),
    G("Metallic Keys", (.4, .6, .4, .3, .5), "Toy Grand, Tin Keys, Brass Keys, Steel Keys, Iron Keys, Copper Keys, Pewter, Alloy",
      pn(L(0, 2), (1, 2.5), inharm=(2, 3), imped=(1.5, 2.5))),
    G("Muted Mellow", (.5, .6, .8, .05, .8), "Twilight Keys, Vesper, Compline, Evensong Keys, Matins, Lauds, Nones, Sext",
      pn(L(3, 1), (.3, .7), imped=(.5, .8))),
    G("Hammered Glass", (.6, .6, .6, .1, .5), "Glass Keys, Crystal Keys, Ice Keys, Frost Keys, Sleet Keys, Quartz Keys, Mica, Selenite",
      pn(L(0, 1), (2, 4), strike=(-.8, -.3)), [src(.6, VEL, PN_HARD, .15, .3)]),
    G("Wide Energy Keys", UP, "Anthem Keys, Rising Keys, Uplift Keys, Soaring Keys, Climbing Keys, Ascent Keys, Surge Keys, Swell Keys",
      pn(L(0), (.8, 2)), [src(1, ENERGY, PN_HARD, .1, .3), src(.6, ENERGY, PN_LVL, .05, .15)]),
    G("Wandering Keys", (.4, .6, .7, .1, .8), "Nomad, Wanderer, Rover, Drifter Keys, Pilgrim, Vagabond, Traveller, Rambler",
      pn(L(1, 2, 3), (.5, 1.3)), [lfo(1, PN_PAN, .3, .7, rate=(.05, .2), shape=SMOOTH), src(.5, RND, PN_PAN, .2, .4)]),
    G("Cinematic Grand", CINE, "Score, Soundtrack, Theme, Leitmotif, Cue Keys, Reel Grand, Credits, Finale",
      pn(L(0), (.6, 1.8)), [src(.7, VEL, PN_HARD, .15, .3), src(.5, ENERGY, PN_HARD, .1, .2), src(.4, WHEEL, PN_PITCH, .03, .05)]),
])

# ================================================================= the strings
STR_ADJ = ["Distant", "Sombre", "Tender", "Warm", "Rich", "Singing", "Radiant", "Soaring"]
def st(players, vib, press, pos, speed, att, rel):
    return [("players", players[0], players[1], "R"), ("vibrato", vib[0], vib[1], "B"), ("pressure", press[0], press[1], "A"),
            ("position", pos[0], pos[1], "A"), ("speed", speed[0], speed[1], "R"), ("attack", att[0], att[1], "B"), ("release", rel[0], rel[1], "R")]
STRINGS = dict(label="strings", module="Strings", instance=0, adj=STR_ADJ, groups=[
    G("Legato Section", CINE, "Adagio, Largo, Lento, Andante, Sostenuto, Legato, Cantabile, Espressivo",
      st((5, 6), (.8, 1.3), (.8, 1.3), (-.2, .2), (.8, 1.2), (80, 250), (150, 400)), [lfo(.6, ST_PRESS, .1, .25, rate=(.1, .3), shape=SMOOTH)]),
    G("Soft Tasto", (.7, .5, .8, .05, .7), "Tasto, Flautando, Dolce, Pianissimo, Sotto Voce, Morendo, Perdendosi, Smorzando",
      st((4, 6), (.5, 1), (.4, .8), (-1, -.5), (.6, .9), (150, 400), (200, 600))),
    G("Ponticello Glass", (.5, .5, .6, .1, .6), "Ponticello, Harmonic, Glassy Bow, Edge, Brink, Rim Bow, Verge, Margin",
      st((4, 6), (.3, .8), (.8, 1.4), (.5, 1), (.8, 1.2), (60, 200), (150, 400)), [lfo(.6, ST_POS, .1, .25, rate=(.1, .4), shape=SMOOTH)]),
    G("Tremolo Rush", (.8, .4, .4, .2, .3), "Tremolo, Shiver, Quiver, Flutter Bow, Tremble, Vibrate, Pulsate, Throb Bow",
      st((5, 6), (.6, 1), (.9, 1.5), (-.1, .3), (1, 1.5), (30, 100), (100, 250)), [lfo(1, ST_PRESS, .4, .7, rate=(10, 16), shape=SAWDN), lfo(.6, ST_LVL, -.3, -.15, rate=(10, 16), shape=SQUARE)]),
    G("Swelling Section", UP, "Crescendo, Swell, Surge Bow, Rise, Welling, Billow Bow, Heave Bow, Groundswell",
      st((5, 6), (.8, 1.2), (.7, 1.2), (-.2, .2), (.8, 1.2), (300, 800), (300, 800)), [menv(1, ST_PRESS, .3, .6, decay=(1500, 5000), attack=(800, 3000), sustain=.8), src(.6, ENERGY, ST_LVL, .1, .25)]),
    G("Solo Violin", (.5, .4, .6, .1, .5), "Stradivari, Guarneri, Amati, Stainer, Gagliano, Guadagnini, Bergonzi, Rugeri",
      st((1, 1), (1, 1.6), (.8, 1.3), (-.2, .3), (.8, 1.2), (60, 180), (120, 300)), [src(.6, VEL, ST_SPEED, .1, .25), src(.5, PRESS, ST_VIB, .3, .6)]),
    G("Chamber Quartet", (.4, .5, .7, .05, .6), "Quartet, Trio, Quintet, Sextet, Octet, Nonet, Chamber, Consort",
      st((2, 3), (.8, 1.3), (.7, 1.2), (-.2, .2), (.8, 1.1), (80, 250), (150, 400))),
    G("Epic Low Strings", CINE, "Cello Section, Bass Section, Contrabass, Violoncello, Low Strings, Double Bass, Viola Section, Tenor Strings",
      st((5, 6), (.6, 1.1), (1, 1.6), (-.1, .3), (.9, 1.3), (60, 200), (200, 500)), [src(.7, ENERGY, ST_PRESS, .15, .3)]),
    G("Dark Sustain", (.4, .6, .6, .1, .8), "Umber, Sepia, Ochre, Sienna, Russet, Mahogany Bow, Walnut Bow, Chestnut",
      st((5, 6), (.4, .8), (.6, 1), (-.6, -.1), (.6, .9), (200, 500), (300, 800))),
    G("Bright Airy", (.8, .5, .7, .1, .5), "Zephyr Strings, Aether, Skylark Strings, Cirrus Strings, Airstream, Wind Strings, Breeze Strings, Sky Strings",
      st((4, 6), (.8, 1.2), (.7, 1.1), (.2, .6), (1, 1.4), (100, 300), (200, 500))),
    G("Vibrato Rich", (.7, .5, .8, .1, .5), "Romantic, Lyrical, Passionate, Ardent, Fervent, Soulful, Heartfelt, Impassioned",
      st((4, 6), (1.4, 2), (.8, 1.2), (-.2, .2), (.8, 1.1), (80, 250), (150, 400)), [src(.8, WHEEL, ST_VIB, .3, .6)]),
    G("Senza Vibrato", (.5, .6, .6, .1, .7), "Pure, Plain Bow, Straight, Senza, Still, Motionless, Frozen Bow, Stasis",
      st((4, 6), (0, .2), (.7, 1.2), (-.2, .2), (.8, 1.1), (100, 300), (200, 500))),
    G("Evolving Bow", (.5, .7, .7, .1, .8), "Metamorphosis, Transfiguration, Transformation, Mutation Bow, Evolve, Becoming, Unfurl, Emerge",
      st((4, 6), (.6, 1.2), (.7, 1.2), (-.3, .3), (.8, 1.1), (200, 500), (300, 700)),
      [lfo(1, ST_POS, .2, .4, sync=BARS4, shape=TRI), lfo(.7, ST_PRESS, .15, .3, sync=BARS2, shape=SMOOTH)]),
    G("Energy Strings", UP, "Momentum Strings, Impetus, Drive Strings, Thrust, Propel, Onrush, Charge Strings, Advance",
      st((5, 6), (.8, 1.2), (.8, 1.3), (-.2, .2), (.8, 1.2), (80, 250), (150, 400)), [src(1, ENERGY, ST_PRESS, .2, .4), src(.7, ENERGY, ST_SPEED, .1, .25)]),
    G("Pizzicato Pad", (.6, .6, .5, .1, .5), "Plucky, Nimble, Deft, Agile, Spry, Brisk, Lively Bow, Sprightly",
      st((3, 6), (.2, .6), (1, 1.5), (0, .4), (1.2, 1.8), (5, 30), (40, 120))),
    G("Wandering Section", DEEP, "Mirage, Illusion, Phantom, Apparition, Spectre, Wraith, Shade, Ghost",
      st((4, 6), (.5, 1), (.5, 1), (-.5, .2), (.7, 1), (300, 800), (400, 1200)),
      [lfo(1, ST_PAN, .3, .6, rate=(.03, .1), shape=SMOOTH), src(.6, RND, ST_POS, .2, .4), lfo(.5, ST_PITCH, .02, .03, rate=(.05, .15), shape=SMOOTH)]),
])

# ================================================================= the choir
CHOIR_ADJ = ["Hollow", "Dusky", "Gentle", "Warm", "Full", "Clear", "Shining", "Heavenly"]
def cr(singers, vowel, vib, breath, tension, att, rel):
    return [("singers", singers[0], singers[1], "R"), ("vowel", vowel[0], vowel[1], "B"), ("vibrato", vib[0], vib[1], "R"),
            ("breath", breath[0], breath[1], "R"), ("tension", tension[0], tension[1], "A"), ("attack", att[0], att[1], "R"), ("release", rel[0], rel[1], "R")]
CHOIR = dict(label="choir", module="Choir", instance=0, adj=CHOIR_ADJ, groups=[
    G("Aah Ensemble", CINE, "Aria, Anthem Choir, Chorale, Cantata, Oratorio, Motet, Madrigal, Requiem",
      cr((5, 6), (0, .15), (.8, 1.2), (.15, .3), (.3, .6), (150, 400), (300, 700))),
    G("Ooh Soft", (.7, .5, .9, .05, .7), "Hum Choir, Croon, Murmur Choir, Lull, Coo, Moan, Sough, Keen",
      cr((4, 6), (.4, .6), (.6, 1), (.2, .4), (.1, .35), (250, 600), (400, 900))),
    G("Uuh Dark", (.4, .5, .6, .1, .8), "Catacomb, Ossuary, Sepulchre, Tomb, Barrow Choir, Mausoleum, Charnel, Reliquary",
      cr((4, 6), (.8, 1), (.4, .8), (.2, .35), (.2, .4), (300, 700), (500, 1200))),
    G("Breathy Whisper", DREAM, "Whisper Choir, Breath Choir, Sigh Choir, Exhale, Inhale, Aspirate, Respire, Waft Choir",
      cr((4, 6), (0, .5), (.4, .8), (.6, 1), (0, .2), (300, 800), (500, 1200))),
    G("Pressed Chant", (.5, .5, .3, .3, .5), "Chant, Drone Chant, Invocation, Incantation, Litany, Kyrie, Sanctus, Agnus",
      cr((5, 6), (0, .3), (.2, .6), (.05, .15), (.7, 1), (100, 300), (200, 500))),
    G("Angelic Airy", UP, "Seraphim, Cherubim, Thrones, Dominions, Virtues, Powers, Principalities, Archangels",
      cr((5, 6), (0, .3), (1, 1.4), (.25, .45), (.2, .45), (200, 600), (400, 1000)), [lfo(.6, CH_VOWEL, .05, .15, rate=(.05, .2), shape=SMOOTH)]),
    G("Solo Voice", (.5, .5, .7, .1, .6), "Soprano, Mezzo, Contralto, Countertenor, Tenor Voice, Baritone, Bass Voice, Treble Voice",
      cr((1, 1), (0, .6), (1, 1.6), (.15, .35), (.3, .6), (100, 300), (200, 500)), [src(.6, PRESS, CH_VIB, .3, .6)]),
    G("Vowel Morph", (.5, .7, .7, .2, .7), "Morphing Voice, Shape Shifter, Chameleon, Proteus, Metamorph Voice, Changeling, Transformer, Shifter",
      cr((4, 6), (0, .5), (.8, 1.2), (.2, .35), (.3, .5), (200, 500), (400, 900)), [lfo(1, CH_VOWEL, .3, .5, sync=BARS2, shape=TRI)]),
    G("Cathedral", CINE, "Notre Dame, Chartres, Cologne, Salisbury, York Minster, Canterbury, Rheims, Amiens",
      cr((6, 6), (0, .4), (.8, 1.2), (.15, .3), (.3, .6), (300, 700), (600, 1500))),
    G("Gregorian Low", (.3, .5, .4, .1, .6), "Gregorian, Plainchant, Cantus, Organum, Neume, Antiphon, Responsory, Gradual",
      cr((5, 6), (.2, .6), (0, .4), (.1, .2), (.5, .8), (150, 400), (300, 700))),
    G("Formant Shift", (.4, .6, .5, .3, .6), "Giant, Dwarf, Titan Voice, Pixie, Troll, Elf, Ogre, Sprite",
      cr((4, 6), (0, .6), (.6, 1), (.2, .35), (.3, .5), (200, 500), (400, 900)), [lfo(1, CH_FORMANT, .3, .6, rate=(.05, .3), shape=SMOOTH)]),
    G("Tremolo Voices", (.5, .5, .5, .2, .4), "Stutter Choir, Pulse Choir, Chop Choir, Beat Choir, Tick Choir, Tap Choir, Clock Choir, Metronome Choir",
      cr((4, 6), (0, .5), (.4, .8), (.2, .35), (.3, .5), (100, 300), (200, 500)), [lfo(1, CH_LVL, -.7, -.4, sync=SIXTEENTH, shape=SQUARE)]),
    G("Epic Chorus", CINE, "Valhalla, Olympus, Asgard, Avalon, Elysian, Empyrean, Celestia, Parnassus",
      cr((6, 6), (0, .3), (1, 1.4), (.1, .25), (.5, .85), (150, 400), (400, 900)), [src(1, ENERGY, CH_TENSION, .2, .4), src(.6, ENERGY, CH_LVL, .1, .2)]),
    G("Dreamy Haze", DREAM, "Sleep, Dreamland, Nod, Somnus, Hypnos, Morpheus, Lethe, Oneiros",
      cr((4, 6), (.3, .7), (.6, 1), (.4, .7), (.05, .25), (500, 1200), (800, 2000)),
      [lfo(.8, CH_VOWEL, .1, .25, rate=(.03, .1), shape=SMOOTH), lfo(.6, CH_PAN, .3, .6, rate=(.03, .1), shape=SMOOTH)]),
    G("Robot Formant", (.3, .5, .3, .5, .4), "Android, Automaton, Cyborg, Droid, Golem, Replicant, Synthetic, Mechanoid",
      cr((1, 3), (0, 1), (0, .2), (0, .1), (.6, .9), (50, 150), (100, 300)), [lfo(1, CH_FORMANT, .4, .8, sync=EIGHTH, shape=SH), lfo(.7, CH_VOWEL, .3, .5, sync=QUARTER, shape=SQUARE)]),
    G("Evolving Mass", (.6, .7, .7, .1, .8), "Mass, Gloria, Credo, Benedictus, Magnificat, Te Deum, Stabat, Miserere",
      cr((5, 6), (0, .5), (.8, 1.2), (.2, .35), (.3, .6), (300, 800), (600, 1500)),
      [lfo(.8, CH_VOWEL, .15, .3, sync=BARS4, shape=TRI), menv(.6, CH_TENSION, .2, .4, decay=(2000, 6000), attack=(1000, 3000), sustain=.6)]),
])

# ================================================================= the brass
BRASS_ADJ = ["Muffled", "Dark", "Mellow", "Round", "Bold", "Brassy", "Blaring", "Triumphant"]
def bs(players, press, blare, att, rel, vib):
    return [("players", players[0], players[1], "R"), ("pressure", press[0], press[1], "A"), ("brassiness", blare[0], blare[1], "A"),
            ("attack", att[0], att[1], "B"), ("release", rel[0], rel[1], "R"), ("vibrato", vib[0], vib[1], "R")]
BRASS = dict(label="brass", module="Brass", instance=0, adj=BRASS_ADJ, groups=[
    G("Braam Blast", CINE, "Braam, Foghorn, Siren Brass, Klaxon Brass, Warhorn, Ram's Horn, Shofar, Alphorn",
      bs((3, 4), (1.2, 1.9), (.5, .9), (20, 80), (200, 500), (0, 3))),
    G("French Horns", CINE, "Cor, Horn Call, Hunting Horn, Waldhorn, Post Horn, Natural Horn, Wagner Tuba, Mellophone",
      bs((3, 4), (.8, 1.3), (.2, .45), (60, 200), (200, 500), (3, 8)), [src(.6, VEL, BR_BREATH, .1, .25)]),
    G("Soft Horns", (.6, .5, .7, .1, .6), "Pastoral Horn, Distant Horn, Echo Horn, Muted Horn, Evening Horn, Sunset Horn, Valley Horn, Lakeside",
      bs((2, 4), (.5, .9), (.05, .25), (150, 400), (300, 700), (3, 8))),
    G("Trumpet Bright", (.7, .4, .3, .2, .2), "Clarion, Trumpet Call, Reveille, Tattoo, Last Post, Tucket, Flourish, Sennet",
      bs((1, 3), (1.1, 1.7), (.4, .75), (20, 80), (150, 400), (4, 10)), [src(.6, VEL, BR_BLARE, .1, .25)]),
    G("Trombone Low", (.6, .4, .3, .2, .3), "Sackbut, Slide, Trombone Section, Bass Bone, Tenor Bone, Valve Bone, Euphonium, Baritone Horn",
      bs((2, 4), (.9, 1.5), (.3, .6), (40, 120), (200, 500), (2, 6))),
    G("Brass Stabs", UP, "Punch Brass, Hit Brass, Short Brass, Staccato Brass, Blat, Burst Brass, Pop Brass, Bark",
      bs((3, 4), (1.2, 1.8), (.4, .8), (5, 25), (60, 150), (0, 2))),
    G("Swell Pad Brass", (.7, .5, .6, .1, .6), "Swell Brass, Bloom Brass, Rising Brass, Tide Brass, Wave Brass, Surge Brass, Flood Brass, Welling Brass",
      bs((3, 4), (.7, 1.2), (.15, .4), (300, 800), (400, 1000), (2, 6)), [menv(1, BR_BREATH, .3, .6, decay=(1500, 4000), attack=(500, 2000), sustain=.7)]),
    G("Fanfare", UP, "Herald Call, Proclamation, Coronation, Procession, Pageant, Jubilee Call, Cortege, Parade Call",
      bs((3, 4), (1.3, 1.9), (.5, .85), (15, 50), (150, 400), (0, 4)), [src(1, ENERGY, BR_BLARE, .1, .25)]),
    G("Muted Warm", (.5, .6, .6, .1, .6), "Cup Mute, Harmon, Plunger, Bucket Mute, Straight Mute, Wah Brass, Solotone, Practice Mute",
      bs((2, 3), (.6, 1), (.05, .2), (60, 200), (150, 400), (4, 10))),
    G("Epic Low Brass", CINE, "Tuba Section, Contrabass Tuba, Cimbasso, Sousaphone, Helicon, Ophicleide, Serpent, Bass Horn",
      bs((3, 4), (1.1, 1.7), (.45, .8), (40, 150), (300, 700), (0, 3)), [src(.8, ENERGY, BR_BREATH, .1, .25)]),
    G("Vibrato Solo", (.5, .4, .6, .1, .5), "Soloist, Virtuoso, Maestro, Principal, Leader, Concertino, Cantor, Troubadour",
      bs((1, 1), (.9, 1.4), (.2, .5), (40, 120), (150, 400), (8, 18)), [src(.7, PRESS, BR_VIB, .3, .6), lfo(.5, BR_BREATH, .05, .15, rate=(4.5, 6), fade=(.4, 1))]),
    G("Fall-Off", (.6, .4, .3, .3, .3), "Fall, Drop Brass, Doit, Plop, Spill, Tumble Brass, Slump, Sag",
      bs((2, 4), (1, 1.5), (.3, .6), (20, 60), (150, 400), (0, 4)), [menv(1, BR_PITCH, -.35, -.2, decay=(300, 900), attack=(200, 600))]),
    G("Growl Brass", (.4, .4, .2, .6, .2), "Growl Horn, Snarl, Rumble Horn, Roar, Bellow, Blare Beast, Grunt, Rasp Horn",
      bs((2, 4), (1.2, 1.8), (.6, 1), (20, 80), (150, 400), (0, 4)), [lfo(1, BR_BLARE, .2, .4, rate=(6, 12), shape=SH)]),
    G("Breath Swell", DREAM, "Breath Horn, Exhale Horn, Sigh Horn, Air Horn Soft, Mist Horn, Vapour Horn, Wind Horn, Draught Horn",
      bs((2, 4), (.5, .9), (0, .2), (400, 1000), (500, 1200), (2, 6)), [lfo(.8, BR_BREATH, .1, .25, rate=(.05, .2), shape=SMOOTH)]),
    G("Section Wide", (.7, .5, .5, .2, .4), "Tutti, Ensemble Brass, Full Section, Massed, Choir Brass, Band, Corps, Consort Brass",
      bs((4, 4), (1, 1.5), (.3, .6), (40, 120), (200, 500), (2, 6)), [src(.6, RND, BR_PAN, .2, .5)]),
    G("Dark Tuba", (.4, .5, .3, .2, .6), "Leviathan Brass, Behemoth Brass, Kraken, Colossus Brass, Mammoth, Mastodon, Titan Brass, Giant Brass",
      bs((2, 4), (.9, 1.4), (.2, .5), (60, 200), (300, 700), (0, 3))),
])

# ================================================================= the timpani
TIMP_ADJ = ["Muffled", "Deep", "Soft", "Warm", "Resonant", "Firm", "Crisp", "Thundering"]
def tp(hard, decay, strike):
    return [("hardness", hard[0], hard[1], "A"), ("decay", decay[0], decay[1], "B"), ("strike", strike[0], strike[1], "R")]
TIMPANI = dict(label="timpani", module="Timpani", instance=0, adj=TIMP_ADJ, groups=[
    G("Orchestral", CINE, "Kettle, Pauke, Timbale, Nakers, Tympanum, Copper Drum, Bowl Drum, Pedal Drum", tp((.7, 1.5), (.8, 1.2), (.6, .75))),
    G("Soft Mallet", (.6, .5, .7, .05, .6), "Lambswool, Felt Ball, Cork, Flannel, Plush, Chenille, Terry, Velour", tp((.3, .7), (.8, 1.3), (.6, .75))),
    G("Hard Mallet", (.8, .4, .3, .2, .3), "Wooden Mallet, Maple Stick, Hickory, Rattan, Bamboo, Ash Stick, Oak Stick, Beech", tp((2, 4), (.6, 1), (.6, .75))),
    G("Boomy", CINE, "Boom, Roll Thunder, Cannonade, Detonation, Blast Drum, Explosion, Eruption, Report", tp((.8, 1.6), (1.5, 2.5), (.5, .7))),
    G("Tight", (.7, .5, .4, .3, .3), "Snap Drum, Tight Head, Taut, Tense, Drumhead, Skin, Batter, Membrane", tp((1, 2), (.3, .6), (.65, .8))),
    G("Pedal Glide", CINE, "Glissando, Portamento, Slide Drum, Bend Drum, Swoop, Dive, Plunge, Soar", tp((.8, 1.5), (1, 1.6), (.6, .75)),
      [menv(1, TI_PITCH, .3, .6, decay=(400, 1500), attack=(.1, 20))]),
    G("Rolling", (.8, .4, .4, .1, .3), "Drum Roll, Rumble Roll, Tremolo Drum, Thunder Roll, Roll Call, Buzz Roll, Double Stroke, Long Roll",
      tp((.6, 1.2), (1, 1.6), (.6, .75)), [src(.7, RND, TI_STRIKE, .1, .2), src(.5, RND, TI_HARD, .1, .2)]),
    G("Cinematic Hit", CINE, "Impact Drum, Epic Hit, Trailer Hit, Doom, Titan Drum, Hammerfall, Warbeat, Onslaught", tp((1.2, 2.5), (1.2, 2), (.55, .7))),
    G("Center Strike", (.5, .4, .4, .2, .5), "Centre, Bullseye, Core Strike, Middle, Heart Drum, Hub Strike, Nucleus, Kernel", tp((.8, 1.5), (.7, 1.2), (.3, .45))),
    G("Edge Strike", (.6, .5, .5, .1, .5), "Rim Drum, Edge Drum, Perimeter, Rim Shot Timp, Border, Fringe, Periphery, Brim", tp((.8, 1.5), (.8, 1.2), (.78, .9))),
    G("Long Ring", (.5, .5, .6, .1, .7), "Resonance Drum, Ring Drum, Sustain Drum, Linger, Echo Drum, Reverberate, Hover, Sustain", tp((.6, 1.2), (2, 3), (.6, .75))),
    G("Dry Short", (.6, .6, .4, .3, .4), "Dry Drum, Deadened, Damped, Choked, Muted Drum, Stopped, Stifled, Clipped", tp((.8, 1.6), (.3, .5), (.6, .75))),
    G("Epic Low", CINE, "Abyss Drum, Deep Drum, Underworld, Hades, Tartarus, Erebus, Styx, Acheron", tp((.8, 1.8), (1.3, 2.2), (.55, .7)),
      [src(.8, ENERGY, TI_HARD, .15, .3)]),
    G("Tuned Toms", (.6, .7, .5, .2, .6), "Tabla Timp, Tom Timp, Roto, Octoban, Melodic Drum, Pitched Drum, Tuned Skin, Tonal Drum", tp((1, 2), (.5, .9), (.6, .8))),
    G("Thunder", CINE, "Thunderhead, Storm, Tempest, Squall, Gale, Cyclone, Typhoon, Hurricane", tp((1.5, 3), (1.5, 2.5), (.5, .7)),
      [src(.6, RND, TI_DECAY, .1, .3)]),
    G("Taiko-ish", (.7, .6, .3, .3, .4), "Odaiko, Shime, Nagado, Okedo, Katsugi, Hira, Tsuke, Uchiwa", tp((1.5, 3), (.6, 1), (.4, .6)),
      [src(.6, VEL, TI_HARD, .15, .3)]),
])

# ================================================================= the effects
SFX_ADJ = ["Shadowy", "Dark", "Hazy", "Smooth", "Wide", "Bright", "Glittering", "Blazing"]
def fx(noise, res, bright, imp, vowel, swell, width, subl, fam):
    ks = [("noise", noise[0], noise[1], "R"), ("resonance", res[0], res[1], "R"), ("brightness", bright[0], bright[1], "A"),
          ("impact_decay", imp[0], imp[1], "B"), ("vowel", vowel[0], vowel[1], "R"), ("swell_decay", swell[0], swell[1], "B"),
          ("width", width[0], width[1], "R"), ("sub_level", subl[0], subl[1], "R")]
    for k, lo, hi, *_ in fam:
        ks.append((k, lo, hi, "R"))
    return ks
SFX = dict(label="sfx", module="Sfx", instance=0, adj=SFX_ADJ, groups=[
    G("White Risers", UP, "Liftoff, Launch, Ascent FX, Takeoff, Blastoff, Uplift FX, Elevator, Escalator",
      fx((.7, 1), (.2, .5), (.3, 1), (800, 2000), (0, .3), (800, 2500), (.6, 1), (-20, -12), [("preset_riser", 1, 64, 0)])),
    G("Tonal Risers", UP, "Crescendo FX, Climb FX, Gain, Swell FX, Raise, Hoist, Heave FX, Boost FX",
      fx((.1, .4), (.4, .8), (.3, 1), (800, 2000), (0, .5), (800, 2500), (.5, .9), (-20, -12), [("preset_riser", 65, 256, 0)])),
    G("Big Impacts", CINE, "Collision, Crash FX, Slam, Wallop, Thud FX, Clobber, Smash, Pound",
      fx((.4, .8), (.3, .6), (.2, .8), (1500, 4000), (0, .4), (800, 2000), (.6, 1), (-14, -6), [("preset_impact", 1, 128, 0)])),
    G("Soft Impacts", DREAM, "Landing, Touchdown, Settle, Alight, Arrive, Dock FX, Moor, Berth",
      fx((.2, .5), (.2, .5), (.1, .5), (800, 2000), (0, .4), (800, 2000), (.4, .8), (-24, -14), [("preset_impact", 1, 128, 0)])),
    G("Downlifters", (.8, .6, .5, .3, .5), "Descent, Dive FX, Plummet, Fall FX, Sink, Subside, Ebb, Wane",
      fx((.5, .9), (.2, .5), (.3, .9), (800, 2000), (0, .4), (1000, 3000), (.5, .9), (-20, -12), [("preset_downlifter", 1, 128, 0)])),
    G("Sweeps", PROG, "Swoosh, Whoosh, Swish, Sweep FX, Rush, Zoom, Whizz, Sough FX",
      fx((.7, 1), (.3, .7), (.2, .8), (800, 2000), (0, .5), (1000, 4000), (.6, 1), (-24, -14), [("preset_sweep", 1, 256, 0)])),
    G("Reverse Swells", (.7, .7, .7, .2, .7), "Inhale FX, Suction, Implosion, Reverse, Rewind, Backdraft, Undertow FX, Retreat",
      fx((.4, .8), (.3, .6), (.2, .8), (800, 2000), (0, .5), (1500, 5000), (.5, .9), (-24, -14), [("preset_reverse_swell", 1, 256, 0)])),
    G("Formant Shots", (.5, .5, .4, .5, .4), "Vox Shot, Yell FX, Hey, Oi, Ahh FX, Ooh FX, Yeah, Whoa",
      fx((.1, .4), (.4, .8), (.3, .9), (500, 1500), (0, 1), (500, 1500), (.4, .8), (-30, -20), [("preset_formant_shot", 1, 96, 0)])),
    G("Zaps", (.4, .5, .2, .8, .3), "Zap, Laser FX, Phaser FX, Blaster, Ray Gun, Pulse Gun, Plasma, Ion",
      fx((0, .3), (.5, .9), (.4, 1), (300, 1000), (0, .4), (300, 1000), (.4, .8), (-30, -20), [("preset_zap", 1, 96, 0), ("preset_squelch", 1, 128, 0)])),
    G("Atmospheres", DEEP, "Nebula FX, Cosmos, Void FX, Ether FX, Aurora FX, Starfield FX, Galaxy, Deep Space",
      fx((.3, .7), (.2, .6), (.1, .6), (1500, 4000), (0, .6), (2000, 6000), (.7, 1), (-30, -20), [("preset_atmosphere", 1, 512, 0)])),
    G("Sub Drops", (.8, .6, .4, .5, .5), "Sub Boom, Low Drop, Bass Drop FX, Earthquake FX, Rumble Drop, Undertone, Infrasonic, Low End Drop",
      fx((.2, .5), (.2, .5), (.1, .5), (1500, 4000), (0, .4), (1000, 3000), (.3, .6), (-10, -4), [("preset_impact", 1, 128, 0)])),
    G("Wandering FX", (.6, .7, .7, .2, .8), "Nomad FX, Roam, Stray, Meander, Ramble FX, Saunter, Wend, Traverse",
      fx((.4, .8), (.3, .6), (.2, .8), (800, 2500), (0, .5), (1000, 4000), (.6, 1), (-24, -14), [("preset_sweep", 1, 256, 0)]) + [("wander", 1, 1, "C"), ("wander_send", .6, 1, "R")]),
    G("Bubbling FX", ACID, "Bubbles, Fizzle, Gurgle FX, Percolate, Simmer, Boil, Seethe, Burble FX",
      fx((0, .3), (.5, .9), (.3, .9), (300, 1000), (0, .5), (300, 1000), (.4, .8), (-30, -20), [("preset_bubble", 1, 64, 0), ("preset_squelch", 1, 128, 0)])),
    G("Reverse Crashes", UP, "Backwash, Rewind Crash, Suck Back, Reverse Cymbal, Pullback, Inrush, Undertow Crash, Drawback",
      fx((.6, 1), (.2, .5), (.4, 1), (800, 2000), (0, .3), (1000, 3000), (.6, 1), (-24, -14), [("preset_reverse_crash", 1, 128, 0)])),
    G("Dark Cinematic", CINE, "Omen, Portent, Foreboding, Dread, Menace, Doomsday, Apocalypse, Armageddon",
      fx((.3, .7), (.3, .7), (.1, .5), (2000, 4000), (0, .6), (2000, 6000), (.6, 1), (-14, -8), [("preset_impact", 1, 128, 0), ("preset_atmosphere", 1, 512, 0)])),
    G("Bright Trance FX", UP, "Glitter, Sparkle FX, Twinkle, Shimmer FX, Glint FX, Dazzle, Coruscate, Scintilla",
      fx((.5, .9), (.3, .6), (.6, 1), (800, 2000), (0, .4), (800, 2500), (.7, 1), (-24, -16), [("preset_riser", 1, 256, 0), ("preset_sweep", 1, 256, 0)])),
])

# ================================================================= the granular cloud
CLOUD_ADJ = ["Faint", "Dim", "Misty", "Soft", "Drifting", "Glinting", "Shimmering", "Radiant"]
def cl(density, size, pitch, spray):
    return [("density", density[0], density[1], "A"), ("size", size[0], size[1], "B"), ("pitch", pitch[0], pitch[1], "R"), ("spray", spray[0], spray[1], "R")]
CLOUD = dict(label="cloud", module="Cloud", instance=0, adj=CLOUD_ADJ, groups=[
    G("Fine Dust", DEEP, "Dust, Powder, Talc, Pollen Dust, Soot, Ash, Spore, Flour", cl((20, 50), (30, 90), (.1, .3), (1, 3))),
    G("Shimmer", DREAM, "Shimmer Cloud, Sheen Cloud, Gleam, Glimmer, Twinkle Cloud, Scintillate, Sparkle Cloud, Glisten", cl((10, 30), (100, 300), (.6, .9), (1, 3))),
    G("Frozen", DEEP, "Freeze, Stasis Cloud, Ice Cloud, Suspended, Paused, Arrested, Held, Stilled", cl((5, 15), (400, 1000), (.2, .4), (.1, .5))),
    G("Sparse Drops", (.3, .5, .7, .1, .8), "Drip, Trickle, Seep, Ooze, Leak, Weep, Exude, Distil", cl((.5, 4), (80, 250), (.2, .5), (2, 4.5))),
    G("Dense Fog", DEEP, "Fog, Pea Souper, Smog Cloud, Brume, Murk, Gloom Cloud, Mizzle, Haar", cl((30, 60), (200, 600), (.1, .3), (2, 4))),
    G("Long Grains", (.3, .5, .7, .1, .9), "Grain Field, Wheat, Barley, Oats, Rye, Millet, Spelt, Sorghum", cl((3, 10), (500, 1000), (.2, .5), (1, 3))),
    G("Pitched Up", (.5, .5, .8, .1, .6), "Octave Cloud, Fifth Cloud, Overtone Cloud, Harmonic Cloud, Partial, Upper Cloud, Treble Cloud, Choir Cloud", cl((8, 25), (150, 400), (.7, 1), (1, 3))),
    G("Scattered", (.3, .6, .6, .2, .8), "Scatter, Diffuse, Disperse, Strew, Spread, Broadcast, Sow, Sprinkle Cloud", cl((5, 20), (60, 200), (.2, .6), (3, 4.5))),
    G("Close Haze", DREAM, "Close Mist, Near Haze, Intimate, Nearby, Adjacent, Beside, Proximate, Neighbour", cl((10, 25), (150, 350), (.2, .4), (.1, .8))),
    G("Wide Spray", (.4, .6, .7, .1, .8), "Spray Cloud, Spume Cloud, Mist Spray, Atomise, Nebulise, Vaporise, Aerosol, Plume", cl((15, 40), (100, 300), (.3, .6), (3.5, 4.5))),
    G("Tidal Cloud", DEEP, "Neap, Spring Tide, Ebb Cloud, Flow, Flux Cloud, Rip, Surf Cloud, Swell Cloud", cl((8, 20), (300, 700), (.2, .5), (2, 4))),
    G("Crystal Grains", (.4, .5, .8, .1, .6), "Salt, Sugar, Crystal Grain, Sequin, Glitter Grain, Spangle, Bead Grain, Seed Pearl", cl((15, 40), (30, 100), (.6, .9), (1, 3))),
    G("Dark Grains", DEEP, "Tar, Pitch, Bitumen, Charcoal, Graphite, Obsidian Grain, Jet, Ink", cl((10, 30), (150, 500), (0, .2), (1.5, 3.5))),
    G("Rhythmic Grains", (.4, .7, .5, .3, .6), "Tick Cloud, Patter Cloud, Tap Cloud, Drip Cloud, Pitter, Rattle Cloud, Clatter Cloud, Stutter Cloud", cl((30, 60), (20, 60), (.3, .6), (.5, 2))),
    G("Evolving Nebula", (.4, .6, .7, .1, .9), "Crab, Orion, Horsehead, Eagle, Lagoon Nebula, Helix Nebula, Ring, Cat's Eye", cl((5, 25), (200, 800), (.2, .7), (1, 4))),
    G("Balanced Cloud", ALL, "Cumulus, Stratus, Altocumulus, Nimbostratus, Cirrostratus, Altostratus, Cumulonimbus, Stratocumulus", cl((8, 20), (150, 400), (.2, .5), (1.5, 3.5))),
])

BANKS = [KICK, SUB, PERC, BASS, ACID_BANK, LEAD, COUNTER, PLUCK, ARP, PAD, STAB, PIANO, STRINGS, CHOIR, BRASS, TIMPANI, SFX, CLOUD]
