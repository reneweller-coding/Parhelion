/**
 * @file Deck.h
 * @brief A deck (PLAN 3): the whole instrument for the tracks of one deck -- every voice, the buses, the sends and rooms,
 *        the track bus -- playing its own score with its own automation. The engine holds three and mixes them.
 *
 * **The low end** (PLAN 5.1, 5.2, Dok. 7). The kick owns the downbeat and 45 to 65 Hz; the sub sine starts in the kick's
 * phase where its lock is on; the mid-bass is high-passed so that its fundamental sits above the sub (bass.low_cut, at
 * least BassMinCut). Under 150 Hz nothing else plays: every polyphonic voice has its high pass (poly.hp_floor), every
 * send input its low cut (sends.low_cut).
 *
 * **The pump** (PLAN 7.3, Dok. 7). The score's ghost kick -- a silent part on every quarter, also where the kick rests --
 * triggers every duck of the deck: the sub's and the mono synths' own (sub.duck, bass.duck, acid.duck), one per
 * polyphonic voice (poly.duck, in dB) and one on the rooms' returns (pump.return_duck), all along the curve of the pump
 * module (attack, hold, release). A duck is an envelope started by an event, never by a level (Ducker.h), so its form is
 * exact and the same in every bus. The composer moves the depths with the energy (0 in a breakdown, back two bars
 * before the drop).
 *
 * **Signal flow**:
 * @code
 *   kick + hats bus + perc bus                      -> drum bus (gentle saturation)                        -+
 *   sub, bass, 303 (each with its own duck)                                                                  |
 *   lead, counter, pluck, arp, pad, stab -> trance gate -> [sends] -> duck -> synth bus                       +-> sum
 *   sends: room (hats, pluck, arp ...), plate (perc, voices), hall (lead, pad ...), inputs high-passed        |
 *          -> returns -> duck                                                                               -+
 *   sum -> group high pass -> tilt -> glue (35 % parallel) -> the Leveler's trim  = the deck's output
 * @endcode
 * The DJ mixer, the master's mono under Mono Below, the clipper and the limiter follow in the engine.
 *
 * **Time and determinism** (the rule of all the siblings). The engine gives every call the absolute sample; parameters
 * and automation are read on its absolute raster of 32 samples and at the samples it names (a mixer's kill), spans end
 * at note events, so a host's block size cannot change a single sample.
 *
 * **Rest.** A deck plays from shortly before its first note to 20 s after its last; between its runs of tracks it rests
 * and costs nothing. Where it rests is fixed by its score (nextRestChange()), so resting changes no bit either.
 *
 * **Loading** allocates and must not run on the audio thread.
 *
 * @note Built from Totality `Core/include/tot/Deck.h` at 4d3c0d2 (29.09.2026): the raster, the events, the knob settings,
 *       the trims and the stems are its; the voices, the pump and the rooms are Parhelion's.
 * @author Rene Weller
 */
#pragma once
#include "parh/Params.h"
#include "parh/NoteTap.h"
#include "parh/Score.h"
#include "parh/fx/Dynamics.h"
#include "parh/fx/Cloud.h"
#include "parh/fx/Plate.h"
#include "parh/fx/Reverb.h"
#include "parh/mix/Ducker.h"
#include "parh/mix/TranceGate.h"
#include "parh/synth/Kick.h"
#include "parh/synth/Kit.h"
#include "parh/synth/Brass.h"
#include "parh/synth/Choir.h"
#include "parh/synth/Piano.h"
#include "parh/synth/Strings.h"
#include "parh/synth/Timpani.h"
#include "parh/synth/Poly.h"
#include "parh/synth/Sfx.h"
#include "parh/synth/SubBass.h"
#include "parh/synth/Synth.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>

