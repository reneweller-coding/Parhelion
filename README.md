# Parhelion

A generator for trance: tracks and DJ sets composed from a seed and synthesised in real time -- in the making. The
sibling of [Noctuary](../AmbientSynth) (ambient), Phosphene (psytrance), Ephemeris (Berlin School) and Totality (techno).

A parhelion, a sun dog, is the bright spot of light beside the sun when it shines through ice crystals: a centre and
its detuned neighbours, which is what the JP-8000's supersaw is.

**State (29.09.2026): Phases 0 to 4a.** The frame (copied modules with their origin in every header), the parameter
system, the score with sections, the layer matrix and the ghost kick, the deck with kick, sub, mid-bass, 303, a
twelve-lane kit, six polyphonic voices (Phosphene's JP-8000 supersaw, VA, FM and wavetable, the circuit filters, the
trance gate with its classic sixteenth masks), the pump on every bus, three rooms (room, plate, hall), the master.
Whole tracks in five styles (Uplifting, Progressive, Dream House, Acid, Deep), calibrated against 30 measured reference
tracks: a planner that writes the form and the layer matrix (the breakdown with its tease and peak, the build, the empty
beat before the drop), the harmony, the drums, the bass, the pad, the melody -- the lead's motif drawn from statistics
of royalty-free MIDI packs under the rules as hard constraints (Pachet and Roy's constrained sampling), in its versions
from the tease to the main drop, and checked bar by bar against 144250 windows of transcriptions of famous tracks so
that no known motif comes out -- the effects and the automation, and a leveler that sets the drop's loudness and the
breakdown's distance to it. A physical piano after Pianoteq's principle (the soundboard solved from plate physics,
coupled strings as complex modes, a hysteretic hammer, the strings' tension and longitudinal modes, dampers, half pedal,
the strings ringing along under the pedal) plays Dream House's motif. The orchestra, the set, the plugin and the Quest
follow (docs/PLAN.md, section 14).

The plan, in German, with the reasons for everything: [docs/PLAN.md](docs/PLAN.md). The research it rests on:
[docs/research](docs/research).

## Build

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
cd build && ctest -C Release
```

## Render

```
build/Tools/render/Release/parh_render --seed 7 --out study.wav --midi study.mid --stems stems
build/Tools/render/Release/parh_render --seed 7 --set "compose.key=F; compose.bpm=136" --out study_f.wav
build/Tools/render/Release/parh_render --seed 7 --plan
build/Tools/render/Release/parh_render --seed 7 --style "Dream House" --out dream.wav --midi dream.mid
build/Tools/render/Release/parh_render --seed 7 --reroll melody --out other_melody.wav
build/Tools/render/Release/parh_render --study --out study.wav
```

```
build/Tools/pianoprobe/Release/parh_pianoprobe --info
build/Tools/pianoprobe/Release/parh_pianoprobe --measure
build/Tools/pianoprobe/Release/parh_pianoprobe --notes "36:0.8:3,60:0.5:2" --pedal 1 --out notes.wav
```

`parh_pianoprobe` shows the piano's design (the board's modes, every register's strings), measures what phase 4a asks
for (inharmonicity, the two-stage decay, the brightness over the velocity, the pitch glide, the cost) and renders single
notes. `parh_render` prints the plan (sections and the layer matrix), the loudness of the whole and of every section, and the
distance between the breakdown's and the drop's loudest three seconds (the research document's 4 to 8 LU). The WAV
carries a cue marker at every section; `--stems dir` writes a WAV per part, their sum the mix before the master;
`--list` prints every parameter, `--set "key=value; ..."` changes them.

## Licence

AGPL-3.0, like the siblings (see LICENSE).
