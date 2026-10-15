/**
 * @file Export.h
 * @brief What an export writes beside the mix (PLAN 8): the cues of a track or a set (for a WAV's cue chunk, as JSON and
 *        as a rekordbox collection) and the seamless DJ loops. Shared by parh_render and the plugin.
 *
 * @note Built from Totality `Core/include/tot/Export.h` at 4d3c0d2 (29.09.2026): the JSON and the rekordbox writer are
 *       its; the cues are trance's (the bass's entry, every breakdown, build and drop, the outro) and the loops the
 *       intro's, the main drop's and the outro's.
 * @author Rene Weller
 */
#pragma once
#include "parh/Params.h"
#include "parh/Score.h"
#include "parh/compose/Composer.h"
#include "parh/compose/Set.h"
#include <string>
#include <vector>

namespace parh
{

/** @brief A cue: seconds and a label. */
struct CueAt
{
    double      m_seconds;   ///< when
    std::string m_label;     ///< its text
};

/**
 * @brief The cues of a track: the bass's entry, every breakdown, every drop, the outro, at set beat @p at.
 * @param[in]     t       the track's info
 * @param[in]     at      where the track begins, in beats
 * @param[in]     tempo   the tempo map
 * @param[in]     prefix  the cue names' prefix
 * @param[in,out] out     the cues, appended to
 */
void trackCues( const TrackInfo &t, double at, const TempoMap &tempo, const std::string &prefix,
                std::vector<CueAt> &out );

/**
 * @brief The cues of a set: every track (its style, form and Camelot key), every bass swap, every tease; in time order.
 * @param[in] si     the set's info
 * @param[in] tempo  the tempo map
 * @return the cues
 */
std::vector<CueAt> setCues( const SetInfo &si, const TempoMap &tempo );

/**
 * @brief The cues as JSON (seconds, sample at @p rate, label), beside the WAV for DJ software that does not read its
 *        chunk.
 * @param[in] path  the file
 * @param[in] cues  the cues
 * @param[in] rate  the sample rate in Hz
 * @return false when the file cannot be written
 */
bool writeCuesJson( const std::string &path, const std::vector<CueAt> &cues, double rate );

/**
 * @brief The WAV as a rekordbox collection (rekordbox's "rekordbox xml" import, Preferences > Advanced): one TRACK with
 *        its beat grid -- a TEMPO from every bar whose tempo changed, so a set's ramps are followed bar by bar -- every
 *        cue a memory cue (the breakdowns and the drops above all, PLAN 8) and the first eight also hot cues, in a
 *        playlist "Parhelion". Written to the published format (DJ_PLAYLISTS 1.0.0).
 * @param[in] path         the XML file
 * @param[in] wavPath      the audio file
 * @param[in] title        the title
 * @param[in] tonality     the key as rekordbox shows it ("Am", "F#m"), or empty
 * @param[in] cues         the cues
 * @param[in] tempo        the tempo map
 * @param[in] lengthBeats  the length in beats
 * @param[in] rate         the sample rate in Hz
 * @return false when the file cannot be written
 */
bool writeRekordboxXml( const std::string &path, const std::string &wavPath, const std::string &title,
                        const std::string &tonality, const std::vector<CueAt> &cues, const TempoMap &tempo,
                        double lengthBeats, double rate );

/**
 * @brief DJ loops (PLAN 8): eight bars each of the intro's last bars (the beat a DJ mixes in on), the main drop's first
 *        and the outro's first, seamless -- the bars rendered three times over, the last pass kept, so its start carries
 *        the tails of the pass before it as a loop played round does -- into @p dir as loop_intro.wav, loop_drop.wav and
 *        loop_outro.wav.
 * @param[in] knobs  the knobs
 * @param[in] track  the track
 * @param[in] info   its info
 * @param[in] dir    the folder the loops go to
 * @param[in] rate   the sample rate in Hz
 * @return false when a file cannot be written
 */
bool renderLoops( const ParamStore &knobs, const Score &track, const TrackInfo &info, const std::string &dir,
                  double rate );

}   // namespace parh