namespace parh {

/**
 * @brief Where the decks add the levels of the mixer's strips while the plugin's mixer page looks (01.10.2026): per strip
 *        (the stems' order, Deck::Stem) the loudest sample and the sum of squares since the page last took them. Written
 *        by the audio thread (both decks into one), read and reset by the message thread; null in a deck: nothing is
 *        measured, nothing costs.
 */
struct MeterSink {
    static constexpr int Strips = 22;   ///< Deck::STEMS
    std::atomic<float> m_peak[Strips] = {};   ///< per strip: the loudest sample since the page last took them
    std::atomic<double> m_sum[Strips] = {};   ///< per strip: the sum of squares since the page last took them
    /**
     * @brief Adds one block's readings (audio thread).
     * @param[in] pk  the block's peak per channel
     * @param[in] ss  the block's sum of squares per channel
     * @pre called on the audio thread
     */
    void add(const float* pk, const double* ss)
    {
        for (int s = 0; s < Strips; ++s) {
            float was = m_peak[s].load(std::memory_order_relaxed);
            while (pk[s] > was && !m_peak[s].compare_exchange_weak(was, pk[s], std::memory_order_relaxed)) {}
            double w = m_sum[s].load(std::memory_order_relaxed);
            while (!m_sum[s].compare_exchange_weak(w, w + ss[s], std::memory_order_relaxed)) {}
        }
    }
};

/** @brief One deck. */
class Deck {
public:
    /** @brief The stems, in the order Engine::setStems() takes them. */
    enum StemE : int { STEM_KICK = 0, STEM_SUB, STEM_BASS, STEM_ACID, STEM_HATS, STEM_PERC, STEM_LEAD, STEM_COUNTER, STEM_PLUCK,
                      STEM_ARP, STEM_PAD, STEM_STAB, STEM_PIANO, STEM_STRINGS, STEM_CHOIR, STEM_BRASS, STEM_TIMPANI, STEM_ROOM,
                      STEM_PLATE, STEM_HALL, STEM_CLOUD, STEM_FX, STEMS };
    static_assert(static_cast<int>(STEMS) == MeterSink::Strips, "a strip per stem");
    static constexpr int Raster = 32;   ///< the engine's parameter raster, samples
    static constexpr int64_t Never = std::numeric_limits<int64_t>::max();   ///< no such sample: nothing is due

