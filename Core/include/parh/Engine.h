/**
 * @file Engine.h
 * @brief The engine: three decks (Deck.h), the DJ mixer and the master (PLAN 3, 7.7, 8).
 *
 * @code
 *   deck A -+-> fader -> isolator (200 Hz / 2.5 kHz, a kill each) -> filter -+-> sum -> mono under Mono Below -> level
 *   deck B -+                                                          ...   +-> fx send -> tempo echo + hall -> sum
 *   deck C -+   (a track alone plays on deck A, without isolator and filter)          -> [vinyl cut] -> clip at 4x -> limiter
 * @endcode
 * **Time** is the sample counter; beats are derived from it through the tempo map (Clock.h), never accumulated. A set's
 * decks share its tempo map.
 *
 * **Determinism across block sizes** (the rule of all three siblings). Parameters and automation are read on an
 * absolute raster of 32 samples and at the samples a deck's score steps its mixer channel (a kill, a bass swap: the
 * outgoing deck's low band closes and the incoming one's opens in the same sample), and spans are split exactly at note
 * events and where a deck starts or stops resting, so a host's block size cannot change a single sample (the self test
 * renders with blocks of 1, 37 and 512 and compares bits, with one deck and with two).
 *
 * **Loading** allocates and must not run on the audio thread (as in Ephemeris).
 * @note Copied from Totality `Core/include/tot/Engine.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 * @author Rene Weller
 */
#pragma once
#include "parh/Cue.h"
#include "parh/Deck.h"
#include "parh/Oversample.h"
#include "parh/Params.h"
#include "parh/Score.h"
#include "parh/fx/Dynamics.h"
#include "parh/fx/Reverb.h"
#include "parh/fx/TapeEcho.h"
#include <atomic>
#include <cstdint>
#include <vector>

namespace parh {

/** @brief Plays a track or a set. Not thread-safe except for parameter writes (ParamStore is atomic). */
class Engine {
public:
    /**
     * @brief The stems: the decks' (Deck::Stem), every deck's stem of one kind summed after its mixer channel, and the
     *        mixer's effects. They sum to the mix as it enters the master (mono below, level, cut, clipper, limiter).
     */
    enum StemE : int { STEM_KICK = 0, STEM_SUB, STEM_BASS, STEM_ACID, STEM_HATS, STEM_PERC, STEM_LEAD, STEM_COUNTER, STEM_PLUCK,
                      STEM_ARP, STEM_PAD, STEM_STAB, STEM_PIANO, STEM_STRINGS, STEM_CHOIR, STEM_BRASS, STEM_TIMPANI, STEM_ROOM,
                      STEM_PLATE, STEM_HALL, STEM_CLOUD, STEM_FX, STEM_MIX_FX, STEMS };
    static_assert(static_cast<int>(STEM_MIX_FX) == static_cast<int>(Deck::STEMS), "the stems are the deck's and the mixer's");
    /**
     * @brief Name of stem @p s.
     * @param[in] s  the stem
     * @return its name
     */
    static const char* stemName(int s);
    /** @brief The raster the parameters and the automation are read on, in samples. */
    static constexpr int Raster = Deck::Raster;

