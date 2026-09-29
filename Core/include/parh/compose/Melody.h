/**
 * @file Melody.h
 * @brief The melodic voices (PLAN 6.6, Dok. 4): the lead's motif and phrases, the counter, the arp, the pluck, the stab.
 *
 * Every writer reads the plan (which cells are on, filtered or off) and the harmony, and appends its notes to the score.
 * The rules of Dok. 4 hold for all of them: tones of the scale, chord tones on the strong sixteenths, the pluck's broken
 * chords with one or two surprises, the arp's sixteenths over the chord and its octave, the lead's motif of two bars
 * repeated in phrases of eight with the last two bars varied, a leap at the phrase's start and a stepwise descent to its
 * end, new melody only in a breakdown.
 *
 * Phase 2 writes the lead with the rules alone; Phase 3 adds the statistics of the corpus (PLAN 6.6, stage A) and the
 * memorisation check.
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Score.h"
#include "parh/compose/Harmony.h"
#include "parh/compose/Planner.h"
#include <vector>

namespace parh {

/** @brief What the melodic writers share: the plan, the harmony, the score to write into, a velocity scatter. */
struct MelodyContext {
    const Plan* plan = nullptr;
    const Harmony* harmony = nullptr;
    Score* score = nullptr;
    float humanize = 0.08f;   ///< velocity scatter (compose.humanize)
    Rng* vel = nullptr;       ///< its stream
    /** @brief Whether the note at @p beat falls into a vacuum (the last beat before a drop): it is not written. */
    bool silent(double beat) const;
    /** @brief Adds a note (dropped in a vacuum), its velocity scattered. */
    void note(double beat, double len, Part part, int pitch, float velocity, int shift = 0, bool accent = false, bool slide = false) const;
};

/** @brief The lead (Phase 2: rules only). @p seed: the stream `melody`. */
void writeLead(const MelodyContext& c, uint64_t seed);
/** @brief The counter: held answers a third or a sixth over the lead's register where the counter cell is on. */
void writeCounter(const MelodyContext& c, uint64_t seed);
/** @brief The arp: sixteenths (eighths in a slow profile) up, down or up and down over the chord and its octave. */
void writeArp(const MelodyContext& c, uint64_t seed, bool slow);
/** @brief The pluck: a broken chord in one of the trance rhythms, a surprise every four bars. */
void writePluck(const MelodyContext& c, uint64_t seed);
/** @brief The stab: short close chords on the off-beats. */
void writeStab(const MelodyContext& c, uint64_t seed);

} // namespace parh
