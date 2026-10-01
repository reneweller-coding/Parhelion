# Parhelion for Meta Quest

The whole generator on the headset: the composer writes a track or a set, the engine synthesizes it, and the hands play
it like hands at a mixer -- including the performer's "Breakdown now" and "Drop now". Native OpenXR, no game engine --
`NativeActivity` + `android_native_app_glue`, EGL, GLES 3, the Khronos OpenXR loader, `XR_EXT_hand_tracking`, Oboe, and
the unchanged core from `../Core`. The frame of the app (session, swapchains, point renderer, font, hands, audio stream)
is Totality's Quest app (c773d65), itself after Ephemeris' and Phosphene's; the player with its two engines, the
performer's "now", the panel and the parhelion in the sky are Parhelion's.

```
Quest/
  CMakeLists.txt        NDK build of libparhquest.so (links ParhelionCore, oboe, openxr_loader)
  AndroidManifest.xml   NativeActivity, hasCode=false, hand-tracking permission/features, VR category
  src/main.cpp          the app: OpenXR session, composer thread, Oboe, hand controls, the panel and the parhelion
  res/mipmap-*/         the launcher icon at five densities (Deploy/make_icon.py)
  fetch_thirdparty.ps1  OpenXR loader (prefab AAR) and Oboe into ../ThirdParty (junctions into Noctuary's)
  build_apk.ps1         CMake/NDK -> aapt2 -> jar -> zipalign -> apksigner (debug key)
  build_tools.ps1       parh_render, parh_selftest and parh_vectest for arm64, to run on the headset through adb
```

## Build

```powershell
powershell -File Quest\fetch_thirdparty.ps1
powershell -File Quest\build_apk.ps1
adb install -r build-quest\ParhelionQuest.apk
```

Needs NDK r27 (`C:\Android-Buildtools\sdk\ndk\27.2.12479018`), build-tools 34, platform android-34 and JDK 17 -- the
parameters at the top of `build_apk.ps1`. No Gradle and no ninja. The APK is 6.8 MB: Parhelion ships no data, every
sound is synthesised.

Built on 30.09.2026 without a headset attached: it compiles and links for arm64 without a warning and the APK is signed,
but it has not run on a device yet. The core's NEON path, compiled for arm64 by the NDK, passed the vector test bit for
bit against the scalar path in the Android emulator (an x86_64 image of Android 14 that runs arm64 programs through its
translation layer: 10 of 10).

## On the headset

`build_tools.ps1` builds the renderer and the tests for arm64. With the headset attached (developer mode):

```
adb push build-quest-tools\Tests\parh_vectest build-quest-tools\Tools\render\parh_render /data/local/tmp/
adb shell chmod +x /data/local/tmp/parh_vectest /data/local/tmp/parh_render
adb shell /data/local/tmp/parh_vectest
adb shell /data/local/tmp/parh_render --seed 5 --dj 24 --bench --quality quest
```

