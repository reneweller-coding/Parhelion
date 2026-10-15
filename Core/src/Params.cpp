/**
 * @file Params.cpp
 * @brief Module descriptor tables and the parameter store.
 * @note The store below the tables is copied from Totality `Core/src/Params.cpp` at 4d3c0d2 (29.09.2026), which had it
 *       from Ephemeris; the tables of kick, sub, kit, mono synth, mix, master and DJ mixer are Totality's, the
 *       polyphonic voice's is Phosphene's `Core/src/Params.cpp` at 76f7100 (the voice's modulation insert, its hall gate
 *       and its distance cue left out: nothing here plays them); compose, pump and sends are Parhelion's own.
 */
#include "parh/Params.h"
#include "parh/fx/Disperser.h"
#include "parh/synth/WaveTableFile.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace parh {

const char* const KeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const ScaleNames[] = { "Aeolian", "Dorian", "Harmonic Minor", "Minor Pentatonic", "Phrygian", "Ionian" };
const char* const StyleNames[] = { "Uplifting", "Progressive", "Dream House", "Acid", "Deep" };
const char* const PercRoleNames[NumPercRoles] = { "Closed Hat", "Rolling Hat", "Open Hat", "Ride", "Clap", "Clap Ghost",
                                                    "Snare", "Rim", "Shaker", "Tom", "Conga", "Noise", "Crash", "Tambourine" };
