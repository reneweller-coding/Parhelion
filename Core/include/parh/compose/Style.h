/**
 * @file Style.h
 * @brief The five style profiles (PLAN 2.1, 6.7) as the numbers the planner and the composer draw from.
 *
 * A profile is a vector of ranges and weights: tempo, length, the forms and their breakdown share, the harmony (mode,
 * progressions, harmonic rhythm, the Cinematic key change), the groove (bass patterns, hats, ride, percussion), the
 * voices (the lead's archetype and which other voices play), the rooms and the pump, the loudness target. Between two
 * profiles the numbers are interpolated and the weights mixed (morphProfile, compose.morph_to); the six axes that
 * separate the sub-genres (Dok. 2: tempo, drum density, lead archetype, breakdown share, reverb length, palette) are all
 * among them.
 *
 * **Sources.** Dok. 2, 3, 4, 6 and 10 (their tables, as practice values [Q]), corrected where the reference measurement
 * of 29.09.2026 (Tools/ref_stats.json, 30 recordings) disagrees [M]: the tempo centres and the track lengths are the
 * references'; the breakdown share (bars whose low band lies 8 dB under the body: 10 to 15 %, not Dok. 10's 20 to
 * 100 %), the main breakdown's length (Uplifting 39 bars), the gap between the breakdown's and the drop's loudest three
 * seconds (1 to 5 LU, the medians 2.1 Uplifting, 3.3 Progressive, 3.2 Deep, 2.9 Acid; Dok. 6 says 4 to 8), Dream House's bass that never leaves and its long ambient
 * intro, Deep's club tempo (126 to 134; Dok. 3 says 90 to 120) and its long outro (PLAN 13.4).
 *
 * @note The shape (designated initialisers, a morph) follows Totality `Core/include/tot/compose/Style.h` at 4d3c0d2
 *       (29.09.2026); the fields are Parhelion's.
 */
#pragma once
#include "parh/Params.h"
#include <array>

namespace parh {

/** @brief The body templates of the form grammar (PLAN 6.2). */
enum class FormTemplate : int {
    Anthem = 0,   ///< groove, short break, drop 1, the main breakdown, build, the main drop, [break 2, final drop] (Dok. 6)
    Dream,        ///< groove, breakdown, drop, [breakdown 2, drop] -- the piano over the pad (Dream House)
    Acid,         ///< groove, kick pauses of 8 bars between long grooves, the peak (Acid)
    Plateau,      ///< groove, several short breaks between plateaus, one breakdown, a flat drop (Progressive)
    Drift,        ///< a slow groove, a long breakdown, a groove again (Deep/Ambient)
    Count
};
constexpr int kFormTemplates = static_cast<int>(FormTemplate::Count);

/** @brief The core progressions of Dok. 4, as scale degrees (0-based, in a seven-note minor scale). */
enum class Progression : int { Anthem = 0, Circling, Lift, Melancholy, Tension, Count };
constexpr int kProgressions = static_cast<int>(Progression::Count);
/** @brief The degrees of each progression (four chords; Lift repeats its last). */
extern const int kProgressionDegrees[kProgressions][4];
extern const char* const kProgressionNames[kProgressions];   ///< "i-VI-III-VII", ...

/** @brief The bass figures of Dok. 3. */
enum class BassPattern : int { Offbeat = 0, Rolling, Gallop, Walking, Acid, Drone, Count };
constexpr int kBassPatterns = static_cast<int>(BassPattern::Count);
extern const char* const kBassPatternNames[kBassPatterns];

/** @brief What carries the melody (Dok. 10, "Lead-Archetyp"). */
enum class LeadKind : int { Supersaw = 0, Piano, Acid, PluckArp, Pad, Count };

/** @brief One style profile. */
struct StyleProfile {
    const char* name = "";
    float bpmLow = 136.0f, bpmHigh = 140.0f;            ///< the tempo range
    float minutesLow = 7.0f, minutesHigh = 9.0f;        ///< a track alone
    float breakdownLow = 0.15f, breakdownHigh = 0.3f;   ///< share of the track in breakdowns (Dok. 6, 10)
    std::array<float, kFormTemplates> forms{};          ///< weights of the body templates
    int introBars = 32, outroBars = 32;                 ///< the DJ's intro and outro (Dok. 6)
    float introLong = 0.5f;                             ///< chance the intro is twice as long
    float ambientIntro = 0.0f;                          ///< chance of bars without a kick before the intro (the piano's, the pad's)
    int ambientIntroBars = 16;                          ///< how many (16 .. 64; measured: Dream House 40 .. 70)
    float ambientOutro = 0.0f;                          ///< chance the outro ends in bars without a kick
    float gapLu = 2.5f;                                 ///< the main breakdown's loudest 3 s under the drop's (measured, PLAN 13.4)
    // Harmony (Dok. 4).
    float minor = 0.85f;                                ///< chance of a minor key (Knees et al.: 84.8 %)
    std::array<float, static_cast<int>(Scale::Count)> scales{};   ///< the minor modes' weights (Ionian: a major key)
    std::array<float, kProgressions> progressions{};    ///< weights of the core progressions
    int barsPerChord = 1;                               ///< in the drop
    int barsPerChordBreak = 2;                          ///< in the breakdown
    float keyChange = 0.0f;                             ///< chance of a minor third up after the second breakdown
    // Groove (Dok. 3).
    std::array<float, kBassPatterns> bass{};            ///< weights of the bass figures
    float hats16 = 0.6f;                                ///< chance the closed hat plays sixteenths (else eighths)
    float ride = 0.6f, perc = 0.5f, shaker = 0.4f;      ///< chances of the ride in the drop, a percussion loop, a shaker
    float kickSoft = 0.0f;                              ///< 0 hard and layered .. 1 soft (Deep)
    float beatless = 0.0f;                              ///< chance of a track without a kick (Deep)
    // Voices (Dok. 5, 10).
    LeadKind lead = LeadKind::Supersaw;
    float pluck = 0.8f, arp = 0.5f, stab = 0.3f, counter = 0.4f, gate = 0.5f;   ///< chances
    float orchestra = 0.0f;                             ///< chance of the orchestra (Cinematic, PLAN 5.9)
    float hatsDb = 0.0f;                                ///< the hats' level over the knob's (the air: Tools/calibrate.py)
    float tiltDb = 0.0f;                                ///< the master's tilt over the knob's (the air: Tools/calibrate.py)
    // Space and pump (Dok. 7, 10).
    float hallBreakS = 4.0f, hallDropS = 1.5f;          ///< the hall's decay in the breakdown and in the drop
    float bassDuckDb = 8.0f;                            ///< the mid-bass's duck (the sub's is 3 dB more)
    float padDuckDb = 4.0f;                             ///< the pads'
    float targetLufs = -8.0f;                           ///< the loudest part's loudness (the Leveler)
    std::array<float, 5> mix{};                         ///< the styles this profile is (Uplifting .. Deep): the sounds' choice (Presets.h)
};

/** @brief The profile of a style (Style order). */
const StyleProfile& styleProfile(Style s);
/** @brief @p a moved towards @p b by @p t (0..1): ranges interpolated, weights mixed, the discrete fields from the nearer. */
StyleProfile morphProfile(const StyleProfile& a, const StyleProfile& b, float t);
/** @brief The profile the knobs describe: compose.style, morphed towards compose.morph_to by compose.morph. */
StyleProfile profileOf(const ParamStore& p);
/** @brief Draws an index by @p weights with the uniform @p u (0..1); the last index with a weight if all are 0. */
int drawWeighted(const float* weights, int n, float u);

} // namespace parh
