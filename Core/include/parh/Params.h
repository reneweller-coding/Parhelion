/**
 * @file Params.h
 * @brief The parameter system: descriptor tables per module, instantiated in blocks.
 *
 * A module declares its parameters once as a table of descriptors and may exist several times -- the twelve
 * lanes of the percussion kit are one module in twelve instances, the six polyphonic voices another. A parameter's id
 * is the base index of its module instance plus its index in the table; its text key is "\<prefix\>.\<key\>",
 * "\<prefix\>\<instance\>.\<key\>" ("perc3.decay") or, where the instances have names, "\<name\>.\<key\>" ("lead.detune").
 *
 * From the same tables come the host parameters, OSC addresses, the preset text form, the manual and the
 * `--list` output of parh_render. The composer never writes parameters; it reads a snapshot, and what it plays on
 * a knob it writes into the score as an automation curve (Score.h).
 *
 * Values are stored as std::atomic\<float\> in their real range (Hz, ms, dB), so the audio thread can read what
 * another thread wrote without a lock.
 *
 * @note The store (ParamStore) is copied from Totality `Core/include/tot/Params.h` at 4d3c0d2 (29.09.2026), which had
 *       it from Ephemeris and Phosphene. The tables of the kick, the sub, the kit, the mono synth, the master and the DJ
 *       mixer are Totality's; the polyphonic voice's is Phosphene's (at 76f7100); compose, pump and sends are
 *       Parhelion's own. Rule kept: a table is only ever appended to, never reordered, once the first preset exists,
 *       because the indices sit in presets, `.parhset` files and the plugin's state.
 * @author Rene Weller
 */
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace parh
{

/** @brief How a parameter maps between its real value and the normalised 0..1 of a knob. */
enum class CurveE : uint8_t
{
    LINEAR,   ///< proportional
    LOG,      ///< logarithmic; minimum must be > 0
    INT,      ///< integer steps, linear
    CHOICE,   ///< integer index into a list of names
    TOGGLE,   ///< 0 or 1
};

/** @brief Static description of one parameter. */
struct ParamDesc
{
    const char        *m_key;                 ///< identifier inside the module, snake_case
    const char        *m_name;                ///< display name (English)
    const char        *m_unit;                ///< unit for display, may be empty
    float              m_minValue;            ///< lowest real value
    float              m_maxValue;            ///< highest real value
    float              m_defValue;            ///< default real value
    CurveE             m_curve;               ///< mapping to the knob
    const char *const *m_choices = nullptr;   ///< names for Curve::Choice (maxValue + 1 entries)
};

/** @brief The modules that own parameters. Appended to, never reordered (once presets exist). */
enum class ModuleE : int
{
    COMPOSE = 0,
    KICK,
    SUB,
    PERC,
    /** The mid-bass and the 303 line: Totality's mono synth (Synth.h), one table. */
    BASS,
    ACID,
    /** The polyphonic voices (Phosphene's Poly.h): lead, counter, pluck, arp, pad, stab. */
    POLY,
    /** The effects (Phosphene's Sfx.h): risers, downlifters, impacts, reverse crashes, sub drops. */
    SFX,
    /** The physical piano (PLAN 5.8, Piano.h). */
    PIANO,
    /** The orchestra (PLAN 5.9): the bowed string section, the choir, the brass, the timpani. */
    STRINGS,
    CHOIR,
    BRASS,
    TIMPANI,
    /** The granular cloud (PLAN 5.7, Deep): grains of the pad's and the keys' past (Cloud.h). */
    CLOUD,
    /** The sidechain's shape (PLAN 7.3): the ghost kick's curve, shared by every bus. */
    PUMP,
    /** The three rooms (PLAN 5.11): a short room, a plate, a hall, on sends. */
    SENDS,
    /** The kit's buses and the track bus. */
    MIX,
    MASTER,
    /** The DJ mixer -- a channel per deck (three instances), and its effects. */
    DECK,
    DJ_FX,
    /** The set composer (PLAN 6.8). */
    SET,
    /** The performer's controls (live only, Engine::setLive), the OSC cues (Cue.h). */
    PERFORM,
    CUE,
    COUNT
};

constexpr int Decks = 3;   ///< the decks of a set (PLAN 3): two for the tracks, a third for what is borrowed

constexpr int PercLanes = 12;   ///< instances of the percussion module (the lanes of the kit)

/** @brief The instances of the polyphonic voice (module Poly), in the order of their parameter blocks. */
enum class PolyInstanceE : int
{
    LEAD = 0,
    COUNTER,
    PLUCK,
    ARP,
    PAD,
    STAB,
    COUNT
};
constexpr int PolyInstances = static_cast<int>( PolyInstanceE::COUNT );   ///< instances of the polyphonic voice
extern const char *const PolyInstanceNames[PolyInstances];   ///< "lead", "counter", "pluck", "arp", "pad", "stab"

/** @brief Parameters of the composer (read as a snapshot when a track is planned). */
namespace compose
{
enum : int
{
    BPM,
    KEY,
    SCALE,
    STYLE,
    MINUTES,
    AUTO,          ///< the composer draws tempo, key and scale from the style (else the knobs)
    MORPH_TO,      ///< Off or a style the profile is morphed towards (PLAN 6.7)
    MORPH,         ///< how far, 0..1
    PICK_SOUNDS,   ///< the composer chooses a factory preset per synth and track (Presets.h), else the knobs sound
    USE_RATINGS,   ///< the player's ratings weigh the choices (Preferences.h)
    HUMANIZE,      ///< velocity scatter in per cent; never timing (Dok. 3: the grid is strict)
    COUNT
};
}
/**
 * @brief Parameters of the kick (PLAN 5.1): Totality's table -- Phosphene's kick, then the 909 engine's layer and the EQ.
 */
namespace kick
{
enum : int
{
    ENGINE,
    TUNE,
    PITCH_END,
    PITCH_START,
    PITCH_DECAY,
    PUNCH_DECAY,
    PUNCH,
    AMP_ATTACK,
    AMP_HOLD,
    AMP_DECAY,
    DRIVE,
    CLIP,
    CLICK_LEVEL,
    CLICK_TONE,
    CLICK_DECAY,
    TONE,
    LEVEL,
    TAIL_LIMIT,
    LOW_CUT,
    DIP_FREQ,
    DIP,
    TOP_LEVEL,
    TOP_PITCH,
    TOP_DECAY,
    TOP_DRIVE,
    TOP_CUT,
    COUNT
};
}
/** @brief Parameters of the sub bass (PLAN 5.2): a sine, locked to the kick's phase. */
namespace sub
{
enum : int
{
    LEVEL,
    OCTAVE,
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE,
    LOW_PASS,
    DRIVE,
    LOCK,
    DUCK,
    DUCK_HOLD,
    DUCK_RELEASE,
    COUNT
};
}
/**
 * @brief Parameters of one percussion lane (module Perc, "perc1" .. "perc12"): Phosphene's lane table, then the
 *        noise source's type (the 909's metal table, Kit.h).
 */
namespace perc
{
enum : int
{
    ACTIVE,
    ROLE,
    ENGINE,
    PITCH,
    PITCH_AMOUNT,
    PITCH_DECAY,
    FM_RATIO,
    FM_INDEX,
    MODE_SET,
    MODE_DAMP,
    METAL_SCALE,
    NOISE,
    NOISE_DECAY,
    BURSTS,
    BURST_SPACING,
    DECAY,
    FILTER,
    CUTOFF,
    RESONANCE,
    LOW_CUT,
    DRIVE,
    LEVEL,
    PAN,
    CHOKE,
    SHIFT,
    DENSITY,
    TUNE,
    PAN_DEPTH,
    PAN_BARS,
    CUT_TRACK,
    NOISE_TYPE,
    COUNT
};
}
/** @brief Parameters of the mix: the two percussion buses and the track bus. */
namespace mix
{
enum : int
{
    HATS_LEVEL,
    HATS_CUT,
    PERC_LEVEL,
    PERC_CUT,
    DRUM_SAT,
    LOW_CUT,
    SYNTH_LEVEL,   ///< the polyphonic voices' bus fader (before their sends): the energy script rides it (PLAN 6.4)
    COUNT
};
}
/** @brief Parameters of the master (PLAN 7.4). */
namespace master
{
enum : int
{
    LEVEL,
    THRESHOLD,
    RATIO,
    CLIP,
    CEILING,
    MONO_BELOW,
    TILT,
    COUNT
};
}
/**
 * @brief Parameters of a mono synth voice (modules Bass and Acid, PLAN 5.2, 5.3): a PolyBLEP oscillator with a sub
 *        oscillator, one of Ephemeris' circuit filters at twice the rate, envelopes, accent and glide, and its strip.
 */
namespace synth
{
enum : int
{
    LEVEL,
    PAN,
    WAVE,
    PULSE_WIDTH,
    SUB_OSC,
    FILTER,
    CUTOFF,
    RESONANCE,
    ENV_AMOUNT,
    DECAY,
    ACCENT,
    AMP_ATTACK,
    AMP_DECAY,
    AMP_SUSTAIN,
    AMP_RELEASE,
    GLIDE,
    DRIVE,
    KEY_TRACK,
    LOW_CUT,
    HIGH_CUT,
    PLATE_SEND,
    ROOM_SEND,
    DUCK,
    DUCK_RELEASE,
    MOD_FIRST,   ///< the modulation block's core from here (Modulation.h, readModCore: 48 knobs; Phase 5b)
    COUNT = MOD_FIRST + 48
};
}
/**
 * @brief Parameters of a polyphonic voice (module Poly; Phosphene's table, Poly.h): supersaw, VA, FM or wavetable,
 *        a second oscillator, the voice filter and its circuit models, the envelopes, the tempo delay, the trance gate,
 *        the sends and the modulation block (Modulation.h, contiguous from FiltAttack).
 */
namespace poly
{
enum : int
{
    OSC,
    DETUNE,
    MIX,
    DYNAMIC_DETUNE,
    WAVE,
    PULSE_WIDTH,
    FM_RATIO,
    FM_INDEX,
    FM_DECAY,
    TABLE,
    POSITION,
    POS_ENV,
    POS_DECAY,
    POS_LFO_DEPTH,
    POS_LFO_BEATS,
    CUTOFF,
    RESONANCE,
    ENV_AMOUNT,
    FILTER_DECAY,
    KEY_TRACK,
    HP_FLOOR,
    HP_TRACK,
    AMP_ATTACK,
    AMP_DECAY,
    AMP_SUSTAIN,
    AMP_RELEASE,
    WIDTH,
    VEL_SENS,
    DELAY_SEND,
    DELAY_LEFT,
    DELAY_RIGHT,
    DELAY_FEEDBACK,
    DELAY_HIGH_PASS,
    DELAY_LOW_PASS,
    ROOM_SEND,
    HALL_SEND,
    DUCK,   ///< dB: how deep the ghost kick ducks this voice (PLAN 7.3: pads 3-5, lead 1-2)
    GATE,
    GATE_PATTERN,
    GATE_DEPTH,
    GATE_DUTY,
    GATE_ATTACK,
    GATE_RELEASE,
    GATE_TONE,
    LEVEL,
    DISPERSE,
    DISPERSE_FREQ,
    DRIFT,
    FILTER_TYPE,
    GLIDE,
    PAN,
    OSC2,
    OSC2_MIX,
    OSC2_INTERVAL,
    OSC2_DETUNE,
    LFO_BEATS,
    LFO_CUTOFF,
    LFO_PITCH,
    LFO_AMP,
    PLATE_SEND,
    SLOW_MOD,
    FILTER_MODEL,
    FILTER_MODE,
    FILT_ATTACK,
    FILT_SUSTAIN,
    FILT_RELEASE,
    MENV_ATTACK,
    MENV_DECAY,
    MENV_SUSTAIN,
    MENV_RELEASE,
    LFO1_RATE,
    LFO1_SHAPE,
    LFO1_SYNC,
    LFO1_RETRIG,
    LFO1_FADE,
    LFO2_RATE,
    LFO2_SHAPE,
    LFO2_SYNC,
    LFO2_RETRIG,
    LFO2_FADE,
    LFO3_RATE,
    LFO3_SHAPE,
    LFO3_SYNC,
    LFO3_RETRIG,
    LFO3_FADE,
    LFO4_RATE,
    LFO4_SHAPE,
    LFO4_SYNC,
    LFO4_RETRIG,
    LFO4_FADE,
    MX1_SRC,
    MX1_DST,
    MX1_AMOUNT,
    MX2_SRC,
    MX2_DST,
    MX2_AMOUNT,
    MX3_SRC,
    MX3_DST,
    MX3_AMOUNT,
    MX4_SRC,
    MX4_DST,
    MX4_AMOUNT,
    MX5_SRC,
    MX5_DST,
    MX5_AMOUNT,
    MX6_SRC,
    MX6_DST,
    MX6_AMOUNT,
    MX7_SRC,
    MX7_DST,
    MX7_AMOUNT,
    MX8_SRC,
    MX8_DST,
    MX8_AMOUNT,
    COUNT
};
}
/** @brief Values of poly.osc. */
enum class PolyOscE : int
{
    SUPERSAW = 0,
    VA,
    FM,
    WAVETABLE,
    COUNT
};
/** @brief Values of poly.osc2: off, or one of the four oscillators on the outer unison pair. */
enum class PolyOsc2E : int
{
    OFF = 0,
    SUPERSAW,
    VA,
    FM,
    WAVETABLE,
    COUNT
};
/** @brief Values of poly.osc2_interval: the second oscillator's interval against the note. */
enum class PolyOsc2IntervalE : int
{
    TWO_OCTAVES_DOWN = 0,
    OCTAVE_DOWN,
    FIFTH_DOWN,
    UNISON,
    FIFTH_UP,
    OCTAVE_UP,
    COUNT
};
/** @brief Semitones of poly.osc2_interval against the note (PolyOsc2Interval order). */
constexpr int Osc2IntervalSemis[] = { -24, -12, -7, 0, 7, 12 };
/** @brief Beats of the delay times (poly.delay_left, delay_right: 1/16, 1/8, 3/16, 1/4, 3/8, 1/2). */
inline constexpr float DelayBeats[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
constexpr int          NumDelayTimes = 6;   ///< entries of DelayBeats
/** @brief Values of poly.filter_type: the outputs of the voice's state-variable filter (PolyKernel.h). */
enum class PolyFilterE : int
{
    LOW_PASS = 0,
    BAND_PASS,
    HIGH_PASS,
    NOTCH,
    COUNT
};
// Names of the modulation block's choices (Modulation.h; Params.cpp).
extern const char *const LfoShapeNames[];    ///< LfoShape
extern const char *const LfoSyncNames[];     ///< lfoCyclesPerBeat's divisions
extern const char *const ModSourceNames[];   ///< ModSource
extern const char *const ModDestNames[];     ///< ModDest (the polyphonic voice's targets)
/** @name The other engines' modulation targets (Modulation.h, SynthModDests ... TimpaniModDests)
 *  @{ */
extern const char *const SynthModDestNames[];
extern const char *const PianoModDestNames[];
extern const char *const StringsModDestNames[];
extern const char *const ChoirModDestNames[];
extern const char *const BrassModDestNames[];
extern const char *const TimpaniModDestNames[];
/** @} */
/**
 * @brief Values of poly.filter_model: entry 0 is the voice's own state-variable filter, entries 1 .. 9 are Filters.h's
 *        FilterModel from Moog to Wasp.
 */
constexpr int VoiceFilterModels = 10;
/** @brief Parameters of the effect generator (module Sfx, prefix "sfx"). */
namespace sfx
{
enum : int
{
    LEVEL,
    NOISE,
    RESONANCE,
    BRIGHTNESS,
    IMPACT_DECAY,
    VOWEL,
    SWELL_DECAY,
    WIDTH,
    ROOM_SEND,
    HALL_SEND,
    DUCK,
    // 19.09.2026, round "fx-psychedelia". Appended.
    SUB_LEVEL,   ///< dB: the sub drop against sfx.level (Sfx.h; it plays mono and ducks under the kick)
    SUB_DUCK,    ///< 0..1: how deep the kick ducks the sub drop
    // 20.09.2026, round "wandering-fx" (Sfx.h): a directed pan trajectory plus a reverb-send
    // trajectory over an event's own length, drawn from its own seed. Off by default, so older
    // sets render unchanged.
    WANDER,        ///< toggle: an event's pan sweeps from one side to the other and its content
                   ///< crosses from dry to the hall's send over its length, instead of the
                   ///< oscillating auto-pan and the constant hall_send fraction
    WANDER_SEND,   ///< 0..1: how far the crossfade reaches by the event's own tail (x = 1)
    // 24.09.2026, the user: "Im SFX-Fenster ist nach wie vor keine Auswahl fuer das Preset". One per
    // family of the effect bank (Sfx.h, SfxPreset): 0 = Auto, the composer's draw per event as before;
    // n = every event of that family plays bank preset n (Sfx::trigger). Appended; Auto everywhere is
    // the composer's own draw, sample for sample.
    PRESET_RISER,
    PRESET_DOWNLIFTER,
    PRESET_IMPACT,
    PRESET_SWEEP,
    PRESET_FORMANT_SHOT,
    PRESET_REVERSE_SWELL,
    PRESET_ZAP,
    PRESET_SQUELCH,
    PRESET_BUBBLE,
    PRESET_REVERSE_CRASH,
    PRESET_ATMOSPHERE,
    PLATE_SEND,   ///< 25.09.2026: the effects' send into the plate (the throw rides it; the hall is the far room now)
    COUNT
};
/** @brief The first of the per-family preset choices, and how many there are. */
constexpr int FirstPreset = PRESET_RISER;
constexpr int NumPresetChoices =
    PRESET_ATMOSPHERE - PRESET_RISER + 1;   ///< how many per-family preset choices there are
}   // namespace sfx
/**
 * @brief Parameters of the physical piano (PLAN 5.8, Piano.h, PianoDesign.h). The first eight build the instrument and
 *        are read when a track loads (a new design, computed off the audio thread); the rest act while it plays.
 */
namespace piano
{
enum : int
{
    INSTRUMENT,   ///< PianoInstrument: Grand, Baby Grand, Upright, Soft (the dull piano of Dok. 5)
    HARDNESS,     ///< factor on the hammer felt's stiffness
    STRIKE,       ///< the strike point, -1 towards the agraffe .. 1 deeper
    UNISON,       ///< cents between the outer strings of a course
    INHARM,       ///< factor on the strings' inharmonicity
    IMPEDANCE,    ///< factor on the bridge's admittance
    STRETCH,      ///< factor on the tuning's stretch
    CONDITION,    ///< random mistuning of each string, cents
    LEVEL,
    PEDAL,         ///< dB; the sustain pedal 0 .. 1 (the middle: the half pedal)
    SYMPATHETIC,   ///< gain on the strings ringing along (1: as the physics gives it)
    PHANTOM,       ///< gain on the longitudinal modes (the phantom partials)
    DAMPER_NOISE,
    MECHANICS,   ///< the damper felt's noise, the key's thump
    WIDTH,       ///< the listening points apart, 0 (mono) .. 1 (bass left, treble right)
    LOW_CUT,     ///< Hz
    DUCK,        ///< dB under the pump
    ROOM_SEND,
    PLATE_SEND,
    HALL_SEND,
    MOD_FIRST,   ///< the modulation block's core (48 knobs; Phase 5b)
    COUNT = MOD_FIRST + 48
};
constexpr int FirstPlayKnob = LEVEL;   ///< the knobs before it change the design
}   // namespace piano
/** @brief Parameters of the string section (PLAN 5.9, Strings.h). */
namespace strings
{
enum : int
{
    LEVEL,      ///< dB
    PLAYERS,    ///< players on a note, 1 .. 6
    VIBRATO,    ///< factor on the players' vibrato depth
    PRESSURE,   ///< factor on the bow force (1: half of Schelleng's upper limit)
    POSITION,   ///< the bow's place, -1 sul tasto .. 0 (a tenth of the string) .. 1 sul ponticello
    SPEED,      ///< factor on the bow speed
    ATTACK,
    RELEASE,   ///< ms: the stroke's start, the bow's lift
    WIDTH,
    LOW_CUT,
    DUCK,
    ROOM_SEND,
    PLATE_SEND,
    HALL_SEND,
    MOD_FIRST,   ///< the modulation block's core (48 knobs; Phase 5b)
    COUNT = MOD_FIRST + 48
};
}
/** @brief Parameters of the choir (PLAN 5.9, Choir.h). */
namespace choir
{
enum : int
{
    LEVEL,     ///< dB
    SINGERS,   ///< singers on a note, 1 .. 6
    VOWEL,     ///< 0 "aah" .. 0.5 "ooh" (o) .. 1 "uuh"
    VIBRATO,   ///< factor on the singers' vibrato depth
    BREATH,    ///< the aspiration noise in the glottal flow
    TENSION,   ///< the voice quality: 0 lax and breathy (Rd 2.7) .. 1 pressed (Rd 0.8)
    ATTACK,
    RELEASE,   ///< ms
    WIDTH,
    LOW_CUT,
    DUCK,
    ROOM_SEND,
    PLATE_SEND,
    HALL_SEND,
    MOD_FIRST,   ///< the modulation block's core (48 knobs; Phase 5b)
    COUNT = MOD_FIRST + 48
};
}
/** @brief Parameters of the brass (PLAN 5.9, Brass.h). */
namespace brass
{
enum : int
{
    LEVEL,        ///< dB
    PLAYERS,      ///< players on a note, 1 .. 4
    PRESSURE,     ///< factor on the blowing pressure (the dynamic and the brightness)
    BRASSINESS,   ///< the nonlinear steepening in the bore (the fortissimo's blare)
    ATTACK,
    RELEASE,   ///< ms
    VIBRATO,   ///< cents
    WIDTH,
    LOW_CUT,
    DUCK,
    ROOM_SEND,
    PLATE_SEND,
    HALL_SEND,
    MOD_FIRST,   ///< the modulation block's core (48 knobs; Phase 5b)
    COUNT = MOD_FIRST + 48
};
}
/** @brief Parameters of the granular cloud (PLAN 5.7, Cloud.h). */
namespace cloud
{
enum : int
{
    LEVEL,
    DENSITY,
    SIZE,
    PITCH,
    SPRAY,
    PAD_SEND,
    KEYS_SEND,
    PLATE_SEND,
    COUNT
};
}
/** @brief Parameters of the timpani (PLAN 5.9, Timpani.h). */
namespace timpani
{
enum : int
{
    LEVEL,      ///< dB
    HARDNESS,   ///< factor on the mallet felt's stiffness
    DECAY,      ///< factor on the modes' decay times
    STRIKE,     ///< the strike point, the fraction of the radius from the centre
    WIDTH,
    LOW_CUT,
    DUCK,
    ROOM_SEND,
    PLATE_SEND,
    HALL_SEND,
    MOD_FIRST,   ///< the modulation block's core (48 knobs; Phase 5b)
    COUNT = MOD_FIRST + 48
};
}
/**
 * @brief The sidechain (PLAN 7.3, Dok. 7): the curve every bus ducks along when the ghost kick triggers -- the attack,
 *        the hold and the release of Ducker.h -- and the depth of the rooms' returns. The depths of the voices are
 *        their own (sub.duck, bass.duck, poly.duck); the composer moves them with the energy (0 in a breakdown).
 */
namespace pump
{
enum : int
{
    ATTACK,
    HOLD,
    RELEASE,
    RETURN_DUCK,
    COUNT
};
}
/**
 * @brief The rooms (PLAN 5.11, Dok. 7): a short room (Totality's FDN, 0.8 to 1.2 s, the pluck's), a plate (Dattorro,
 *        1.2 to 1.8 s, the glue) and a hall (the FDN large, 4 to 8 s, the emotion), each with its return; every send
 *        input high-passed (Dok. 7: 250 to 300 Hz), the returns' highs cut.
 */
namespace sends
{
enum : int
{
    ROOM_SIZE,
    ROOM_DECAY,
    ROOM_RETURN,
    PLATE_DECAY,
    PLATE_DAMPING,
    PLATE_PRE_DELAY,
    PLATE_RETURN,
    HALL_SIZE,
    HALL_DECAY,
    HALL_DAMPING,
    HALL_PRE_DELAY,
    HALL_RETURN,
    LOW_CUT,
    HIGH_CUT,
    HATS_SEND,
    PERC_SEND,
    COUNT
};
}
/** @brief Parameters of a DJ mixer channel (PLAN 6.8): fader, the isolator's three bands (kill at -60 dB), a bipolar
 *         filter (below 0 a low pass, above 0 a high pass), the send into the mixer's effects. */
namespace deck
{
enum : int
{
    FADER,
    LOW,
    MID,
    HIGH,
    FILTER,
    FX_SEND,
    COUNT
};
}
/** @brief Parameters of the DJ mixer's effects: a tempo echo and a hall on one send. */
namespace djfx
{
enum : int
{
    ECHO_TIME,
    FEEDBACK,
    ECHO_RETURN,
    HALL_DECAY,
    HALL_RETURN,
    COUNT
};
}
/** @brief Parameters of the set (PLAN 6.8). */
namespace set
{
enum : int
{
    DRAMATURGY,
    JOURNEY,
    BLEND_BARS,
    MINUTES,
    TRACK_MINUTES,
    COUNT
};
}
/**
 * @brief The performer's controls (PLAN 9, Perform): the master filter, the echo throw of the whole mix into the
 *        mixer's echo, and a mute per group of parts. Live only (Engine::setLive).
 */
namespace perform
{
enum : int
{
    FILTER,
    THROW,
    MUTE_KICK,
    MUTE_BASS,
    MUTE_HATS,
    MUTE_PERC,
    MUTE_LEAD,
    MUTE_SYNTHS,
    MUTE_PADS,
    WHEEL,           ///< the modulation wheel, 0..1 (MIDI CC 1): a source of every voice's matrix (Phase 5b)
    PRESSURE,        ///< the channel pressure, 0..1: another
    KEYBOARD_PART,   ///< what a MIDI keyboard plays (perform::keys, 01.10.2026); off: the keys toggle the mutes
    KEYBOARD_MODE,   ///< 0 Replace: the played voice's generated notes are left out; 1 Layer: it plays over them
    COMPOSER,        ///< on: the composer's notes play; off: only what the keyboard plays
    KEYBOARD_LOWER,   ///< the keys below KeyboardSplit play this voice instead (perform::keys; Off: no split; 02.10.2026)
    KEYBOARD_SPLIT,   ///< the split key: C1 (36) .. C5 (84), 12 a step
    KEYBOARD_SCALE,   ///< Scale Lock: a played key goes to the nearest note of the track's key and scale (not the kit)
    KEYBOARD_VELOCITY,   ///< the velocity curve: as played, soft, hard, fixed (frame::shapeVelocity)
    COUNT
};
/**
 * @brief The keyboard's targets (perform.keyboard_part, 01.10.2026, Engine::queueLive): the kit -- C1 (36) the kick,
 *        C#1 .. C2 the twelve lanes --, the bass, the 303, the six polyphonic voices, the piano, the strings, the choir,
 *        the brass, or by channel (1 kit, 2 bass, 3 303, 4 lead, 5 counter, 6 pluck, 7 arp, 8 pad, 9 stab, 10 kit as
 *        General MIDI's drum channel, 11 piano, 12 strings, 13 choir, 14 brass).
 */
namespace keys
{
enum : int
{
    OFF,
    KIT,
    BASS,
    ACID,
    LEAD,
    COUNTER,
    PLUCK,
    ARP,
    PAD,
    STAB,
    PIANO,
    STRINGS,
    CHOIR,
    BRASS,
    BY_CHANNEL,
    COUNT
};
}
constexpr int Mutes =
    MUTE_PADS - MUTE_KICK + 1;   ///< kick; sub, bass, 303; hats; perc; lead, counter; pluck, arp; pad, stab
}   // namespace perform
/** @brief The OSC cues (Cue.h): on or off, and the UDP port. */
namespace cue
{
enum : int
{
    ENABLED,
    PORT,
    COUNT
};
}
/** @brief The set's dramaturgies (PLAN 6.8). */
enum class DramaturgyE : int
{
    WARM_UP = 0,
    PEAK,
    CLOSING,
    SUNRISE,
    JOURNEY,
    COUNT
};

/** @brief What a percussion lane plays in the groove; decides its patterns and its MIDI note. */
enum class PercRoleE : int
{
    CLOSED_HAT = 0,
    ROLLING_HAT,
    OPEN_HAT,
    RIDE,
    CLAP,
    CLAP_GHOST,
    SNARE,
    RIM,
    SHAKER,
    TOM,
    CONGA,
    NOISE,
    CRASH,
    TAMBOURINE,
    COUNT
};
constexpr int NumPercRoles = static_cast<int>( PercRoleE::COUNT );   ///< number of roles
/** @brief Sound sources of a percussion lane (Phosphene's five). */
enum class PercEngineE : int
{
    NOISE = 0,
    METAL,
    MODAL,
    TONE,
    FM,
    COUNT
};
/** @brief What the noise source plays. */
enum class NoiseTypeE : int
{
    WHITE = 0,
    METAL909,
    COUNT
};
/** @brief The kick's engines: Phosphene's sweep and resonator, and the 909's shaped triangle (Kick.h). */
enum class KickEngineE : int
{
    SWEEP = 0,
    RESONATOR,
    TR909,
    COUNT
};
/** @brief The five style profiles of PLAN 2.1, in the order of compose.style; Uplifting (with Cinematic) is the default. */
enum class StyleE : int
{
    UPLIFTING = 0,
    PROGRESSIVE,
    DREAM_HOUSE,
    ACID,
    DEEP,
    COUNT
};

extern const char *const KeyNames[12];                  ///< names of compose.key, C .. B
extern const char *const ScaleNames[];                  ///< names of compose.scale
extern const char *const StyleNames[];                  ///< names of compose.style
extern const char *const PercRoleNames[NumPercRoles];   ///< names of perc.role

/**
 * @brief The scales of compose.scale (PLAN 2.5): natural minor first (Dok. 4: it dominates), Dorian (Progressive),
 *        harmonic minor (the "trance interval" b6-7, used punctually), minor pentatonic (the leads' stock), Phrygian,
 *        and major for the rest.
 */
enum class ScaleE : int
{
    AEOLIAN = 0,
    DORIAN,
    HARMONIC_MINOR,
    MINOR_PENTATONIC,
    PHRYGIAN,
    IONIAN,
    COUNT
};
/** @brief One scale: its size and the semitones of its degrees above the root. */
struct ScaleDef
{
    int m_size;       ///< number of degrees (5 .. 7)
    int m_steps[7];   ///< semitones of the degrees, ascending from 0
};
/**
 * @brief The definition of scale @p scale (compose.scale order); Aeolian for an index out of range.
 * @param[in] scale  the scale
 * @return its definition
 */
const ScaleDef &scaleDef( int scale );
/**
 * @brief Whether @p semitones above the root lies in scale @p scale.
 * @param[in] scale      the scale
 * @param[in] semitones  the interval above the root
 * @return true when the scale has it
 */
bool inScale( int scale, int semitones );
/** @brief The kick's tuning rules, in the order of kick.tune (Kick.h, tuneToKey). */
enum class KickTuneE : int
{
    FREE = 0,
    KEY,
    FIFTH,
    FLAT_SEVENTH,
    COUNT
};

/**
 * @brief All parameter values of one engine, lock-free readable from the audio thread.
 *
 * Construction builds the registry from the module tables. Not copyable (atomics); use copyValuesFrom() for
 * snapshots.
 */
class ParamStore
{
public:
    ParamStore();
    ParamStore( const ParamStore & ) = delete;
    ParamStore &operator=( const ParamStore & ) = delete;

    /**
     * @brief Number of parameters.
     * @return the number of parameters
     */
    int count() const { return static_cast<int>( m_entries.size() ); }
    /**
     * @brief First id of a module instance; -1 if it does not exist.
     * @param[in] m         the module
     * @param[in] instance  its instance
     * @return the first id of the module instance, -1 if it does not exist
     */
    int base( ModuleE m, int instance = 0 ) const;
    /**
     * @brief Id of parameter @p index of a module instance; -1 if it does not exist.
     * @param[in] m         the module
     * @param[in] instance  its instance
     * @param[in] index     the parameter's index within the module
     * @return the parameter's id, -1 if it does not exist
     */
    int id( ModuleE m, int instance, int index ) const
    {
        const int b = base( m, instance );
        return b < 0 ? -1 : b + index;
    }
    /**
     * @brief Descriptor of @p id.
     * @param[in] id  the parameter
     * @return its descriptor
     */
    const ParamDesc &desc( int id ) const { return *m_entries[static_cast<size_t>( id )].m_desc; }
    /**
     * @brief Full text key of @p id ("perc3.decay").
     * @param[in] id  the parameter
     * @return its full text key
     */
    const std::string &key( int id ) const { return m_entries[static_cast<size_t>( id )].m_key; }
    /**
     * @brief The module, the instance and the index within the module of parameter @p id.
     * @param[in] id  the parameter
     * @return its module
     */
    ModuleE moduleOf( int id ) const { return m_entries[static_cast<size_t>( id )].m_module; }
    /**
     * @copybrief moduleOf
     * @param[in] id  the parameter
     * @return its module's instance
     */
    int instanceOf( int id ) const { return m_entries[static_cast<size_t>( id )].m_instance; }
    /**
     * @copybrief moduleOf
     * @param[in] id  the parameter
     * @return its index within the module
     */
    int indexOf( int id ) const
    {
        const Entry &e = m_entries[static_cast<size_t>( id )];
        return id - base( e.m_module, e.m_instance );
    }
    /**
     * @brief Id for a text key, or -1.
     * @param[in] key  the text key
     * @return its id, -1 when there is none
     */
    int find( std::string_view key ) const;

    /**
     * @brief Current real value.
     * @param[in] id  the parameter
     * @return its current real value
     */
    float get( int id ) const { return m_values[static_cast<size_t>( id )].load( std::memory_order_relaxed ); }
    /**
     * @brief Current value rounded to an integer (Int, Choice, Toggle).
     * @param[in] id  the parameter
     * @return its current value rounded to an integer
     */
    int getInt( int id ) const;
    /**
     * @brief Current value as a switch.
     * @param[in] id  the parameter
     * @return its current value as a switch
     */
    bool getBool( int id ) const { return get( id ) >= 0.5f; }
    /**
     * @brief Sets a real value, clamped to the range (and rounded for discrete curves).
     * @param[in] id     the parameter
     * @param[in] value  the real value
     */
    void set( int id, float value );
    /**
     * @brief Counts the writes to the values (set, resetDefaults, copyValuesFrom). A reader that sees the same count as
     *        before its last read of the values saw no write since (the Deck's cell cache): the count is raised after
     *        the value is stored (release) and read before the values are (acquire).
     * @return the count of writes
     */
    uint32_t version() const { return m_version.load( std::memory_order_acquire ); }
    /**
     * @brief Sets from a normalised 0..1 position.
     * @param[in] id    the parameter
     * @param[in] norm  the position, 0..1
     */
    void setNormalised( int id, float norm ) { set( id, fromNormalised( id, norm ) ); }

    /**
     * @brief Real value to normalised 0..1.
     * @param[in] id     the parameter
     * @param[in] value  a real value
     * @return its normalised position, 0..1
     */
    float toNormalised( int id, float value ) const;
    /**
     * @brief Normalised 0..1 to real value.
     * @param[in] id    the parameter
     * @param[in] norm  a normalised position, 0..1
     * @return the real value there
     */
    float fromNormalised( int id, float norm ) const;

    /** @brief All parameters back to their defaults. */
    void resetDefaults();
    /**
     * @brief Default of @p id: the descriptor's, or the instance's own where instances differ (the kit's lanes).
     * @param[in] id  the parameter
     * @return its default value
     */
    float defaultValue( int id ) const { return m_defaults[static_cast<size_t>( id )]; }
    /**
     * @brief Copies every value from another store (for snapshots on another thread).
     * @param[in] other  the store whose values are copied
     */
    void copyValuesFrom( const ParamStore &other );
    /**
     * @brief Copies the current values of one module instance into @p out, indexed like its table.
     * @param[in]     m         module
     * @param[in]     instance  instance index
     * @param[in,out] out       at least as many floats as the module has parameters
     */
    void readModule( ModuleE m, int instance, float *out ) const;
    /**
     * @brief Number of parameters of a module.
     * @param[in] m  the module
     * @return how many parameters an instance of it has
     */
    static int moduleCount( ModuleE m );

    /**
     * @brief Applies "key=value" assignments separated by whitespace, newlines or ';'.
     *
     * Choice parameters accept their name ("compose.key=F#") or index. Lines starting with '#' are comments.
     * @param[in]  text   the assignments
     * @param[out] error  receives a message for the first bad assignment, may be null
     * @return false if any assignment failed (the good ones are still applied)
     */
    bool parseText( std::string_view text, std::string *error = nullptr );
    /**
     * @brief The text form, one "key=value" per line.
     * @param[in] onlyChanged  leave out parameters at their default
     * @return the text form
     */
    std::string toText( bool onlyChanged ) const;
    /**
     * @brief A value formatted for display ("330 Hz", "F#").
     * @param[in] id  the parameter
     * @return its value formatted for display
     */
    std::string format( int id ) const;

private:
    /** @brief One parameter of the store: its description, its key, its module and instance. */
    struct Entry
    {
        const ParamDesc *m_desc;       ///< its description
        std::string      m_key;        ///< its key ("kick.decay", "perc3.level")
        ModuleE          m_module;     ///< its module
        int              m_instance;   ///< its instance
    };
    std::vector<Entry>                    m_entries;           ///< the parameters, by id
    std::unique_ptr<std::atomic<float>[]> m_values;            ///< their values, real units (atomic: any thread)
    std::atomic<uint32_t>                 m_version{ 0 };      ///< version()
    std::vector<float>                    m_defaults;          ///< their defaults
    std::unordered_map<std::string, int>  m_index;             ///< key -> id
    static constexpr int                  MaxInstances = 16;   ///< the most instances a module can have
    int m_bases[static_cast<int>( ModuleE::COUNT )]
               [MaxInstances] = {};   ///< per module and instance: the id of its first parameter, -1 none
};

}   // namespace parh