const char* const PolyInstanceNames[PolyInstances] = { "lead", "counter", "pluck", "arp", "pad", "stab" };
const char* const LfoShapeNames[] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Smooth Random" };
const char* const LfoSyncNames[] = { "Free", "4 Bars", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16", "1/4 T", "1/8 T" };
const char* const ModSourceNames[] = { "Off", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "Mod Env", "Filter Env", "Velocity", "Key", "Random",
                                        "Wheel", "Pressure", "Energy" };
const char* const ModDestNames[] = { "Off", "Pitch", "Osc 2 Pitch", "Pulse Width", "Table Position", "FM Index", "Cutoff",
                                      "Resonance", "Filter Mode", "Level", "Pan", "Detune" };
/// The other engines' targets (Modulation.h maps each list onto ModDest).
const char* const SynthModDestNames[] = { "Off", "Pitch", "Pulse Width", "Cutoff", "Resonance", "Env Amount", "Drive", "Level", "Pan" };
const char* const PianoModDestNames[] = { "Off", "Pitch", "Hardness", "Level", "Pan" };
const char* const StringsModDestNames[] = { "Off", "Pitch", "Bow Pressure", "Bow Speed", "Bow Position", "Vibrato", "Vibrato Rate",
                                             "Level", "Pan" };
const char* const ChoirModDestNames[] = { "Off", "Pitch", "Vowel", "Tension", "Breath", "Vibrato", "Formant", "Level", "Pan" };
const char* const BrassModDestNames[] = { "Off", "Pitch", "Breath", "Brassiness", "Vibrato", "Level", "Pan" };
const char* const TimpaniModDestNames[] = { "Off", "Pitch", "Hardness", "Strike", "Decay", "Level", "Pan" };

/// The modulation block's core as rows (Phase 5b): the modulation envelope, four LFOs, eight slots with the engine's
/// targets -- the same keys as the polyphonic voice's, so a preset's modulation reads the same everywhere.
#define PARH_MOD_LFO(n) \
    { "lfo" #n "_rate",   "LFO " #n " Rate",   "Hz", 0.01f, 40.0f, 1.0f, CurveE::LOG }, \
    { "lfo" #n "_shape",  "LFO " #n " Shape",  "",   0.0f,  6.0f,  0.0f, CurveE::CHOICE, LfoShapeNames }, \
    { "lfo" #n "_sync",   "LFO " #n " Sync",   "",   0.0f,  9.0f,  0.0f, CurveE::CHOICE, LfoSyncNames }, \
    { "lfo" #n "_retrig", "LFO " #n " Retrig", "",   0.0f,  1.0f,  0.0f, CurveE::TOGGLE }, \
    { "lfo" #n "_fade",   "LFO " #n " Fade",   "s",  0.0f,  8.0f,  0.0f, CurveE::LINEAR }
/** @brief The three knobs of modulation slot @p n: its source, its target from @p DST (up to @p DMAX), its amount. */
#define PARH_MOD_SLOT(n, DST, DMAX) \
    { "mx" #n "_src",    "Mod " #n " Source", "", 0.0f, 12.0f, 0.0f, CurveE::CHOICE, ModSourceNames }, \
    { "mx" #n "_dst",    "Mod " #n " Target", "", 0.0f, DMAX,  0.0f, CurveE::CHOICE, DST }, \
    { "mx" #n "_amount", "Mod " #n " Amount", "", -1.0f, 1.0f, 0.0f, CurveE::LINEAR }
/** @brief The modulation block's core with the targets @p DST (up to @p DMAX): the envelope, four LFOs, eight slots. */
#define PARH_MOD_CORE(DST, DMAX) \
    { "menv_attack",  "Mod Attack",  "ms", 0.1f, 8000.0f,  10.0f, CurveE::LOG }, \
    { "menv_decay",   "Mod Decay",   "ms", 5.0f, 12000.0f, 800.0f, CurveE::LOG }, \
    { "menv_sustain", "Mod Sustain", "",   0.0f, 1.0f,     0.0f, CurveE::LINEAR }, \
    { "menv_release", "Mod Release", "ms", 5.0f, 12000.0f, 400.0f, CurveE::LOG }, \
    PARH_MOD_LFO(1), PARH_MOD_LFO(2), PARH_MOD_LFO(3), PARH_MOD_LFO(4), \
    PARH_MOD_SLOT(1, DST, DMAX), PARH_MOD_SLOT(2, DST, DMAX), PARH_MOD_SLOT(3, DST, DMAX), PARH_MOD_SLOT(4, DST, DMAX), \
    PARH_MOD_SLOT(5, DST, DMAX), PARH_MOD_SLOT(6, DST, DMAX), PARH_MOD_SLOT(7, DST, DMAX), PARH_MOD_SLOT(8, DST, DMAX)

namespace {

/** @brief compose.morph_to: the style a track morphs into */
const char* const MorphToNames[] = { "Off", "Uplifting", "Progressive", "Dream House", "Acid", "Deep" };
/** @brief perform.keyboard_part (perform::keys) */
const char* const KeyboardPartNames[] = { "Off", "Kit", "Bass", "303", "Lead", "Counter", "Pluck", "Arp", "Pad", "Stab", "Piano",
                                           "Strings", "Choir", "Brass", "By channel" };   // perform::keys
const char* const KeyboardModeNames[] = { "Replace", "Layer" };   ///< perform.keyboard_mode
const char* const KeyboardSplitNames[] = { "C1", "C2", "C3", "C4", "C5" };   ///< perform.keyboard_split: 36 .. 84
const char* const KeyboardVelocityNames[] = { "As Played", "Soft", "Hard", "Fixed" };   ///< perform.keyboard_velocity
const char* const KickEngineNames[] = { "Sweep", "Resonator", "909" };   ///< kick.engine
const char* const KickTuneNames[] = { "Free", "Key", "Fifth", "Flat Seventh" };   ///< kick.tune (KickTune)
const char* const KickClipNames[] = { "Soft", "Hard" };   ///< kick.clip
const char* const LockNames[] = { "Off", "Kick" };   ///< sub.lock: the sub's phase locked to the kick or not
const char* const PercEngineNames[] = { "Noise", "Metal", "Modal", "Tone", "FM" };   ///< perc.engine (PercEngine)
const char* const ModeSetNames[] = { "Membrane", "Bar", "Harmonic" };   ///< perc.modes: the modal engine's mode sets
const char* const PercFilterNames[] = { "Low Pass", "Band Pass", "High Pass" };   ///< perc.filter: the lane filter's output
const char* const NoiseTypeNames[] = { "White", "909 Metal" };   ///< perc.noise_type
/** @brief synth.model (FilterModel). */
const char* const FilterModelNames[] = { "Moog Ladder", "Prophet (SSM2040)", "Juno (IR3109)", "Oberheim SEM", "Xpander",
                                          "Diode Ladder (303)", "Korg35 (MS-20)", "Polivoks", "Wasp", "Comb" };
const char* const EchoTimeNames[] = { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };   ///< the echo times, in notes
const char* const DelayTimeNames[] = { "1/16", "1/8", "3/16", "1/4", "3/8", "1/2" };   ///< the tempo delay's times, in notes
const char* const PolyOscNames[] = { "Supersaw", "VA", "FM", "Wavetable" };   ///< poly.osc: the first oscillator's engine
const char* const PolyOsc2Names[] = { "Off", "Supersaw", "VA", "FM", "Wavetable" };   ///< poly.osc2: the second oscillator's engine, or none
/** @brief poly.osc2_interval: the second oscillator's interval */
const char* const PolyOsc2IntervalNames[] = { "-2 Oct", "-1 Oct", "-5th", "Unison", "+5th", "+1 Oct" };
const char* const PolyFilterNames[] = { "Low Pass", "Band Pass", "High Pass", "Notch" };   ///< poly.filter_type: the filter's response
/** @brief poly.filter_model: the filter models */
const char* const PolyFilterModelNames[VoiceFilterModels] = { "State Variable", "Moog Ladder", "Prophet (SSM2040)", "Juno (IR3109)",
                                                                "Oberheim SEM", "Xpander", "Diode Ladder", "Korg35 (MS-20)",
                                                                "Polivoks", "Wasp" };
/// The `table` choice: the six tables written as spectra in code, then the library's (Phosphene's list; WaveTableFile.h).
#define PARH_WT(index, name, id, lane, fallback) name,
/** @brief poly.table: the wavetables. */
const char* const WaveTableNames[] = { "Classic", "Vocal", "Glass", "PWM", "Sync", "Formant Saw",
#include "parh/synth/WaveTableList.inl"
};
#undef PARH_WT
static_assert(sizeof(WaveTableNames) / sizeof(WaveTableNames[0]) == NumWaveTables,
              "the table choice list and NumWaveTables have come apart");
/// Phosphene's six patterns, then the trance gate's classic sixteenth masks (PLAN 5.7, Dok. 5: "x.xx.x.xx.x.x.x.").
const char* const GatePatternNames[] = { "Sixteenths", "Eighths", "Rolling", "Gallop", "3-3-2", "Triplets",
                                          "Trance 1", "Trance 2", "Trance 3" };

/**
 * Natural minor, Dorian, harmonic minor, minor pentatonic, Phrygian, major (PLAN 2.5: Dok. 4, "natuerliches Moll
 * dominiert"; harmonic minor punctually for the augmented second b6-7).
 */
const ScaleDef Scales[static_cast<int>(ScaleE::COUNT)] = {
    { 7, { 0, 2, 3, 5, 7, 8, 10 } },   // Aeolian
    { 7, { 0, 2, 3, 5, 7, 9, 10 } },   // Dorian
    { 7, { 0, 2, 3, 5, 7, 8, 11 } },   // harmonic minor
    { 5, { 0, 3, 5, 7, 10, 0, 0 } },   // minor pentatonic
    { 7, { 0, 1, 3, 5, 7, 8, 10 } },   // Phrygian
    { 7, { 0, 2, 4, 5, 7, 9, 11 } },   // Ionian
};

/**
 * The composer (PLAN 6). 138 BPM and A minor are the Uplifting centre (Dok. 3; Dok. 4: A and F minor the workhorses);
 * a track of eight minutes (Dok. 6).
 */
const ParamDesc ComposeParams[compose::COUNT] = {
    { "bpm",         "Tempo",      "BPM",  90.0f, 150.0f, 138.0f, CurveE::LINEAR },
    { "key",         "Key",        "",      0.0f,  11.0f,   9.0f, CurveE::CHOICE, KeyNames },
    { "scale",       "Scale",      "",      0.0f,   5.0f,   0.0f, CurveE::CHOICE, ScaleNames },
    { "style",       "Style",      "",      0.0f,   4.0f,   0.0f, CurveE::CHOICE, StyleNames },
    { "minutes",     "Length",     "min",   2.0f,  14.0f,   8.0f, CurveE::LINEAR },
    { "auto",        "Auto",       "",      0.0f,   1.0f,   1.0f, CurveE::TOGGLE },
    { "morph_to",    "Morph To",   "",      0.0f,   5.0f,   0.0f, CurveE::CHOICE, MorphToNames },
    { "morph",       "Morph",      "",      0.0f,   1.0f,   0.5f, CurveE::LINEAR },
    { "pick_sounds", "Composer's Sounds", "", 0.0f, 1.0f,   1.0f, CurveE::TOGGLE },
    { "use_ratings", "Favor Ratings", "",   0.0f,   1.0f,   0.0f, CurveE::TOGGLE },
    { "humanize",    "Humanize",   "%",     0.0f,  30.0f,   8.0f, CurveE::LINEAR },
};

/**
 * The kick (PLAN 5.1, Dok. 5): Totality's table. The trance kick is layered -- a sine sweeping from about 150 Hz to its
 * tuned 45 to 65 Hz in some 30 ms, a 909-like body at 100 to 180 Hz (the top layer), a click at 2 to 5 kHz -- shorter
 * than the rolling techno kick (320 ms), tuned to the key [I, to be measured, PLAN 13.4].
 */
const ParamDesc KickParams[kick::COUNT] = {
    { "engine",      "Engine",       "",     0.0f,     2.0f,    0.0f, CurveE::CHOICE, KickEngineNames },
    { "tune",        "Tune",         "",     0.0f,     3.0f,    1.0f, CurveE::CHOICE, KickTuneNames },
    { "pitch_end",   "Pitch End",    "Hz",  30.0f,   120.0f,   52.0f, CurveE::LOG },
    { "pitch_start", "Pitch Start",  "Hz",  60.0f,  1500.0f,  220.0f, CurveE::LOG },
    { "pitch_decay", "Body Decay",   "ms",   5.0f,   300.0f,   24.0f, CurveE::LOG },
    { "punch_decay", "Punch Decay",  "ms",   0.5f,    20.0f,    3.0f, CurveE::LOG },
    { "punch",       "Punch",        "",     0.0f,     1.0f,   0.55f, CurveE::LINEAR },
    { "amp_attack",  "Attack",       "ms",   0.0f,    10.0f,    0.2f, CurveE::LINEAR },
    { "amp_hold",    "Hold",         "ms",   0.0f,   150.0f,   20.0f, CurveE::LINEAR },
    { "amp_decay",   "Decay",        "ms",  20.0f,  1500.0f,  320.0f, CurveE::LOG },
    { "drive",       "Drive",        "",     0.0f,     1.0f,   0.30f, CurveE::LINEAR },
    { "clip",        "Clip",         "",     0.0f,     1.0f,    0.0f, CurveE::CHOICE, KickClipNames },
    { "click_level", "Click",        "",     0.0f,     1.0f,   0.40f, CurveE::LINEAR },
    { "click_tone",  "Click Tone",   "Hz", 500.0f, 12000.0f, 3500.0f, CurveE::LOG },
    { "click_decay", "Click Decay",  "ms",   0.5f,    30.0f,    5.0f, CurveE::LOG },
    { "tone",        "Tone",         "Hz", 200.0f, 20000.0f, 8000.0f, CurveE::LOG },
    { "level",       "Level",        "dB", -36.0f,     6.0f,   -3.0f, CurveE::LINEAR },
    { "tail_limit",  "Tail Limit",   "dB", -60.0f,     0.0f,    0.0f, CurveE::LINEAR },   // 0: off
    { "low_cut",     "Low Cut",      "Hz",  15.0f,    60.0f,   30.0f, CurveE::LOG },
    { "dip_freq",    "Dip Freq",     "Hz", 200.0f,  1500.0f,  400.0f, CurveE::LOG },
    { "dip",         "Dip",          "dB", -12.0f,     0.0f,   -2.0f, CurveE::LINEAR },
    { "top_level",   "Top Level",    "dB", -60.0f,     0.0f,  -10.0f, CurveE::LINEAR },   // -60: off
    { "top_pitch",   "Top Pitch",    "st", -24.0f,     0.0f,   -5.0f, CurveE::LINEAR },
    { "top_decay",   "Top Decay",    "ms",  20.0f,   400.0f,   70.0f, CurveE::LOG },
    { "top_drive",   "Top Drive",    "",     0.0f,     1.0f,   0.40f, CurveE::LINEAR },
    { "top_cut",     "Top Cut",      "Hz",  80.0f,  2000.0f,  100.0f, CurveE::LOG },
};

/**
 * The sub (PLAN 5.2, Dok. 5, 7): a sine, high-passed by the engine at 35 Hz and low-passed at 100 to 120 Hz, ducked
 * harder than the mid-bass (10 to 12 dB), a release of 80 to 150 ms so the note is back on the off-beat.
 */
const ParamDesc SubParams[sub::COUNT] = {
    { "level",        "Level",        "dB", -60.0f,   6.0f,  -4.0f, CurveE::LINEAR },
    { "octave",       "Octave",       "",    -2.0f,   2.0f,   0.0f, CurveE::INT },
    { "attack",       "Attack",       "ms",   0.5f,  50.0f,   1.0f, CurveE::LOG },
    { "decay",        "Decay",        "ms",  20.0f, 2000.0f,  60.0f, CurveE::LOG },
    { "sustain",      "Sustain",      "",     0.0f,   1.0f,   0.0f, CurveE::LINEAR },
    { "release",      "Release",      "ms",  10.0f, 800.0f,  40.0f, CurveE::LOG },
    { "low_pass",     "Low Pass",     "Hz",  40.0f, 300.0f, 110.0f, CurveE::LOG },
    { "drive",        "Drive",        "",     0.0f,   1.0f,   0.1f, CurveE::LINEAR },
    { "lock",         "Kick Lock",    "",     0.0f,   1.0f,   0.0f, CurveE::CHOICE, LockNames },
    { "duck",         "Duck",         "dB",   0.0f,  24.0f,  11.0f, CurveE::LINEAR },
    { "duck_hold",    "Duck Hold",    "ms",   0.0f, 250.0f,  20.0f, CurveE::LINEAR },
    { "duck_release", "Duck Release", "ms",  30.0f, 600.0f, 110.0f, CurveE::LOG },
};

/** A percussion lane: Phosphene's table (Perc.h there), then the noise type (Totality). */
const ParamDesc PercParams[perc::COUNT] = {
    { "active",        "Active",        "",      0.0f,     1.0f,    1.0f, CurveE::TOGGLE },
    { "role",          "Role",          "",      0.0f,    13.0f,    0.0f, CurveE::CHOICE, PercRoleNames },
    { "engine",        "Engine",        "",      0.0f,     4.0f,    0.0f, CurveE::CHOICE, PercEngineNames },
    { "pitch",         "Pitch",         "Hz",   40.0f, 12000.0f,  400.0f, CurveE::LOG },
    { "pitch_amount",  "Pitch Amount",  "x",     1.0f,    16.0f,    1.0f, CurveE::LOG },
    { "pitch_decay",   "Pitch Decay",   "ms",    0.5f,   300.0f,   10.0f, CurveE::LOG },
    { "fm_ratio",      "FM Ratio",      "",     0.25f,     8.0f,   1.41f, CurveE::LINEAR },
    { "fm_index",      "FM Index",      "",      0.0f,     8.0f,    0.0f, CurveE::LINEAR },
    { "mode_set",      "Modes",         "",      0.0f,     2.0f,    0.0f, CurveE::CHOICE, ModeSetNames },
    { "mode_damp",     "Mode Damping",  "",      0.0f,     1.0f,    0.5f, CurveE::LINEAR },
    { "metal_scale",   "Metal Scale",   "x",    0.25f,     4.0f,    1.0f, CurveE::LOG },
    { "noise",         "Noise",         "",      0.0f,     1.0f,    0.0f, CurveE::LINEAR },
    { "noise_decay",   "Noise Decay",   "ms",    2.0f,  3000.0f,   60.0f, CurveE::LOG },
    { "bursts",        "Bursts",        "",      1.0f,     6.0f,    1.0f, CurveE::INT },
    { "burst_spacing", "Burst Spacing", "ms",    2.0f,    40.0f,   10.0f, CurveE::LINEAR },
    { "decay",         "Decay",         "ms",    2.0f,  3000.0f,  120.0f, CurveE::LOG },
    { "filter",        "Filter",        "",      0.0f,     2.0f,    2.0f, CurveE::CHOICE, PercFilterNames },
    { "cutoff",        "Cutoff",        "Hz",  100.0f, 18000.0f, 8000.0f, CurveE::LOG },
    { "resonance",     "Resonance",     "",      0.0f,     1.0f,    0.2f, CurveE::LINEAR },
    { "low_cut",       "Low Cut",       "Hz",  150.0f,  8000.0f,  150.0f, CurveE::LOG },
    { "drive",         "Drive",         "",      0.0f,     1.0f,    0.0f, CurveE::LINEAR },
    { "level",         "Level",         "dB",  -36.0f,     6.0f,  -12.0f, CurveE::LINEAR },
    { "pan",           "Pan",           "",     -1.0f,     1.0f,    0.0f, CurveE::LINEAR },
    { "choke",         "Choke Group",   "",      0.0f,     4.0f,    0.0f, CurveE::INT },
    { "shift",         "Shift",         "ms",  -10.0f,    10.0f,    0.0f, CurveE::LINEAR },
    { "density",       "Density",       "",      0.0f,     1.0f,    0.5f, CurveE::LINEAR },
    { "tune",          "Tune to Key",   "",      0.0f,     1.0f,    0.0f, CurveE::TOGGLE },
    { "pan_depth",     "Pan Depth",     "",      0.0f,     1.0f,    0.0f, CurveE::LINEAR },
    { "pan_bars",      "Pan Period",    "bars",  0.0625f, 16.0f,  0.1875f, CurveE::LOG },
    { "cut_track",     "Cut Tracks Pitch","",    0.0f,     2.0f,    0.0f, CurveE::LINEAR },
    { "noise_type",    "Noise Type",    "",      0.0f,     1.0f,    0.0f, CurveE::CHOICE, NoiseTypeNames },
};

/** The kit's buses and the track bus (PLAN 7). Levels [I] until the reference measurement (PLAN 13.4). */
const ParamDesc MixParams[mix::COUNT] = {
    { "hats_level", "Hats Level", "dB", -24.0f,    12.0f,    -1.0f, CurveE::LINEAR },
    { "hats_cut",   "Hats Cut",   "Hz", 200.0f, 20000.0f, 20000.0f, CurveE::LOG },
    { "perc_level", "Perc Level", "dB", -24.0f,    12.0f,    -4.0f, CurveE::LINEAR },
    { "perc_cut",   "Perc Cut",   "Hz", 200.0f, 20000.0f, 20000.0f, CurveE::LOG },
    { "drum_sat",   "Drum Saturation", "", 0.0f,   1.0f,     0.20f, CurveE::LINEAR },
    { "low_cut",    "Group Low Cut", "Hz", 20.0f,   500.0f,    20.0f, CurveE::LOG },   // off at 20 Hz
    { "synth_level", "Synth Level", "dB", -24.0f,    6.0f,     0.0f, CurveE::LINEAR },
};

/**
 * The track bus and the master (PLAN 7.4, Dok. 7): a gentle glue (about 0.5 dB of gain reduction), a soft clipper at
 * four times the rate, a true-peak limiter at -1 dBTP, the side mono under 120 Hz, no tilt until measured.
 */
const ParamDesc MasterParams[master::COUNT] = {
    { "level",      "Level",      "dB", -24.0f,  12.0f,   0.0f, CurveE::LINEAR },
    { "threshold",  "Glue Threshold", "dB", -40.0f, 0.0f, -12.0f, CurveE::LINEAR },
    { "ratio",      "Glue Ratio", "",     1.0f,  10.0f,   1.5f, CurveE::LOG },
    { "clip",       "Clip Drive", "dB",   0.0f,   9.0f,   2.0f, CurveE::LINEAR },
    { "ceiling",    "Ceiling",    "dBTP", -6.0f,  0.0f,  -1.0f, CurveE::LINEAR },
    { "mono_below", "Mono Below", "Hz",  40.0f, 250.0f, 120.0f, CurveE::LOG },
    { "tilt",       "Tilt",       "dB", -12.0f,  12.0f,   1.5f, CurveE::LINEAR },
};

/**
 * A mono synth voice (PLAN 5.2): Totality's table. The defaults are the trance mid-bass's (Dok. 5: a saw, a 24 dB low
 * pass with a short filter envelope, no unison, mono, a little saturation, 100 to 400 Hz; for the off-beat eighth an amp
 * decay of about 40 ms and no sustain, so the note has died before the next kick), ducked 6 to 10 dB; the 303 line's
 * differ (DefaultAcid).
 */
const ParamDesc SynthParams[synth::COUNT] = {
    { "level",        "Level",        "dB", -60.0f,    6.0f,  -1.0f, CurveE::LINEAR },
    { "pan",          "Pan",          "",    -1.0f,    1.0f,   0.0f, CurveE::LINEAR },
    { "wave",         "Wave",         "",     0.0f,    1.0f,   0.0f, CurveE::LINEAR },
    { "pulse_width",  "Pulse Width",  "",     0.05f,   0.95f,  0.5f, CurveE::LINEAR },
    { "sub_osc",      "Sub Osc",      "",     0.0f,    1.0f,   0.0f, CurveE::LINEAR },
    { "filter",       "Filter",       "",     0.0f,    9.0f,   0.0f, CurveE::CHOICE, FilterModelNames },
    { "cutoff",       "Cutoff",       "Hz",  40.0f, 12000.0f, 420.0f, CurveE::LOG },
    { "resonance",    "Resonance",    "",     0.0f,    1.0f,  0.15f, CurveE::LINEAR },
    { "env_amount",   "Env Amount",   "oct",  0.0f,    6.0f,   2.2f, CurveE::LINEAR },
    { "decay",        "Decay",        "ms",   5.0f, 3000.0f,  90.0f, CurveE::LOG },
    { "accent",       "Accent",       "",     0.0f,    1.0f,   0.2f, CurveE::LINEAR },
    { "amp_attack",   "Attack",       "ms",   0.5f,  500.0f,   0.5f, CurveE::LOG },
    { "amp_decay",    "Amp Decay",    "ms",   5.0f, 3000.0f, 150.0f, CurveE::LOG },
    { "amp_sustain",  "Sustain",      "",     0.0f,    1.0f,   0.0f, CurveE::LINEAR },
    { "amp_release",  "Release",      "ms",   5.0f, 3000.0f,  40.0f, CurveE::LOG },
    { "glide",        "Glide",        "ms",   0.0f,  500.0f,   0.0f, CurveE::LINEAR },
    { "drive",        "Drive",        "",     0.0f,    1.0f,  0.25f, CurveE::LINEAR },
    { "key_track",    "Key Track",    "",     0.0f,    1.0f,   0.5f, CurveE::LINEAR },
    { "low_cut",      "Low Cut",      "Hz",  20.0f,  500.0f,  80.0f, CurveE::LOG },
    { "high_cut",     "High Cut",     "Hz", 500.0f, 20000.0f, 6000.0f, CurveE::LOG },
    { "plate_send",   "Plate Send",   "",     0.0f,    1.0f,   0.0f, CurveE::LINEAR },
    { "room_send",    "Room Send",    "",     0.0f,    1.0f,   0.0f, CurveE::LINEAR },
    { "duck",         "Duck",         "dB",   0.0f,   24.0f,   8.0f, CurveE::LINEAR },
    { "duck_release", "Duck Release", "ms",  30.0f,  600.0f, 110.0f, CurveE::LOG },
    PARH_MOD_CORE(SynthModDestNames, 8.0f),
};

/**
 * The polyphonic voice (PLAN 5.5 to 5.7): Phosphene's table, its order kept but for the entries nothing here plays;
 * poly.duck is in dB here (the ghost kick's depth on the voice), the gate has the trance masks.
 */
const ParamDesc PolyParams[poly::COUNT] = {
    { "osc",            "Oscillator",     "",      0.0f,     3.0f,   0.0f, CurveE::CHOICE, PolyOscNames },
    { "detune",         "Detune",         "",      0.0f,     1.0f,  0.55f, CurveE::LINEAR },
    { "mix",            "Mix",            "",      0.0f,     1.0f,  0.75f, CurveE::LINEAR },
    { "dynamic_detune", "Dynamic Detune", "",      0.0f,     1.0f,   0.6f, CurveE::LINEAR },
    { "wave",           "Wave",           "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "pulse_width",    "Pulse Width",    "",     0.05f,    0.95f,   0.5f, CurveE::LINEAR },
    { "fm_ratio",       "FM Ratio",       "",      0.5f,     8.0f,   2.0f, CurveE::LINEAR },
    { "fm_index",       "FM Index",       "",      0.0f,    10.0f,   2.5f, CurveE::LINEAR },
    { "fm_decay",       "FM Decay",       "ms",    5.0f,  2000.0f, 250.0f, CurveE::LOG },
    { "table",          "Table",          "",      0.0f, static_cast<float>(NumWaveTables - 1), 1.0f, CurveE::CHOICE, WaveTableNames },
    { "position",       "Position",       "",      0.0f,     1.0f,   0.3f, CurveE::LINEAR },
    { "pos_env",        "Position Env",   "",     -1.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "pos_decay",      "Position Decay", "ms",   10.0f,  8000.0f, 1500.0f, CurveE::LOG },
    { "pos_lfo_depth",  "Position LFO",   "",      0.0f,     0.5f,  0.15f, CurveE::LINEAR },
    { "pos_lfo_beats",  "LFO Period",     "beats", 0.25f,   64.0f,  16.0f, CurveE::LOG },
    { "cutoff",         "Cutoff",         "Hz",  200.0f, 18000.0f,10000.0f, CurveE::LOG },
    { "resonance",      "Resonance",      "",      0.0f,     1.0f,  0.15f, CurveE::LINEAR },
    { "env_amount",     "Env Amount",     "oct",   0.0f,     6.0f,   2.0f, CurveE::LINEAR },
    { "filter_decay",   "Filter Decay",   "ms",    5.0f,  3000.0f, 400.0f, CurveE::LOG },
    { "key_track",      "Key Track",      "",      0.0f,     1.0f,   0.5f, CurveE::LINEAR },
    { "hp_floor",       "HP Floor",       "Hz",   40.0f,   400.0f, 220.0f, CurveE::LOG },
    { "hp_track",       "HP Track",       "x f0",  0.0f,     1.0f,   1.0f, CurveE::LINEAR },
    { "amp_attack",     "Attack",         "ms",    0.3f,  2000.0f,   4.0f, CurveE::LOG },
    { "amp_decay",      "Decay",          "ms",    5.0f,  4000.0f, 500.0f, CurveE::LOG },
    { "amp_sustain",    "Sustain",        "",      0.0f,     1.0f,  0.75f, CurveE::LINEAR },
    { "amp_release",    "Release",        "ms",    5.0f,  4000.0f, 180.0f, CurveE::LOG },
    { "width",          "Width",          "",      0.0f,     1.0f,   0.8f, CurveE::LINEAR },
    { "vel_sens",       "Velocity",       "",      0.0f,     1.0f,   0.4f, CurveE::LINEAR },
    { "delay_send",     "Delay Send",     "",      0.0f,     1.0f,   0.3f, CurveE::LINEAR },
    { "delay_left",     "Delay Left",     "",      0.0f,     5.0f,   2.0f, CurveE::CHOICE, DelayTimeNames },
    { "delay_right",    "Delay Right",    "",      0.0f,     5.0f,   3.0f, CurveE::CHOICE, DelayTimeNames },
    { "delay_feedback", "Delay Feedback", "",      0.0f,     0.9f,   0.4f, CurveE::LINEAR },
    { "delay_high_pass","Delay High Pass","Hz",  150.0f,  2000.0f, 350.0f, CurveE::LOG },
    { "delay_low_pass", "Delay Low Pass", "Hz",  800.0f, 16000.0f,6000.0f, CurveE::LOG },
    { "room_send",      "Room Send",      "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "hall_send",      "Hall Send",      "",      0.0f,     1.0f,  0.25f, CurveE::LINEAR },
    { "duck",           "Duck",           "dB",    0.0f,    18.0f,   3.0f, CurveE::LINEAR },
    { "gate",           "Trance Gate",    "",      0.0f,     1.0f,   0.0f, CurveE::TOGGLE },
    { "gate_pattern",   "Gate Pattern",   "",      0.0f,     8.0f,   6.0f, CurveE::CHOICE, GatePatternNames },
    { "gate_depth",     "Gate Depth",     "",      0.0f,     1.0f,  0.85f, CurveE::LINEAR },
    { "gate_duty",      "Gate Duty",      "",     0.05f,     1.0f,   0.5f, CurveE::LINEAR },
    { "gate_attack",    "Gate Attack",    "ms",    0.5f,    60.0f,   3.0f, CurveE::LOG },
    { "gate_release",   "Gate Release",   "ms",    0.5f,   120.0f,  12.0f, CurveE::LOG },
    { "gate_tone",      "Gate Tone",      "",      0.0f,     1.0f,   0.4f, CurveE::LINEAR },
    { "level",          "Level",          "dB",  -36.0f,     6.0f,  -4.0f, CurveE::LINEAR },
    { "disperse",       "Disperse",       "x",     0.0f, static_cast<float>(DisperseStages), 0.0f, CurveE::INT },
    { "disperse_freq",  "Disperse Freq",  "Hz",  200.0f,  8000.0f, 1250.0f, CurveE::LOG },
    { "drift",          "Drift",          "ct",    0.0f,     8.0f,   1.0f, CurveE::LINEAR },
    { "filter_type",    "Filter Type",    "",      0.0f,     3.0f,   0.0f, CurveE::CHOICE, PolyFilterNames },
    { "glide",          "Glide",          "ms",    0.0f,   400.0f,   0.0f, CurveE::LINEAR },
    { "pan",            "Pan",            "",     -1.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "osc2",           "Oscillator 2",   "",      0.0f,     4.0f,   0.0f, CurveE::CHOICE, PolyOsc2Names },
    { "osc2_mix",       "Osc 2 Mix",      "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "osc2_interval",  "Osc 2 Interval", "",      0.0f,     5.0f,   1.0f, CurveE::CHOICE, PolyOsc2IntervalNames },
    { "osc2_detune",    "Osc 2 Detune",   "ct",  -50.0f,    50.0f,   0.0f, CurveE::LINEAR },
    { "lfo_beats",      "Voice LFO",      "beats", 0.25f,   64.0f,   8.0f, CurveE::LOG },
    { "lfo_cutoff",     "LFO to Cutoff",  "oct",   0.0f,     4.0f,   0.0f, CurveE::LINEAR },
    { "lfo_pitch",      "LFO to Pitch",   "ct",    0.0f,    50.0f,   0.0f, CurveE::LINEAR },
    { "lfo_amp",        "LFO to Level",   "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "plate_send",     "Plate Send",     "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "slow_mod",       "Slow Movement",  "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "filter_model",   "Filter Model",   "",      0.0f,     9.0f,   0.0f, CurveE::CHOICE, PolyFilterModelNames },
    { "filter_mode",    "Filter Mode",    "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "filt_attack",    "Filter Attack",  "ms",    0.1f,  4000.0f,   0.1f, CurveE::LOG },
    { "filt_sustain",   "Filter Sustain", "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "filt_release",   "Filter Release", "ms",    5.0f,  8000.0f, 300.0f, CurveE::LOG },
    { "menv_attack",    "Mod Attack",     "ms",    0.1f,  8000.0f,  10.0f, CurveE::LOG },
    { "menv_decay",     "Mod Decay",      "ms",    5.0f, 12000.0f, 800.0f, CurveE::LOG },
    { "menv_sustain",   "Mod Sustain",    "",      0.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "menv_release",   "Mod Release",    "ms",    5.0f, 12000.0f, 400.0f, CurveE::LOG },
    { "lfo1_rate",     "LFO 1 Rate",     "Hz",    0.01f,   40.0f,   1.0f, CurveE::LOG },
    { "lfo1_shape",    "LFO 1 Shape",    "",      0.0f,     6.0f,   0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo1_sync",     "LFO 1 Sync",     "",      0.0f,     9.0f,   0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo1_retrig",   "LFO 1 Retrig",   "",      0.0f,     1.0f,   0.0f, CurveE::TOGGLE },
    { "lfo1_fade",     "LFO 1 Fade",     "s",     0.0f,     8.0f,   0.0f, CurveE::LINEAR },
    { "lfo2_rate",     "LFO 2 Rate",     "Hz",    0.01f,   40.0f,   1.0f, CurveE::LOG },
    { "lfo2_shape",    "LFO 2 Shape",    "",      0.0f,     6.0f,   0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo2_sync",     "LFO 2 Sync",     "",      0.0f,     9.0f,   0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo2_retrig",   "LFO 2 Retrig",   "",      0.0f,     1.0f,   0.0f, CurveE::TOGGLE },
    { "lfo2_fade",     "LFO 2 Fade",     "s",     0.0f,     8.0f,   0.0f, CurveE::LINEAR },
    { "lfo3_rate",     "LFO 3 Rate",     "Hz",    0.01f,   40.0f,   1.0f, CurveE::LOG },
    { "lfo3_shape",    "LFO 3 Shape",    "",      0.0f,     6.0f,   0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo3_sync",     "LFO 3 Sync",     "",      0.0f,     9.0f,   0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo3_retrig",   "LFO 3 Retrig",   "",      0.0f,     1.0f,   0.0f, CurveE::TOGGLE },
    { "lfo3_fade",     "LFO 3 Fade",     "s",     0.0f,     8.0f,   0.0f, CurveE::LINEAR },
    { "lfo4_rate",     "LFO 4 Rate",     "Hz",    0.01f,   40.0f,   1.0f, CurveE::LOG },
    { "lfo4_shape",    "LFO 4 Shape",    "",      0.0f,     6.0f,   0.0f, CurveE::CHOICE, LfoShapeNames },
    { "lfo4_sync",     "LFO 4 Sync",     "",      0.0f,     9.0f,   0.0f, CurveE::CHOICE, LfoSyncNames },
    { "lfo4_retrig",   "LFO 4 Retrig",   "",      0.0f,     1.0f,   0.0f, CurveE::TOGGLE },
    { "lfo4_fade",     "LFO 4 Fade",     "s",     0.0f,     8.0f,   0.0f, CurveE::LINEAR },
    { "mx1_src",       "Mod 1 Source",   "",      0.0f,     12.0f,   0.0f, CurveE::CHOICE, ModSourceNames },
    { "mx1_dst",       "Mod 1 Target",   "",      0.0f,    11.0f,   0.0f, CurveE::CHOICE, ModDestNames },
    { "mx1_amount",    "Mod 1 Amount",   "",     -1.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "mx2_src",       "Mod 2 Source",   "",      0.0f,     12.0f,   0.0f, CurveE::CHOICE, ModSourceNames },
    { "mx2_dst",       "Mod 2 Target",   "",      0.0f,    11.0f,   0.0f, CurveE::CHOICE, ModDestNames },
    { "mx2_amount",    "Mod 2 Amount",   "",     -1.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "mx3_src",       "Mod 3 Source",   "",      0.0f,     12.0f,   0.0f, CurveE::CHOICE, ModSourceNames },
    { "mx3_dst",       "Mod 3 Target",   "",      0.0f,    11.0f,   0.0f, CurveE::CHOICE, ModDestNames },
    { "mx3_amount",    "Mod 3 Amount",   "",     -1.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "mx4_src",       "Mod 4 Source",   "",      0.0f,     12.0f,   0.0f, CurveE::CHOICE, ModSourceNames },
    { "mx4_dst",       "Mod 4 Target",   "",      0.0f,    11.0f,   0.0f, CurveE::CHOICE, ModDestNames },
    { "mx4_amount",    "Mod 4 Amount",   "",     -1.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "mx5_src",       "Mod 5 Source",   "",      0.0f,     12.0f,   0.0f, CurveE::CHOICE, ModSourceNames },
    { "mx5_dst",       "Mod 5 Target",   "",      0.0f,    11.0f,   0.0f, CurveE::CHOICE, ModDestNames },
    { "mx5_amount",    "Mod 5 Amount",   "",     -1.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "mx6_src",       "Mod 6 Source",   "",      0.0f,     12.0f,   0.0f, CurveE::CHOICE, ModSourceNames },
    { "mx6_dst",       "Mod 6 Target",   "",      0.0f,    11.0f,   0.0f, CurveE::CHOICE, ModDestNames },
    { "mx6_amount",    "Mod 6 Amount",   "",     -1.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "mx7_src",       "Mod 7 Source",   "",      0.0f,     12.0f,   0.0f, CurveE::CHOICE, ModSourceNames },
    { "mx7_dst",       "Mod 7 Target",   "",      0.0f,    11.0f,   0.0f, CurveE::CHOICE, ModDestNames },
    { "mx7_amount",    "Mod 7 Amount",   "",     -1.0f,     1.0f,   0.0f, CurveE::LINEAR },
    { "mx8_src",       "Mod 8 Source",   "",      0.0f,     12.0f,   0.0f, CurveE::CHOICE, ModSourceNames },
    { "mx8_dst",       "Mod 8 Target",   "",      0.0f,    11.0f,   0.0f, CurveE::CHOICE, ModDestNames },
    { "mx8_amount",    "Mod 8 Amount",   "",     -1.0f,     1.0f,   0.0f, CurveE::LINEAR },
};

/**
 * The effects (PLAN 5.10): Phosphene's table; sfx.duck is in dB here (the ghost kick's depth on the effects).
 */
const ParamDesc SfxParams[sfx::COUNT] = {
    { "level",        "Level",         "dB", -36.0f,   6.0f,  -3.0f, CurveE::LINEAR },
    { "noise",        "Noise",         "",     0.0f,   1.0f,   0.6f, CurveE::LINEAR },
    { "resonance",    "Resonance",     "",     0.0f,   1.0f,   0.5f, CurveE::LINEAR },
    { "brightness",   "Brightness",    "",     0.0f,   1.0f,   0.5f, CurveE::LINEAR },
    { "impact_decay", "Impact Decay",  "ms",  200.0f, 4000.0f, 1400.0f, CurveE::LOG },
    { "vowel",        "Vowel",         "",     0.0f,   1.0f,   0.3f, CurveE::LINEAR },
    { "swell_decay",  "Swell Decay",   "ms",  200.0f, 6000.0f, 1500.0f, CurveE::LOG },
    { "width",        "Width",         "",     0.0f,   1.0f,   0.7f, CurveE::LINEAR },
    { "room_send",    "Room Send",     "",     0.0f,   1.0f,   0.0f, CurveE::LINEAR },
    { "hall_send",    "Hall Send",     "",     0.0f,   1.0f,   0.35f, CurveE::LINEAR },
    { "duck",         "Duck",          "dB",   0.0f,  18.0f,   2.0f, CurveE::LINEAR },
    { "sub_level",    "Sub Drop",      "dB", -36.0f,   6.0f, -14.0f, CurveE::LINEAR },
    { "sub_duck",     "Sub Duck",      "",     0.0f,   1.0f,   1.0f, CurveE::LINEAR },
    { "wander",       "Wander",        "",     0.0f,   1.0f,   0.0f, CurveE::TOGGLE },
    { "wander_send",  "Wander Send",   "",     0.0f,   1.0f,   0.85f, CurveE::LINEAR },
    { "preset_riser",         "Riser",         "", 0.0f, 256.0f, 0.0f, CurveE::INT },
    { "preset_downlifter",    "Downlifter",    "", 0.0f, 128.0f, 0.0f, CurveE::INT },
    { "preset_impact",        "Impact",        "", 0.0f, 128.0f, 0.0f, CurveE::INT },
    { "preset_sweep",         "Sweep",         "", 0.0f, 256.0f, 0.0f, CurveE::INT },
    { "preset_formant_shot",  "Formant Shot",  "", 0.0f,  96.0f, 0.0f, CurveE::INT },
    { "preset_reverse_swell", "Reverse Swell", "", 0.0f, 256.0f, 0.0f, CurveE::INT },
    { "preset_zap",           "Zap",           "", 0.0f,  96.0f, 0.0f, CurveE::INT },
    { "preset_squelch",       "Squelch",       "", 0.0f, 128.0f, 0.0f, CurveE::INT },
    { "preset_bubble",        "Bubble",        "", 0.0f,  64.0f, 0.0f, CurveE::INT },
    { "preset_reverse_crash", "Reverse Crash", "", 0.0f, 128.0f, 0.0f, CurveE::INT },
    { "preset_atmosphere",    "Atmosphere",    "", 0.0f, 512.0f, 0.0f, CurveE::INT },
    { "plate_send",           "Plate Send",    "", 0.0f, 1.0f, 0.1f, CurveE::LINEAR },
};

const char* const PianoInstrumentChoices[] = { "Grand", "Baby Grand", "Upright", "Soft" };   ///< piano.instrument

/**
 * The physical piano (PLAN 5.8): the instrument first (read at load), then what acts while it plays. Level -6 dB and a
 * low cut at 80 Hz keep it off the kick's and the sub's range; the hall carries it (Dok. 5: 2 to 4 s).
 */
const ParamDesc PianoParams[piano::COUNT] = {
    { "instrument",   "Instrument",   "",      0.0f,   3.0f,   0.0f, CurveE::CHOICE, PianoInstrumentChoices },
    { "hardness",     "Hardness",     "",      0.25f,  4.0f,   1.0f, CurveE::LOG },
    { "strike",       "Strike Point", "",     -1.0f,   1.0f,   0.0f, CurveE::LINEAR },
    { "unison",       "Unison Width", "ct",    0.0f,   6.0f,   1.0f, CurveE::LINEAR },
    { "inharm",       "Inharmonicity","",      0.25f,  3.0f,   1.0f, CurveE::LOG },
    { "impedance",    "Impedance",    "",      0.3f,   3.0f,   1.0f, CurveE::LOG },
    { "stretch",      "Stretch",      "",      0.0f,   2.0f,   1.0f, CurveE::LINEAR },
    { "condition",    "Condition",    "ct",    0.0f,   3.0f,   0.3f, CurveE::LINEAR },
    { "level",        "Level",        "dB",  -36.0f,   6.0f,  -6.0f, CurveE::LINEAR },
    { "pedal",        "Pedal",        "",      0.0f,   1.0f,   0.0f, CurveE::LINEAR },
    { "sympathetic",  "Sympathetic",  "",      0.0f,   2.0f,   1.0f, CurveE::LINEAR },
    { "phantom",      "Phantom",      "",      0.0f,   3.0f,   1.0f, CurveE::LINEAR },
    { "damper_noise", "Damper Noise", "",      0.0f,   1.0f,   0.3f, CurveE::LINEAR },
    { "mechanics",    "Mechanics",    "",      0.0f,   1.0f,   0.3f, CurveE::LINEAR },
    { "width",        "Width",        "",      0.0f,   1.0f,   0.7f, CurveE::LINEAR },
    { "low_cut",      "Low Cut",      "Hz",   20.0f, 400.0f,  80.0f, CurveE::LOG },
    { "duck",         "Duck",         "dB",    0.0f,  18.0f,   2.0f, CurveE::LINEAR },
    { "room_send",    "Room Send",    "",      0.0f,   1.0f,   0.05f, CurveE::LINEAR },
    { "plate_send",   "Plate Send",   "",      0.0f,   1.0f,   0.1f, CurveE::LINEAR },
    { "hall_send",    "Hall Send",    "",      0.0f,   1.0f,   0.35f, CurveE::LINEAR },
    PARH_MOD_CORE(PianoModDestNames, 4.0f),
};

/** The string section (PLAN 5.9): six players a note, the bow at half Schelleng's upper force, the hall behind it. */
const ParamDesc StringsParams[strings::COUNT] = {
    { "level",      "Level",      "dB", -36.0f,   6.0f,  -6.0f, CurveE::LINEAR },
    { "players",    "Players",    "",     1.0f,   6.0f,   6.0f, CurveE::INT },
    { "vibrato",    "Vibrato",    "",     0.0f,   2.0f,   1.0f, CurveE::LINEAR },
    { "pressure",   "Bow Force",  "",     0.3f,   3.0f,   1.0f, CurveE::LOG },
    { "position",   "Bow Position", "",  -1.0f,   1.0f,   0.0f, CurveE::LINEAR },
    { "speed",      "Bow Speed",  "",     0.3f,   3.0f,   1.0f, CurveE::LOG },
    { "attack",     "Attack",     "ms",   5.0f, 800.0f,  90.0f, CurveE::LOG },
    { "release",    "Release",    "ms",  10.0f, 1500.0f, 180.0f, CurveE::LOG },
    { "width",      "Width",      "",     0.0f,   1.0f,   0.55f, CurveE::LINEAR },
    { "low_cut",    "Low Cut",    "Hz",  20.0f, 400.0f,  60.0f, CurveE::LOG },
    { "duck",       "Duck",       "dB",   0.0f,  18.0f,   3.0f, CurveE::LINEAR },
    { "room_send",  "Room Send",  "",     0.0f,   1.0f,   0.05f, CurveE::LINEAR },
    { "plate_send", "Plate Send", "",     0.0f,   1.0f,   0.1f, CurveE::LINEAR },
    { "hall_send",  "Hall Send",  "",     0.0f,   1.0f,   0.45f, CurveE::LINEAR },
    PARH_MOD_CORE(StringsModDestNames, 8.0f),
};

/** The choir (PLAN 5.9): six singers a note on "aah", a modal voice (Rd 1.5), the hall behind it. */
const ParamDesc ChoirParams[choir::COUNT] = {
    { "level",      "Level",      "dB", -36.0f,   6.0f,  -6.0f, CurveE::LINEAR },
    { "singers",    "Singers",    "",     1.0f,   6.0f,   6.0f, CurveE::INT },
    { "vowel",      "Vowel",      "",     0.0f,   1.0f,   0.0f, CurveE::LINEAR },
    { "vibrato",    "Vibrato",    "",     0.0f,   2.0f,   1.0f, CurveE::LINEAR },
    { "breath",     "Breath",     "",     0.0f,   1.0f,   0.25f, CurveE::LINEAR },
    { "tension",    "Tension",    "",     0.0f,   1.0f,   0.4f, CurveE::LINEAR },
    { "attack",     "Attack",     "ms",   5.0f, 1500.0f, 250.0f, CurveE::LOG },
    { "release",    "Release",    "ms",  10.0f, 2000.0f, 400.0f, CurveE::LOG },
    { "width",      "Width",      "",     0.0f,   1.0f,   0.55f, CurveE::LINEAR },
    { "low_cut",    "Low Cut",    "Hz",  20.0f, 400.0f,  90.0f, CurveE::LOG },
    { "duck",       "Duck",       "dB",   0.0f,  18.0f,   3.0f, CurveE::LINEAR },
    { "room_send",  "Room Send",  "",     0.0f,   1.0f,   0.0f, CurveE::LINEAR },
    { "plate_send", "Plate Send", "",     0.0f,   1.0f,   0.1f, CurveE::LINEAR },
    { "hall_send",  "Hall Send",  "",     0.0f,   1.0f,   0.55f, CurveE::LINEAR },
    PARH_MOD_CORE(ChoirModDestNames, 8.0f),
};

/** The brass (PLAN 5.9): three players a note, mezzo-forte, a little blare. */
const ParamDesc BrassParams[brass::COUNT] = {
    { "level",      "Level",      "dB", -36.0f,   6.0f,  -6.0f, CurveE::LINEAR },
    { "players",    "Players",    "",     1.0f,   4.0f,   3.0f, CurveE::INT },
    { "pressure",   "Pressure",   "",     0.3f,   2.0f,   1.0f, CurveE::LOG },
    { "brassiness", "Brassiness", "",     0.0f,   1.0f,   0.35f, CurveE::LINEAR },
    { "attack",     "Attack",     "ms",   5.0f, 800.0f,  40.0f, CurveE::LOG },
    { "release",    "Release",    "ms",  10.0f, 1500.0f, 150.0f, CurveE::LOG },
    { "vibrato",    "Vibrato",    "ct",   0.0f,  40.0f,   6.0f, CurveE::LINEAR },
    { "width",      "Width",      "",     0.0f,   1.0f,   0.6f, CurveE::LINEAR },
    { "low_cut",    "Low Cut",    "Hz",  20.0f, 400.0f,  50.0f, CurveE::LOG },
    { "duck",       "Duck",       "dB",   0.0f,  18.0f,   3.0f, CurveE::LINEAR },
    { "room_send",  "Room Send",  "",     0.0f,   1.0f,   0.05f, CurveE::LINEAR },
    { "plate_send", "Plate Send", "",     0.0f,   1.0f,   0.1f, CurveE::LINEAR },
    { "hall_send",  "Hall Send",  "",     0.0f,   1.0f,   0.45f, CurveE::LINEAR },
    PARH_MOD_CORE(BrassModDestNames, 6.0f),
};

/** The timpani (PLAN 5.9): a felt mallet a third of the radius in from the rim. */
const ParamDesc TimpaniParams[timpani::COUNT] = {
    { "level",      "Level",      "dB", -36.0f,   6.0f,  -6.0f, CurveE::LINEAR },
    { "hardness",   "Hardness",   "",     0.25f,  4.0f,   1.0f, CurveE::LOG },
    { "decay",      "Decay",      "",     0.3f,   3.0f,   1.0f, CurveE::LOG },
    { "strike",     "Strike Point", "",   0.3f,   0.9f,   0.68f, CurveE::LINEAR },
    { "width",      "Width",      "",     0.0f,   1.0f,   0.5f, CurveE::LINEAR },
    { "low_cut",    "Low Cut",    "Hz",  20.0f, 400.0f,  35.0f, CurveE::LOG },
    { "duck",       "Duck",       "dB",   0.0f,  18.0f,   0.0f, CurveE::LINEAR },
    { "room_send",  "Room Send",  "",     0.0f,   1.0f,   0.05f, CurveE::LINEAR },
    { "plate_send", "Plate Send", "",     0.0f,   1.0f,   0.05f, CurveE::LINEAR },
    { "hall_send",  "Hall Send",  "",     0.0f,   1.0f,   0.4f, CurveE::LINEAR },
    PARH_MOD_CORE(TimpaniModDestNames, 6.0f),
};

/** The granular cloud (PLAN 5.7): silent until a track asks for it (Deep); from the pad and the keys, into the plate. */
const ParamDesc CloudParams[cloud::COUNT] = {
    { "level",      "Level",      "dB", -60.0f,    6.0f,  -60.0f, CurveE::LINEAR },
    { "density",    "Density",    "/s",   0.5f,   60.0f,   10.0f, CurveE::LOG },
    { "size",       "Grain Size", "ms",  20.0f, 1000.0f,  260.0f, CurveE::LOG },
    { "pitch",      "Pitch",      "",     0.0f,    1.0f,    0.3f, CurveE::LINEAR },
    { "spray",      "Spray",      "s",   0.05f,    4.5f,    2.5f, CurveE::LOG },
    { "pad_send",   "From Pad",   "",     0.0f,    1.0f,    0.8f, CurveE::LINEAR },
    { "keys_send",  "From Keys",  "",     0.0f,    1.0f,    0.5f, CurveE::LINEAR },
    { "plate_send", "Plate Send", "",     0.0f,    1.0f,    0.5f, CurveE::LINEAR },
};

/**
 * The sidechain's curve (PLAN 7.3, Dok. 7): the fastest attack, a short hold, a release of 80 to 150 ms so a bass on the
 * off-beat is back when it plays; the rooms' returns ducked 3 to 5 dB.
 */
const ParamDesc PumpParams[pump::COUNT] = {
    { "attack",      "Attack",      "ms",  0.1f,  20.0f,   0.5f, CurveE::LOG },
    { "hold",        "Hold",        "ms",  0.0f, 150.0f,  20.0f, CurveE::LINEAR },
    { "release",     "Release",     "ms", 30.0f, 600.0f, 120.0f, CurveE::LOG },
    { "return_duck", "Return Duck", "dB",  0.0f,  12.0f,   4.0f, CurveE::LINEAR },
};

/**
 * The rooms (PLAN 5.11, Dok. 5, 7): a short room of 1 s for the plucks' stereo tail, a plate of 1.5 s with 20 ms of
 * pre-delay for glue, a hall of 6 s with 60 ms for emotion; every send input high-passed at 280 Hz (Dok. 7: 250 to
 * 300 Hz), the returns' highs cut at 9 kHz (Dok. 5: "Hoehen im Reverb beschneiden"). The composer moves the hall's decay
 * with the section (breakdown 2 to 4 s and more, drop 0.8 to 1.5 s).
 */
const ParamDesc SendsParams[sends::COUNT] = {
    { "room_size",      "Room Size",      "",    0.3f,   3.0f,   0.7f, CurveE::LOG },
    { "room_decay",     "Room Decay",     "s",   0.2f,   4.0f,   1.0f, CurveE::LOG },
    { "room_return",    "Room Return",    "dB", -60.0f, 18.0f,   6.0f, CurveE::LINEAR },
    { "plate_decay",    "Plate Decay",    "s",   0.3f,   6.0f,   1.5f, CurveE::LOG },
    { "plate_damping",  "Plate Damping",  "",    0.0f,   1.0f,   0.4f, CurveE::LINEAR },
    { "plate_predelay", "Plate Pre-Delay","ms",  0.0f, 100.0f,  20.0f, CurveE::LINEAR },
    { "plate_return",   "Plate Return",   "dB", -60.0f, 18.0f,   0.0f, CurveE::LINEAR },
    { "hall_size",      "Hall Size",      "",    0.5f,   3.0f,   2.2f, CurveE::LOG },
    { "hall_decay",     "Hall Decay",     "s",   0.5f,  14.0f,   6.0f, CurveE::LOG },
    { "hall_damping",   "Hall Damping",   "",    0.0f,   1.0f,   0.45f, CurveE::LINEAR },
    { "hall_predelay",  "Hall Pre-Delay", "ms",  0.0f, 120.0f,  60.0f, CurveE::LINEAR },
    { "hall_return",    "Hall Return",    "dB", -60.0f, 18.0f,   0.0f, CurveE::LINEAR },
    { "low_cut",        "Send Low Cut",   "Hz",  80.0f, 800.0f, 280.0f, CurveE::LOG },
    { "high_cut",       "Return High Cut","Hz", 2000.0f, 18000.0f, 9000.0f, CurveE::LOG },
    { "hats_send",      "Hats to Room",   "",    0.0f,   1.0f,  0.15f, CurveE::LINEAR },
    { "perc_send",      "Perc to Plate",  "",    0.0f,   1.0f,  0.20f, CurveE::LINEAR },
};

/**
 * A DJ mixer channel (PLAN 6.8): the isolator's bands split at 200 Hz and 2.5 kHz (Linkwitz-Riley), each from a kill at
 * -60 dB to +6; the filter a low pass below 0, a high pass above.
 */
const ParamDesc DeckParams[deck::COUNT] = {
    { "fader",   "Fader",   "dB", -60.0f, 6.0f, 0.0f, CurveE::LINEAR },
    { "low",     "Low",     "dB", -60.0f, 6.0f, 0.0f, CurveE::LINEAR },
    { "mid",     "Mid",     "dB", -60.0f, 6.0f, 0.0f, CurveE::LINEAR },
    { "high",    "High",    "dB", -60.0f, 6.0f, 0.0f, CurveE::LINEAR },
    { "filter",  "Filter",  "",    -1.0f, 1.0f, 0.0f, CurveE::LINEAR },
    { "fx_send", "FX Send", "",     0.0f, 1.0f, 0.0f, CurveE::LINEAR },
};

/** The DJ mixer's effects: a dotted-eighth tape echo and a long hall on one send. */
const ParamDesc DjFxParams[djfx::COUNT] = {
    { "echo_time",   "Echo Time",   "",    0.0f,  5.0f,  4.0f, CurveE::CHOICE, EchoTimeNames },
    { "feedback",    "Feedback",    "",    0.0f,  1.0f, 0.45f, CurveE::LINEAR },
    { "echo_return", "Echo Return", "dB", -60.0f, 6.0f, -3.0f, CurveE::LINEAR },
    { "hall_decay",  "Hall Decay",  "s",   0.5f, 12.0f,  5.0f, CurveE::LOG },
    { "hall_return", "Hall Return", "dB", -60.0f, 12.0f, 6.0f, CurveE::LINEAR },
};

const char* const DramaturgyChoiceNames[] = { "Warm-up", "Peak", "Closing", "Sunrise", "Journey" };   ///< set.dramaturgy: the set's arc
const char* const JourneyNames[] = { "Stay", "Wander" };   ///< set.journey: the key stays or wanders
const char* const BlendNames[] = { "32 bars", "64 bars" };   ///< set.blend: how long two tracks overlap
/** The set (PLAN 6.8): trance tracks are extended mixes, blended over their DJ intros and outros. */
const ParamDesc SetParams[set::COUNT] = {
    { "dramaturgy",    "Dramaturgy", "",    0.0f,   4.0f, 1.0f, CurveE::CHOICE, DramaturgyChoiceNames },
    { "journey",       "Styles",     "",    0.0f,   1.0f, 0.0f, CurveE::CHOICE, JourneyNames },
    { "blend",         "Blend",      "",    0.0f,   1.0f, 0.0f, CurveE::CHOICE, BlendNames },
    { "minutes",       "Set Length", "min", 0.0f, 720.0f, 0.0f, CurveE::LINEAR },
    { "track_minutes", "Track Time", "min", 3.0f,  10.0f, 6.0f, CurveE::LINEAR },
};

/** The performer's controls (live only). */
const ParamDesc PerformParams[perform::COUNT] = {
    { "filter",      "Master Filter", "",  -1.0f, 1.0f, 0.0f, CurveE::LINEAR },
    { "throw",       "Echo Throw",    "",   0.0f, 1.0f, 0.0f, CurveE::LINEAR },
    { "mute_kick",   "Mute Kick",     "",   0.0f, 1.0f, 0.0f, CurveE::TOGGLE },
    { "mute_bass",   "Mute Bass",     "",   0.0f, 1.0f, 0.0f, CurveE::TOGGLE },
    { "mute_hats",   "Mute Hats",     "",   0.0f, 1.0f, 0.0f, CurveE::TOGGLE },
    { "mute_perc",   "Mute Perc",     "",   0.0f, 1.0f, 0.0f, CurveE::TOGGLE },
    { "mute_lead",   "Mute Lead",     "",   0.0f, 1.0f, 0.0f, CurveE::TOGGLE },
    { "mute_synths", "Mute Synths",   "",   0.0f, 1.0f, 0.0f, CurveE::TOGGLE },
    { "mute_pads",   "Mute Pads",     "",   0.0f, 1.0f, 0.0f, CurveE::TOGGLE },
    { "wheel",       "Mod Wheel",     "",   0.0f, 1.0f, 0.0f, CurveE::LINEAR },
    { "pressure",    "Pressure",      "",   0.0f, 1.0f, 0.0f, CurveE::LINEAR },
    // 01.10.2026: a MIDI keyboard plays a voice (Engine::queueLive); the composer can be switched off.
    { "keyboard_part", "Keyboard Plays", "", 0.0f, 14.0f, 0.0f, CurveE::CHOICE, KeyboardPartNames },
    { "keyboard_mode", "Keyboard Mode",  "", 0.0f, 1.0f, 0.0f, CurveE::CHOICE, KeyboardModeNames },
    { "composer",      "Composer",       "", 0.0f, 1.0f, 1.0f, CurveE::TOGGLE },
    // The keyboard's options (02.10.2026), all off by default: a split with a second voice under it, Scale Lock, the
    // velocity curve. The lower keys choose among the voices (not by channel).
    { "keyboard_lower",    "Lower Keys Play", "", 0.0f, 13.0f, 0.0f, CurveE::CHOICE, KeyboardPartNames },
    { "keyboard_split",    "Split At",        "", 0.0f, 4.0f, 2.0f, CurveE::CHOICE, KeyboardSplitNames },
    { "keyboard_scale",    "Scale Lock",      "", 0.0f, 1.0f, 0.0f, CurveE::TOGGLE },
    { "keyboard_velocity", "Velocity Curve",  "", 0.0f, 3.0f, 0.0f, CurveE::CHOICE, KeyboardVelocityNames },
};

/** The OSC cues (Cue.h). */
const ParamDesc CueParams[cue::COUNT] = {
    { "enabled", "OSC Cues", "", 0.0f,    1.0f,     0.0f,    CurveE::TOGGLE },
    { "port",    "OSC Port", "", 1024.0f, 65535.0f, 9000.0f, CurveE::INT },
};

/** The 303 line's own defaults on the synth table (PLAN 5.3): the diode ladder, resonant, accent, slide, drive. */
const char* const DefaultAcid =
    "acid.level=-12; acid.filter=Diode Ladder (303); acid.cutoff=600; acid.resonance=0.72; acid.env_amount=2.5; acid.decay=220;"
    "acid.accent=0.6; acid.sub_osc=0; acid.amp_decay=900; acid.amp_sustain=0.8; acid.amp_release=60; acid.glide=60; acid.drive=0.6;"
    "acid.key_track=0.3; acid.low_cut=120; acid.high_cut=9000; acid.plate_send=0.1; acid.room_send=0.05; acid.duck=6; acid.pan=0.1\n";

/**
 * The six polyphonic voices (PLAN 5.5 to 5.7, Dok. 5), from Phosphene's instance defaults re-voiced for trance:
 *  - lead: the JP-8000 supersaw, detune in the upper-middle range, a mix of 0.8, the second oscillator an octave down
 *    (Dok. 5: "Osc B ... eine Oktave tiefer"), a quarter-note delay, a big hall, 1.5 dB of duck;
 *  - counter: a thinner supersaw an octave above the lead, drier;
 *  - pluck: a VA saw with 0 ms attack, 250 ms decay, no sustain, 200 ms release, a short filter envelope, a ping-pong
 *    delay of a dotted sixteenth (left) against an eighth (right) at 25 % feedback, the short room;
 *  - arp: a pluckier VA, sixteenth-note material, a dotted-eighth delay;
 *  - pad: seven detuned saws of the wavetable voice (the classic saw frame), 800 ms attack, a closed filter the composer
 *    opens, a long hall, 4 dB of duck;
 *  - stab: a short supersaw chord for the off-beat stabs, 3 dB of duck.
 * All numbers [I] until the listening round and the measurement (PLAN 13.4).
 */
const char* const DefaultPoly =
    "lead.osc=Supersaw; lead.detune=0.62; lead.mix=0.8; lead.dynamic_detune=0.5; lead.cutoff=9000; lead.resonance=0.1;"
    "lead.env_amount=1.0; lead.filter_decay=600; lead.amp_attack=3; lead.amp_decay=800; lead.amp_sustain=0.85; lead.amp_release=220;"
    "lead.osc2=Supersaw; lead.osc2_mix=0.35; lead.osc2_interval=-1 Oct; lead.delay_send=0.22; lead.delay_left=1/4; lead.delay_right=3/8;"
    "lead.delay_feedback=0.35; lead.hall_send=0.35; lead.plate_send=0.1; lead.duck=1.5; lead.hp_floor=250; lead.level=-9; lead.pan=-0.05\n"
    "counter.osc=Supersaw; counter.detune=0.45; counter.mix=0.7; counter.cutoff=7000; counter.amp_attack=5; counter.amp_sustain=0.8;"
    "counter.amp_release=180; counter.delay_send=0.15; counter.hall_send=0.25; counter.duck=1.5; counter.hp_floor=400; counter.level=-12;"
    "counter.pan=0.1\n"
    "pluck.osc=VA; pluck.wave=0.15; pluck.cutoff=1800; pluck.resonance=0.2; pluck.env_amount=3.2; pluck.filter_decay=180;"
    "pluck.amp_attack=0.3; pluck.amp_decay=260; pluck.amp_sustain=0; pluck.amp_release=200; pluck.detune=0.3; pluck.mix=0.55;"
    "pluck.width=0.9; pluck.delay_send=0.22; pluck.delay_left=3/16; pluck.delay_right=1/8; pluck.delay_feedback=0.25;"
    "pluck.room_send=0.25; pluck.plate_send=0.05; pluck.hall_send=0.05; pluck.duck=3; pluck.hp_floor=200; pluck.level=-8\n"
    "arp.osc=VA; arp.wave=0.1; arp.cutoff=2600; arp.env_amount=2.5; arp.filter_decay=120; arp.amp_attack=0.3; arp.amp_decay=150;"
    "arp.amp_sustain=0; arp.amp_release=60; arp.detune=0.2; arp.mix=0.5; arp.delay_send=0.2; arp.delay_left=3/8; arp.delay_right=3/16;"
    "arp.delay_feedback=0.3; arp.room_send=0.15; arp.hall_send=0.1; arp.duck=3; arp.hp_floor=300; arp.level=-11; arp.pan=0.15\n"
    "pad.osc=Supersaw; pad.detune=0.5; pad.mix=0.85; pad.dynamic_detune=0; pad.cutoff=2200; pad.resonance=0.1; pad.env_amount=0.5;"
    "pad.filter_decay=2000; pad.amp_attack=800; pad.amp_decay=2000; pad.amp_sustain=0.9; pad.amp_release=1500; pad.width=1;"
    "pad.delay_send=0; pad.hall_send=0.45; pad.plate_send=0.1; pad.duck=4; pad.hp_floor=200; pad.hp_track=0.6; pad.level=-17;"
    "pad.lfo_beats=16; pad.lfo_cutoff=0.3\n"
    "stab.osc=Supersaw; stab.detune=0.4; stab.mix=0.7; stab.dynamic_detune=0.7; stab.cutoff=3500; stab.env_amount=2.0;"
    "stab.filter_decay=200; stab.amp_attack=1; stab.amp_decay=180; stab.amp_sustain=0; stab.amp_release=120; stab.delay_send=0.15;"
    "stab.delay_left=3/16; stab.delay_right=3/8; stab.room_send=0.1; stab.plate_send=0.15; stab.duck=3; stab.hp_floor=250; stab.level=-11\n";

/**
 * The default kit (PLAN 5.4, Dok. 3): Totality's twelve lanes re-rolled for trance -- the closed hat on the sixteenths,
 * the open hat on the off-beat eighths (the "tsss"), a 909 clap and a snare on 2 and 4, the ride for the drop, a crash
 * for the first beat of a phrase, a shaker, a tambourine, a conga, a tom, and the snare roll's own snare. Levels after
 * Totality's calibration against the kick (Dok. 8.7 of its research: CH -8, OH -10, perc -15 dB) [I for trance].
 */
const char* const DefaultKit =
    "perc1.role=Closed Hat; perc1.engine=Noise; perc1.noise_type=909 Metal; perc1.noise=1; perc1.noise_decay=55; perc1.decay=55;"
    "perc1.filter=High Pass; perc1.cutoff=7500; perc1.resonance=0.15; perc1.low_cut=3000; perc1.drive=0.1; perc1.level=-2.5;"
    "perc1.pan=0.1; perc1.choke=1\n"
    "perc2.role=Open Hat; perc2.engine=Noise; perc2.noise_type=909 Metal; perc2.metal_scale=0.95; perc2.noise=1;"
    "perc2.noise_decay=260; perc2.decay=260; perc2.filter=High Pass; perc2.cutoff=6500; perc2.resonance=0.1; perc2.low_cut=3000;"
    "perc2.level=-2.0; perc2.pan=-0.05; perc2.choke=1\n"
    "perc3.role=Clap; perc3.engine=Noise; perc3.noise_decay=180; perc3.bursts=4; perc3.burst_spacing=11; perc3.filter=Band Pass;"
    "perc3.cutoff=1200; perc3.resonance=0.5; perc3.low_cut=300; perc3.drive=0.2; perc3.level=-1.5\n"
    "perc4.role=Snare; perc4.engine=Tone; perc4.pitch=200; perc4.pitch_amount=1.6; perc4.pitch_decay=18; perc4.decay=120;"
    "perc4.noise=0.6; perc4.noise_decay=140; perc4.filter=Low Pass; perc4.cutoff=6000; perc4.low_cut=180; perc4.level=-6\n"
    "perc5.role=Ride; perc5.engine=Metal; perc5.metal_scale=2.2; perc5.noise=0.5; perc5.noise_decay=700; perc5.decay=700;"
    "perc5.filter=High Pass; perc5.cutoff=4500; perc5.low_cut=1500; perc5.level=-11.0; perc5.pan=-0.3\n"
    "perc6.role=Crash; perc6.engine=Metal; perc6.metal_scale=1.6; perc6.noise=0.7; perc6.noise_decay=1800; perc6.decay=1800;"
    "perc6.filter=High Pass; perc6.cutoff=3500; perc6.low_cut=1200; perc6.level=-9.0; perc6.pan=0.25\n"
    "perc7.role=Shaker; perc7.engine=Noise; perc7.noise_decay=60; perc7.filter=High Pass; perc7.cutoff=6500; perc7.resonance=0.3;"
    "perc7.low_cut=2500; perc7.drive=0.3; perc7.level=-11; perc7.pan=0.35; perc7.pan_depth=0.4\n"
    "perc8.role=Tambourine; perc8.engine=Metal; perc8.metal_scale=3.0; perc8.noise=0.6; perc8.noise_decay=90; perc8.decay=90;"
    "perc8.bursts=2; perc8.burst_spacing=8; perc8.filter=High Pass; perc8.cutoff=6000; perc8.low_cut=3000; perc8.level=-13; perc8.pan=-0.35\n"
    "perc9.role=Conga; perc9.engine=Modal; perc9.mode_set=Harmonic; perc9.pitch=330; perc9.decay=240; perc9.filter=Low Pass;"
    "perc9.cutoff=5000; perc9.level=-15; perc9.tune=1; perc9.pan=0.3\n"
    "perc10.role=Tom; perc10.engine=Tone; perc10.pitch=180; perc10.pitch_amount=1.5; perc10.pitch_decay=60; perc10.decay=320;"
    "perc10.noise=0.05; perc10.noise_decay=20; perc10.filter=Low Pass; perc10.cutoff=3000; perc10.level=-15; perc10.tune=1;"
    "perc10.pan=-0.25\n"
    "perc11.role=Rim; perc11.engine=Tone; perc11.pitch=1700; perc11.pitch_amount=1.2; perc11.pitch_decay=3; perc11.decay=25;"
    "perc11.noise=0.15; perc11.noise_decay=8; perc11.filter=Band Pass; perc11.cutoff=1900; perc11.resonance=0.35; perc11.low_cut=400;"
    "perc11.drive=0.6; perc11.level=-9; perc11.tune=1; perc11.pan=-0.2\n"
    "perc12.role=Noise; perc12.engine=Noise; perc12.noise_decay=2500; perc12.filter=Band Pass; perc12.cutoff=2000;"
    "perc12.resonance=0.4; perc12.low_cut=300; perc12.level=-9.0\n";

/** @brief A module of the store: its key prefix, its parameters, how many instances. */
struct ModuleSpec {
    const char* m_prefix;   ///< the key prefix ("kick", "perc" ...)
    const ParamDesc* m_descs;   ///< its parameters
    int m_count;   ///< how many
    int m_instances;   ///< how many instances
    const char* const* m_names = nullptr;   ///< the instances' own prefixes (else prefix + number)
};

/** @brief The modules, in the order of Module. */
const ModuleSpec Modules[static_cast<int>(ModuleE::COUNT)] = {
    { "compose", ComposeParams, compose::COUNT, 1 },
    { "kick",    KickParams,    kick::COUNT,    1 },
    { "sub",     SubParams,     sub::COUNT,     1 },
    { "perc",    PercParams,    perc::COUNT,    PercLanes },
    { "bass",    SynthParams,   synth::COUNT,   1 },
    { "acid",    SynthParams,   synth::COUNT,   1 },
    { "poly",    PolyParams,    poly::COUNT,    PolyInstances, PolyInstanceNames },
    { "sfx",     SfxParams,     sfx::COUNT,     1 },
    { "piano",   PianoParams,   piano::COUNT,   1 },
    { "strings", StringsParams, strings::COUNT, 1 },
    { "choir",   ChoirParams,   choir::COUNT,   1 },
    { "brass",   BrassParams,   brass::COUNT,   1 },
    { "timpani", TimpaniParams, timpani::COUNT, 1 },
    { "cloud",   CloudParams,   cloud::COUNT,   1 },
    { "pump",    PumpParams,    pump::COUNT,    1 },
    { "sends",   SendsParams,   sends::COUNT,   1 },
    { "mix",     MixParams,     mix::COUNT,     1 },
    { "master",  MasterParams,  master::COUNT,  1 },
    { "deck",    DeckParams,    deck::COUNT,    Decks },
    { "djfx",    DjFxParams,    djfx::COUNT,    1 },
    { "set",     SetParams,     set::COUNT,     1 },
    { "perform", PerformParams, perform::COUNT, 1 },
    { "cue",     CueParams,     cue::COUNT,     1 },
};

/** @brief Whether a curve takes whole steps (Int, Choice, Toggle). */
bool isDiscrete(CurveE c) { return c == CurveE::INT || c == CurveE::CHOICE || c == CurveE::TOGGLE; }

/** @brief @p s without spaces, tabs and carriage returns at either end. */
std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

/** @brief Lower case without spaces, for matching choice names ("909 Metal" = "909metal"). */
std::string foldName(std::string_view s)
{
    std::string out;
    for (char ch : s) {
        if (ch == ' ' || ch == '_' || ch == '-') continue;
        out += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
    }
    return out;
}

} // namespace

const ScaleDef& scaleDef(int scale)
{
    return Scales[scale >= 0 && scale < static_cast<int>(ScaleE::COUNT) ? scale : 0];
}

bool inScale(int scale, int semitones)
{
    const ScaleDef& d = scaleDef(scale);
    const int pc = ((semitones % 12) + 12) % 12;
    for (int i = 0; i < d.m_size; ++i) if (d.m_steps[i] == pc) return true;
    return false;
}

ParamStore::ParamStore()
{
    for (auto& row : m_bases) for (int& b : row) b = -1;
    int total = 0;
    for (const ModuleSpec& m : Modules) total += m.m_count * m.m_instances;
    m_entries.reserve(static_cast<size_t>(total));
    for (int mi = 0; mi < static_cast<int>(ModuleE::COUNT); ++mi) {
        const ModuleSpec& m = Modules[mi];
        for (int inst = 0; inst < m.m_instances && inst < MaxInstances; ++inst) {
            m_bases[mi][inst] = static_cast<int>(m_entries.size());
            std::string prefix = m.m_prefix;
            if (m.m_names != nullptr) prefix = m.m_names[inst];
            else if (m.m_instances > 1) prefix += std::to_string(inst + 1);
            for (int p = 0; p < m.m_count; ++p) {
                Entry e{ &m.m_descs[p], prefix + "." + m.m_descs[p].m_key, static_cast<ModuleE>(mi), inst };
                m_index.emplace(e.m_key, static_cast<int>(m_entries.size()));
                m_entries.push_back(std::move(e));
            }
        }
    }
    m_values = std::make_unique<std::atomic<float>[]>(m_entries.size());
    // Defaults: the descriptors', then the lanes' own on top.
    m_defaults.resize(m_entries.size());
    for (int i = 0; i < count(); ++i) {
        m_defaults[static_cast<size_t>(i)] = desc(i).m_defValue;
        m_values[static_cast<size_t>(i)].store(desc(i).m_defValue, std::memory_order_relaxed);
    }
    parseText(DefaultKit);
    parseText(DefaultAcid);
    parseText(DefaultPoly);
    for (int i = 0; i < count(); ++i) m_defaults[static_cast<size_t>(i)] = get(i);
}

int ParamStore::base(ModuleE m, int instance) const
{
    const int mi = static_cast<int>(m);
    if (mi < 0 || mi >= static_cast<int>(ModuleE::COUNT) || instance < 0 || instance >= MaxInstances) return -1;
    return m_bases[mi][instance];
}

int ParamStore::find(std::string_view key) const
{
    const auto it = m_index.find(std::string(key));
    return it == m_index.end() ? -1 : it->second;
}

int ParamStore::getInt(int id) const
{
    return static_cast<int>(std::lround(get(id)));
}

void ParamStore::set(int id, float value)
{
    if (id < 0 || id >= count()) return;
    const ParamDesc& d = desc(id);
    if (!(value == value)) value = m_defaults.empty() ? d.m_defValue : m_defaults[static_cast<size_t>(id)];   // NaN
    float v = value < d.m_minValue ? d.m_minValue : (value > d.m_maxValue ? d.m_maxValue : value);
    if (isDiscrete(d.m_curve)) v = std::round(v);
    m_values[static_cast<size_t>(id)].store(v, std::memory_order_relaxed);
    m_version.fetch_add(1, std::memory_order_release);
}

float ParamStore::toNormalised(int id, float value) const
{
    const ParamDesc& d = desc(id);
    if (d.m_maxValue <= d.m_minValue) return 0.0f;
    float n;
    if (d.m_curve == CurveE::LOG) n = std::log(value / d.m_minValue) / std::log(d.m_maxValue / d.m_minValue);
    else n = (value - d.m_minValue) / (d.m_maxValue - d.m_minValue);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

float ParamStore::fromNormalised(int id, float norm) const
{
    const ParamDesc& d = desc(id);
    const float n = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
    float v;
    if (d.m_curve == CurveE::LOG) v = d.m_minValue * std::pow(d.m_maxValue / d.m_minValue, n);
    else v = d.m_minValue + n * (d.m_maxValue - d.m_minValue);
    if (isDiscrete(d.m_curve)) v = std::round(v);
    return v;
}

void ParamStore::resetDefaults()
{
    for (int i = 0; i < count(); ++i) m_values[static_cast<size_t>(i)].store(m_defaults[static_cast<size_t>(i)], std::memory_order_relaxed);
    m_version.fetch_add(1, std::memory_order_release);
}

int ParamStore::moduleCount(ModuleE m)
{
    const int mi = static_cast<int>(m);
    return mi >= 0 && mi < static_cast<int>(ModuleE::COUNT) ? Modules[mi].m_count : 0;
}

void ParamStore::readModule(ModuleE m, int instance, float* out) const
{
    const int b = base(m, instance);
    if (b < 0) return;
    const int n = moduleCount(m);
    for (int i = 0; i < n; ++i) out[i] = get(b + i);
}

void ParamStore::copyValuesFrom(const ParamStore& other)
{
    const int n = count() < other.count() ? count() : other.count();
    for (int i = 0; i < n; ++i) m_values[static_cast<size_t>(i)].store(other.get(i), std::memory_order_relaxed);
    m_version.fetch_add(1, std::memory_order_release);
}

bool ParamStore::parseText(std::string_view text, std::string* error)
{
    // Split into assignments. Newlines and ';' always separate; whitespace separates only where the next word
    // contains '=' -- so a choice name with a space ("perc1.noise_type=909 Metal") stays one value while "a=1 b=2"
    // is still two assignments. '#' starts a comment to the end of the line.
    std::vector<std::string> items;
    size_t pos = 0;
    bool newItem = true;
    while (pos < text.size()) {
        const char ch = text[pos];
        if (ch == '\n' || ch == ';' || ch == '\r') { newItem = true; ++pos; continue; }
        if (ch == ' ' || ch == '\t') { ++pos; continue; }
        if (ch == '#') { while (pos < text.size() && text[pos] != '\n') ++pos; continue; }
        size_t end = pos;
        while (end < text.size() && text[end] != '\n' && text[end] != ';' && text[end] != '\r' && text[end] != ' ' && text[end] != '\t') ++end;
        const std::string_view word = text.substr(pos, end - pos);
        pos = end;
        if (newItem || word.find('=') != std::string_view::npos || items.empty()) items.emplace_back(word);
        else { items.back() += ' '; items.back() += word; }
        newItem = false;
    }

    bool ok = true;
    for (const std::string& item : items) {
        const std::string_view tok = trim(item);
        const size_t eq = tok.find('=');
        if (eq == std::string_view::npos) {
            if (error && ok) *error = "missing '=' in \"" + std::string(tok) + "\"";
            ok = false;
            continue;
        }
        const std::string_view k = trim(tok.substr(0, eq)), v = trim(tok.substr(eq + 1));
        const int id = find(k);
        if (id < 0) {
            if (error && ok) *error = "unknown parameter \"" + std::string(k) + "\"";
            ok = false;
            continue;
        }
        const ParamDesc& d = desc(id);
        bool matched = false;
        if (d.m_choices != nullptr || d.m_curve == CurveE::TOGGLE) {
            const std::string fv = foldName(v);
            if (d.m_curve == CurveE::TOGGLE && (fv == "on" || fv == "off")) { set(id, fv == "on" ? 1.0f : 0.0f); matched = true; }
            for (int c = 0; !matched && d.m_choices != nullptr && c <= static_cast<int>(d.m_maxValue); ++c) {
                if (fv == foldName(d.m_choices[c])) { set(id, static_cast<float>(c)); matched = true; }
            }
        }
        if (!matched) {
            const std::string vs(v);
            char* stop = nullptr;
            const double x = std::strtod(vs.c_str(), &stop);
            if (vs.empty() || stop == nullptr || *stop != 0) {
                if (error && ok) *error = "bad value \"" + vs + "\" for " + std::string(k);
                ok = false;
                continue;
            }
            set(id, static_cast<float>(x));
        }
    }
    return ok;
}

std::string ParamStore::toText(bool onlyChanged) const
{
    std::string out;
    char buf[64];
    for (int i = 0; i < count(); ++i) {
        const float v = get(i);
        if (onlyChanged && v == m_defaults[static_cast<size_t>(i)]) continue;
        // %.9g round-trips every float exactly.
        std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
        out += key(i);
        out += '=';
        out += buf;
        out += '\n';
    }
    return out;
}

std::string ParamStore::format(int id) const
{
    const ParamDesc& d = desc(id);
    const float v = get(id);
    if (d.m_curve == CurveE::CHOICE && d.m_choices != nullptr) return d.m_choices[getInt(id)];
    if (d.m_curve == CurveE::TOGGLE) return v >= 0.5f ? "On" : "Off";
    char buf[64];
    if (d.m_curve == CurveE::INT) std::snprintf(buf, sizeof(buf), "%d", getInt(id));
    else if (std::fabs(v) >= 100.0f) std::snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(v));
    else if (std::fabs(v) >= 10.0f) std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    else std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    std::string s = buf;
    if (d.m_unit != nullptr && d.m_unit[0] != 0) { s += ' '; s += d.m_unit; }
    return s;
}

} // namespace parh
