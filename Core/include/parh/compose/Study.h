/**
 * @file Study.h
 * @brief The study of Phases 1 and 2 (PLAN 14): one short Uplifting track in the form of Dok. 6, fixed, so that the pump,
 *        the low end, the voices, the gate and the rooms can be heard as a track before the planner exists.
 *
 * The shape, 104 bars (3 minutes at 138 BPM), every section on an 8-bar line:
 * @code
 *   bars   0 .. 15   intro      kick; the closed hat from bar 8                                   energy 2 -> 4
 *   bars  16 .. 31   groove     sub and off-beat mid-bass, open hat; clap, pluck and a filtered pad from bar 24   5
 *   bars  32 .. 47   breakdown  kick and bass out: the pad alone, the pluck filtered, from bar 40 the lead's tease  3 -> 6
 *   bars  48 .. 55   build      the tease, a snare roll that thickens from quarters to 32nds, the last beat empty 6 -> 8
 *   bars  56 .. 87   drop       everything; the pad through the trance gate, the ride, from bar 72 the arp       9 -> 10
 *   bars  88 .. 103  outro      the lead and the pad leave, then the pluck; the last 8 bars kick, bass and hats  6 -> 2
 * @endcode
 * Harmony (Dok. 4): i-VI-III-VII, a chord a bar (two in the breakdown), the last two bars of the build on VII, resolved on
 * the drop's first beat; the pad voiced from about A3 up with a third or fifth at the bottom; the bass on the chord's
 * root, the sub an octave under it, both on the off-beat eighths. The lead is a stand-in until Phase 3: a two-bar motif
 * that opens with a leap and walks down, in an 8-bar phrase A A' B A''.
 *
 * The pump (PLAN 7.3): the ghost kick on every quarter, the pad's and the returns' duck taken to nothing through the
 * breakdown and brought back over the last two bars before the drop; the pad's filter opens over the breakdown, the hall
 * grows there. The sections and the layer matrix go into the score (Score.h) as the planner will write them.
 */
#pragma once
#include "parh/Params.h"
#include "parh/Score.h"
#include <cstdint>

namespace parh {

/**
 * @brief Composes the study.
 * @param p    the knobs (compose.bpm, compose.key, compose.scale, compose.humanize, and the voices' for the automation)
 * @param seed the seed
 * @return the score, sorted
 */
Score composeStudy(const ParamStore& p, uint64_t seed);

} // namespace parh