    /**
     * @brief Sets the knobs it reads, the rate and which deck it is (0 .. 2).
     * @param[in] params      the knobs it reads
     * @param[in] sampleRate  the sample rate in Hz
     * @param[in] index       which deck it is, 0 .. 2
     */
    void prepare(const ParamStore* params, double sampleRate, int index);
    /**
     * @brief Plays @p score from now on (allocates).
     * @param[in] score  the score to play
     */
    void load(const Score& score);
    /** @brief Plays nothing. */
    void clear();
    /**
     * @brief Silence; the next note and curve at @p sample found again (a jump to 0 plays what is due at it).
     * @param[in] sample  the position to continue from
     */
    void seek(int64_t sample);
    /**
     * @brief Reads the knobs and the automation at @p sample.
     * @param[in] sample  the position whose knobs and automation are read
     */
    void updateCell(int64_t sample);
    /**
     * @brief Plays every note due at or before @p sample.
     * @param[in] sample  the position up to which the notes are played
     */
    void dispatchUntil(int64_t sample);
    /**
     * @brief The sample of the next note not yet played (Never when none).
     * @return the sample of the next note not yet played, Never when none
     */
    int64_t nextEvent() const { return m_evCursor < m_events.size() ? m_events[m_evCursor].m_sample : Never; }
    /**
     * @brief Whether it plays at @p sample (else it rests).
     * @param[in] sample  the position asked about
     * @return true when the deck plays there, false when it rests
     */
    bool playsAt(int64_t sample) const;
    /**
     * @brief The first sample after @p sample where it starts or stops resting (Never when none).
     * @param[in] sample  the position to look from
     * @return the first sample after it where the deck starts or stops resting, Never when none
     */
    int64_t nextRestChange(int64_t sample) const;
    /**
     * @brief Renders @p n samples from @p sample into @p L and @p R (replaced): at most up to the next raster line and the
     *        next note. With @p stemL / @p stemR, also writes its stems there (replaced, from index 0): each through its
     *        own copy of the linear stages (group high pass, tilt) and times the gains the nonlinear ones gave the whole
     *        (the drum bus, the glue) and the trim, so they sum to L and R.
     * @param[in]  sample  the position of the first sample
     * @param[out] L       receives the left channel (replaced)
     * @param[out] R       receives the right channel (replaced)
     * @param[in]  n       how many samples
     * @param[out] stemL   per stem its left channel (replaced), or null for none
     * @param[out] stemR   per stem its right channel (replaced), or null for none
     */
    void render(int64_t sample, float* L, float* R, int n, float* const* stemL, float* const* stemR);
    /**
     * @brief Live play (Engine::setLive): the performer's mutes act.
     * @param[in] on  true for live play
     */
    void setLive(bool on) { m_live = on; }
    /**
     * @brief A key of a MIDI keyboard (01.10.2026, Engine::queueLive), played at @p sample on this deck with its sound as
     *        its knobs have it -- past the mutes, which are the composer's. @p target is a perform::keys value (Kit ..
     *        Brass); @p on false releases the key. A mono voice (the bass, the 303) slides to a key pressed while another
     *        is held, and its release ends the note only when it is the sounding key; a polyphonic voice holds a key
     *        until its release.
     * @param[in] sample    the position the key is played at
     * @param[in] target    the perform::keys value it plays (Kit .. Drone)
     * @param[in] pitch     the MIDI note
     * @param[in] velocity  the velocity, 0..1
     * @param[in] on        true for a key pressed, false for one released
     */
    void liveNote(int64_t sample, int target, int pitch, float velocity, bool on);
    /** @brief Releases every played key (a stop, another keyboard target), and forgets which parts were played. */
    void liveAllOff();
    /**
     * @brief From now on every composer note it plays is also written to @p tap (null: stops; NoteTap.h, MIDI out).
     * @param[out] tap  where the notes go, null to stop
     */
    void setNoteTap(NoteTap* tap) { m_noteTap = tap; }
    /**
     * @brief The family jam (02.10.2026, the plugin's Jam.h): every pitched part's composed notes @p transpose semitones
     *        from where they were written, from the next note on (a note ends with the shift it began with), and with
     *        @p rhythmOut the rhythm's foundation out, as the leader's break has it.
     * @param[in] transpose  semitones the pitched parts move
     * @param[in] rhythmOut  true to leave out the rhythm's foundation
     */
    void setJam(int transpose, bool rhythmOut) { m_jamTranspose = transpose; m_jamMutes = rhythmOut ? JamRhythm : 0u; }
    /**
     * @brief Where the engine keeps what it wrote on the knobs (Engine.h; NaN: never), read by played().
     * @param[in] shown  per parameter the value the engine last wrote on the knob, NaN for never
     */
    void setShown(const float* shown) { m_shown = shown; }
    /**
     * @brief The beat of the knob settings this deck plays from (Score::knobs), -1 before any.
     * @return the beat of the knob settings it plays from, -1 before any
     */
    double knobGroup() const { return m_knobGroup; }
    /**
     * @brief Whether a new group of knob settings was taken since the last call (and forgets it).
     * @return true when a new group of knob settings was taken since the last call
     */
    bool takeNewGroup() { const bool n = m_newGroup; m_newGroup = false; return n; }
    /**
     * @brief The value this deck plays parameter @p id from, NaN where it follows the knob.
     * @param[in] id  the parameter
     * @return the value it is played from, NaN where it follows the knob
     */
    float baseOf(int id) const { return id >= 0 && static_cast<size_t>(id) < m_base.size() ? m_base[static_cast<size_t>(id)] : std::numeric_limits<float>::quiet_NaN(); }
    /**
     * @brief The Quest's quality (Engine::setQuality): fewer unison oscillators and voices (Poly.h).
     * @param[in] on  true for the Quest's quality
     */
    void setQuest(bool on);
    /**
     * @brief The score it plays.
     * @return the score it plays
     */
    const Score& score() const { return m_score; }
    /**
     * @brief Whether it has a score.
     * @return true when it has a score
     */
    bool loaded() const { return m_loaded; }
    /**
     * @brief From now on adds the strips' levels into @p sink (null: stops).
     * @param[out] sink  where the strips' levels go, null to stop
     */
    void setMeterSink(MeterSink* sink) { m_meter = sink; }
    /**
     * @brief The value of parameter @p id as this deck plays it: the knob plus its score's automation.
     * @param[in] id  the parameter
     * @return its value as played: the knob plus the score's automation
     */
    float played(int id) const;
    /**
     * @brief Reads a module instance as played.
     * @param[in]  m         the module
     * @param[in]  instance  its instance
     * @param[out] out       receives the instance's values as played, one per parameter
     */
    void readPlayed(ModuleE m, int instance, float* out) const;
    /**
     * @brief The samples of its score's steps on the DJ mixer's knobs (a kill, a swap): the engine reads them there.
     * @return the samples of the score's steps on the DJ mixer's knobs
     */
    const std::vector<int64_t>& mixerSteps() const { return m_mixerSteps; }
    /**
     * @brief The loudness corrections of its tracks (a trim per LevelMark), found after they began; they glide in.
     * @param[in] trims  one loudness trim per LevelMark of the score
     */
    void setLevelTrims(const std::vector<float>& trims) { m_lateTrims.assign(trims.begin(), trims.end()); }
    /**
     * @brief The parts' corrections of its tracks (one per LevelMark), found after they began; as the trims.
     * @param[in] bal  one set of part corrections per LevelMark of the score
     */
    void setLevelBalance(const std::vector<BalanceDbT>& bal) { m_lateBal.assign(bal.begin(), bal.end()); }
    /**
     * @brief From now on keeps the loudest sample of the kick and of every part (the Leveler's reading).
     * @param[in] on  true to keep the peaks, false to stop
     */
    void watchPeaks(bool on);
    /**
     * @brief The kick's loudest sample since watchPeaks(true).
     * @return the kick's loudest sample since watchPeaks(true)
     */
    float kickPeak() const { return m_kickPeak; }
    /**
     * @brief Part @p p's (BalPart, a lane 0 .. 11 first) loudest sample since watchPeaks(true).
     * @param[in] p  the part (BalPart; the lanes 0 .. 11 first)
     * @return its loudest sample since watchPeaks(true)
     */
    float partPeak(int p) const { return m_partPeak[p]; }
    /**
     * @brief The kick (for the tests).
     * @return the kick
     */
    const Kick& kick() const { return m_kick; }
    /**
     * @brief The piano (for the tests).
     * @return the piano
     */
    const Piano& piano() const { return m_piano; }
    /**
     * @brief Polyphonic voice @p i (for the tests).
     * @param[in] i  the voice's index
     * @return the voice
     */
    const Poly& poly(int i) const { return m_poly[i]; }
    /**
     * @brief The gain the pump gives polyphonic voice @p i at the last sample rendered (for the tests).
     * @param[in] i  the voice's index
     * @return the pump's gain, 0..1
     */
    float polyDuckGain(int i) const { return m_lastDuck[i]; }

private:
    /** @brief A note event on the sample grid. */
    struct Ev {
        int64_t m_sample;   ///< when
        uint8_t m_on;       ///< 0 off, 1 on (offs first at equal samples)
        uint8_t m_part;     ///< Part
        bool m_accent;      ///< the 303's accent, a poly voice's filter accent
        bool m_slide;       ///< slides into the next note
        int m_pitch;        ///< MIDI note
        float m_velocity;   ///< 0..1
        int m_shift;        ///< a kit hit's pitch shift
        double m_late;      ///< how many samples ago it ideally happened (0 <= late < 1)
        int m_id;           ///< pairs an off with its on
        double m_lengthBeats;   ///< the note's written length (a poly voice's dynamic detune)
        int m_gate;         ///< a poly voice's gate, samples
    };
    /** @brief The automation curves on one parameter, in time order, with a cursor. */
    struct Track {
        int m_param;                      ///< the knob
        std::vector<Gesture> m_gestures;  ///< its curves
        size_t m_cursor = 0;              ///< index of the latest curve that has started, or gestures.size()
        float m_offset = 0.0f;            ///< the offset at the current cell
    };
    /** @brief A voice's sends. */
    struct Sends {
        float m_room = 0.0f;    ///< to the room
        float m_plate = 0.0f;   ///< to the plate
        float m_hall = 0.0f;    ///< to the hall
    };

