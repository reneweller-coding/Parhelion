# Parhelion release notes

## 1.0.0 (30.09.2026)

The first release: a generator of trance -- tracks and DJ sets composed from a seed and synthesised in real time -- as a
VST3 plugin, a standalone application for Windows, a command-line renderer and an app for Meta Quest.

**Composing.** Five styles, Uplifting (with a Cinematic side: the orchestra in the main breakdown, a key change),
Progressive, Dream House, Acid and Deep, which morph into each other. A planner writes the form -- intro, groove, the
breakdown with its tease and its peak, the build, the empty beat before the drop, the drops, the outro -- and the layer
matrix, one cell per part and 8-bar block, with an energy curve over it; the harmony on the core progressions of the
genre (i-VI-III-VII and its relatives), the VII before every drop and its resolution. The intro opens on an atmosphere:
the pad filtered dark from the first bar and opening, the effects' swell, reverse swells into the lines, the arp
ghosting through; a track alone may open without the kick. The lead's motif is drawn from statistics of royalty-free
MIDI under the rules as hard constraints, in its versions from the tease to the main drop (A A' B A''), and checked
bar by bar against windows of transcriptions of famous tracks so that no known motif comes out; the counter answers
it, the pluck, the arp and the stab carry the chords. Dream House's piano plays the motif with its left hand under it;
Acid's 303 rises in four curves to the filter's peaks; Deep's pad drifts and a grain cloud breathes its past;
Progressive rolls. Every part on its own seed stream, each can be rerolled alone; the ratings (+ and -) weigh the forms
and sounds with Favor Ratings.

**Sets.** DJ sets of any length from one seed: five dramaturgies (Warm-up, Peak, Closing, Sunrise, Journey), tempo and
energy over the set, harmonic mixing round the Camelot wheel, blends over the tracks' DJ intros and outros with one
bass swap, the next track's hook teased on a third deck, the mixer's echo and hall.

**Sound.** A kick tuned to the key, sine sub and off-beat mid-bass, a 303, twelve kit lanes, six polyphonic voices (a
JP-8000 supersaw, VA, FM and wavetable oscillators through ten circuit-modelled filters, the trance gate), a physical
piano after Pianoteq's principle -- the soundboard from plate physics, coupled strings, a hysteretic hammer, dampers,
half pedal, sympathetic strings -- voiced note by note against Pianoteq's Steinway D, an orchestra synthesised as well
(a section of bowed modal strings, a choir from a glottal source, brass from lips on a bore, timpani), risers,
impacts, three rooms, the pump on every bus, a track bus, a DJ mixer, a master with a 4x clipper and a true-peak limiter
at -1 dBTP, and a leveler that sets every part against the kick and each track's drop to its style's loudness, the
breakdown at its distance under it. Calibrated against thirty measured reference tracks (Tools/calibrate.py): tempo,
length, loudness and its range, the breakdown's share and distance, the balance of the bands, the width, the pump, the
rise into the drop, the atmosphere of the intros.

**Presets.** Eighteen factory banks of 1024 presets -- kick, sub, kit, bass, 303, lead, counter, pluck, arp, pad, stab,
piano, strings, choir, brass, timpani, effects, cloud --, sixteen groups each, every preset with its modulation (an
envelope, four LFOs, an eight-slot matrix fed by velocity, key, the wheel, pressure and the score's energy) and a
measured level trim. The composer chooses one per synth for every track by its style; every page names the preset of the
track that plays; your own presets beside them.

**Playing.** In a DAW Parhelion follows the host's transport and tempo. The Perform page: seven mutes, the master
filter, the echo throw, the mod wheel, the isolators and faders per deck, "Breakdown now" and "Drop now", which rewrite
the playing track from its next 8-bar line (a second keeps the first); every control learnable from a MIDI controller.
The Arrange page shows the layer matrix and the energy curve.

**Export.** A 24-bit WAV with cue markers, the cues as JSON and a rekordbox collection (beat grid and cues), MIDI with
the tempo map and each voice's preset as its program change, stems whose sum is the mix before the master, DJ loops,
`.parhset` files; OSC cues for a visualiser while it plays (`/parh/beat`, `/parh/bar`, `/parh/block`, `/parh/op`,
`/parh/key`).

**Meta Quest.** The whole generator on the headset, played with the hands (pinches for play, kick out and the next
track, the right pinch held for "Breakdown now" and "Drop now", handed over to a second engine on a beat without a gap;
the hands' height for the filter and the throw), under the parhelion in the sky: the sun swelling with the kick, the
halo the bar, the sun dogs burning with the energy, the track along the parhelic circle. The headset's quality plays
fewer unison oscillators, voices and players and solves the voices' filter models in one Newton step (each voice within
0.3 dB of the desktop's). The NEON path passed the vector test bit for bit on arm64 (in the Android emulator). **Built,
not yet run on a headset.** `Quest\build_tools.ps1` builds the renderer and the tests for it, to measure there.

**Performance.** A 24-minute set with its blends takes about a tenth of one desktop core (AVX2, bit for bit the same
render at any block size and on every vector path).

**Tested.** 135 self-test checks in 31 sections, the vector paths (AVX2, NEON, scalar) bit for bit, the VST3 loaded as a
host loads it, pluginval at strictness 10.

**Known.** The Quest's share of a core is estimated, not measured (29 to 41 % for a set against the plan's 30 %). The
plugin's "now" still loads the rewritten track in place (a short silence, held notes stop); the Quest's handover could
serve it. The piano's middle register is brighter than the Steinway's (its partials 2 to 5). Against the references,
Dream House stays under their corridor in the low mids and the top octaves on some tracks, and Deep's loudness range is
smaller than theirs. Whether rekordbox, Traktor and Serato read everything as written has not been tried with them.
