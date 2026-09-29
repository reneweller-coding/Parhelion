/**
 * @file GestureEngine.h
 * @brief The player's hands (PLAN 7.3): what moves the knobs, when, how far and how fast.
 *
 * Two hands, each a process in time: it takes a knob, moves it along a curve to a new place, lets go, rests, takes the
 * next. The rules that make it sound like a person:
 * - **Never more than two hands**, and never both on the same knob.
 * - **Continuity**: a movement starts where the knob was left, so no value ever jumps.
 * - **Human shape**: most movements follow the minimum-jerk profile of a reach (Flash and Hogan 1985); some start
 *   slowly, a few are quick twists that settle; a knob that throws (an echo's feedback) is thrown and caught.
 * - **The energy** of the moment (0..1) sets where a knob tends to go; around that centre the target scatters.
 *
 * **The grid** (Totality, PLAN 7.3: "Kurven auf 4/16/32-Takt-Rastern verankert"): with HandStyle::gridBeats a movement
 * starts on a line of the grid counted from @c start and lasts a whole number of grid cells, so a filter ride of the
 * form begins with a phrase and ends with one. The techno defaults move slowly: a median of 16 bars, rests of about as
 * long -- the meso level of Dok. 8.5's table (ramps of 16 to 32 bars), the micro level being the composer's own events.
 *
 * @note Copied from Ephemeris `Core/include/eph/compose/GestureEngine.h` (namespace eph, prefix EPH_) at d047d79
 *       (27.09.2026); the grid and the techno timing added.
 * @note Copied from Totality `Core/include/tot/compose/GestureEngine.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Score.h"
#include <functional>
#include <vector>

namespace parh {

/** @brief One knob the hands may move, with its range in offsets of the normalised knob. */
struct HandKnob {
    int param = -1;           ///< parameter id
    float low = -0.4f;        ///< lowest offset a hand takes it to
    float high = 0.4f;        ///< highest offset
    float weight = 1.0f;      ///< how often it is chosen
    float atRest = 0.0f;      ///< its centre at energy 0
    float atPeak = 0.0f;      ///< its centre at energy 1
    float scatter = 0.12f;    ///< spread of targets around the centre
    bool throws = false;      ///< moved in quick throws and caught again (an echo's feedback)
    double from = 0.0;        ///< beat before which nobody touches it (a layer that has not entered yet)
    double until = 1e300;     ///< beat after which nobody touches it
};

/** @brief The timing of the hands. */
struct HandStyle {
    double medianSeconds = 30.0;    ///< median duration of a movement (16 bars at 130 BPM)
    double spread = 0.6;            ///< log-normal sigma of the durations
    double longChance = 0.10;       ///< chance of a long sweep (2 to 4 times the median)
    double quickChance = 0.05;      ///< chance of a quick twist (one grid cell)
    double restSeconds = 30.0;      ///< median rest between two movements of one hand
    double gridBeats = 16.0;        ///< movements start on and last multiples of this (0: free)
};

/**
 * @brief Plays two hands over a stretch of a track and writes their gestures into the score.
 * @param score   the score (gestures are appended; tempo map read)
 * @param knobs   the knobs, with their ranges and weights
 * @param style   timing
 * @param energy  the energy at a beat, 0..1
 * @param start   first beat (the grid counts from here)
 * @param end     last beat; no movement runs past it
 * @param rng     the stream of the hands
 */
void playHands(Score& score, const std::vector<HandKnob>& knobs, const HandStyle& style,
               const std::function<float(double)>& energy, double start, double end, Rng& rng);

} // namespace parh