    /**
     * @brief Plays note event @p e: a voice started or released, past the mutes and the keyboard as they stand.
     * @param[in] e  the note event
     */
    void dispatch(const Ev& e);

    const ParamStore* m_params = nullptr;   ///< the knobs it reads
    int m_index = 0;   ///< which deck it is (0 .. 2)
    double m_sampleRate = 48000.0;   ///< the sample rate, Hz
    bool m_loaded = false;   ///< it has a score
    bool m_live = false;   ///< live play (setLive): the performer's mutes and the keyboard act
    /// The track's absolute knob settings (Score::knobs): the value this deck plays each knob from, NaN where it plays the
    /// knob; a hand's turn of the knob away from what the engine wrote on it (m_shown) moves it from there.
    std::vector<float> m_base;
    size_t m_knobCursor = 0;   ///< index of the next knob setting of the score not yet taken
    double m_knobGroup = -1.0;   ///< the beat of the knob settings it plays from, -1 before any
    bool m_newGroup = false;   ///< a new group of knob settings was taken since takeNewGroup()
    const float* m_shown = nullptr;   ///< what the engine wrote on the knobs (setShown; NaN never), or null
    /**
     * @brief Takes the knob settings due up to @p beat (a group's replaces the last group's); whether it took any.
     * @param[in] beat  the position in beats
     * @return true when it took any
     */
    bool applyKnobs(double beat);
    /**
     * @name The cell's parameter cache (the optimisation pass, 29.09.2026)
     * Most cells change nothing: the knobs stand and no curve moves, yet every cell read all 1670 parameters and handed
     * every module to its engine again -- 2 % of a core, an eighth of a track's render. The played value of every
     * parameter is now kept from the cell before and read again only where it can have changed: all of them after a
     * write to the store (ParamStore::version), a knob setting of the score, a load, a seek or a new quality level;
     * otherwise only those whose curve moved. A module instance whose values stayed bit for bit what they were is not
     * handed to its engine again -- the engines' update() are functions of their values (and of the key, the scale
     * and the tempo, which are compared as well), so a second call with the same values changes nothing, and every
     * render is the render it was (the check: the WAVs of five styles and a set, desktop and Quest, bit for bit).
     * @{ */
    std::vector<float> m_cellValue;      ///< every parameter as played at the last cell
    std::vector<int> m_firstOf;          ///< parameter id -> the first id of its module instance
    std::vector<uint8_t> m_instChanged;  ///< by a module instance's first id: its values changed since it was last read
    std::vector<int> m_staleIds;         ///< the parameters whose curve moved at this cell
    uint32_t m_seenVersion = 0;          ///< the store's version() the cache was read at
    bool m_cellFresh = true;             ///< the next cell reads every parameter and hands every module over
    int m_cellKey = -1, m_cellScale = -1; ///< the key and the scale the modules were last handed over with
    double m_cellBpm = -1.0;             ///< and the tempo
    /**
     * @brief Brings the cache to this cell: every parameter when @p all, else the stale ones.
     * @param[in] all  true for every parameter, false for the stale ones
     */
    void refreshCell(bool all);
    /**
     * @brief Module instance @p m / @p instance as played at this cell into @p out; whether it changed since last read.
     * @param[in]  m         the module
     * @param[in]  instance  its instance
     * @param[out] out       its parameter values
     * @return true when they changed since the last read
     */
    bool readCell(ModuleE m, int instance, float* out);
    /** @} */
    uint32_t m_mutes = 0;   ///< the performer's muted groups (perform::MuteKick ..), bit k for group k
    /**
     * @brief Whether a group is muted -- never for a played key (liveNote).
     * @param[in] param  the perform mute parameter of the group
     * @return true when the group is muted
     */
    bool muted(int param) const { return !m_liveEvent && (((m_mutes | m_jamMutes) >> (param - perform::MUTE_KICK)) & 1u) != 0; }
    /** @brief The perform mutes the family jam adds in the leader's break (setJam). */
    static constexpr uint32_t JamRhythm = (1u << (perform::MUTE_KICK - perform::MUTE_KICK)) | (1u << (perform::MUTE_BASS - perform::MUTE_KICK));
    int m_jamTranspose = 0;           ///< the family jam's transposition of the pitched parts (setJam)
    uint32_t m_jamMutes = 0;          ///< the perform mutes the family jam adds now (setJam)
    std::array<std::array<int8_t, 128>, NumParts> m_jamShift{};   ///< the shift each held pitched note began with (dispatch)
    /**
     * @brief Plays @p e past the mutes and the keyboard (dispatch, after the family jam's transposition).
     * @param[in] e  the note event
     */
    void dispatchPlayed(const Ev& e);
    // The keyboard (01.10.2026, liveNote): read from the perform module in live play (updateCell).
    int m_keyTarget = 0;              ///< perform.keyboard_part (perform::keys)
    bool m_keyReplace = true;         ///< perform.keyboard_mode Replace: the played part's generated notes are left out
    bool m_composerOff = false;       ///< perform.composer off: no generated note at all
    uint32_t m_keyPlayed = 0;         ///< by channel: the targets played since the last liveAllOff (bit = target)
    bool m_liveEvent = false;         ///< dispatch() plays a key now: no mute, no silencing
    NoteTap* m_noteTap = nullptr;     ///< where the composer's played notes go for MIDI out (setNoteTap), or null
    /**
     * @brief The perform mute (perform::MuteKick ..) that silences @p part's notes, -1 for none.
     * @param[in] part  the part
     * @return the perform mute that silences its notes, -1 for none
     */
    int muteOf(PartE part) const;
    int m_liveHeld[perform::keys::COUNT] = {};   ///< a mono target's sounding key + 1 (0 none)
    uint8_t m_liveKey[128] = {};      ///< a polyphonic target's held keys: its target + 1 (0 none) -- for liveAllOff
    /**
     * @brief Whether the composer's note-ons of @p part are left out (the composer off, or the keyboard replaces it).
     * @param[in] part  the part
     * @return true when its composed note-ons are left out
     */
    bool silenced(PartE part) const;
    /**
     * @brief The keyboard target a part belongs to (perform::keys; Off for none).
     * @param[in] part  the part
     * @return the keyboard target it belongs to (perform::keys), Off for none
     */
    static int targetOf(PartE part);
    Score m_score;   ///< the score it plays
    std::vector<Ev> m_events;   ///< the score's notes on the sample grid, in time order
    size_t m_evCursor = 0;   ///< index of the next event not yet played
    std::vector<Track> m_tracks;   ///< the automation, one track per parameter that has curves
    std::vector<int> m_trackOf;   ///< parameter id -> index into m_tracks, or -1
    std::vector<int64_t> m_mixerSteps;   ///< the samples of its score's steps on the DJ mixer's knobs
    std::vector<std::pair<int64_t, int64_t>> m_plays;   ///< where it plays: [from, to) in samples, sorted

