/**
 * @file Planner.h
 * @brief The planner (PLAN 6.2 to 6.4): the sections, the layer matrix and the energy of a track, before a note exists.
 *
 * **Sections** (Dok. 6, 10). A body template per profile (Style.h, FormTemplate), each a sequence of sections with
 * lengths from {8, 16, 32, 64}:
 * @code
 *   Anthem   intro groove break drop breakdown build drop [break drop] outro        (Uplifting, the reference arrangement)
 *   Dream    intro groove breakdown build drop [breakdown build drop] outro          (Dream House)
 *   Acid     intro groove (break groove)+ drop outro                                  (Acid: the breaks are kick pauses)
 *   Plateau  intro groove (break groove)+ breakdown build drop outro                  (Progressive: plateaus, one breakdown)
 *   Drift    intro groove breakdown groove outro                                      (Deep: the breakdown is the track)
 * @endcode
 * Eight candidates are drawn and the one nearest the track's length and the profile's breakdown share is kept; the
 * total is a multiple of 32 bars (a set's bass swap lies on a 32-bar line), every section starts on an 8-bar line.
 *
 * **The breakdown** is a song of its own (Dok. 6, rule 2): intro (the pad alone), tease (the motif's fragment, filtered),
 * peak (the whole melody, the strings and the choir), then the build: the kick back, the snare roll, the riser, the last
 * beat empty (rule 5).
 *
 * **The layer matrix** (PLAN 6.3): per 8-bar block every element off, filtered or on. Every block changes at least one
 * element (rule 1); the intro builds up in the order of Dok. 6 (kick, hats, bass, open hat, percussion, the filtered
 * pluck), the outro takes it apart in reverse; the lead enters in a breakdown, never before the first break (rule 6) and
 * never in the intro or outro (rule 7); the drop brings everything and, block by block, a variation (the arp, the counter,
 * the ride).
 *
 * **Energy** (Dok. 6, table): intro 2 -> 4, groove 5, break 3 -> 6, drop 8, the main breakdown 1 -> 7, build 7 -> 8, the
 * main drop 10, the second break 5, the final drop 9, outro 6 -> 2; a block's energy is its section's at its middle.
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Score.h"
#include "parh/compose/Style.h"
#include <vector>

namespace parh {

/** @brief The inner parts of a breakdown (bars, absolute). */
struct BreakdownParts {
    int start = 0;    ///< the breakdown's first bar
    int tease = 0;    ///< where the motif's fragment enters
    int peak = 0;     ///< where the whole melody plays
    int end = 0;      ///< the build's first bar (the breakdown's end)
};

/** @brief What the planner decided. */
struct Plan {
    FormTemplate form = FormTemplate::Anthem;
    int bars = 0;                          ///< the track's length
    std::vector<Section> sections;         ///< in order (beats)
    std::vector<LayerBlock> blocks;        ///< one per 8 bars (beats)
    std::vector<BreakdownParts> breakdowns;   ///< every breakdown's parts
    int mainDrop = -1;                     ///< index into sections of the main drop
    int mainBreakdown = -1;                ///< index of the main breakdown (-1: none)
    int firstLeadBar = -1;                 ///< where the lead is first heard
    bool beatless = false;                 ///< no kick at all (Deep)
    std::vector<int> miniBreaks;           ///< bars in which the kick rests (Dok. 6: "Kick raus fuer 1 Takt")
    std::vector<int> vacuums;              ///< bars whose last beat is empty (the bar before a drop, rule 5)
    /** @brief The section of bar @p bar (index), -1 past the end. */
    int sectionAt(int bar) const;
    /** @brief The state of @p l in bar @p bar. */
    LayerState at(int bar, Layer l) const;
    /** @brief The energy (0..10) at bar @p bar, interpolated inside its section. */
    float energyAt(int bar) const;
};

/**
 * @brief Plans a track.
 * @param prof  the profile
 * @param bars  the length asked for (the plan comes as near as the grammar allows, a multiple of 32)
 * @param seed  the stream `form`
 */
Plan planTrack(const StyleProfile& prof, int bars, uint64_t seed);

/** @brief The share of @p plan's bars in breaks, breakdowns and builds. */
float breakdownShare(const Plan& plan);

} // namespace parh
