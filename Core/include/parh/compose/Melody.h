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
 * **Rules first, statistics second** (PLAN 6.6, stage A). The lead's, the piano's and the 303's pitches are drawn from
 * the step models of the EMP corpus (Corpus.h) under the rules as hard constraints: the scale (the minor pentatonic's
 * five where drawn), the register, chord tones on the beats and at the motif's end; the contour of Dok. 4 (the leap up
 * after the first note, the walk down after it) weights the transitions. Eight candidates for the motif, the best by
 * the rules (range about an octave, enough different tones, mostly stepwise, no droning).
 *
 * **Versions** (PLAN 6.6): where the lead's cell is filtered (the tease in a breakdown, a break, a build) only the motif
 * A plays, every four bars; where it is on, the phrase A A' B A''. A' keeps A's rhythm and its tones that fit the
 * chords, A'' keeps A's head and walks to the cadence (the tonic at its end where the chord has it), B is a second
 * motif, a little higher. The main drop's second half an octave up (not the piano's).
 *
 * **Memorisation** (Memo.h): the lead is scanned bar by bar against the transcriptions' windows and drawn again on a hit.
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
    /** @brief The scatter's seed: a note's scatter is a hash of it and the note (its beat, part and pitch), so a voice
     *         drawn again changes no other voice's velocities (PLAN 6.9). */
    uint64_t velSeed = 0;
    bool piano = false;       ///< the lead is the piano's (Dream House): Part::Piano, legato, the piano corpus, no octave up
    float anthemShare = 0.7f; ///< the chance of the anthem motif (long notes) over the sixteenth riff
    /** @brief Whether a note of @p part at @p beat falls into a vacuum (the silence before a drop): it is not written. */
    bool silent(double beat, Part part) const;
    /** @brief Adds a note (dropped in a vacuum), its velocity scattered. */
    void note(double beat, double len, Part part, int pitch, float velocity, int shift = 0, bool accent = false, bool slide = false) const;
};

/**
 * @brief The lead (the motif and its versions, see above).
 * @param motif the stream `motif`: the kind (anthem or riff), the scale's colour, the motifs A and B
 * @param lead  the stream `lead`: A', A'' and the tones drawn again where a motif meets other chords
 */
void writeLead(const MelodyContext& c, uint64_t motif, uint64_t lead);
/**
 * @brief The counter: answers in the lead's held tones, an octave over them, chord tones walking down (Phosphene); a held
 *        third or fifth over the riff where the lead has no long notes. Written after the lead.
 */
void writeCounter(const MelodyContext& c, uint64_t seed);
/** @brief The arp: sixteenths (eighths in a slow profile) up, down or up and down over the chord and its octave. */
void writeArp(const MelodyContext& c, uint64_t seed, bool slow);
/** @brief The pluck: a broken chord in one of the trance rhythms, a surprise every four bars. */
void writePluck(const MelodyContext& c, uint64_t seed);
/** @brief The stab: short close chords on the off-beats. */
void writeStab(const MelodyContext& c, uint64_t seed);
/**
 * @brief The 303 (Dok. 5): a bar of sixteen steps, the onsets and pitches from the acid corpus (the first step the
 *        chord's root), accents and slides by the rules; two steps drawn again every four bars; the line moves with the
 *        chord's root. @p seed: the stream `acid`.
 */
void writeAcid(const MelodyContext& c, uint64_t seed);

} // namespace parh