    Kick m_kick;   ///< the kick
    SubBass m_sub;   ///< the sub bass sine
    PercKit m_kit;   ///< the percussion: twelve lanes
    MonoSynth m_bass;   ///< the bass line
    MonoSynth m_acid;   ///< the 303
    Poly m_poly[PolyInstances];   ///< the polyphonic voices: lead, counter, pluck, arp, pad, stab
    TranceGate m_gate[PolyInstances];   ///< per voice: its trance gate
    Ducker m_polyDuck[PolyInstances];   ///< per voice: its pump
    Ducker m_retDuck;   ///< the returns' pump
    Piano m_piano;              ///< the physical piano (PLAN 5.8)
    Ducker m_pianoDuck;   ///< the piano's pump
    Sends m_pianoSends;   ///< the piano's sends
    /// The orchestra (PLAN 5.9): four instruments, each with its duck and sends, in BalPart order from Strings.
    StringSection m_strings;
    Choir m_choir;   ///< the choir
    Brass m_brass;   ///< the brass
    Timpani m_timpani;   ///< the timpani
    static constexpr int Orch = 4;   ///< the orchestra's instruments
    Ducker m_orchDuck[Orch];   ///< per instrument: its pump
    Sends m_orchSends[Orch];   ///< per instrument: its sends
    GrainCloud m_cloud;         ///< the granular cloud (Deep)
    float m_cloudPad = 0.8f;   ///< the pad's send to the cloud
    float m_cloudKeys = 0.5f;   ///< the pluck's and the piano's send to the cloud
    float m_cloudPlate = 0.5f;   ///< the cloud's send to the plate
    Sfx m_sfx;                  ///< the effects (PLAN 5.10)
    Ducker m_fxDuck;   ///< the effects' pump
    Ducker m_subDropDuck;   ///< the sub drop's own pump (sfx.sub_duck)
    Sends m_fxSends;   ///< the effects' sends
    int m_keyRoot = 9;   ///< the key's root, 0 = C (compose.key)
    int m_scale = 0;   ///< the scale (compose.scale)
    double m_beat = 0.0;   ///< the beat at the cell's first sample
    double m_beatsPerSample = 0.0;   ///< its step: beats per sample (the gates, the poly clock)
    int64_t m_cellSample = 0;                     ///< the cell's first sample: a span that starts inside a cell counts from it
    /// The last kick, for the sub's lock.
    bool m_haveKick = false;
    double m_kickTime = 0.0;   ///< its ideal start, in samples
    double m_kickC = 0.0;   ///< the last kick's asymptotic phase, cycles: the sub's lock
    double m_kickF0 = 50.0;   ///< the last kick's tuned end frequency, Hz: the sub's lock
    int m_subNote = -1;   ///< the id of the sub's sounding note, -1 none
    int m_bassNote = -1;   ///< the id of the bass's sounding note, -1 none
    int m_acidNote = -1;   ///< the id of the 303's sounding note, -1 none

