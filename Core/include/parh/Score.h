/**
 * @file Score.h
 * @brief The score: notes, automation curves, sections, the layer matrix and markers of a track or a set, on a tempo map.
 *
 * The composer writes the score ahead of the audio thread (PLAN 3); the audio render, the MIDI export and the
 * `.parhset` file all read the same score. Its content:
 *
 * - **Notes** of every part, in beats. The kit's lanes are parts of their own, so a lane is a MIDI track and a stem.
 *   The **ghost kick** is a part too (PLAN 7.3, Dok. 7): a silent trigger on every quarter that ducks every bus, also
 *   where the kick rests, so the pump's form is the same in a breakdown and can be moved there by the composer.
 * - **Automation**: a ramp on a knob -- a filter opening over a breakdown, the hall's decay growing, the sidechain's
 *   depth falling to nothing and back two bars before the drop (PLAN 6.4). A curve: start, length, from, to, shape.
 *   Values are **offsets in the knob's normalised range** (-1..1), added to where the knob is; after a curve ends its
 *   end value stays until the next curve on the same knob starts. The form is Ephemeris' gesture.
 * - **The plan** (PLAN 6.2 to 6.4): the sections (intro, groove, break, breakdown, build, drop, outro) with their energy,
 *   and the layer matrix -- for every 8-bar block which element is off, filtered or on -- so that locking and rerolling
 *   work on a section and a block, and a display can draw them.
 * - **Markers**: a section, a track.
 *
 * @note The gesture curve and the housekeeping come from Ephemeris by way of Totality; parts, sections and the layer
 *       matrix are Parhelion's.
 * @note Copied from Totality `Core/include/tot/Score.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 * @author Rene Weller
 */
#pragma once
#include "parh/Clock.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace parh {

/** @brief The parts of the score, one MIDI track and one stem each. Appended to, never reordered (once files exist). */
enum class PartE : int { KICK = 0, SUB,
                        PERC1, PERC2, PERC3, PERC4, PERC5, PERC6, PERC7, PERC8, PERC9, PERC10, PERC11, PERC12,
                        /** The mid-bass and the 303 (Synth.h). */
                        BASS, ACID,
                        /** The polyphonic voices (Poly.h), in the order of PolyInstance. */
                        LEAD, COUNTER, PLUCK, ARP, PAD, STAB,
                        /** The physical piano (Piano.h). */
                        PIANO,
                        /** The orchestra (PLAN 5.9): the string section, the choir, the brass, the timpani. */
                        STRINGS, CHOIR, BRASS, TIMPANI,
                        /** The ghost kick: silent, it only ducks (PLAN 7.3). */
                        GHOST,
                        /** The effects (Sfx.h): the type is the pitch (SfxBaseNote + type). */
                        FX,
                        COUNT };
constexpr int NumParts = static_cast<int>(PartE::COUNT);   ///< number of parts
extern const char* const PartNames[NumParts];            ///< "kick", "sub", "perc1" .. "perc12", "bass", "acid", "lead", ...
/**
 * @brief The part of kit lane @p lane (0-based).
 * @param[in] lane  the kit lane, 0-based
 * @return its part
 */
constexpr PartE percPart(int lane) { return static_cast<PartE>(static_cast<int>(PartE::PERC1) + lane); }
/**
 * @brief The kit lane of @p part, or -1 if it is not a lane.
 * @param[in] part  the part
 * @return its kit lane, -1 if it is not a lane
 */
constexpr int laneOf(PartE part)
{
    const int i = static_cast<int>(part) - static_cast<int>(PartE::PERC1);
    return i >= 0 && i < 12 ? i : -1;
}
/**
 * @brief The polyphonic instance of @p part (PolyInstance order), or -1.
 * @param[in] part  the part
 * @return its polyphonic instance, or -1
 */
constexpr int polyOf(PartE part)
{
    const int i = static_cast<int>(part) - static_cast<int>(PartE::LEAD);
    return i >= 0 && i < 6 ? i : -1;
}
/**
 * @brief The part of polyphonic instance @p i.
 * @param[in] i  the polyphonic instance
 * @return its part
 */
