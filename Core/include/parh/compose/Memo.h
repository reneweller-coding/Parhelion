/**
 * @file Memo.h
 * @brief The memorisation gate (PLAN 6.6): no written lead repeats two bars of a known track.
 *
 * Tools/corpus/memorisation.py takes every window of two bars (at every beat) of every track of the 5452 fan
 * transcriptions of famous trance tracks -- the onsets on the window's 32 sixteenths and each note's interval to the one
 * before, so the comparison holds in any key -- and writes their hashes as a Bloom filter (MemoFilter.cpp, 256 KiB). The
 * melody writers build the same window at every bar of the line they wrote and draw again when one is in the filter.
 * A Bloom filter answers "maybe" for about one window in a thousand that no transcription has (a redraw too many, never
 * a copy let through), and "no" for every window that is not there. Hashes only: no window can be read back.
 *
 * A window counts with at least five notes and three distinct pitches; fewer is a figure every track shares.
 */
#pragma once
#include <cstdint>
#include <vector>

namespace parh {

constexpr int kMemoMinNotes = 5;
constexpr int kMemoMinPitches = 3;

extern const int kMemoBloomBits;
extern const int kMemoBloomK;
extern const uint64_t kMemoBloom[];
/** @brief The hash of the probe window {(0, 0), (4, +2), (8, +1), (12, -3), (16, +5)} as the tool computed it. */
extern const uint64_t kMemoProbeHash;

/**
 * @brief The hash of the window of two bars from sixteenth @p start of @p top (a pitch per sixteenth, -1: no onset).
 * @return false if the window is too thin to count (@p hash untouched)
 */
bool memoWindowHash(const std::vector<int>& top, int start, uint64_t& hash);
/** @brief Whether @p hash is (maybe) a transcription's window. */
bool memoContains(uint64_t hash);
/** @brief Whether any window of @p top that starts on a bar is (maybe) a transcription's. */
bool memoHitsAnyBar(const std::vector<int>& top);

} // namespace parh