    /** @brief A voice's strip: its gate and its sends. */
    struct PolyStrip {
        bool m_gate = false;   ///< the gate is in
        int m_pattern = 0;   ///< the gate's pattern (TranceGate::open)
        float m_depth = 0.85f;   ///< the gate's depth
        float m_duty = 0.5f;   ///< the gate's duty cycle
        float m_attackBeats = 0.0f;   ///< the gate's attack, beats
        float m_releaseBeats = 0.0f;   ///< the gate's release, beats
        float m_tone = 0.4f;   ///< the gate's tone: how much of the top it takes along
        Sends m_sends;   ///< the voice's sends
    };
    PolyStrip m_strip[PolyInstances];   ///< per voice: its strip
    float m_lastDuck[PolyInstances] = {};   ///< per voice: its pump's last gain
    Sends m_bassSends;   ///< the bass's sends
    Sends m_acidSends;   ///< the 303's sends

    // Buses, sends, rooms.
    Svf m_hatsLp[2];   ///< the hats bus's low pass, per channel
    Svf m_percLp[2];   ///< the percussion bus's low pass, per channel
    float m_hatsGain = 1.0f;   ///< the hats bus's level (mix.hats_level, with its motion)
    float m_percGain = 1.0f;   ///< the percussion bus's level
    float m_synthGain = 1.0f;   ///< the synths' fader as it stands (mix.synth_level, the breaks)
    float m_synthTarget = 1.0f;   ///< where it glides to
    float m_fxGain = 1.0f;   ///< the effects' fader as it stands (the breaks)
    float m_fxTarget = 1.0f;   ///< where it glides to
    std::vector<std::pair<double, double>> m_breaks;   ///< the beats of the breakdowns and breaks (LevelMark::breakDb)
    std::vector<std::pair<double, double>> m_builds;   ///< the beats of the builds (LevelMark::buildDb)
    float m_percBuild = 1.0f;                          ///< the build's correction on the percussion bus
    bool m_snapFaders = true;   ///< after a seek the faders start where the score has them, not gliding there
    bool m_laneIsHat[PercLanes] = {};   ///< per kit lane: it goes to the hats bus (a hat), else to the percussion bus
    float m_hatsSend = 0.0f;   ///< the hats bus's room send
    float m_percSend = 0.0f;   ///< the percussion bus's room send
    Reverb m_room;   ///< the room (space)
    Reverb m_hall;   ///< the hall
    Plate m_plate;   ///< the plate
    float m_roomReturn = 1.0f;   ///< the room's return level
    float m_plateReturn = 1.0f;   ///< the plate's return level
    float m_hallReturn = 1.0f;   ///< the hall's return level
    float m_drumSat = 0.2f;   ///< the drum bus's saturation, 0 .. 1 (mix.drum_sat)
    // The track bus.
    Svf m_groupHp[2][2];              ///< the group high pass (mix.low_cut), fourth order, per channel
    bool m_groupHpOn = false;   ///< the group high pass is in (mix.low_cut above 20.5 Hz)
    float m_tiltHigh = 1.0f;   ///< the master tilt's gain on the highs (master.tilt)
    float m_tiltCoef = 0.1f;   ///< the tilt's one-pole coefficient (1 kHz)
    float m_tiltState[2] = {};   ///< the tilt's low pass, per channel
    BusCompressor m_glue;   ///< the bus compressor on the track
    float m_trimGain = 1.0f;   ///< the loudness trim as it stands, gliding to m_trimTarget
    float m_trimTarget = 1.0f;   ///< the trim of the track that plays now
    float m_trimCoef = 0.0f;   ///< the trim's glide (about a second)
    std::vector<float> m_lateTrims;   ///< setLevelTrims: the corrections found while playing
    float m_balGain[BalParts] = {};   ///< (1 from prepare() on)
    float m_balTarget[BalParts] = {};   ///< the parts' gains of the track that plays now
    bool m_watch = false;   ///< the loudest samples are kept (watchPeaks)
    MeterSink* m_meter = nullptr;     ///< setMeterSink
    float m_kickPeak = 0.0f;   ///< the kick's loudest sample since watchPeaks(true)
    float m_partPeak[BalParts] = {};   ///< every part's loudest sample since watchPeaks(true)
    std::vector<BalanceDbT> m_lateBal;   ///< setLevelBalance: the parts' corrections found while playing