constexpr PartE polyPart(int i) { return static_cast<PartE>(static_cast<int>(PartE::LEAD) + i); }
/**
 * @brief Whether a part's notes start with their length known and need no off event: the kick, the ghost, the kit and
 *        the polyphonic voices (which take their gate at note on, Poly.h). The sub and the mono synths are held.
 * @param[in] p  the part
 * @return true when its notes are one-shots
 */
constexpr bool isOneShot(PartE p)
{
    return p == PartE::KICK || p == PartE::GHOST || laneOf(p) >= 0 || polyOf(p) >= 0;
}

/** @brief One note. */
struct NoteEvent {
    double m_beat = 0.0;        ///< onset in beats (swing and offsets already applied)
    double m_length = 0.25;     ///< duration in beats
    PartE m_part = PartE::KICK;   ///< which part plays it
    int m_pitch = 36;           ///< MIDI note number (the kit: its General MIDI instrument; the sub: its pitch)
    float m_velocity = 1.0f;    ///< 0..1
    int m_shift = 0;            ///< a kit hit's pitch shift in semitones
    bool m_accent = false;      ///< the 303's accent (Synth.h)
    bool m_slide = false;       ///< slides into the next note (Synth.h)
    uint8_t m_voice = 0;        ///< 0 the part's line; 1 an accompaniment under it (the piano's left hand), not its melody
};

/** @brief How a curve moves between its two values. */
enum class GestureShapeE : uint8_t {
    MINIMUM_JERK,   ///< the smooth S of a human reach: 10t^3 - 15t^4 + 6t^5 (Flash and Hogan 1985)
    LINEAR,        ///< constant speed
    EASE_IN,        ///< slow start, t^2
    EASE_OUT,       ///< fast start, 1 - (1 - t)^2
    STEP,          ///< jumps to the end value at the start
};
/** @brief One movement of one knob. */
struct Gesture {
    int m_param = -1;             ///< parameter id (Params.h)
    double m_beat = 0.0;          ///< start in beats
    double m_length = 16.0;       ///< duration in beats (0 behaves like Step)
    float m_from = 0.0f;          ///< offset at the start, in the knob's normalised range (-1..1)
    float m_to = 0.0f;            ///< offset at the end
    GestureShapeE m_shape = GestureShapeE::MINIMUM_JERK;   ///< the path between the two
    uint8_t m_hand = 0;           ///< which hand (PLAN 7.3: at most two at once)
};
/**
 * @brief Position 0..1 along a shape at normalised time @p t (clamped to 0..1).
 * @param[in] shape  the shape
 * @param[in] t      the position in the gesture, 0..1
 * @return the shape's value, 0..1
 */
double gestureShape(GestureShapeE shape, double t);
/**
 * @brief Offset of one curve at @p beat: @p from before it, its shape during it, @p to after it.
 * @param[in] g     the gesture
 * @param[in] beat  the position in beats
 * @return its offset there
 */
float gestureValue(const Gesture& g, double beat);

/** @brief The kinds of section (PLAN 6.2, Dok. 6). */
enum class SectionKindE : uint8_t { INTRO = 0, GROOVE, BREAK, BREAKDOWN, BUILD, DROP, OUTRO, COUNT };
extern const char* const SectionNames[];   ///< "Intro", "Groove", "Break", "Breakdown", "Build", "Drop", "Outro"
/** @brief One section of a track: where, how long, what, and its energy (0..10) at its start and its end. */
struct Section {
    double m_beat = 0.0;                    ///< start in beats
    double m_length = 128.0;                ///< length in beats (a multiple of 32: 8 bars)
    SectionKindE m_kind = SectionKindE::GROOVE;   ///< what it is
    float m_energyFrom = 5.0f;              ///< energy at its start (Dok. 6, 0..10)
    float m_energyTo = 5.0f;                ///< and at its end
};

/** @brief The elements of the layer matrix (PLAN 6.3). Appended to, never reordered. */
enum class LayerE : int { KICK = 0, SUB, BASS, ACID, CLOSED_HAT, OPEN_HAT, CLAP, RIDE, PERC, CRASH, PLUCK, ARP, PAD, LEAD,
                         COUNTER, STAB, FX, COUNT };
