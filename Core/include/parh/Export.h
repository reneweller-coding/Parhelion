/**
 * @file Export.h
 * @brief What an export writes beside the mix (PLAN 8): the cues of a track or a set (for a WAV's cue chunk, as JSON and
 *        as a rekordbox collection) and the seamless DJ loops. Shared by parh_render and the plugin.
 *
 * @note Built from Totality `Core/include/tot/Export.h` at 4d3c0d2 (29.09.2026): the JSON and the rekordbox writer are
 *       its; the cues are trance's (the bass's entry, every breakdown, build and drop, the outro) and the loops the
 *       intro's, the main drop's and the outro's.
 */
#pragma once
#include "parh/Params.h"
#include "parh/Score.h"
#include "parh/compose/Composer.h"
#include "parh/compose/Set.h"
#include <string>
#include <vector>

namespace parh {

/** @brief A cue: seconds and a label. */
struct CueAt {
    double seconds;
    std::string label;
};

/** @brief The cues of a track: the bass's entry, every breakdown, every drop, the outro, at set beat @p at. */
void trackCues(const TrackInfo& t, double at, const TempoMap& tempo, const std::string& prefix, std::vector<CueAt>& out);

/** @brief The cues of a set: every track (its style, form and Camelot key), every bass swap, every tease; in time order. */
std::vector<CueAt> setCues(const SetInfo& si, const TempoMap& tempo);

/** @brief The cues as JSON (seconds, sample at @p rate, label), beside the WAV for DJ software that does not read its chunk. */
bool writeCuesJson(const std::string& path, const std::vector<CueAt>& cues, double rate);

/**
 * @brief The WAV as a rekordbox collection (rekordbox's "rekordbox xml" import, Preferences > Advanced): one TRACK with
 *        its beat grid -- a TEMPO from every bar whose tempo changed, so a set's ramps are followed bar by bar -- every
 *        cue a memory cue (the breakdowns and the drops above all, PLAN 8) and the first eight also hot cues, in a
 *        playlist "Parhelion". Written to the published format (DJ_PLAYLISTS 1.0.0).
 * @param tonality the key as rekordbox shows it ("Am", "F#m"), or empty
 */
bool writeRekordboxXml(const std::string& path, const std::string& wavPath, const std::string& title, const std::string& tonality,
                       const std::vector<CueAt>& cues, const TempoMap& tempo, double lengthBeats, double rate);

/**
 * @brief DJ loops (PLAN 8): eight bars each of the intro's last bars (the beat a DJ mixes in on), the main drop's first
 *        and the outro's first, seamless -- the bars rendered three times over, the last pass kept, so its start carries
 *        the tails of the pass before it as a loop played round does -- into @p dir as loop_intro.wav, loop_drop.wav and
 *        loop_outro.wav.
 */
bool renderLoops(const ParamStore& knobs, const Score& track, const TrackInfo& info, const std::string& dir, double rate);

} // namespace parh