    std::vector<float> m_kickBuf;   ///< the kick's click and body, mono
    std::vector<float> m_bodyBuf;   ///< the kick's body alone, mono
    std::vector<float> m_subBuf;   ///< the sub bass, mono
    std::vector<float> m_bassL;   ///< the bass, left
    std::vector<float> m_bassR;   ///< the bass, right
    std::vector<float> m_acidL;   ///< the 303, left
    std::vector<float> m_acidR;   ///< the 303, right
    std::vector<float> m_polyL[PolyInstances];   ///< per voice: its block, left
    std::vector<float> m_polyR[PolyInstances];   ///< per voice: its block, right
    std::vector<float> m_pianoL;   ///< the piano, left
    std::vector<float> m_pianoR;   ///< the piano, right
    std::vector<float> m_orchL[Orch];   ///< per instrument: its block, left
    std::vector<float> m_orchR[Orch];   ///< per instrument: its block, right
    std::vector<float> m_cloudInL;   ///< the cloud's input, left
    std::vector<float> m_cloudInR;   ///< the cloud's input, right
    std::vector<float> m_cloudL;   ///< the cloud's return, left
    std::vector<float> m_cloudR;   ///< the cloud's return, right
    std::vector<float> m_roomInL;   ///< the room's input, left
    std::vector<float> m_roomInR;   ///< the room's input, right
    std::vector<float> m_plateInL;   ///< the plate's input, left
    std::vector<float> m_plateInR;   ///< the plate's input, right
    std::vector<float> m_hallInL;   ///< the hall's input, left
    std::vector<float> m_hallInR;   ///< the hall's input, right
    std::vector<float> m_roomL;   ///< the room's return, left
    std::vector<float> m_roomR;   ///< the room's return, right
    std::vector<float> m_plateL;   ///< the plate's return, left
    std::vector<float> m_plateR;   ///< the plate's return, right
    std::vector<float> m_hallL;   ///< the hall's return, left
    std::vector<float> m_hallR;   ///< the hall's return, right
    std::vector<float> m_drumL;   ///< the drum bus after its saturation, left
    std::vector<float> m_drumR;   ///< the drum bus after its saturation, right
    std::vector<float> m_synthL;   ///< the synths summed, left
    std::vector<float> m_synthR;   ///< the synths summed, right
    std::vector<float> m_fxL;   ///< the effects, left
    std::vector<float> m_fxR;   ///< the effects, right
    std::vector<float> m_fxSub;   ///< the effects' sub layer, mono (the sub drop)
    std::vector<float> m_fxWetL;   ///< the effects' share for the reverb, left
    std::vector<float> m_fxWetR;   ///< the effects' share for the reverb, right

    // The stems' own copies of the linear stages (render() with stems), and the gains of the nonlinear ones.
    /** @brief A stem's own copy of the track bus's linear stages. */
    struct StemBus {
        Svf m_hp[2][2];          ///< the group high pass: two stages per channel
        float m_tilt[2] = {};    ///< the tilt's low pass, per channel
    };
    StemBus m_stemBus[STEMS];   ///< per stem: its copy of the group high pass and the tilt
    float m_satGain[2][Raster] = {};   ///< per channel and sample of a raster cell: the drum bus saturation's gain
    float m_glueGains[Raster] = {};   ///< the glue's gain per sample of a cell
    float m_trimGains[Raster] = {};   ///< the trim per sample of a cell
    /** @brief Clears the stems' copies of the linear stages (a seek, a load). */
    void resetStems();
};

} // namespace parh