constexpr int NumLayers = static_cast<int>(LayerE::COUNT);   ///< number of elements
extern const char* const LayerNames[NumLayers];   ///< "kick", "sub", "bass", ...
/** @brief A cell of the layer matrix: off, filtered (high- or low-passed, PLAN 6.3), on. */
enum class LayerStateE : uint8_t { OFF = 0, FILTERED, ON };
/** @brief One 8-bar block of the layer matrix. */
struct LayerBlock {
    double m_beat = 0.0;                                        ///< its start (32 beats long)
    std::array<LayerStateE, NumLayers> m_state{};               ///< every element's state
    float m_energy = 5.0f;                                      ///< the energy of the block, 0..10
    /**
     * the state of @p l
     * @param[in] l  the layer
     * @return its state
     */
    LayerStateE at(LayerE l) const { return m_state[static_cast<size_t>(l)]; }
};

/**
 * @brief The parts the Leveler sets against the kick (Totality's Phase 18), each by its own correction: the kit's twelve
 *        lanes (0 .. 11, each by its role), the tonal voices and the rooms' returns.
 */
enum class BalPartE : int { BASS = 12, ACID, LEAD, COUNTER, PLUCK, ARP, PAD, STAB, PIANO, STRINGS, CHOIR, BRASS, TIMPANI, ROOM, FX, COUNT };
constexpr int BalLanes = 12;   ///< the kit's lanes come first
constexpr int BalParts = static_cast<int>(BalPartE::COUNT);   ///< lanes and voices
/** @brief A correction per part, dB (0: none). */
using BalanceDbT = std::array<float, BalParts>;
extern const char* const BalPartNames[BalParts];   ///< "perc1" .. "perc12", "bass", "acid", "lead", ..., "room"

/**
 * @brief Where a track's loudness is set (Leveler.h, after Ephemeris): its start, where its loudest part begins, what that
 *        part should measure, and the correction found for it -- the master gain from the track's start on.
 */
struct LevelMark {
    double m_beat = 0.0;          ///< the track's start
    double m_peakBeat = 0.0;      ///< where its loudest part begins
    float m_targetLufs = -8.0f;   ///< what that part should measure (the style's, PLAN 7.5)
    float m_trimDb = 0.0f;        ///< the correction (levelScore); 0 until measured
    BalanceDbT m_balDb{};          ///< Phase 18: the parts' corrections against the kick (levelScore); 0 until measured
    /** @brief The windows of every part but the kit's lanes, the sub and the bass moved up by this, dB: a soft kick's
     *         peak says less of its weight than a hard one's (the composer's, 6 dB times the profile's kickSoft; measured,
     *         Tools/calibrate.py: Deep's pad, pluck and arp found 3 to 5 dB over their windows and cut, the kick's body
     *         then carried 60 % of the drop's power in 60 to 150 Hz, the references 23 to 43 %). */
    float m_windowDb = 0.0f;
    float m_gapLu = 2.0f;         ///< the main breakdown's loudest 3 s under the drop's, LU (the style's, Leveler.h)
    float m_breakDb = 0.0f;       ///< the correction of the synths, rooms and effects in every breakdown and break (levelScore)
    float m_buildDb = 0.0f;       ///< the correction of the synths, effects and percussion in every build: under the drop (levelScore)
};

/**
 * @brief A knob a track sets, from a beat on (28.09.2026, after Ephemeris' KnobSet): the sound of a synth as a program
 *        change -- the preset the composer chose for the track (Presets.h) -- and the mix its style sets. Unlike a gesture
 *        it is no offset: the deck that plays the track plays from this value, the knobs show it (Engine.h), and a hand
 *        that turns the knob turns it from there. The knob settings of one beat are a group: a new group on a deck
 *        replaces the last one's.
 */
struct KnobSet {
    double m_beat = 0.0;    ///< from where it holds (a track's start)
    int m_param = -1;       ///< the parameter
    float m_value = 0.0f;   ///< its value, real units
    int m_kind = 0;         ///< 0 a sound (a preset's knob), 1 a setting of the mix or the composer
};

