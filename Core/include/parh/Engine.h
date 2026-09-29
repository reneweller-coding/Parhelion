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
    enum Stem : int { kStemKick = 0, kStemSub, kStemBass, kStemAcid, kStemHats, kStemPerc, kStemLead, kStemCounter, kStemPluck,
                      kStemArp, kStemPad, kStemStab, kStemRoom, kStemPlate, kStemHall, kStemMixFx, kStems };
    static_assert(static_cast<int>(kStemMixFx) == static_cast<int>(Deck::kStems), "the stems are the deck's and the mixer's");
    /** @brief Name of stem @p s. */
    static const char* stemName(int s);
    /** @brief The raster the parameters and the automation are read on, in samples. */
    static constexpr int kRaster = Deck::kRaster;

    /** @brief The parameters; any thread may write them. */
    ParamStore& params() { return params_; }
    /** @brief The parameters, read-only. */
    const ParamStore& params() const { return params_; }
    /** @brief Sets the sample rate and the largest block; call before load(). */
    void prepare(double sampleRate, int maxBlock);
    /** @brief Plays a track on deck A (not from the audio thread) and returns to its start. */
    void load(const Score& score);
    /** @brief Plays a set on the three decks (not from the audio thread) and returns to its start. */
    void loadSet(const SetScore& set);
    /**
     * @brief Renders @p n samples into @p L and @p R (overwritten) and advances the position.
     * @return false once the position has passed the end (the tails still ring)
     */
    bool process(float* L, float* R, int n);
    /** @brief Jumps to @p beat: every voice falls silent, the next note and curve are found again. */
    void seek(double beat);
    /** @brief The score deck A plays (the track). */
    const Score& score() const { return decks_[0].score(); }
    /** @brief Current position in seconds. */
    double seconds() const { return static_cast<double>(sample_) / sampleRate_; }
    /** @brief Current position in beats. */
    double beat() const { return tempo_.beatAt(seconds()); }
    /** @brief Length of the track or set in seconds. */
    double lengthSeconds() const { return tempo_.secondsAt(lengthBeats_); }
    /** @brief Sample rate set by prepare(). */
    double sampleRate() const { return sampleRate_; }
    /** @brief The value of parameter @p id as deck A plays it: the knob plus its score's automation. */
    float played(int id) const { return decks_[0].played(id); }
    /**
     * @brief Stems: from now on every process() call also writes the stems (Stem) into @p left[s] and @p right[s], each
     *        at least the block long. Null switches them off. Set them before the first sample: each stem runs through
     *        copies of the filters it passes, which start from silence.
     */
    void setStems(float* const* left, float* const* right) { stemL_ = left; stemR_ = right; }
    /** @brief The mix as it enters the master into @p left and @p right on every process() call (the stems' sum; for the
     *         tests). Null switches it off. */
    void setPremasterTap(float* left, float* right) { preL_ = left; preR_ = right; }
    /**
     * @brief Live play (the plugin; before load): the mixer is in the path of a single track too, and the perform module
     *        acts -- the mutes, the master filter, the echo throw into the mixer's echo. Off (renders, exports, the
     *        default): the score plays as it was composed, and the stems sum to the mix.
     */
    void setLive(bool on);
    /** @brief How much the engine does: all of it (the desktop, the default), or the Quest's share (PLAN 10: fewer unison
     *         oscillators and voices, Deck::setQuest). */
    enum class Quality { Desktop, Quest };
    /** @brief Sets the quality (before load; any time is safe, the cloud's tail is cut). */
    void setQuality(Quality q) { for (Deck& d : decks_) d.setQuest(q == Quality::Quest); }
    /**
     * @brief Counts the times the engine wrote a track's knob settings onto the knobs (Score::knobs): a track began on
     *        deck A or B, or a jump landed in another. The knobs then show that track's sounds and mix -- its presets,
     *        its style's settings -- and every deck plays from its own track's values, moved by as much as a hand turns
     *        a knob away from what was written on it. The plugin tells the host when the count changes.
     */
    uint32_t soundsVersion() const { return soundsVersion_.load(std::memory_order_relaxed); }
    /** @brief The deck whose track's knob settings the knobs show, -1 before any. */
    int leadDeck() const { return lead_.load(std::memory_order_relaxed); }
    /** @brief The cue marks of what is loaded (Cue.h: blocks, operations, keys, a set's tracks), for CueTap::scan(). */
    const std::vector<CueMark>& cueMarks() const { return cueMarks_; }
    /** @brief Deck taps: from now on every process() call also writes each deck after its mixer channel (before the
     *         effects and the master) into @p left[d] and @p right[d]. Null switches them off. */
    void setDeckTaps(float* const* left, float* const* right) { tapL_ = left; tapR_ = right; }
    /** @brief The loudness corrections of deck A's tracks (a trim per LevelMark), found after they began; they glide in. */
    void setLevelTrims(const std::vector<float>& trims) { decks_[0].setLevelTrims(trims); }
    /** @brief The same for deck @p d. */
    void setLevelTrims(int d, const std::vector<float>& trims) { decks_[d].setLevelTrims(trims); }
    /** @brief Phase 18: the parts' corrections of deck @p d's tracks (one per LevelMark), found after they began. */
    void setLevelBalance(int d, const std::vector<BalanceDb>& bal) { decks_[d].setLevelBalance(bal); }
    /** @brief Phase 18: every deck keeps its parts' loudest samples from now on (Deck::watchPeaks), or stops. */
    void watchPeaks(bool on) { for (Deck& d : decks_) d.watchPeaks(on); }
    /** @brief Deck @p d (0 .. 2). */
    const Deck& deck(int d) const { return decks_[d]; }
    /** @brief The kick of deck A (for the tests). */
    const Kick& kick() const { return decks_[0].kick(); }
    /** @brief The isolator's low band gain of deck @p d at the current sample, 0..1 (for the tests). */
    float lowGain(int d) const { return ch_[d].g[0]; }
    /** @brief Whether deck @p d plays at the current sample (for the tests). */
    bool deckPlays(int d) const { return playing_[d]; }

