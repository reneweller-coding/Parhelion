/**
 * @file Set.h
 * @brief The set composer (PLAN 6.8): an evening of trance tracks on two decks, mixed as a trance DJ mixes them.
 *
 * **Dramaturgy** (set.dramaturgy). Five arcs of energy and tempo: Warm-up (130 -> 136 BPM, rising), Peak (136 -> 140,
 * highest at 70 %), Closing (139 -> 132, falling), **Sunrise** (134 -> 138, rising to the end: the trance gesture, the
 * most euphoric track last -- an Uplifting track with its orchestra), Journey (132 -> 138, a wave). The tempo moves at
 * most 2 BPM a track and ramps inside the blend. With set.journey on (Wander), the styles follow the arc along the
 * ladder Deep, Progressive, Dream House, Uplifting, Acid, morphed between neighbours: Warm-up from Deep to Progressive,
 * Peak between Uplifting and Acid, Closing from Uplifting down to Progressive, Sunrise from Progressive up to Uplifting,
 * Journey wherever the energy is; else the knobs' style plays throughout.
 *
 * **Keys** (harmonic mixing after the Camelot wheel): the same key (p 0.3), a fifth up or down (p 0.45: one step round
 * the wheel), the relative major or minor (p 0.15), the "energy boost" a semitone or a whole tone up (p 0.05, the rare
 * gesture), any (p 0.05). A minor key keeps a minor mode (Aeolian, Dorian, harmonic minor, pentatonic, Phrygian after
 * the profile), a major one plays Ionian.
 *
 * **Tracks** are extended mixes (set.track_minutes, six by default) written for a DJ (TrackRequest::mixable): an intro and
 * an outro of 32 bars at least, both with the beat.
 *
 * **The blend**. The incoming track's intro lies over the outgoing track's end: its first groove begins where the other
 * track ends, so the two intros and outros overlap by the incoming intro's length and never a breakdown of the incoming
 * track is in the blend. The **bass swap** lies 16 bars before the incoming groove, on a 16-bar line of both tracks:
 * until then the incoming track's low band is killed at the isolator (its kick, its sub, its bass), from then on the
 * outgoing one's -- only one deck ever owns the band under 200 Hz. The incoming fader opens set.blend bars (32 or 64)
 * before the swap (or where the track starts) with its highs 8 dB and its mids 10 dB down, coming up over the blend; the
 * outgoing deck keeps its hats and its highs over the rest of its outro and its fader falls through the last 8 bars.
 *
 * **The tease** (deck C, PLAN 6.8: "Tease der nächsten Hookline gefiltert, nur bei verträglichen Tonarten"): where the
 * next track's key is the same or a fifth away, its hook -- the lead's (or the piano's, the pluck's) first eight bars of
 * its main drop -- plays twice on the third deck in the sixteen bars before its fader opens, high-passed and opening,
 * with the low band killed.
 *
 * **Streams.** The set's own choices (keys, tempi, lengths) on the stream `set`; each track on its own seed (reroll
 * `track<n>`) and its units as `track<n>.form` and so on (SetFile.h).
 */
#pragma once
#include "parh/Params.h"
#include "parh/Score.h"
#include "parh/SetFile.h"
#include "parh/compose/Composer.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace parh {

extern const char* const kDramaturgyNames[];   ///< "Warm-up", "Peak", "Closing", "Sunrise", "Journey"

/** @brief One track of a set, where it lies. */
struct SetTrack {
    int deck = 0;              ///< 0 or 1
    double start = 0.0;        ///< set beat of its first bar
    double swapIn = 0.0;       ///< set beat where its low end opens (the set's start for the first)
    double swapOut = 0.0;      ///< set beat where it gives the low end up (its end for the last)
    double end = 0.0;          ///< set beat of its end
    float energy = 0.0f;       ///< the set's energy at its start
    uint64_t seed = 0;         ///< its seed
    TrackInfo info;            ///< what it told
};

/** @brief A tease on the third deck: the hook of track @p from, before its blend. */
struct SetTease {
    int from = 0;
    double start = 0.0, end = 0.0;
};

/** @brief What a set tells. */
struct SetInfo {
    Dramaturgy dramaturgy = Dramaturgy::Peak;
    std::vector<SetTrack> tracks;
    std::vector<SetTease> teases;
};

/**
 * @brief Writes a set of about @p minutes.
 * @param p        the knobs (set.*, compose.*)
 * @param seed     the set's seed
 * @param minutes  its length; the last track's outro begins at or after it
 * @param curation rerolls, or null
 * @param info     receives where everything lies, or null
 * @param prepare  applied to every track's own score before it is placed (the Leveler), or empty
 * @param stop     asked before every track: true breaks off with what is composed
 */
SetScore composeSet(const ParamStore& p, uint64_t seed, double minutes, const Curation* curation = nullptr, SetInfo* info = nullptr,
                    const Preferences* prefs = nullptr, const std::function<void(Score&)>& prepare = {},
                    const std::function<bool()>& stop = {});

/** @brief The energy of dramaturgy @p d at @p t (0..1 of the set). */
float setEnergy(Dramaturgy d, float t);
/** @brief The tempo of dramaturgy @p d at @p t, before the 2-BPM rule. */
float setTempo(Dramaturgy d, float t);
/** @brief The place on the style ladder (0 Deep .. 4 Acid) of dramaturgy @p d at @p t and energy @p e (set.journey). */
float setLadder(Dramaturgy d, float t, float e);

/** @brief All of a set as one score (the decks' notes and markers on the set's tempo; for MIDI and the displays). */
Score flattenSet(const SetScore& set);

} // namespace parh
