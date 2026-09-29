/**
 * @file Composer.h
 * @brief The composer (PLAN 6): one track from a seed and a style profile -- the plan, the harmony, every part's notes,
 *        the effects and the automation.
 *
 * **The parts** (PLAN 5, 6.6, Dok. 3 to 5), bar by bar from the layer matrix (Planner.h):
 *  - the kick on the quarters (a mini-break rests it for a bar), the ghost kick on every quarter (the pump, PLAN 7.3),
 *    both silent on the last beat before a drop;
 *  - the closed hat on the sixteenths or eighths (the profile's hats16), the open hat on the off-beat eighths, the clap on
 *    2 and 4, the ride in the drops, a crash on the first beat of a block that asks for it, a percussion loop (a shaker's
 *    sixteenths or a conga figure with a tambourine), the snare roll that thickens from quarters to thirty-seconds over a
 *    build and the last bars of a break;
 *  - the bass on the chord's root in the figure the profile draws (off-beat eighths, rolling sixteenths, the gallop, a
 *    walking line with syncopes, a drone), the sub an octave under it on the off-beat eighth (or the drone's whole bar);
 *    the 303 as a sixteen-step sequence with accents and slides (Acid);
 *  - the pad's chords voiced from about A3, the pluck's broken chord in one of the trance rhythms with a surprise now and
 *    then, the arp's sixteenths over the chord and its octave, the stab's off-beat chords, the counter's held answers;
 *  - the lead: until Phase 3 a stand-in (compose/Melody.h) -- a motif of two bars, phrases of eight (A A' B A''), filtered
 *    as a tease, in full in the drop, an octave up in the main drop's second half;
 *  - the effects (Sfx.h): a riser over every build (arriving on the drop's first beat), a reverse crash into every drop and
 *    breakdown, an impact on the drop's first beat, a downlifter and a sub drop where a breakdown begins.
 *
 * **Automation** (PLAN 6.4, Dok. 7): the filtered cells (the pluck, the pad, the lead, the hats, the 303) as cutoff
 * offsets per block; the pad opening through the breakdown; the hall long in breakdowns and short in drops; the pump's
 * depth taken to nothing where the kick rests and back over the last two bars before it returns; the synth fader lower
 * in a breakdown, the group's high pass drawn up over the last bars of a build; a delay throw on the lead before a
 * breakdown; the pad's trance gate in the drops.
 *
 * **Streams.** Every unit draws on its own stream of the seed: `form`, `harmony`, `drums`, `bass`, `chords`, `melody`,
 * `fx`, `mix` (SetFile.h). A set asks for a track with its own tempo, key and energy (TrackRequest).
 */
#pragma once
#include "parh/Params.h"
#include "parh/Score.h"
#include "parh/SetFile.h"
#include "parh/compose/Planner.h"
#include "parh/compose/Style.h"
#include <cstdint>
#include <string>
#include <vector>

namespace parh {

/** @brief What a set asks of a track; every field left at its default is the composer's (or the knobs') to decide. */
struct TrackRequest {
    const StyleProfile* profile = nullptr;   ///< the profile (null: profileOf(the knobs))
    float bpm = 0.0f;                        ///< 0: drawn (compose.auto) or compose.bpm
    int key = -1;                            ///< -1: drawn (compose.auto) or compose.key
    int scale = -1;                          ///< -1: drawn or compose.scale
    int bars = 0;                            ///< 0: from compose.minutes (or the profile's lengths with compose.auto)
    float energy = -1.0f;                    ///< the set's energy here, 0..1 (-1: a track alone)
};

/** @brief What a track tells the set, the cues and the displays. */
struct TrackInfo {
    std::string style;           ///< the profile's name
    FormTemplate form = FormTemplate::Anthem;
    float bpm = 138.0f;
    int key = 9, scale = 0;
    int bars = 0;
    int mainDropBar = -1;        ///< the main drop's first bar
    int breakdownBar = -1;       ///< the main breakdown's first bar
    int firstLeadBar = -1;       ///< where the lead is first heard
    bool orchestra = false;      ///< the orchestra plays (Cinematic)
    int keyChangeBar = -1;       ///< the Cinematic key change (-1: none)
    std::string progression;     ///< "i-VI-III-VII"
    std::string bass;            ///< the bass figure
    std::string camelot;         ///< "8A"
    bool beatless = false;
};

/** @brief The names of a track's units, in stream order. */
extern const char* const kUnitNames[8];

/**
 * @brief Writes one track, beat 0 its first bar.
 * @param p        the knobs
 * @param seed     the track's seed
 * @param req      what a set asks (all defaults: a track alone)
 * @param curation rerolls, or null
 * @param unit     prefix of this track's units in @p curation ("" alone, "track3." in a set)
 * @param info     receives what the track tells, or null
 */
Score composeTrack(const ParamStore& p, uint64_t seed, const TrackRequest& req = TrackRequest{}, const Curation* curation = nullptr,
                   const std::string& unit = std::string(), TrackInfo* info = nullptr);

/** @brief The Camelot label of a key (minor: "8A" for A minor; @p major: "11B" for A major). */
std::string camelotLabel(int key, bool major);

} // namespace parh