/** @brief The factory preset the composer chose for a synth of a track (Presets.h), from where it holds. */
struct SoundPick {
    double m_beat = 0.0;   ///< from where it holds (a track's start)
    int m_module = 0;      ///< the synth's Module, as an int
    int m_instance = 0;    ///< which instance (the lane, for the kit)
    int m_preset = -1;     ///< its index in factoryPresets(module)
};

/** @brief A named position. */
struct Marker {
    double m_beat = 0.0;   ///< position in beats
    std::string m_text;    ///< name, e.g. "Intro", "Block 3", "Reduction"
};

/** @brief A score of one track or a whole set. */
struct Score {
    TempoMap m_tempo;                  ///< the tempo map; every other time is in beats
    uint64_t m_seed = 1;               ///< the seed the score was composed from
    int m_keyRoot = 9;                 ///< pitch class of the key (A = 9)
    int m_scale = 0;                   ///< compose.scale of the track
    double m_lengthBeats = 0.0;        ///< where the score ends
    std::vector<NoteEvent> m_notes;    ///< notes, sorted by beat after sort()
    std::vector<Gesture> m_gestures;   ///< automation, sorted by beat after sort()
    std::vector<Section> m_sections;   ///< the plan's sections, sorted by beat after sort()
    std::vector<LayerBlock> m_layers;  ///< the layer matrix, one entry per 8-bar block, sorted by beat after sort()
    std::vector<Marker> m_markers;     ///< markers, sorted by beat after sort()
    std::vector<LevelMark> m_levels;   ///< every track's loudness mark, in beat order (Leveler.h)
    std::vector<KnobSet> m_knobs;      ///< the knobs every track sets at its start, sorted by beat after sort()
    std::vector<SoundPick> m_sounds;   ///< the presets the composer chose, sorted by beat after sort()
    /**
     * @brief The value the latest knob setting of @p param at or before @p beat gives it; NaN if none does.
     * @param[in] param  the parameter
     * @param[in] beat   the position in beats
     * @return the value the latest knob setting gives it, NaN if none does
     */
    float knobAt(int param, double beat) const;
    /**
     * @brief The loudness correction at @p beat in dB: the latest mark's (0 before the first).
     * @param[in] beat  the position in beats
     * @return the loudness correction there in dB
     */
    float trimAt(double beat) const;
    /**
     * @brief The parts' corrections at @p beat, dB: the latest mark's (none before the first).
     * @param[in] beat  the position in beats
     * @return the parts' corrections there in dB
     */
    BalanceDbT balanceAt(double beat) const;
    /** @brief Sorts every list by beat (stable, so equal beats keep the order they were written in). */
    void sort();
    /**
     * @brief Empties the score and resets the tempo to @p bpm.
     * @param[in] bpm  the new tempo in beats per minute
     */
    void clear(double bpm);
    /**
     * @brief The automation offset of @p param at @p beat, in the normalised range.
     *
     * Of the curves on one knob, the latest that has started counts; before the first one the offset is 0. Linear
     * in the number of curves -- for the offline render and the tests; the audio thread keeps a cursor instead.
     * @param[in] param  the parameter
     * @param[in] beat   the position in beats
     * @return the automation offset in the normalised range
     */
    float gestureOffset(int param, double beat) const;
};

/**
 * @brief A set (PLAN 7.7): what each deck plays, one after another on it -- the tracks, their automation, the DJ
 *        mixer's moves on the deck's own channel (module deck, instance = the deck) -- all on the set's tempo map
 *        (every deck's Score::tempo is the same), and the set's own markers.
 */
struct SetScore {
    static constexpr int Decks = 3;     ///< as Params.h Decks
    Score m_decks[Decks];                 ///< deck A, B, C
    double m_lengthBeats = 0.0;            ///< the set's length
    std::vector<Marker> m_markers;         ///< tracks, blends, bass swaps
    std::vector<Gesture> m_gestures;       ///< the set's own automation (the mixer's effects, module djfx)
};

} // namespace parh