    /**
     * @brief The parameters; any thread may write them.
     * @return the parameters
     */
    ParamStore& params() { return m_params; }
    /**
     * @brief The parameters, read-only.
     * @return the parameters
     */
    const ParamStore& params() const { return m_params; }
    /**
     * @brief Sets the sample rate and the largest block; call before load().
     * @param[in] sampleRate  the sample rate in Hz
     * @param[in] maxBlock    the largest block process() will be asked for
     * @pre called before load()
     */
    void prepare(double sampleRate, int maxBlock);
    /**
     * @brief Plays a track on deck A (not from the audio thread) and returns to its start.
     * @param[in] score  the track
     * @pre not called on the audio thread
     */
    void load(const Score& score);
    /**
     * @brief Plays a set on the three decks (not from the audio thread) and returns to its start.
     * @param[in] set  the set
     * @pre not called on the audio thread
     */
    void loadSet(const SetScore& set);
    /**
     * @brief Renders @p n samples into @p L and @p R (overwritten) and advances the position.
     * @param[out] L  receives the left channel (overwritten)
     * @param[out] R  receives the right channel (overwritten)
     * @param[in]  n  how many samples
     * @return false once the position has passed the end (the tails still ring)
     */
    bool process(float* L, float* R, int n);
    /**
     * @brief Jumps to @p beat: every voice falls silent, the next note and curve are found again.
     * @param[in] beat  the position to jump to, in beats
     */
    void seek(double beat);
    /**
     * @brief The score deck A plays (the track).
     * @return the score deck A plays
     */
    const Score& score() const { return m_decks[0].score(); }
    /**
     * @brief Current position in seconds.
     * @return the current position in seconds
     */
    double seconds() const { return static_cast<double>(m_sample) / m_sampleRate; }
    /**
     * @brief Current position in samples (Handover.h: two engines meet on it).
     * @return the current position in samples
     */
    int64_t samplePosition() const { return m_sample; }
    /**
     * @brief Every deck writes the composer notes it plays into @p tap from now on (null: stops; MIDI out, 02.10.2026).
     * @param[out] tap  where the notes go, null to stop
     */
    void setNoteTap(NoteTap* tap) { for (Deck& d : m_decks) d.setNoteTap(tap); }
    /**
     * @brief The family jam (02.10.2026, the plugin's Jam.h), live only: the pitched parts @p transpose semitones away,
     *        the leader's @p energy (0..1; negative: none) as a darker master filter while its knob rests, and with
     *        @p rhythmOut the kick, the sub and the bass out for the leader's break.
     * @param[in] transpose  semitones the pitched parts move
     * @param[in] energy     the leader's energy, 0..1; negative for none
     * @param[in] rhythmOut  true to leave out the rhythm's foundation
     */
    void setJam(int transpose, float energy, bool rhythmOut)
    {
        for (Deck& d : m_decks) d.setJam(transpose, rhythmOut);
        m_jamEnergy = energy;
    }
    /**
     * @brief Current position in beats.
     * @return the current position in beats
     */
    double beat() const { return m_tempo.beatAt(seconds()); }
    /**
     * @brief Length of the track or set in seconds.
     * @return the length of the track or set in seconds
     */
    double lengthSeconds() const { return m_tempo.secondsAt(m_lengthBeats); }
    /**
     * @brief Sample rate set by prepare().
     * @return the sample rate in Hz
     */
    double sampleRate() const { return m_sampleRate; }
    /**
     * @brief The value of parameter @p id as deck A plays it: the knob plus its score's automation.
     * @param[in] id  the parameter
     * @return its value as deck A plays it
     */
    float played(int id) const { return m_decks[0].played(id); }
    /**
     * @brief Stems: from now on every process() call also writes the stems (Stem) into @p left[s] and @p right[s], each
     *        at least the block long. Null switches them off. Set them before the first sample: each stem runs through
     *        copies of the filters it passes, which start from silence.
     * @param[in] left   per stem its left channel buffer, null to switch the stems off
     * @param[in] right  per stem its right channel buffer
     */
    void setStems(float* const* left, float* const* right) { m_stemL = left; m_stemR = right; }
    /**
     * @brief The mix as it enters the master into @p left and @p right on every process() call (the stems' sum; for the
     *         tests). Null switches it off.
     * @param[in,out] left   the left buffer, null to switch the tap off
     * @param[in,out] right  the right buffer
     */
    void setPremasterTap(float* left, float* right) { m_preL = left; m_preR = right; }
    /**
     * @brief Live play (the plugin; before load): the mixer is in the path of a single track too, and the perform module
     *        acts -- the mutes, the master filter, the echo throw into the mixer's echo. Off (renders, exports, the
     *        default): the score plays as it was composed, and the stems sum to the mix.
     * @param[in] on  true for live play
     */
    void setLive(bool on);
    /**
     * @name A MIDI keyboard (01.10.2026, perform.keyboard_part)
     * The keys play a voice of the lead deck (the track whose sounds the knobs show) with its sound, past the mutes;
     * perform.keyboard_mode Replace leaves that voice's generated notes out (by channel: from a voice's first played key
     * on), Layer plays over them; perform.composer off leaves every generated note out. Live play only (setLive).
     * @{ */
    /**
     * @brief Queues a key for the next process() call, @p offset samples into it: it is played on that sample. The
     *        rendering thread, before process().
     * @param[in] offset    the sample within the next block the key is played at
     * @param[in] pitch     MIDI note;
     * @param[in] velocity  1..127;
     * @param[in] channel   0..15 (by channel);
     * @param[in] on        false: its release
     * @param[in] target    the perform::keys value it plays, -1 for the keyboard part of its channel
     */
    void queueLive(int offset, int pitch, int velocity, int channel, bool on, int target = -1);
    /**
     * @brief The target a key on @p channel plays when it names none (perform.keyboard_part; perform::keys).
     * @param[in] channel  the MIDI channel
     * @return the target its keys play (perform::keys)
     */
    int keyTargetFor(int channel) const { return keyboardTarget(channel); }
    /** @brief Releases every played key and forgets the queued ones (a stop, another keyboard target). */
    void liveAllOff();
    /** @} */
    /** @brief How much the engine does: all of it (the desktop, the default), or the Quest's share (PLAN 10: fewer unison
     *         oscillators and voices, Deck::setQuest). */
    enum class QualityE { DESKTOP, QUEST };
    /**
     * @brief Sets the quality (before load; any time is safe, the cloud's tail is cut).
     * @param[in] q  the quality
     */
    void setQuality(QualityE q) { for (Deck& d : m_decks) d.setQuest(q == QualityE::QUEST); }
    /**
     * @brief Counts the times the engine wrote a track's knob settings onto the knobs (Score::knobs): a track began on
     *        deck A or B, or a jump landed in another. The knobs then show that track's sounds and mix -- its presets,
     *        its style's settings -- and every deck plays from its own track's values, moved by as much as a hand turns
     *        a knob away from what was written on it. The plugin tells the host when the count changes.
     * @return how many times a track's knob settings were written onto the knobs
     */
    uint32_t soundsVersion() const { return m_soundsVersion.load(std::memory_order_relaxed); }
    /**
     * @brief The deck whose track's knob settings the knobs show, -1 before any.
     * @return the deck whose knob settings the knobs show, -1 before any
     */
    int leadDeck() const { return m_lead.load(std::memory_order_relaxed); }
    /**
     * @brief The cue marks of what is loaded (Cue.h: blocks, operations, keys, a set's tracks), for CueTap::scan().
     * @return the cue marks of what is loaded
     */
    const std::vector<CueMark>& cueMarks() const { return m_cueMarks; }
    /**
     * @brief Deck taps: from now on every process() call also writes each deck after its mixer channel (before the
     *         effects and the master) into @p left[d] and @p right[d]. Null switches them off.
     * @param[in] left   per deck its left channel buffer, null to switch the taps off
     * @param[in] right  per deck its right channel buffer
     */
    void setDeckTaps(float* const* left, float* const* right) { m_tapL = left; m_tapR = right; }
    /**
     * @brief The loudness corrections of deck A's tracks (a trim per LevelMark), found after they began; they glide in.
     * @param[in] trims  one loudness trim per LevelMark of deck A's score
     */
    void setLevelTrims(const std::vector<float>& trims) { m_decks[0].setLevelTrims(trims); }
    /**
     * @brief The same for deck @p d.
     * @param[in] d      the deck
     * @param[in] trims  one loudness trim per LevelMark of its score
     */
    void setLevelTrims(int d, const std::vector<float>& trims) { m_decks[d].setLevelTrims(trims); }
    /**
     * @brief Phase 18: the parts' corrections of deck @p d's tracks (one per LevelMark), found after they began.
     * @param[in] d    the deck
     * @param[in] bal  one set of part corrections per LevelMark of its score
     */
    void setLevelBalance(int d, const std::vector<BalanceDbT>& bal) { m_decks[d].setLevelBalance(bal); }
    /**
     * @brief Phase 18: every deck keeps its parts' loudest samples from now on (Deck::watchPeaks), or stops.
     * @param[in] on  true to keep the peaks, false to stop
     */
    void watchPeaks(bool on) { for (Deck& d : m_decks) d.watchPeaks(on); }
    /**
     * @brief The mixer page's strip meters: every deck adds into @p sink from now on (null: stops).
     * @param[out] sink  where the strips' levels go, null to stop
     */
    void setMeterSink(MeterSink* sink) { for (Deck& d : m_decks) d.setMeterSink(sink); }
    /**
     * @brief Deck @p d (0 .. 2).
     * @param[in] d  the deck, 0 .. 2
     * @return the deck
     */
    const Deck& deck(int d) const { return m_decks[d]; }
    /**
     * @brief The kick of deck A (for the tests).
     * @return deck A's kick
     */
    const Kick& kick() const { return m_decks[0].kick(); }
    /**
     * @brief The isolator's low band gain of deck @p d at the current sample, 0..1 (for the tests).
     * @param[in] d  the deck
     * @return its isolator's low band gain at the current sample, 0..1
     */
    float lowGain(int d) const { return m_ch[d].m_g[0]; }
    /**
     * @brief Whether deck @p d plays at the current sample (for the tests).
     * @param[in] d  the deck
     * @return true when it plays at the current sample
     */
    bool deckPlays(int d) const { return m_playing[d]; }

private:
    /** @brief A three-band Linkwitz-Riley split at 200 Hz and 2.5 kHz (the low band through the upper crossover's allpass). */
    struct ThreeBand {
        Svf m_split1;   ///< the 200 Hz split
        Svf m_low2;   ///< its low pass again (fourth order)
        Svf m_high2;   ///< its high pass again
        Svf m_split2;   ///< the 2.5 kHz split of the high band
        Svf m_mid2;   ///< its low pass again
        Svf m_top2;   ///< its high pass again
        Svf m_ap;   ///< the 2.5 kHz allpass the low band goes through
        /**
         * @brief Tunes the crossovers for sample rate @p fs.
         * @param[in] fs  the sample rate in Hz
         */
        void set(float fs);
        /** @brief Silence: every filter state cleared. */
        void reset();
        /**
         * @brief Splits sample @p x into @p low (in phase with the others), @p mid and @p top.
         * @param[in]  x    the input sample
         * @param[out] low  receives the low band
         * @param[out] mid  receives the middle band
         * @param[out] top  receives the top band
         */
        void process(float x, float& low, float& mid, float& top);
    };
    /** @brief A mixer channel. */
    struct Channel {
        float m_target[3] = { 1.0f, 1.0f, 1.0f };   ///< the isolator's gains to go to
        float m_g[3] = { 1.0f, 1.0f, 1.0f };        ///< where they are (smoothed over a millisecond)
        float m_fader = 1.0f;   ///< the channel fader's gain to go to
        float m_gFader = 1.0f;   ///< where it is (smoothed like the isolator)
        float m_send = 0.0f;   ///< the channel's send into the mixer's effects (deck.fx_send)
        float m_filter = 0.0f;   ///< the channel's filter, -1 (low pass) .. 1 (high pass), 0 open
        ThreeBand m_bands[2];   ///< the isolator's split, per channel
        Svf m_filt[2];   ///< the channel's filter, per channel
    };
    /** @brief The set's own automation on one knob (the mixer's effects), with a cursor. */
    struct Track {
        int m_param;   ///< the knob
        std::vector<Gesture> m_gestures;   ///< its curves, in time order
        size_t m_cursor = 0;   ///< index of the latest curve that has started, or gestures.size()
        float m_offset = 0.0f;   ///< the offset at the current cell
    };

