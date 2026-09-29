# Parhelion

A generator for trance: tracks and DJ sets composed from a seed and synthesised in real time -- in the making. The
sibling of [Noctuary](../AmbientSynth) (ambient), Phosphene (psytrance), Ephemeris (Berlin School) and Totality (techno).

A parhelion, a sun dog, is the bright spot of light beside the sun when it shines through ice crystals: a centre and
its detuned neighbours, which is what the JP-8000's supersaw is.

**State (29.09.2026): Phases 0 and 1.** The frame (copied modules with their origin in every header), the parameter
system, the score with sections, the layer matrix and the ghost kick, the deck with kick, sub, mid-bass, 303, a
twelve-lane kit, six polyphonic voices (Phosphene's JP-8000 supersaw, VA, FM and wavetable, the circuit filters, the
trance gate with its classic sixteenth masks), the pump on every bus, three rooms (room, plate, hall), the master, and
a fixed study of 104 bars in the form of the research document (intro, groove, breakdown, build with its snare roll,
drop, outro). The planner, the melody, the sub-genres, the physical piano and orchestra, the set, the plugin and the
Quest follow (docs/PLAN.md, section 14).

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
```

`parh_render` prints the plan (sections and the layer matrix), the loudness of the whole and of every section, and the
distance between the breakdown's and the drop's loudest three seconds (the research document's 4 to 8 LU). The WAV
carries a cue marker at every section; `--stems dir` writes a WAV per part, their sum the mix before the master;
`--list` prints every parameter, `--set "key=value; ..."` changes them.

## Licence

AGPL-3.0, like the siblings (see LICENSE).
