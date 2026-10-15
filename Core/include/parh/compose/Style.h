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
 * @author Rene Weller
 */
#pragma once
#include "parh/Params.h"
#include <array>

namespace parh {

/** @brief The body templates of the form grammar (PLAN 6.2). */
enum class FormTemplateE : int {
    ANTHEM = 0,   ///< groove, short break, drop 1, the main breakdown, build, the main drop, [break 2, final drop] (Dok. 6)
    DREAM,        ///< groove, breakdown, drop, [breakdown 2, drop] -- the piano over the pad (Dream House)
    ACID,         ///< groove, kick pauses of 8 bars between long grooves, the peak (Acid)
    PLATEAU,      ///< groove, several short breaks between plateaus, one breakdown, a flat drop (Progressive)
    DRIFT,        ///< a slow groove, a long breakdown, a groove again (Deep/Ambient)
    COUNT
};
constexpr int FormTemplates = static_cast<int>(FormTemplateE::COUNT);   ///< how many form templates there are

/** @brief The core progressions of Dok. 4, as scale degrees (0-based, in a seven-note minor scale). */
enum class ProgressionE : int { ANTHEM = 0, CIRCLING, LIFT, MELANCHOLY, TENSION, COUNT };
constexpr int Progressions = static_cast<int>(ProgressionE::COUNT);   ///< how many progressions there are
/** @brief The degrees of each progression (four chords; Lift repeats its last). */
extern const int ProgressionDegrees[Progressions][4];
extern const char* const FormTemplateNames[FormTemplates];   ///< "Anthem", "Dream", "Acid", "Plateau", "Drift"
extern const char* const ProgressionNames[Progressions];   ///< "i-VI-III-VII", ...

/** @brief The bass figures of Dok. 3. */
enum class BassPatternE : int { OFFBEAT = 0, ROLLING, GALLOP, WALKING, ACID, DRONE, COUNT };
constexpr int BassPatterns = static_cast<int>(BassPatternE::COUNT);   ///< how many bass figures there are
extern const char* const BassPatternNames[BassPatterns];

/** @brief What carries the melody (Dok. 10, "Lead-Archetyp"). */
enum class LeadKindE : int { SUPERSAW = 0, PIANO, ACID, PLUCK_ARP, PAD, COUNT };

/** @brief One style profile. */
struct StyleProfile {
    const char* m_name = "";   ///< the style's name
    float m_bpmLow = 136.0f;   ///< the tempo range's low end
    float m_bpmHigh = 140.0f;   ///< ... and its high end
    float m_minutesLow = 7.0f;   ///< a track alone: the length's low end, minutes
    float m_minutesHigh = 9.0f;   ///< ... and its high end
    float m_breakdownLow = 0.15f;   ///< the share of the track in breakdowns: low end (Dok. 6, 10)
    float m_breakdownHigh = 0.3f;   ///< ... and high end
    std::array<float, FormTemplates> m_forms{};          ///< weights of the body templates
    int m_introBars = 32;   ///< the DJ's intro, bars (Dok. 6)
    int m_outroBars = 32;   ///< the DJ's outro, bars
    float m_introLong = 0.5f;                             ///< chance the intro is twice as long
    float m_ambientIntro = 0.0f;                          ///< chance of bars without a kick before the intro (the piano's, the pad's)
    int m_ambientIntroBars = 16;                          ///< how many (16 .. 64; measured: Dream House 40 .. 70)
    float m_ambientOutro = 0.0f;                          ///< chance the outro ends in bars without a kick
    float m_gapLu = 2.5f;                                 ///< the main breakdown's loudest 3 s under the drop's (measured, PLAN 13.4)
    float m_introSynthDb = 0.0f;                          ///< the synths' bus in the intro against the drop's, dB (Phase 8, measured)
    // Harmony (Dok. 4).
    float m_minor = 0.85f;                                ///< chance of a minor key (Knees et al.: 84.8 %)
    std::array<float, static_cast<int>(ScaleE::COUNT)> m_scales{};   ///< the minor modes' weights (Ionian: a major key)
    std::array<float, Progressions> m_progressions{};    ///< weights of the core progressions
    int m_barsPerChord = 1;                               ///< in the drop
    int m_barsPerChordBreak = 2;                          ///< in the breakdown
    float m_keyChange = 0.0f;                             ///< chance of a minor third up after the second breakdown
    // Groove (Dok. 3).
    std::array<float, BassPatterns> m_bass{};            ///< weights of the bass figures
    float m_hats16 = 0.6f;                                ///< chance the closed hat plays sixteenths (else eighths)
    float m_ride = 0.6f;   ///< the chance of the ride in the drop
    float m_perc = 0.5f;   ///< the chance of a percussion loop
    float m_shaker = 0.4f;   ///< the chance of a shaker
    float m_kickSoft = 0.0f;                              ///< 0 hard and layered .. 1 soft (Deep)
    float m_beatless = 0.0f;                              ///< chance of a track without a kick (Deep)
    // Voices (Dok. 5, 10).
    LeadKindE m_lead = LeadKindE::SUPERSAW;   ///< the lead voice
    float m_pluck = 0.8f;   ///< the chance of a pluck
    float m_arp = 0.5f;   ///< the chance of an arp
    float m_stab = 0.3f;   ///< the chance of a stab
    float m_counter = 0.4f;   ///< the chance of a counter melody
    float m_gate = 0.5f;   ///< the chance of a trance gate on the pad
    float m_orchestra = 0.0f;                             ///< chance of the orchestra (Cinematic, PLAN 5.9)
    float m_hatsDb = 0.0f;                                ///< the hats' level over the knob's (the air: Tools/calibrate.py)
    float m_tiltDb = 0.0f;                                ///< the master's tilt over the knob's (the air: Tools/calibrate.py)
    // Space and pump (Dok. 7, 10).
    float m_hallBreakS = 4.0f;   ///< the hall's decay in the breakdown, s
    float m_hallDropS = 1.5f;   ///< ... and in the drop
    float m_bassDuckDb = 8.0f;                            ///< the mid-bass's duck (the sub's is 3 dB more)
    float m_padDuckDb = 4.0f;                             ///< the pads'
    float m_targetLufs = -8.0f;                           ///< the loudest part's loudness (the Leveler)
    std::array<float, 5> m_mix{};                         ///< the styles this profile is (Uplifting .. Deep): the sounds' choice (Presets.h)
};

/**
 * @brief The profile of a style (Style order).
 * @param[in] s  the style
 * @return its profile
 */
const StyleProfile& styleProfile(StyleE s);
/**
 * @brief @p a moved towards @p b by @p t (0..1): ranges interpolated, weights mixed, the discrete fields from the
 *        nearer.
 * @param[in] a  the profile at 0
 * @param[in] b  the profile at 1
 * @param[in] t  the place between them, 0..1
 * @return the profile between
 */
StyleProfile morphProfile(const StyleProfile& a, const StyleProfile& b, float t);
/**
 * @brief The profile the knobs describe: compose.style, morphed towards compose.morph_to by compose.morph.
 * @param[in] p  the knobs
 * @return the profile they ask for
 */
StyleProfile profileOf(const ParamStore& p);
/**
 * @brief Draws an index by @p weights with the uniform @p u (0..1); the last index with a weight if all are 0.
 * @param[in] weights  the weights
 * @param[in] n        their count
 * @param[in] u        a uniform draw, 0..1
 * @return the index drawn
 */
int drawWeighted(const float* weights, int n, float u);

} // namespace parh
