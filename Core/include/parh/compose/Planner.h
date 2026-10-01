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
 * peak (the whole melody, the strings and the choir), then the build: the kick back, the snare roll, the riser, and
 * before the drop the vacuum (rule 5; Phosphene's four, Dok. 3: "ein bis zwei Zaehlzeiten Stille"): the last beat empty,
 * the last two, the whole bar, or the last beat empty but for the kick on it; drawn on the drop's section stream.
 *
 * **The layer matrix** (PLAN 6.3): per 8-bar block every element off, filtered or on. Every block changes at least one
 * element (rule 1); the intro builds up in the order of Dok. 6 (kick, hats, bass, open hat, percussion, the filtered
 * pluck), the outro takes it apart in reverse; the lead enters in a breakdown, never before the first break (rule 6) and
 * never in the intro or outro (rule 7); the drop brings everything and, block by block, a variation (the arp, the counter,
 * the ride).
 *
 * **Energy** (Dok. 6, table): intro 2 -> 4, groove 5, break 3 -> 6, drop 8, the main breakdown 1 -> 7, build 7 -> 8, the
 * main drop 10, the second break 5, the final drop 9, outro 6 -> 2; every value but the main drop's scattered by half a
 * point, a build never falling under the breakdown before it; a block's energy is its section's at its middle.
 *
 * **Streams** (PLAN 6.9): the form (the template, the lengths, beatless or not) on `form`, the bass figure on `bass`, the
 * cast (which voices the track has) on `matrix`, the curve on `energy`, and every section's own variations (when the arp,
 * the stab, the percussion, the ride and the counter enter, the intro's order, the mini-break) on `section<n>` (n from 1)
 * together with `matrix`. None of the scattered choices touches the kick or the low end, so the breakdown share, and with
 * it the candidate the form keeps, is the form's alone: a rerolled matrix or section leaves the form as it was.
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Score.h"
#include "parh/compose/Style.h"
#include <functional>
#include <string>
#include <vector>

namespace parh {

/** @brief The inner parts of a breakdown (bars, absolute). */
struct BreakdownParts {
    int start = 0;    ///< the breakdown's first bar
    int tease = 0;    ///< where the motif's fragment enters
    int peak = 0;     ///< where the whole melody plays
    int end = 0;      ///< the build's first bar (the breakdown's end)
};

/** @brief The silence before a drop (rule 5): from beat @ref from of bar @ref bar to the drop, the kick alone on the bar's
 *         last beat where @ref kickOn4. */
struct Vacuum {
    int bar = 0;            ///< the bar before the drop
    double from = 3.0;      ///< the beat in the bar where the silence begins (3: the last beat, 2: the last two, 0: the bar)
    bool kickOn4 = false;   ///< the kick still on the last beat (and only the kick)
};

/** @brief What the planner decided. */
struct Plan {
    FormTemplate form = FormTemplate::Anthem;   ///< the track's form
    int bars = 0;                          ///< the track's length
    std::vector<Section> sections;         ///< in order (beats)
    std::vector<LayerBlock> blocks;        ///< one per 8 bars (beats)
    std::vector<BreakdownParts> breakdowns;   ///< every breakdown's parts
    int mainDrop = -1;                     ///< index into sections of the main drop
    int mainBreakdown = -1;                ///< index of the main breakdown (-1: none)
    int firstLeadBar = -1;                 ///< where the lead is first heard
    bool beatless = false;                 ///< no kick at all (Deep)
    int bassPattern = 0;                   ///< BassPattern (Acid where the 303 carries the bass)
    std::vector<int> miniBreaks;           ///< bars in which the kick rests (Dok. 6: "Kick raus fuer 1 Takt")
    std::vector<Vacuum> vacuums;           ///< the silence before every drop (rule 5)
    /** @brief The section of bar @p bar (index), -1 past the end. */
    int sectionAt(int bar) const;
    /** @brief The state of @p l in bar @p bar. */
    LayerState at(int bar, Layer l) const;
    /** @brief The energy (0..10) at bar @p bar, interpolated inside its section. */
    float energyAt(int bar) const;
};

/** @brief A unit's seed by its name ("form", "matrix", "section3"; Composer.h). */
using UnitStream = std::function<uint64_t(const std::string&)>;

/** @brief The seed of unit @p name of a track's @p seed after @p rerolls rerolls (SetFile.h, Curation). */
uint64_t unitSeed(uint64_t seed, const std::string& name, int rerolls = 0);

/**
 * @brief Plans a track.
 * @param prof  the profile
 * @param bars  the length asked for (the plan comes as near as the grammar allows, a multiple of 32)
 * @param stream   the units' seeds (see above)
 * @param mixable  a DJ's track (a set, PLAN 6.8): an intro and an outro of 32 bars at least, both with the beat -- no
 *                 ambient intro or outro, never beatless -- so a blend can lie over them
 */
Plan planTrack(const StyleProfile& prof, int bars, const UnitStream& stream, bool mixable = false);
// (A mixable track that opens a mix -- TrackRequest::opener -- may have the ambient intro a track alone may have.)

/** @brief A performer's "now": from bar @p bar (an 8-bar line) a breakdown with its build and drop, or a drop at once. */
struct Rewrite {
    int bar = -1;   ///< the 8-bar line it begins on, -1 none
    SectionKind kind = SectionKind::Breakdown;   ///< a breakdown (with its build and drop) or a drop at once
};

/**
 * @brief The performer's "Breakdown now" and "Drop now" (PLAN 9, Perform; Phase 6): the plan of planTrack as it was --
 *        the same form, cast and lengths up to bar @p bar (an 8-bar line) --, and from there a breakdown (its build and
 *        a drop after it) or a drop at once, then the outro. A drop after the main drop is the final drop, a breakdown
 *        after it the second break. The units' streams are the plan's own, so everything before @p bar stays as it was --
 *        but a drop's approach: the harmony turns towards it over the eight bars before (VI-VII-i, PLAN 6.5).
 */
Plan planTrackRewritten(const StyleProfile& prof, int bars, const UnitStream& stream, bool mixable, int bar, SectionKind kind);

/**
 * @brief The same for several "now"s one after the other (Phase 7, 30.09.2026: a second "now" keeps the first): each
 *        rewrites the plan the ones before it left, from its own line on, so the plan up to the last line is the plan
 *        that was played up to it. The lines rise; one that does not is left out.
 */
Plan planTrackRewritten(const StyleProfile& prof, int bars, const UnitStream& stream, bool mixable, const std::vector<Rewrite>& rewrites,
                        bool opener = false);

/** @brief Plans a track from a track's seed without rerolls (as composeTrack does). */
Plan planTrack(const StyleProfile& prof, int bars, uint64_t seed, bool mixable = false);

/** @brief The share of @p plan's bars in breaks, breakdowns and builds. */
float breakdownShare(const Plan& plan);

} // namespace parh
