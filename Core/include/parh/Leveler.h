/**
 * @file Leveler.h
 * @brief Every track as loud as its style means, its parts balanced against the kick, its breakdown where the references
 *        put it (PLAN 7.5).
 *
 * What plays is drawn anew for every track, so tracks of one style come out dB apart. The composer marks where each track
 * is at its loudest and what that part should measure (LevelMark); levelScore renders it and sets three corrections, all
 * of them part of the score (live and offline play them alike):
 *
 *  1. **The balance** (Totality's Phase 18): the loudest sample of every part -- each lane of the kit, the bass, the 303,
 *     the six polyphonic voices, the effects -- against the kick's, read in the main drop and before it; a part outside
 *     its window is moved to the window's edge (LevelMark::balDb). The windows start from the practice of Dok. 5 and 7
 *     (the lead up front, the pad and the returns behind it, the hats at the fader levels of the research) and are [I]
 *     until the listening round.
 *  2. **The breakdown** (Dok. 6, rule 3, as measured): the main breakdown's loudest three seconds (after its first three,
 *     which still hold the drop) against the drop's, set to the profile's gap (StyleProfile::gapLu: the references' 1.5 to
 *     3 LU) by a gain on the synths, the rooms and the effects in every breakdown and break (LevelMark::breakDb).
 *  3. **The loudness**: the main drop measured (BS.1770, gated) and the master gain corrected to the style's target
 *     (LevelMark::trimDb, at most 4 dB either way), measured again with the correction because the master compresses,
 *     clips and limits after it.
 *
 * The measuring renders about two minutes per track: parh_render does it before it renders, the plugin while the track
 * already plays.
 *
 * @note After Totality `Core/include/tot/Leveler.h` at 4d3c0d2 (29.09.2026), which had it from Ephemeris; the windows and
 *       the breakdown's correction are Parhelion's.
 */
#pragma once
#include "parh/Params.h"
#include "parh/Score.h"
#include <functional>
#include <vector>

namespace parh {

/** @brief What levelScore found for one track. */
struct LevelReading {
    double beat = 0.0;       ///< the track's start
    float measured = 0.0f;   ///< its loudest part as composed, LUFS
    float target = 0.0f;     ///< what it should measure
    float trim = 0.0f;       ///< the correction set, dB
    float after = 0.0f;      ///< the part with the correction, LUFS
    float gapBefore = 0.0f;  ///< the breakdown's loudest 3 s under the drop's, as composed, LU (NaN: no breakdown)
    float gapAfter = 0.0f;   ///< and with the correction
    float breakDb = 0.0f;    ///< the breakdown's correction, dB
    float buildDb = 0.0f;    ///< the builds' correction, dB
    BalanceDb found{};       ///< every part's loudest sample against the kick's as composed, dB (NaN: silent)
    BalanceDb bal{};         ///< the parts' corrections set, dB
};

/**
 * @brief Measures every track of @p score (its LevelMarks) with the knobs of @p params and sets their corrections.
 * @param seconds how much of each loudest part is measured
 * @param stop    asked between blocks: true breaks off, and @p score is left as it was
 * @return what was found, a reading per track (nothing if broken off)
 */
std::vector<LevelReading> levelScore(Score& score, const ParamStore& params, double seconds = 20.0,
                                     const std::function<bool()>& stop = {});

/**
 * @brief Every track of a set (Phase 6, the plugin: a set plays at once and its corrections glide in): deck A's and deck
 *        B's scores levelled as levelScore does, and the teases on deck C given their source's corrections (their marks
 *        have no target of their own, only the source's peak).
 * @note After Totality `Core/src/Leveler.cpp` (levelSet) at 4d3c0d2 (29.09.2026).
 */
std::vector<LevelReading> levelSet(SetScore& set, const ParamStore& params, double seconds = 20.0,
                                   const std::function<bool()>& stop = {});

/**
 * @brief The window of part @p part's loudest sample against the kick's, dB; a lane (0 .. 11) by its @p role (PercRole),
 *        a voice (BalPart) by itself.
 */
void balanceWindow(int part, int role, float& lo, float& hi);

} // namespace parh
