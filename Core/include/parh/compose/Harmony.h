/**
 * @file Harmony.h
 * @brief The harmony of a track (PLAN 6.5, Dok. 4): the key, the progression per section, the chord of every bar, the
 *        voicings.
 *
 * **Key.** Minor by the profile's chance (Knees et al.: 84.8 %), its mode by the profile's weights; the tonic weighted
 * towards D .. A, where the bass and the kick share a window (Dok. 4: A and F minor the workhorses).
 *
 * **Chords.** Triads stacked in thirds on the degrees of a seven-note scale: natural minor's for Aeolian, harmonic minor
 * and the pentatonic (the "VII" of trance is the flat seventh, G in A minor), the mode's own for Dorian, Phrygian and
 * Ionian; harmonic minor lends its major V to the Tension progression in a breakdown (Dok. 4: "V7 aus harmonisch Moll").
 *
 * **Harmonic rhythm.** A progression of the profile per track (the Anthem's i-VI-III-VII, the circling i-VII-VI-VII, the
 * melancholic i-iv-VI-VII, the minor dominant's i-VI-VII-v), a chord every barsPerChord bars in grooves and drops, every
 * barsPerChordBreak in breakdowns; the last two bars before a drop hold VII and the drop resolves to i on its first beat
 * (Dok. 4: "Halten der bVII ueber die letzten zwei Takte"). Acid: one chord for 16 bars, a fourth or a fifth away every
 * 16 (Dok. 4). Cinematic: after the second breakdown a minor third up (the "Epic" key change), by the profile's chance.
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Params.h"
#include "parh/compose/Planner.h"
#include "parh/compose/Style.h"
#include <array>
#include <vector>

namespace parh {

/** @brief A chord: its three tones in semitones above the key's root (root, third, fifth; they may pass 12). */
struct Chord {
    int degree = 0;       ///< scale degree of the root (0-based)
    int tones[3] = {};    ///< semitones above the key's root
    /** @brief The root, semitones above the key's root. */
    int root() const { return tones[0]; }
};

/** @brief The harmony of a whole track. */
struct Harmony {
    int key = 9;                       ///< pitch class of the tonic
    int scale = 0;                     ///< compose.scale
    int progression = 0;               ///< Progression
    std::vector<Chord> bars;           ///< the chord of every bar
    std::vector<int> shift;            ///< the key change in semitones from each bar on (0, or 3 after the Cinematic change)
    int changeBar = -1;                ///< where the key changes (-1: never)
    /** @brief The absolute pitch class of the chord's tone @p i at @p bar. */
    int pc(int bar, int i) const;
};

/** @brief Draws a key and a mode from the profile; @p key and @p scale are set (compose.auto). */
void drawKey(const StyleProfile& prof, Rng& r, int& key, int& scale);
/** @brief The chord on @p degree of @p scale (triads in thirds on the chord scale described above). */
Chord chordOn(int scale, int degree, bool majorFive = false);
/** @brief The harmony of @p plan in @p key and @p scale, from the stream @p seed. */
Harmony composeHarmony(const Plan& plan, const StyleProfile& prof, int key, int scale, uint64_t seed);
/** @brief The pitch of pitch class @p pc at or above @p floor. */
int atOrAbove(int pc, int floor);
/**
 * @brief The pad's four voices over the chord of @p bar: the third or the fifth at the bottom from about E3 (Dok. 4:
 *        "tiefste Stimme Terz oder Quinte, nicht der Grundton"), the smallest movement from @p prev.
 */
std::array<int, 4> voicePad(const Harmony& h, int bar, const std::array<int, 4>& prev, bool first, int floor = 52);

} // namespace parh