    /** @brief Reads the knobs and the automation for the next raster cell: the mixer, its effects, the master. */
    void updateCell();
    /**
     * @brief Mixes the decks' blocks through their channels and the mixer's effects into @p L and @p R (@p n samples).
     * @param[out] L  receives the left channel
     * @param[out] R  receives the right channel
     * @param[in]  n  how many samples
     */
    void mix(float* L, float* R, int n);
    /**
     * @brief The value of parameter @p id as the set plays it: the knob plus the set's own automation.
     * @param[in] id  the parameter
     * @return its value as the set plays it
     */
    float setPlayed(int id) const;

    ParamStore m_params;   ///< the knobs
    double m_sampleRate = 48000.0;   ///< the sample rate, Hz
    int m_maxBlock = 512;   ///< the largest block process() is given
    int64_t m_sample = 0;   ///< the position, samples
    int64_t m_endSample = 0;   ///< the end of what is loaded, samples
    TempoMap m_tempo;   ///< the beats' times
    double m_lengthBeats = 0.0;   ///< the length of what is loaded, beats
    bool m_cellDirty = true;   ///< the knobs are to be read again before the next sample
    bool m_isSet = false;           ///< a set: the isolators and filters are in the path
    bool m_fxOn = false;            ///< the set sends into the mixer's effects
    Deck m_decks[Decks];   ///< the three decks: A plays a track, all three a set
    bool m_playing[Decks] = {};   ///< per deck: it plays at the current sample
    std::vector<int64_t> m_steps;   ///< where the decks' mixer channels step, merged
    size_t m_stepCursor = 0;   ///< index of the next step in m_steps
    std::vector<Track> m_tracks;   ///< the set's own automation, one track per knob
    std::vector<int> m_trackOf;   ///< parameter id -> index into m_tracks, or -1