The vector test checks the NEON path on the Quest's own core; `--bench` says what a 24-minute set with its blends costs
one core of it (PLAN 10: under 30 % in the Quest's quality).

## Three threads, two engines

| Thread | Does |
|---|---|
| Audio (Oboe, low latency, exclusive) | `TrackPlayer::process`: the front engine through the handover (`Handover.h`) and the play/stop fade; publishes beat, seconds and level. No lock, no allocation, never waits. |
| Composer | composes a whole track (or set) and loads it before the stream starts; "next" composes the next one into the back engine, which the audio thread swaps in behind a fade; "now" composes the rewritten track into the back engine, pre-rolls it and hands over on a beat; then measures the loudness while it plays. |
| Render (the glue thread) | OpenXR frame loop, hands, gestures, picture. |

The composer only ever loads the engine that does not play, and only while the audio thread has no use for it; the
audio thread alone decides which engine plays. The knobs the hands play are written into both engines.

**"Now" without a gap.** The plugin loads a rewritten track into the engine that plays and jumps to where it was (a short
silence, and the notes that were held stop). Here the rewritten track -- the old one up to its 8-bar line, the
composer's planTrackRewritten -- goes into the second engine, which starts four bars back and is rendered up to where
the first one plays (its voices, rooms and echoes are then the first one's), then a little beyond, to the next beat. The
audio thread plays the first up to that sample and fades to the second in 21 ms. The handover begins before the first
note the two tracks play differently, on a beat, where the kick hides that the two engines' free-running oscillators are
not in phase (measured in the self test: no click, the level at most 0.24 dB under the quieter engine through the fade,
and after it the second engine bit for bit as it plays alone). A second "now" keeps the first (the track is rewritten
from both lines). The line is the next 8-bar line at least 10 s ahead, time for the composer and the pre-roll on the
headset's cores; a set is not rewritten (its next blend would have to move with it).

## Playing it

The engines play live (`Engine::setLive`): the perform module acts, as in the plugin.

| Gesture | Effect |
|---|---|
| left pinch | play / stop -- a 15 ms fade, the music pauses where it is |
| right pinch | kick out / kick in (`perform.mute_kick`) |
| right pinch held 0.6 s | "Breakdown now" in an intro, groove, drop or outro; "Drop now" in a breakdown, break or build |
| both hands pinched together | the next track (or set), from the next seed, swapped in behind a fade |
| left hand height | the master filter (`perform.filter`): low pass below mid height, high pass above, open in the middle |
| right hand height | the echo throw (`perform.throw`), from mid height up |

A short pinch acts when it opens again, so a pinch of both hands never also counts as two single ones, and a held one
never also toggles the kick. A hand moves its control only while it is **not** pinching. Height is measured against the
head, so it works standing or sitting; both controls are smoothed over 0.15 s, the filter has a dead zone round the
middle, and nothing ever jumps. When a track has ended and its rooms have rung out, the next one follows by itself.

**The bridge.** With `bridge_host` set, the app sends its hands to Parhelion on that computer as well: OSC `/hands` with
six floats (left and right height, left and right pinch, left and right tracked), 30 times a second, to `bridge_port`
(9104 by default, the port in the plugin's settings under Headset). The plugin reads them with the same grammar and
the same numbers -- every generator of the family has them (its `Plugin/Frame.h`) -- and shows its headset controls
while they arrive. `audio=0` leaves the headset silent, so only the computer plays.

The panel is head-locked (yaw only) and drawn as points: the logo, the track (or the set and its track), style and
form, the section and the bar, key, scale and Camelot label, the lead's and the pad's preset (the composer chooses one
of 1024 per synth for every track, as in the plugin), the time and the tempo, the level, the filter and the throw, KICK
OUT while the kick is out, a pending "now" and the bar it lands on, four beat lamps.

**The parhelion in the sky** (the logo at the size of a room): 3 m ahead and 1.3 m above the eyes where the session
began, tilted towards the player. The sun swells after every kick; the 22-degree halo is the bar, once round, every
other part a ring of beads about it in its colour and the playhead's hand sweeping them; the two sun dogs on the halo
burn with the plan's energy (brighter, larger, longer tails in the drops); the parhelic circle through all three is the
track from left to right, its sections as ticks (the drops warm, the breakdowns blue), the part played brighter, the
playhead a light on it and a pending "now" a mark at its bar. The next bar's beads fade in over the first eighth of the
bar while the last bar's fade out: every brightness is a continuous function of the position (the Kaleidoscope rules),
and nothing about the camera moves with the audio.

## Config

`parh.cfg` in the app's external data folder, every key optional:

```
adb push parh.cfg /sdcard/Android/data/com.reneweller.parhelion.quest/files/parh.cfg
```

```
mute=1                     start silent (the test rule); the engines still run
seed=2026                  the first track's seed; the next takes the next seed
minutes=7                  length of a track
set_minutes=60             a set of so many minutes instead of single tracks
style=Uplifting            Uplifting, Progressive, Dream House, Acid or Deep
quality=desktop            everything (default here: quest)
osc_host=192.168.1.20      the score cues to a visualiser (Cue.h)
osc_port=9000
bridge_host=192.168.1.20   the bridge: the hands to Parhelion on that computer; empty = off
bridge_port=9104                its headset port (the plugin's settings, Headset)
audio=0                    no sound on the headset, the computer plays (the same as mute=1)
knobs=compose.key=D;set.dramaturgy=Sunrise      any knobs, repeatable
```

## CPU (PLAN 10)

The Quest's quality (`Engine::Quality::Quest`, the default here): three unison oscillators and four voices a polyphonic
voice (the pad five and six), ten piano voices, half the orchestra's players and singers, and the polyphonic voices'
filter models with one Newton step a sample instead of three (each voice within 0.3 dB of the desktop's, measured on the
stems of five tracks; the 303 keeps its three).

Measured on the desktop with the core's NEON path on SSE's four lanes (`PARH_NEON_BENCH`, Tests/neonbench: the Quest's
lane width and its scalar table reads; timing only, not bit-exact), in per cent of one i9-12900K core:

| | before Phase 7 | now |
|---|---|---|
| a 24-minute set of seed 5 with its blends | 14.3 | 11.7 |
| an Uplifting track with the orchestra (seed 4) | 17.1 | 12.0 |

With a factor of 2.5 to 3.5 between that core and one of the Quest 2's, the set would lie at 29 to 41 % -- at or over
the plan's 30 %. It has to be measured on the headset (`build_tools.ps1`, above); the levers not yet pulled are in
docs/PLAN.md (Phase 7).