private:
    /** @brief A three-band Linkwitz-Riley split at 200 Hz and 2.5 kHz (the low band through the upper crossover's allpass). */
    struct ThreeBand {
        Svf split1, low2, high2, split2, mid2, top2, ap;
        void set(float fs);
        void reset();
        void process(float x, float& low, float& mid, float& top);
    };
    /** @brief A mixer channel. */
    struct Channel {
        float target[3] = { 1.0f, 1.0f, 1.0f };   ///< the isolator's gains to go to
        float g[3] = { 1.0f, 1.0f, 1.0f };        ///< where they are (smoothed over a millisecond)
        float fader = 1.0f, gFader = 1.0f;
        float send = 0.0f;
        float filter = 0.0f;
        ThreeBand bands[2];
        Svf filt[2];
    };
    /** @brief The set's own automation on one knob (the mixer's effects), with a cursor. */
    struct Track {
        int param;
        std::vector<Gesture> gestures;
        size_t cursor = 0;
        float offset = 0.0f;
    };

    void updateCell();
    void mix(float* L, float* R, int n);
    float setPlayed(int id) const;

    ParamStore params_;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    int64_t sample_ = 0, endSample_ = 0;
    TempoMap tempo_;
    double lengthBeats_ = 0.0;
    bool cellDirty_ = true;
    bool isSet_ = false;           ///< a set: the isolators and filters are in the path
    bool fxOn_ = false;            ///< the set sends into the mixer's effects
    Deck decks_[kDecks];
    bool playing_[kDecks] = {};
    std::vector<int64_t> steps_;   ///< where the decks' mixer channels step, merged
    size_t stepCursor_ = 0;
    std::vector<Track> tracks_;
    std::vector<int> trackOf_;

    // The mixer.
    Channel ch_[kDecks];
    float smooth_ = 0.02f;         ///< the channel gains' one-pole coefficient (a millisecond)
    TapeEcho djEcho_;
    Reverb djHall_;
    float echoReturn_ = 0.0f, hallReturn_ = 0.0f;
    // The master.
    Svf sideHp1_, sideHp2_;
    float masterGain_ = 1.0f, clipDrive_ = 1.0f;
    bool cut_ = false;
    BandLimit cutLp_[2];
    Svf cutHp1_[2], cutHp2_[2];
    float cutEnv_ = 0.0f, cutAtt_ = 0.1f, cutRel_ = 0.001f;
    Oversampler4 clipOs_[2];
    TruePeakLimiter limiter_;

    std::vector<float> deckL_[kDecks], deckR_[kDecks], sendL_, sendR_, fxL_, fxR_;
    float* const* stemL_ = nullptr;
    float* const* stemR_ = nullptr;
    float* const* tapL_ = nullptr;
    float* const* tapR_ = nullptr;
    float* preL_ = nullptr;
    float* preR_ = nullptr;
    // The knob settings (soundsVersion): what the engine wrote on each knob (NaN: never), the deck they came from.
    std::vector<float> shown_;
    std::atomic<int> lead_{ -1 };
    double leadGroup_ = -1.0;
    std::atomic<uint32_t> soundsVersion_{ 0 };
    /** @brief Puts deck @p d's knob settings on the knobs. */
    void showKnobs(int d);
    // Live play (setLive).
    bool live_ = false;
    float perfFilter_ = 0.0f, perfThrow_ = 0.0f;
    Svf perfFilt_[2];
    std::vector<CueMark> cueMarks_;
    int tapOffset_ = 0;
    // The stems: each deck's (Deck::render writes them), and their copies of the mixer channel's filters.
    std::vector<float> deckStems_;                   ///< kDecks x Deck::kStems x 2 channels x kRaster
    float* deckStemL_[kDecks][Deck::kStems] = {};
    float* deckStemR_[kDecks][Deck::kStems] = {};
    ThreeBand stemBands_[kDecks][Deck::kStems][2];
    Svf stemFilt_[kDecks][Deck::kStems][2];
    float chGains_[4][kRaster] = {};                 ///< a channel's isolator gains and fader, sample by sample
};

} // namespace parh