    /// The mixer.
    Channel m_ch[Decks];
    float m_smooth = 0.02f;         ///< the channel gains' one-pole coefficient (a millisecond)
    TapeEcho m_djEcho;   ///< the mixer's echo
    Reverb m_djHall;   ///< the mixer's hall
    float m_echoReturn = 0.0f;   ///< the echo's return level
    float m_hallReturn = 0.0f;   ///< the hall's return level
    // The master.
    Svf m_sideHp1;   ///< mono below (master.mono_below): the side's high pass, first stage
    Svf m_sideHp2;   ///< ... and the second (fourth-order Butterworth together)
    float m_masterGain = 1.0f;   ///< master.level, linear
    float m_clipDrive = 1.0f;   ///< the clipper's drive (master.clip), linear
    bool m_cut = false;   ///< the vinyl cut is in (master.cut)
    BandLimit m_cutLp[2];   ///< the cut's 16 kHz band limit, per channel
    Svf m_cutHp1[2];   ///< the cut's 6 kHz high pass, first stage, per channel
    Svf m_cutHp2[2];   ///< ... and the second
    float m_cutEnv = 0.0f;   ///< the cut's envelope of the band over 6 kHz
    float m_cutAtt = 0.1f;   ///< the envelope's attack coefficient (a millisecond)
    float m_cutRel = 0.001f;   ///< the envelope's release coefficient (50 ms)
    Oversampler4 m_clipOs[2];   ///< the clipper's fourfold oversampling, per channel
    TruePeakLimiter m_limiter;   ///< the true-peak limiter (master.ceiling)

