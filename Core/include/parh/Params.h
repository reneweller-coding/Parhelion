/**
 * @file Params.h
 * @brief The parameter system: descriptor tables per module, instantiated in blocks.
 *
 * A module declares its parameters once as a table of descriptors and may exist several times -- the twelve
 * lanes of the percussion kit are one module in twelve instances, the six polyphonic voices another. A parameter's id
 * is the base index of its module instance plus its index in the table; its text key is "<prefix>.<key>",
 * "<prefix><instance>.<key>" ("perc3.decay") or, where the instances have names, "<name>.<key>" ("lead.detune").
 *
 * From the same tables come the host parameters, OSC addresses, the preset text form, the manual and the
 * `--list` output of parh_render. The composer never writes parameters; it reads a snapshot, and what it plays on
 * a knob it writes into the score as an automation curve (Score.h).
 *
 * Values are stored as std::atomic<float> in their real range (Hz, ms, dB), so the audio thread can read what
 * another thread wrote without a lock.
 *
 * @note The store (ParamStore) is copied from Totality `Core/include/tot/Params.h` at 4d3c0d2 (29.09.2026), which had
 *       it from Ephemeris and Phosphene. The tables of the kick, the sub, the kit, the mono synth, the master and the DJ
 *       mixer are Totality's; the polyphonic voice's is Phosphene's (at 76f7100); compose, pump and sends are
 *       Parhelion's own. Rule kept: a table is only ever appended to, never reordered, once the first preset exists,
 *       because the indices sit in presets, `.parhset` files and the plugin's state.
 */
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace parh {

/** @brief How a parameter maps between its real value and the normalised 0..1 of a knob. */
enum class Curve : uint8_t {
    Linear,   ///< proportional
    Log,      ///< logarithmic; minimum must be > 0
    Int,      ///< integer steps, linear
    Choice,   ///< integer index into a list of names
    Toggle,   ///< 0 or 1
};

/** @brief Static description of one parameter. */
struct ParamDesc {
    const char* key;                        ///< identifier inside the module, snake_case
    const char* name;                       ///< display name (English)
    const char* unit;                       ///< unit for display, may be empty
    float minValue;                         ///< lowest real value
    float maxValue;                         ///< highest real value
    float defValue;                         ///< default real value
    Curve curve;                            ///< mapping to the knob
    const char* const* choices = nullptr;   ///< names for Curve::Choice (maxValue + 1 entries)
};

/** @brief The modules that own parameters. Appended to, never reordered (once presets exist). */
enum class Module : int { Compose = 0, Kick, Sub, Perc,
                          /** The mid-bass and the 303 line: Totality's mono synth (Synth.h), one table. */
                          Bass, Acid,
                          /** The polyphonic voices (Phosphene's Poly.h): lead, counter, pluck, arp, pad, stab. */
                          Poly,
                          /** The effects (Phosphene's Sfx.h): risers, downlifters, impacts, reverse crashes, sub drops. */
                          Sfx,
                          /** The physical piano (PLAN 5.8, Piano.h). */
                          Piano,
                          /** The orchestra (PLAN 5.9): the bowed string section, the choir, the brass, the timpani. */
                          Strings, Choir, Brass, Timpani,
                          /** The granular cloud (PLAN 5.7, Deep): grains of the pad's and the keys' past (Cloud.h). */
                          Cloud,
                          /** The sidechain's shape (PLAN 7.3): the ghost kick's curve, shared by every bus. */
                          Pump,
                          /** The three rooms (PLAN 5.11): a short room, a plate, a hall, on sends. */
                          Sends,
                          /** The kit's buses and the track bus. */
                          Mix, Master,
                          /** The DJ mixer -- a channel per deck (three instances), and its effects. */
                          Deck, DjFx,
                          /** The set composer (PLAN 6.8). */
                          Set,
                          /** The performer's controls (live only, Engine::setLive), the OSC cues (Cue.h). */
                          Perform, Cue, Count };

constexpr int kDecks = 3;        ///< the decks of a set (PLAN 3): two for the tracks, a third for what is borrowed

constexpr int kPercLanes = 12;   ///< instances of the percussion module (the lanes of the kit)

/** @brief The instances of the polyphonic voice (module Poly), in the order of their parameter blocks. */
enum class PolyInstance : int { Lead = 0, Counter, Pluck, Arp, Pad, Stab, Count };
constexpr int kPolyInstances = static_cast<int>(PolyInstance::Count);   ///< instances of the polyphonic voice
extern const char* const kPolyInstanceNames[kPolyInstances];            ///< "lead", "counter", "pluck", "arp", "pad", "stab"

/** @brief Parameters of the composer (read as a snapshot when a track is planned). */
namespace compose {
enum : int { Bpm, Key, Scale, Style, Minutes,
             Auto,        ///< the composer draws tempo, key and scale from the style (else the knobs)
             MorphTo,     ///< Off or a style the profile is morphed towards (PLAN 6.7)
             Morph,       ///< how far, 0..1
             PickSounds,  ///< the composer chooses a factory preset per synth and track (Presets.h), else the knobs sound
             UseRatings,  ///< the player's ratings weigh the choices (Preferences.h)
             Humanize,    ///< velocity scatter in per cent; never timing (Dok. 3: the grid is strict)
             Count };
}
/**
 * @brief Parameters of the kick (PLAN 5.1): Totality's table -- Phosphene's kick, then the 909 engine's layer and the EQ.
 */
namespace kick {
enum : int { Engine, Tune, PitchEnd, PitchStart, PitchDecay, PunchDecay, Punch, AmpAttack, AmpHold, AmpDecay,
             Drive, Clip, ClickLevel, ClickTone, ClickDecay, Tone, Level, TailLimit,
             LowCut, DipFreq, Dip, TopLevel, TopPitch, TopDecay, TopDrive, TopCut, Count };
}
/** @brief Parameters of the sub bass (PLAN 5.2): a sine, locked to the kick's phase. */
namespace sub {
enum : int { Level, Octave, Attack, Decay, Sustain, Release, LowPass, Drive, Lock, Duck, DuckHold, DuckRelease, Count };
}
/**
 * @brief Parameters of one percussion lane (module Perc, "perc1" .. "perc12"): Phosphene's lane table, then the
 *        noise source's type (the 909's metal table, Kit.h).
 */
namespace perc {
enum : int { Active, Role, Engine, Pitch, PitchAmount, PitchDecay, FmRatio, FmIndex, ModeSet, ModeDamp,
             MetalScale, Noise, NoiseDecay, Bursts, BurstSpacing, Decay, Filter, Cutoff, Resonance, LowCut,
             Drive, Level, Pan, Choke, Shift, Density, Tune, PanDepth, PanBars, CutTrack,
             NoiseType, Count };
}
/** @brief Parameters of the mix: the two percussion buses and the track bus. */
namespace mix {
enum : int { HatsLevel, HatsCut, PercLevel, PercCut, DrumSat, LowCut,
             SynthLevel,   ///< the polyphonic voices' bus fader (before their sends): the energy script rides it (PLAN 6.4)
             Count };
}
/** @brief Parameters of the master (PLAN 7.4). */
namespace master {
enum : int { Level, Threshold, Ratio, Clip, Ceiling, MonoBelow, Tilt, Count };
}
/**
 * @brief Parameters of a mono synth voice (modules Bass and Acid, PLAN 5.2, 5.3): a PolyBLEP oscillator with a sub
 *        oscillator, one of Ephemeris' circuit filters at twice the rate, envelopes, accent and glide, and its strip.
 */
namespace synth {
enum : int { Level, Pan, Wave, PulseWidth, SubOsc, Filter, Cutoff, Resonance, EnvAmount, Decay, Accent, AmpAttack,
             AmpDecay, AmpSustain, AmpRelease, Glide, Drive, KeyTrack, LowCut, HighCut, PlateSend, RoomSend, Duck, DuckRelease,
             ModFirst,   ///< the modulation block's core from here (Modulation.h, readModCore: 48 knobs; Phase 5b)
             Count = ModFirst + 48 };
}
/**
 * @brief Parameters of a polyphonic voice (module Poly; Phosphene's table, Poly.h): supersaw, VA, FM or wavetable,
 *        a second oscillator, the voice filter and its circuit models, the envelopes, the tempo delay, the trance gate,
 *        the sends and the modulation block (Modulation.h, contiguous from FiltAttack).
 */
namespace poly {
enum : int { Osc, Detune, Mix, DynamicDetune, Wave, PulseWidth, FmRatio, FmIndex, FmDecay,
             Table, Position, PosEnv, PosDecay, PosLfoDepth, PosLfoBeats,
             Cutoff, Resonance, EnvAmount, FilterDecay, KeyTrack, HpFloor, HpTrack,
             AmpAttack, AmpDecay, AmpSustain, AmpRelease, Width, VelSens,
             DelaySend, DelayLeft, DelayRight, DelayFeedback, DelayHighPass, DelayLowPass,
             RoomSend, HallSend,
             Duck,         ///< dB: how deep the ghost kick ducks this voice (PLAN 7.3: pads 3-5, lead 1-2)
             Gate, GatePattern, GateDepth, GateDuty, GateAttack, GateRelease, GateTone,
             Level, Disperse, DisperseFreq, Drift, FilterType, Glide, Pan,
             Osc2, Osc2Mix, Osc2Interval, Osc2Detune,
             LfoBeats, LfoCutoff, LfoPitch, LfoAmp,
             PlateSend, SlowMod, FilterModel, FilterMode,
             FiltAttack, FiltSustain, FiltRelease, MenvAttack, MenvDecay, MenvSustain, MenvRelease, Lfo1Rate,
             Lfo1Shape, Lfo1Sync, Lfo1Retrig, Lfo1Fade, Lfo2Rate, Lfo2Shape, Lfo2Sync, Lfo2Retrig, Lfo2Fade,
             Lfo3Rate, Lfo3Shape, Lfo3Sync, Lfo3Retrig, Lfo3Fade, Lfo4Rate, Lfo4Shape, Lfo4Sync, Lfo4Retrig,
             Lfo4Fade, Mx1Src, Mx1Dst, Mx1Amount, Mx2Src, Mx2Dst, Mx2Amount, Mx3Src, Mx3Dst, Mx3Amount, Mx4Src,
             Mx4Dst, Mx4Amount, Mx5Src, Mx5Dst, Mx5Amount, Mx6Src, Mx6Dst, Mx6Amount, Mx7Src, Mx7Dst, Mx7Amount,
             Mx8Src, Mx8Dst, Mx8Amount,
             Count };
}
/** @brief Values of poly.osc. */
enum class PolyOsc : int { Supersaw = 0, Va, Fm, Wavetable, Count };
/** @brief Values of poly.osc2: off, or one of the four oscillators on the outer unison pair. */
enum class PolyOsc2 : int { Off = 0, Supersaw, Va, Fm, Wavetable, Count };
/** @brief Values of poly.osc2_interval: the second oscillator's interval against the note. */
enum class PolyOsc2Interval : int { TwoOctavesDown = 0, OctaveDown, FifthDown, Unison, FifthUp, OctaveUp, Count };
/** @brief Semitones of poly.osc2_interval against the note (PolyOsc2Interval order). */
constexpr int kOsc2IntervalSemis[] = { -24, -12, -7, 0, 7, 12 };
/** @brief Beats of the delay times (poly.delay_left, delay_right: 1/16, 1/8, 3/16, 1/4, 3/8, 1/2). */
inline constexpr float kDelayBeats[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
constexpr int kNumDelayTimes = 6;   ///< entries of kDelayBeats
/** @brief Values of poly.filter_type: the outputs of the voice's state-variable filter (PolyKernel.h). */
enum class PolyFilter : int { LowPass = 0, BandPass, HighPass, Notch, Count };
// Names of the modulation block's choices (Modulation.h; Params.cpp).
extern const char* const kLfoShapeNames[];    ///< LfoShape
extern const char* const kLfoSyncNames[];     ///< lfoCyclesPerBeat's divisions
extern const char* const kModSourceNames[];   ///< ModSource
extern const char* const kModDestNames[];     ///< ModDest (the polyphonic voice's targets)
/** @name The other engines' modulation targets (Modulation.h, kSynthModDests ... kTimpaniModDests)
 *  @{ */
extern const char* const kSynthModDestNames[];
extern const char* const kPianoModDestNames[];
extern const char* const kStringsModDestNames[];
extern const char* const kChoirModDestNames[];
extern const char* const kBrassModDestNames[];
extern const char* const kTimpaniModDestNames[];
/** @} */
/**
 * @brief Values of poly.filter_model: entry 0 is the voice's own state-variable filter, entries 1 .. 9 are Filters.h's
 *        FilterModel from Moog to Wasp.
 */
constexpr int kVoiceFilterModels = 10;
/** @brief Parameters of the effect generator (module Sfx, prefix "sfx"). */
namespace sfx {
enum : int { Level, Noise, Resonance, Brightness, ImpactDecay, Vowel, SwellDecay, Width, RoomSend, HallSend, Duck,
             // 19.09.2026, round "fx-psychedelia". Appended.
             SubLevel,     ///< dB: the sub drop against sfx.level (Sfx.h; it plays mono and ducks under the kick)
             SubDuck,      ///< 0..1: how deep the kick ducks the sub drop
             // 20.09.2026, round "wandering-fx" (Sfx.h): a directed pan trajectory plus a reverb-send
             // trajectory over an event's own length, drawn from its own seed. Off by default, so older
             // sets render unchanged.
             Wander,       ///< toggle: an event's pan sweeps from one side to the other and its content
                           ///< crosses from dry to the hall's send over its length, instead of the
                           ///< oscillating auto-pan and the constant hall_send fraction
             WanderSend,   ///< 0..1: how far the crossfade reaches by the event's own tail (x = 1)
             // 24.09.2026, the user: "Im SFX-Fenster ist nach wie vor keine Auswahl fuer das Preset". One per
             // family of the effect bank (Sfx.h, SfxPreset): 0 = Auto, the composer's draw per event as before;
             // n = every event of that family plays bank preset n (Sfx::trigger). Appended; Auto everywhere is
             // the composer's own draw, sample for sample.
             PresetRiser, PresetDownlifter, PresetImpact, PresetSweep, PresetFormantShot, PresetReverseSwell,
             PresetZap, PresetSquelch, PresetBubble, PresetReverseCrash, PresetAtmosphere,
             PlateSend,   ///< 25.09.2026: the effects' send into the plate (the throw rides it; the hall is the far room now)
             Count };
/** @brief The first of the per-family preset choices, and how many there are. */
constexpr int kFirstPreset = PresetRiser;
constexpr int kNumPresetChoices = PresetAtmosphere - PresetRiser + 1;   ///< how many per-family preset choices there are
}
/**
 * @brief Parameters of the physical piano (PLAN 5.8, Piano.h, PianoDesign.h). The first eight build the instrument and
 *        are read when a track loads (a new design, computed off the audio thread); the rest act while it plays.
 */
namespace piano {
enum : int { Instrument,   ///< PianoInstrument: Grand, Baby Grand, Upright, Soft (the dull piano of Dok. 5)
             Hardness,     ///< factor on the hammer felt's stiffness
             Strike,       ///< the strike point, -1 towards the agraffe .. 1 deeper
             Unison,       ///< cents between the outer strings of a course
             Inharm,       ///< factor on the strings' inharmonicity
             Impedance,    ///< factor on the bridge's admittance
             Stretch,      ///< factor on the tuning's stretch
             Condition,    ///< random mistuning of each string, cents
             Level, Pedal, ///< dB; the sustain pedal 0 .. 1 (the middle: the half pedal)
             Sympathetic,  ///< gain on the strings ringing along (1: as the physics gives it)
             Phantom,      ///< gain on the longitudinal modes (the phantom partials)
             DamperNoise, Mechanics,   ///< the damper felt's noise, the key's thump
             Width,        ///< the listening points apart, 0 (mono) .. 1 (bass left, treble right)
             LowCut,       ///< Hz
             Duck,         ///< dB under the pump
             RoomSend, PlateSend, HallSend,
             ModFirst,     ///< the modulation block's core (48 knobs; Phase 5b)
             Count = ModFirst + 48 };
constexpr int kFirstPlayKnob = Level;   ///< the knobs before it change the design
}
/** @brief Parameters of the string section (PLAN 5.9, Strings.h). */
namespace strings {
enum : int { Level,        ///< dB
             Players,      ///< players on a note, 1 .. 6
             Vibrato,      ///< factor on the players' vibrato depth
             Pressure,     ///< factor on the bow force (1: half of Schelleng's upper limit)
             Position,     ///< the bow's place, -1 sul tasto .. 0 (a tenth of the string) .. 1 sul ponticello
             Speed,        ///< factor on the bow speed
             Attack, Release,   ///< ms: the stroke's start, the bow's lift
             Width, LowCut, Duck, RoomSend, PlateSend, HallSend,
             ModFirst,   ///< the modulation block's core (48 knobs; Phase 5b)
             Count = ModFirst + 48 };
}
/** @brief Parameters of the choir (PLAN 5.9, Choir.h). */
namespace choir {
enum : int { Level,        ///< dB
             Singers,      ///< singers on a note, 1 .. 6
             Vowel,        ///< 0 "aah" .. 0.5 "ooh" (o) .. 1 "uuh"
             Vibrato,      ///< factor on the singers' vibrato depth
             Breath,       ///< the aspiration noise in the glottal flow
             Tension,      ///< the voice quality: 0 lax and breathy (Rd 2.7) .. 1 pressed (Rd 0.8)
             Attack, Release,   ///< ms
             Width, LowCut, Duck, RoomSend, PlateSend, HallSend,
             ModFirst,   ///< the modulation block's core (48 knobs; Phase 5b)
             Count = ModFirst + 48 };
}
/** @brief Parameters of the brass (PLAN 5.9, Brass.h). */
namespace brass {
enum : int { Level,        ///< dB
             Players,      ///< players on a note, 1 .. 4
             Pressure,     ///< factor on the blowing pressure (the dynamic and the brightness)
             Brassiness,   ///< the nonlinear steepening in the bore (the fortissimo's blare)
             Attack, Release,   ///< ms
             Vibrato,      ///< cents
             Width, LowCut, Duck, RoomSend, PlateSend, HallSend,
             ModFirst,   ///< the modulation block's core (48 knobs; Phase 5b)
             Count = ModFirst + 48 };
}
/** @brief Parameters of the granular cloud (PLAN 5.7, Cloud.h). */
namespace cloud {
enum : int { Level, Density, Size, Pitch, Spray, PadSend, KeysSend, PlateSend, Count };
}
/** @brief Parameters of the timpani (PLAN 5.9, Timpani.h). */
namespace timpani {
enum : int { Level,        ///< dB
             Hardness,     ///< factor on the mallet felt's stiffness
             Decay,        ///< factor on the modes' decay times
             Strike,       ///< the strike point, the fraction of the radius from the centre
             Width, LowCut, Duck, RoomSend, PlateSend, HallSend,
             ModFirst,   ///< the modulation block's core (48 knobs; Phase 5b)
             Count = ModFirst + 48 };
}
/**
 * @brief The sidechain (PLAN 7.3, Dok. 7): the curve every bus ducks along when the ghost kick triggers -- the attack,
 *        the hold and the release of Ducker.h -- and the depth of the rooms' returns. The depths of the voices are
 *        their own (sub.duck, bass.duck, poly.duck); the composer moves them with the energy (0 in a breakdown).
 */
namespace pump {
enum : int { Attack, Hold, Release, ReturnDuck, Count };
}
/**
 * @brief The rooms (PLAN 5.11, Dok. 7): a short room (Totality's FDN, 0.8 to 1.2 s, the pluck's), a plate (Dattorro,
 *        1.2 to 1.8 s, the glue) and a hall (the FDN large, 4 to 8 s, the emotion), each with its return; every send
 *        input high-passed (Dok. 7: 250 to 300 Hz), the returns' highs cut.
 */
namespace sends {
enum : int { RoomSize, RoomDecay, RoomReturn, PlateDecay, PlateDamping, PlatePreDelay, PlateReturn, HallSize, HallDecay,
             HallDamping, HallPreDelay, HallReturn, LowCut, HighCut, HatsSend, PercSend, Count };
}
/** @brief Parameters of a DJ mixer channel (PLAN 6.8): fader, the isolator's three bands (kill at -60 dB), a bipolar
 *         filter (below 0 a low pass, above 0 a high pass), the send into the mixer's effects. */
namespace deck {
enum : int { Fader, Low, Mid, High, Filter, FxSend, Count };
}
/** @brief Parameters of the DJ mixer's effects: a tempo echo and a hall on one send. */
namespace djfx {
enum : int { EchoTime, Feedback, EchoReturn, HallDecay, HallReturn, Count };
}
/** @brief Parameters of the set (PLAN 6.8). */
namespace set {
enum : int { Dramaturgy, Journey, BlendBars, Minutes, TrackMinutes, Count };
}
/**
 * @brief The performer's controls (PLAN 9, Perform): the master filter, the echo throw of the whole mix into the
 *        mixer's echo, and a mute per group of parts. Live only (Engine::setLive).
 */
namespace perform {
enum : int { Filter, Throw, MuteKick, MuteBass, MuteHats, MutePerc, MuteLead, MuteSynths, MutePads,
             Wheel,      ///< the modulation wheel, 0..1 (MIDI CC 1): a source of every voice's matrix (Phase 5b)
             Pressure,   ///< the channel pressure, 0..1: another
             KeyboardPart,   ///< what a MIDI keyboard plays (perform::keys, 01.10.2026); off: the keys toggle the mutes
             KeyboardMode,   ///< 0 Replace: the played voice's generated notes are left out; 1 Layer: it plays over them
             Composer,       ///< on: the composer's notes play; off: only what the keyboard plays
             KeyboardLower,  ///< the keys below KeyboardSplit play this voice instead (perform::keys; Off: no split; 02.10.2026)
             KeyboardSplit,  ///< the split key: C1 (36) .. C5 (84), 12 a step
             KeyboardScale,  ///< Scale Lock: a played key goes to the nearest note of the track's key and scale (not the kit)
             KeyboardVelocity,   ///< the velocity curve: as played, soft, hard, fixed (frame::shapeVelocity)
             Count };
/**
 * @brief The keyboard's targets (perform.keyboard_part, 01.10.2026, Engine::queueLive): the kit -- C1 (36) the kick,
 *        C#1 .. C2 the twelve lanes --, the bass, the 303, the six polyphonic voices, the piano, the strings, the choir,
 *        the brass, or by channel (1 kit, 2 bass, 3 303, 4 lead, 5 counter, 6 pluck, 7 arp, 8 pad, 9 stab, 10 kit as
 *        General MIDI's drum channel, 11 piano, 12 strings, 13 choir, 14 brass).
 */
namespace keys {
enum : int { Off, Kit, Bass, Acid, Lead, Counter, Pluck, Arp, Pad, Stab, Piano, Strings, Choir, Brass, ByChannel, Count };
}
constexpr int kMutes = MutePads - MuteKick + 1;   ///< kick; sub, bass, 303; hats; perc; lead, counter; pluck, arp; pad, stab
}
/** @brief The OSC cues (Cue.h): on or off, and the UDP port. */
namespace cue {
enum : int { Enabled, Port, Count };
}
/** @brief The set's dramaturgies (PLAN 6.8). */
enum class Dramaturgy : int { WarmUp = 0, Peak, Closing, Sunrise, Journey, Count };

/** @brief What a percussion lane plays in the groove; decides its patterns and its MIDI note. */
enum class PercRole : int { ClosedHat = 0, RollingHat, OpenHat, Ride, Clap, ClapGhost, Snare, Rim, Shaker, Tom, Conga,
                            Noise, Crash, Tambourine, Count };
constexpr int kNumPercRoles = static_cast<int>(PercRole::Count);   ///< number of roles
/** @brief Sound sources of a percussion lane (Phosphene's five). */
enum class PercEngine : int { Noise = 0, Metal, Modal, Tone, Fm, Count };
/** @brief What the noise source plays. */
enum class NoiseType : int { White = 0, Metal909, Count };
/** @brief The kick's engines: Phosphene's sweep and resonator, and the 909's shaped triangle (Kick.h). */
enum class KickEngine : int { Sweep = 0, Resonator, Tr909, Count };
/** @brief The five style profiles of PLAN 2.1, in the order of compose.style; Uplifting (with Cinematic) is the default. */
enum class Style : int { Uplifting = 0, Progressive, DreamHouse, Acid, Deep, Count };

extern const char* const kKeyNames[12];          ///< names of compose.key, C .. B
extern const char* const kScaleNames[];          ///< names of compose.scale
extern const char* const kStyleNames[];          ///< names of compose.style
extern const char* const kPercRoleNames[kNumPercRoles];   ///< names of perc.role

/**
 * @brief The scales of compose.scale (PLAN 2.5): natural minor first (Dok. 4: it dominates), Dorian (Progressive),
 *        harmonic minor (the "trance interval" b6-7, used punctually), minor pentatonic (the leads' stock), Phrygian,
 *        and major for the rest.
 */
enum class Scale : int { Aeolian = 0, Dorian, HarmonicMinor, MinorPentatonic, Phrygian, Ionian, Count };
/** @brief One scale: its size and the semitones of its degrees above the root. */
struct ScaleDef {
    int size;       ///< number of degrees (5 .. 7)
    int steps[7];   ///< semitones of the degrees, ascending from 0
};
/** @brief The definition of scale @p scale (compose.scale order); Aeolian for an index out of range. */
const ScaleDef& scaleDef(int scale);
/** @brief Whether @p semitones above the root lies in scale @p scale. */
bool inScale(int scale, int semitones);
/** @brief The kick's tuning rules, in the order of kick.tune (Kick.h, tuneToKey). */
enum class KickTune : int { Free = 0, Key, Fifth, FlatSeventh, Count };

/**
 * @brief All parameter values of one engine, lock-free readable from the audio thread.
 *
 * Construction builds the registry from the module tables. Not copyable (atomics); use copyValuesFrom() for
 * snapshots.
 */
class ParamStore {
public:
    ParamStore();
    ParamStore(const ParamStore&) = delete;
    ParamStore& operator=(const ParamStore&) = delete;

    /** @brief Number of parameters. */
    int count() const { return static_cast<int>(entries_.size()); }
    /** @brief First id of a module instance; -1 if it does not exist. */
    int base(Module m, int instance = 0) const;
    /** @brief Id of parameter @p index of a module instance; -1 if it does not exist. */
    int id(Module m, int instance, int index) const { const int b = base(m, instance); return b < 0 ? -1 : b + index; }
    /** @brief Descriptor of @p id. */
    const ParamDesc& desc(int id) const { return *entries_[static_cast<size_t>(id)].desc; }
    /** @brief Full text key of @p id ("perc3.decay"). */
    const std::string& key(int id) const { return entries_[static_cast<size_t>(id)].key; }
    /** @brief The module, the instance and the index within the module of parameter @p id. */
    Module moduleOf(int id) const { return entries_[static_cast<size_t>(id)].module; }
    int instanceOf(int id) const { return entries_[static_cast<size_t>(id)].instance; }   ///< @copydoc moduleOf
    int indexOf(int id) const { const Entry& e = entries_[static_cast<size_t>(id)]; return id - base(e.module, e.instance); }   ///< @copydoc moduleOf
    /** @brief Id for a text key, or -1. */
    int find(std::string_view key) const;

    /** @brief Current real value. */
    float get(int id) const { return values_[static_cast<size_t>(id)].load(std::memory_order_relaxed); }
    /** @brief Current value rounded to an integer (Int, Choice, Toggle). */
    int getInt(int id) const;
    /** @brief Current value as a switch. */
    bool getBool(int id) const { return get(id) >= 0.5f; }
    /** @brief Sets a real value, clamped to the range (and rounded for discrete curves). */
    void set(int id, float value);
    /**
     * @brief Counts the writes to the values (set, resetDefaults, copyValuesFrom). A reader that sees the same count as
     *        before its last read of the values saw no write since (the Deck's cell cache): the count is raised after
     *        the value is stored (release) and read before the values are (acquire).
     */
    uint32_t version() const { return version_.load(std::memory_order_acquire); }
    /** @brief Sets from a normalised 0..1 position. */
    void setNormalised(int id, float norm) { set(id, fromNormalised(id, norm)); }

    /** @brief Real value to normalised 0..1. */
    float toNormalised(int id, float value) const;
    /** @brief Normalised 0..1 to real value. */
    float fromNormalised(int id, float norm) const;

    /** @brief All parameters back to their defaults. */
    void resetDefaults();
    /** @brief Default of @p id: the descriptor's, or the instance's own where instances differ (the kit's lanes). */
    float defaultValue(int id) const { return defaults_[static_cast<size_t>(id)]; }
    /** @brief Copies every value from another store (for snapshots on another thread). */
    void copyValuesFrom(const ParamStore& other);
    /**
     * @brief Copies the current values of one module instance into @p out, indexed like its table.
     * @param m        module
     * @param instance instance index
     * @param out      at least as many floats as the module has parameters
     */
    void readModule(Module m, int instance, float* out) const;
    /** @brief Number of parameters of a module. */
    static int moduleCount(Module m);

    /**
     * @brief Applies "key=value" assignments separated by whitespace, newlines or ';'.
     *
     * Choice parameters accept their name ("compose.key=F#") or index. Lines starting with '#' are comments.
     * @param text  the assignments
     * @param error receives a message for the first bad assignment, may be null
     * @return false if any assignment failed (the good ones are still applied)
     */
    bool parseText(std::string_view text, std::string* error = nullptr);
    /**
     * @brief The text form, one "key=value" per line.
     * @param onlyChanged leave out parameters at their default
     */
    std::string toText(bool onlyChanged) const;
    /** @brief A value formatted for display ("330 Hz", "F#"). */
    std::string format(int id) const;

private:
    /** @brief One parameter of the store: its description, its key, its module and instance. */
    struct Entry {
        const ParamDesc* desc;   ///< its description
        std::string key;   ///< its key ("kick.decay", "perc3.level")
        Module module;   ///< its module
        int instance;   ///< its instance
    };
    std::vector<Entry> entries_;   ///< the parameters, by id
    std::unique_ptr<std::atomic<float>[]> values_;   ///< their values, real units (atomic: any thread)
    std::atomic<uint32_t> version_{ 0 };   ///< version()
    std::vector<float> defaults_;   ///< their defaults
    std::unordered_map<std::string, int> index_;   ///< key -> id
    static constexpr int kMaxInstances = 16;   ///< the most instances a module can have
    int bases_[static_cast<int>(Module::Count)][kMaxInstances] = {};   ///< per module and instance: the id of its first parameter, -1 none
};

} // namespace parh
