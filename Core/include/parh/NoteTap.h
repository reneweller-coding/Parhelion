/**
 * @file NoteTap.h
 * @brief The notes the decks played in one process() call, for the plugin's MIDI out (02.10.2026).
 *
 * A deck writes every composer note it actually plays into the tap -- after the performer's mutes and the keyboard's
 * Replace and composer switch, so MIDI out is what is heard -- with its absolute sample; the plugin turns them into
 * MIDI events on the channels of the MIDI export (Midi.h, midiChannelOf). A note whose part has no note-off of its own
 * carries the sample its off is due (a polyphonic voice: its gate; the kick, the ghost kick and the kit: 50 ms after
 * the on). A played key is not written: it came in as MIDI already.
 * Fixed capacity, no allocation: the audio thread fills it and the same thread empties it.
 * @author Rene Weller
 */
#pragma once
#include <cstdint>

namespace parh
{

/** @brief A fixed-capacity list of played notes; clear() before a process() call, read after it. */
struct NoteTap
{
    /** @brief One note-on or note-off as the deck played it. */
    struct Note
    {
        int64_t m_sample;      ///< when, on the engine's sample counter
        int64_t m_offSample;   ///< a note-on without an off of its own: when its off is due; else -1
        uint8_t m_part;        ///< the Part
        uint8_t m_pitch;       ///< MIDI note number
        uint8_t m_velocity;    ///< 1..127 for an on, 0 for an off
    };
    static constexpr int Capacity = 2048;     ///< more notes than any block of 4096 samples plays
    Note                 m_notes[Capacity];   ///< the notes, in the order they were played
    int                  m_count = 0;         ///< how many of notes[] are valid
    /** @brief Forgets the notes of the last call. */
    void clear() { m_count = 0; }
    /**
     * @brief Adds a note (dropped when full); @p offSample as in Note.
     * @param[in] sample     the position of the note-on
     * @param[in] part       the part that plays it
     * @param[in] pitch      the MIDI note
     * @param[in] velocity   the velocity, 0..1
     * @param[in] on         true for a note-on
     * @param[in] offSample  the position of its note-off, as in Note
     */
    void add( int64_t sample, int part, int pitch, float velocity, bool on, int64_t offSample )
    {
        if ( m_count >= Capacity || pitch < 0 || pitch > 127 ) return;
        const int v = on ? ( velocity <= 0.0f   ? 1
                             : velocity >= 1.0f ? 127
                                                : 1 + static_cast<int>( velocity * 126.0f + 0.5f ) )
                         : 0;
        m_notes[m_count++] = Note{ sample, on ? offSample : -1, static_cast<uint8_t>( part ),
                                   static_cast<uint8_t>( pitch ), static_cast<uint8_t>( v ) };
    }
};

}   // namespace parh