    std::vector<float> m_deckL[Decks];   ///< per deck: its block, left
    std::vector<float> m_deckR[Decks];   ///< per deck: its block, right
    std::vector<float> m_sendL;   ///< the send into the mixer's effects, left
    std::vector<float> m_sendR;   ///< the send into the mixer's effects, right
    std::vector<float> m_fxL;   ///< the mixer's effects' output, left
    std::vector<float> m_fxR;   ///< the mixer's effects' output, right
    float* const* m_stemL = nullptr;   ///< the stems' left channels (setStems), or null
    float* const* m_stemR = nullptr;   ///< the stems' right channels, or null
    float* const* m_tapL = nullptr;   ///< the deck taps' left channels (setDeckTaps), or null
    float* const* m_tapR = nullptr;   ///< the deck taps' right channels, or null
    float* m_preL = nullptr;   ///< the premaster tap, left (setPremasterTap), or null
    float* m_preR = nullptr;   ///< the premaster tap, right, or null
    /// The knob settings (soundsVersion): what the engine wrote on each knob (NaN: never), the deck they came from.
    std::vector<float> m_shown;
    std::atomic<int> m_lead{ -1 };   ///< the deck whose knob settings the knobs show, -1 before any
    double m_leadGroup = -1.0;   ///< the beat of the lead deck's knob settings last shown, -1 before any
    std::atomic<uint32_t> m_soundsVersion{ 0 };   ///< how often knob settings were written onto the knobs
    /**
     * @brief Puts deck @p d's knob settings on the knobs.
     * @param[in] d  the deck
     */
    void showKnobs(int d);
    /// Live play (setLive).
    bool m_live = false;
    /** @brief A key queued for the current process() call (queueLive). */
    struct LiveKey {
        int64_t m_at;       ///< the engine sample it plays on
        int m_pitch;        ///< MIDI note
        float m_velocity;   ///< 0..1
        int m_target;       ///< perform::keys (the release's is the one its key went to)
        bool m_on;          ///< false: a release
    };
    static constexpr int LiveQueue = 256;   ///< keys per process() call at most
    LiveKey m_liveQueue[LiveQueue] = {};   ///< the keys queued for this process() call, in time order
    int m_liveCount = 0;   ///< how many keys are queued
    int m_liveCursor = 0;   ///< index of the next queued key not yet played
    uint8_t m_liveTarget[128] = {};   ///< per key: the target it plays + 1 (0: not held)
    uint8_t m_liveDeck[128] = {};     ///< per key: the deck it plays on
    /**
     * perform.keyboard_part for a key on @p channel (perform::keys; Off: none)
     * @param[in] channel  the MIDI channel
     * @return the target a key there plays (perform::keys), Off for none
     */
    int keyboardTarget(int channel) const;
    /**
     * a queued key, now
     * @param[in] k  the queued key
     */
    void playLive(const LiveKey& k);
    /**
     * the key @p pitch, wherever it plays
     * @param[in] pitch  the MIDI note of the key
     */
    void releaseLive(int pitch);
    float m_perfFilter = 0.0f;   ///< perform.filter as read at the cell: -1 low pass .. 1 high pass
    float m_jamEnergy = -1.0f;   ///< the family jam's energy (setJam); negative: no leader
    float m_jamFilter = 0.0f;    ///< the master filter the jam's energy asks for, glided (updateCell)
    float m_perfThrow = 0.0f;   ///< perform.throw as read at the cell: the echo throw 0..1
    Svf m_perfFilt[2];   ///< the master filter of the perform module, per channel
    std::vector<CueMark> m_cueMarks;   ///< the cue marks of what is loaded
    int m_tapOffset = 0;   ///< where in the taps and stems the current piece of the block goes
    // The stems: each deck's (Deck::render writes them), and their copies of the mixer channel's filters.
    std::vector<float> m_deckStems;                   ///< Decks x Deck::STEMS x 2 channels x Raster
    float* m_deckStemL[Decks][Deck::STEMS] = {};   ///< per deck and stem: its left channel in m_deckStems
    float* m_deckStemR[Decks][Deck::STEMS] = {};   ///< per deck and stem: its right channel in m_deckStems
    ThreeBand m_stemBands[Decks][Deck::STEMS][2];   ///< per deck, stem and channel: its copy of the isolator's split
    Svf m_stemFilt[Decks][Deck::STEMS][2];   ///< per deck, stem and channel: its copy of the channel's filter
    float m_chGains[4][Raster] = {};                 ///< a channel's isolator gains and fader, sample by sample
};

} // namespace parh
