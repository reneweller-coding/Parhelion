/**
 * @file Deck.h
 * @brief A deck (PLAN 3): the whole instrument for the tracks of one deck -- every voice, the buses, the sends and rooms,
 *        the track bus -- playing its own score with its own automation. The engine holds three and mixes them.
 *
 * **The low end** (PLAN 5.1, 5.2, Dok. 7). The kick owns the downbeat and 45 to 65 Hz; the sub sine starts in the kick's
 * phase where its lock is on; the mid-bass is high-passed so that its fundamental sits above the sub (bass.low_cut, at
 * least kBassMinCut). Under 150 Hz nothing else plays: every polyphonic voice has its high pass (poly.hp_floor), every
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
    static constexpr int kStrips = 22;   ///< Deck::kStems
    std::atomic<float> peak[kStrips] = {};   ///< per strip: the loudest sample since the page last took them
    std::atomic<double> sum[kStrips] = {};   ///< per strip: the sum of squares since the page last took them
    /** @brief Adds one block's readings (audio thread). */
    void add(const float* pk, const double* ss)
    {
        for (int s = 0; s < kStrips; ++s) {
            float was = peak[s].load(std::memory_order_relaxed);
            while (pk[s] > was && !peak[s].compare_exchange_weak(was, pk[s], std::memory_order_relaxed)) {}
            double w = sum[s].load(std::memory_order_relaxed);
            while (!sum[s].compare_exchange_weak(w, w + ss[s], std::memory_order_relaxed)) {}
        }
    }
};

/** @brief One deck. */
class Deck {
public:
    /** @brief The stems, in the order Engine::setStems() takes them. */
    enum Stem : int { kStemKick = 0, kStemSub, kStemBass, kStemAcid, kStemHats, kStemPerc, kStemLead, kStemCounter, kStemPluck,
                      kStemArp, kStemPad, kStemStab, kStemPiano, kStemStrings, kStemChoir, kStemBrass, kStemTimpani, kStemRoom,
                      kStemPlate, kStemHall, kStemCloud, kStemFx, kStems };
    static_assert(static_cast<int>(kStems) == MeterSink::kStrips, "a strip per stem");
    static constexpr int kRaster = 32;   ///< the engine's parameter raster, samples
    static constexpr int64_t kNever = std::numeric_limits<int64_t>::max();   ///< no such sample: nothing is due

    /** @brief Sets the knobs it reads, the rate and which deck it is (0 .. 2). */
    void prepare(const ParamStore* params, double sampleRate, int index);
    /** @brief Plays @p score from now on (allocates). */
    void load(const Score& score);
    /** @brief Plays nothing. */
    void clear();
    /** @brief Silence; the next note and curve at @p sample found again (a jump to 0 plays what is due at it). */
    void seek(int64_t sample);
    /** @brief Reads the knobs and the automation at @p sample. */
    void updateCell(int64_t sample);
    /** @brief Plays every note due at or before @p sample. */
    void dispatchUntil(int64_t sample);
    /** @brief The sample of the next note not yet played (kNever when none). */
    int64_t nextEvent() const { return evCursor_ < events_.size() ? events_[evCursor_].sample : kNever; }
    /** @brief Whether it plays at @p sample (else it rests). */
    bool playsAt(int64_t sample) const;
    /** @brief The first sample after @p sample where it starts or stops resting (kNever when none). */
    int64_t nextRestChange(int64_t sample) const;
    /**
     * @brief Renders @p n samples from @p sample into @p L and @p R (replaced): at most up to the next raster line and the
     *        next note. With @p stemL / @p stemR, also writes its stems there (replaced, from index 0): each through its
     *        own copy of the linear stages (group high pass, tilt) and times the gains the nonlinear ones gave the whole
     *        (the drum bus, the glue) and the trim, so they sum to L and R.
     */
    void render(int64_t sample, float* L, float* R, int n, float* const* stemL, float* const* stemR);
    /** @brief Live play (Engine::setLive): the performer's mutes act. */
    void setLive(bool on) { live_ = on; }
    /**
     * @brief A key of a MIDI keyboard (01.10.2026, Engine::queueLive), played at @p sample on this deck with its sound as
     *        its knobs have it -- past the mutes, which are the composer's. @p target is a perform::keys value (Kit ..
     *        Brass); @p on false releases the key. A mono voice (the bass, the 303) slides to a key pressed while another
     *        is held, and its release ends the note only when it is the sounding key; a polyphonic voice holds a key
     *        until its release.
     */
    void liveNote(int64_t sample, int target, int pitch, float velocity, bool on);
    /** @brief Releases every played key (a stop, another keyboard target), and forgets which parts were played. */
    void liveAllOff();
    /** @brief From now on every composer note it plays is also written to @p tap (null: stops; NoteTap.h, MIDI out). */
    void setNoteTap(NoteTap* tap) { noteTap_ = tap; }
    /** @brief Where the engine keeps what it wrote on the knobs (Engine.h; NaN: never), read by played(). */
    void setShown(const float* shown) { shown_ = shown; }
    /** @brief The beat of the knob settings this deck plays from (Score::knobs), -1 before any. */
    double knobGroup() const { return knobGroup_; }
    /** @brief Whether a new group of knob settings was taken since the last call (and forgets it). */
    bool takeNewGroup() { const bool n = newGroup_; newGroup_ = false; return n; }
    /** @brief The value this deck plays parameter @p id from, NaN where it follows the knob. */
    float baseOf(int id) const { return id >= 0 && static_cast<size_t>(id) < base_.size() ? base_[static_cast<size_t>(id)] : std::numeric_limits<float>::quiet_NaN(); }
    /** @brief The Quest's quality (Engine::setQuality): fewer unison oscillators and voices (Poly.h). */
    void setQuest(bool on);
    /** @brief The score it plays. */
    const Score& score() const { return score_; }
    /** @brief Whether it has a score. */
    bool loaded() const { return loaded_; }
    /** @brief From now on adds the strips' levels into @p sink (null: stops). */
    void setMeterSink(MeterSink* sink) { meter_ = sink; }
    /** @brief The value of parameter @p id as this deck plays it: the knob plus its score's automation. */
    float played(int id) const;
    /** @brief Reads a module instance as played. */
    void readPlayed(Module m, int instance, float* out) const;
    /** @brief The samples of its score's steps on the DJ mixer's knobs (a kill, a swap): the engine reads them there. */
    const std::vector<int64_t>& mixerSteps() const { return mixerSteps_; }
    /** @brief The loudness corrections of its tracks (a trim per LevelMark), found after they began; they glide in. */
    void setLevelTrims(const std::vector<float>& trims) { lateTrims_.assign(trims.begin(), trims.end()); }
    /** @brief The parts' corrections of its tracks (one per LevelMark), found after they began; as the trims. */
    void setLevelBalance(const std::vector<BalanceDb>& bal) { lateBal_.assign(bal.begin(), bal.end()); }
    /** @brief From now on keeps the loudest sample of the kick and of every part (the Leveler's reading). */
    void watchPeaks(bool on);
    /** @brief The kick's loudest sample since watchPeaks(true). */
    float kickPeak() const { return kickPeak_; }
    /** @brief Part @p p's (BalPart, a lane 0 .. 11 first) loudest sample since watchPeaks(true). */
    float partPeak(int p) const { return partPeak_[p]; }
    /** @brief The kick (for the tests). */
    const Kick& kick() const { return kick_; }
    /** @brief The piano (for the tests). */
    const Piano& piano() const { return piano_; }
    /** @brief Polyphonic voice @p i (for the tests). */
    const Poly& poly(int i) const { return poly_[i]; }
    /** @brief The gain the pump gives polyphonic voice @p i at the last sample rendered (for the tests). */
    float polyDuckGain(int i) const { return lastDuck_[i]; }

private:
    /** @brief A note event on the sample grid. */
    struct Ev {
        int64_t sample;   ///< when
        uint8_t on;       ///< 0 off, 1 on (offs first at equal samples)
        uint8_t part;     ///< Part
        bool accent;      ///< the 303's accent, a poly voice's filter accent
        bool slide;       ///< slides into the next note
        int pitch;        ///< MIDI note
        float velocity;   ///< 0..1
        int shift;        ///< a kit hit's pitch shift
        double late;      ///< how many samples ago it ideally happened (0 <= late < 1)
        int id;           ///< pairs an off with its on
        double lengthBeats;   ///< the note's written length (a poly voice's dynamic detune)
        int gate;         ///< a poly voice's gate, samples
    };
    /** @brief The automation curves on one parameter, in time order, with a cursor. */
    struct Track {
        int param;                      ///< the knob
        std::vector<Gesture> gestures;  ///< its curves
        size_t cursor = 0;              ///< index of the latest curve that has started, or gestures.size()
        float offset = 0.0f;            ///< the offset at the current cell
    };
    /** @brief A voice's sends. */
    struct Sends {
        float room = 0.0f;    ///< to the room
        float plate = 0.0f;   ///< to the plate
        float hall = 0.0f;    ///< to the hall
    };

    /** @brief Plays note event @p e: a voice started or released, past the mutes and the keyboard as they stand. */
    void dispatch(const Ev& e);

    const ParamStore* params_ = nullptr;   ///< the knobs it reads
    int index_ = 0;   ///< which deck it is (0 .. 2)
    double sampleRate_ = 48000.0;   ///< the sample rate, Hz
    bool loaded_ = false;   ///< it has a score
    bool live_ = false;   ///< live play (setLive): the performer's mutes and the keyboard act
    /// The track's absolute knob settings (Score::knobs): the value this deck plays each knob from, NaN where it plays the
    /// knob; a hand's turn of the knob away from what the engine wrote on it (shown_) moves it from there.
    std::vector<float> base_;
    size_t knobCursor_ = 0;   ///< index of the next knob setting of the score not yet taken
    double knobGroup_ = -1.0;   ///< the beat of the knob settings it plays from, -1 before any
    bool newGroup_ = false;   ///< a new group of knob settings was taken since takeNewGroup()
    const float* shown_ = nullptr;   ///< what the engine wrote on the knobs (setShown; NaN never), or null
    /** @brief Takes the knob settings due up to @p beat (a group's replaces the last group's); whether it took any. */
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
    std::vector<float> cellValue_;      ///< every parameter as played at the last cell
    std::vector<int> firstOf_;          ///< parameter id -> the first id of its module instance
    std::vector<uint8_t> instChanged_;  ///< by a module instance's first id: its values changed since it was last read
    std::vector<int> staleIds_;         ///< the parameters whose curve moved at this cell
    uint32_t seenVersion_ = 0;          ///< the store's version() the cache was read at
    bool cellFresh_ = true;             ///< the next cell reads every parameter and hands every module over
    int cellKey_ = -1, cellScale_ = -1; ///< the key and the scale the modules were last handed over with
    double cellBpm_ = -1.0;             ///< and the tempo
    /** @brief Brings the cache to this cell: every parameter when @p all, else the stale ones. */
    void refreshCell(bool all);
    /** @brief Module instance @p m / @p instance as played at this cell into @p out; whether it changed since last read. */
    bool readCell(Module m, int instance, float* out);
    /** @} */
    uint32_t mutes_ = 0;   ///< the performer's muted groups (perform::MuteKick ..), bit k for group k
    /** @brief Whether a group is muted -- never for a played key (liveNote). */
    bool muted(int param) const { return !liveEvent_ && ((mutes_ >> (param - perform::MuteKick)) & 1u) != 0; }
    // The keyboard (01.10.2026, liveNote): read from the perform module in live play (updateCell).
    int keyTarget_ = 0;              ///< perform.keyboard_part (perform::keys)
    bool keyReplace_ = true;         ///< perform.keyboard_mode Replace: the played part's generated notes are left out
    bool composerOff_ = false;       ///< perform.composer off: no generated note at all
    uint32_t keyPlayed_ = 0;         ///< by channel: the targets played since the last liveAllOff (bit = target)
    bool liveEvent_ = false;         ///< dispatch() plays a key now: no mute, no silencing
    NoteTap* noteTap_ = nullptr;     ///< where the composer's played notes go for MIDI out (setNoteTap), or null
    /** @brief The perform mute (perform::MuteKick ..) that silences @p part's notes, -1 for none. */
    int muteOf(Part part) const;
    int liveHeld_[perform::keys::Count] = {};   ///< a mono target's sounding key + 1 (0 none)
    uint8_t liveKey_[128] = {};      ///< a polyphonic target's held keys: its target + 1 (0 none) -- for liveAllOff
    /** @brief Whether the composer's note-ons of @p part are left out (the composer off, or the keyboard replaces it). */
    bool silenced(Part part) const;
    /** @brief The keyboard target a part belongs to (perform::keys; Off for none). */
    static int targetOf(Part part);
    Score score_;   ///< the score it plays
    std::vector<Ev> events_;   ///< the score's notes on the sample grid, in time order
    size_t evCursor_ = 0;   ///< index of the next event not yet played
    std::vector<Track> tracks_;   ///< the automation, one track per parameter that has curves
    std::vector<int> trackOf_;   ///< parameter id -> index into tracks_, or -1
    std::vector<int64_t> mixerSteps_;   ///< the samples of its score's steps on the DJ mixer's knobs
    std::vector<std::pair<int64_t, int64_t>> plays_;   ///< where it plays: [from, to) in samples, sorted

    Kick kick_;   ///< the kick
    SubBass sub_;   ///< the sub bass sine
    PercKit kit_;   ///< the percussion: twelve lanes
    MonoSynth bass_;   ///< the bass line
    MonoSynth acid_;   ///< the 303
    Poly poly_[kPolyInstances];   ///< the polyphonic voices: lead, counter, pluck, arp, pad, stab
    TranceGate gate_[kPolyInstances];   ///< per voice: its trance gate
    Ducker polyDuck_[kPolyInstances];   ///< per voice: its pump
    Ducker retDuck_;   ///< the returns' pump
    Piano piano_;              ///< the physical piano (PLAN 5.8)
    Ducker pianoDuck_;   ///< the piano's pump
    Sends pianoSends_;   ///< the piano's sends
    /// The orchestra (PLAN 5.9): four instruments, each with its duck and sends, in BalPart order from Strings.
    StringSection strings_;
    Choir choir_;   ///< the choir
    Brass brass_;   ///< the brass
    Timpani timpani_;   ///< the timpani
    static constexpr int kOrch = 4;   ///< the orchestra's instruments
    Ducker orchDuck_[kOrch];   ///< per instrument: its pump
    Sends orchSends_[kOrch];   ///< per instrument: its sends
    GrainCloud cloud_;         ///< the granular cloud (Deep)
    float cloudPad_ = 0.8f;   ///< the pad's send to the cloud
    float cloudKeys_ = 0.5f;   ///< the pluck's and the piano's send to the cloud
    float cloudPlate_ = 0.5f;   ///< the cloud's send to the plate
    Sfx sfx_;                  ///< the effects (PLAN 5.10)
    Ducker fxDuck_;   ///< the effects' pump
    Ducker subDropDuck_;   ///< the sub drop's own pump (sfx.sub_duck)
    Sends fxSends_;   ///< the effects' sends
    int keyRoot_ = 9;   ///< the key's root, 0 = C (compose.key)
    int scale_ = 0;   ///< the scale (compose.scale)
    double beat_ = 0.0;   ///< the beat at the cell's first sample
    double beatsPerSample_ = 0.0;   ///< its step: beats per sample (the gates, the poly clock)
    int64_t cellSample_ = 0;                     ///< the cell's first sample: a span that starts inside a cell counts from it
    /// The last kick, for the sub's lock.
    bool haveKick_ = false;
    double kickTime_ = 0.0;   ///< its ideal start, in samples
    double kickC_ = 0.0;   ///< the last kick's asymptotic phase, cycles: the sub's lock
    double kickF0_ = 50.0;   ///< the last kick's tuned end frequency, Hz: the sub's lock
    int subNote_ = -1;   ///< the id of the sub's sounding note, -1 none
    int bassNote_ = -1;   ///< the id of the bass's sounding note, -1 none
    int acidNote_ = -1;   ///< the id of the 303's sounding note, -1 none

    /** @brief A voice's strip: its gate and its sends. */
    struct PolyStrip {
        bool gate = false;   ///< the gate is in
        int pattern = 0;   ///< the gate's pattern (TranceGate::open)
        float depth = 0.85f;   ///< the gate's depth
        float duty = 0.5f;   ///< the gate's duty cycle
        float attackBeats = 0.0f;   ///< the gate's attack, beats
        float releaseBeats = 0.0f;   ///< the gate's release, beats
        float tone = 0.4f;   ///< the gate's tone: how much of the top it takes along
        Sends sends;   ///< the voice's sends
    };
    PolyStrip strip_[kPolyInstances];   ///< per voice: its strip
    float lastDuck_[kPolyInstances] = {};   ///< per voice: its pump's last gain
    Sends bassSends_;   ///< the bass's sends
    Sends acidSends_;   ///< the 303's sends

    // Buses, sends, rooms.
    Svf hatsLp_[2];   ///< the hats bus's low pass, per channel
    Svf percLp_[2];   ///< the percussion bus's low pass, per channel
    float hatsGain_ = 1.0f;   ///< the hats bus's level (mix.hats_level, with its motion)
    float percGain_ = 1.0f;   ///< the percussion bus's level
    float synthGain_ = 1.0f;   ///< the synths' fader as it stands (mix.synth_level, the breaks)
    float synthTarget_ = 1.0f;   ///< where it glides to
    float fxGain_ = 1.0f;   ///< the effects' fader as it stands (the breaks)
    float fxTarget_ = 1.0f;   ///< where it glides to
    std::vector<std::pair<double, double>> breaks_;   ///< the beats of the breakdowns and breaks (LevelMark::breakDb)
    std::vector<std::pair<double, double>> builds_;   ///< the beats of the builds (LevelMark::buildDb)
    float percBuild_ = 1.0f;                          ///< the build's correction on the percussion bus
    bool snapFaders_ = true;   ///< after a seek the faders start where the score has them, not gliding there
    bool laneIsHat_[kPercLanes] = {};   ///< per kit lane: it goes to the hats bus (a hat), else to the percussion bus
    float hatsSend_ = 0.0f;   ///< the hats bus's room send
    float percSend_ = 0.0f;   ///< the percussion bus's room send
    Reverb room_;   ///< the room (space)
    Reverb hall_;   ///< the hall
    Plate plate_;   ///< the plate
    float roomReturn_ = 1.0f;   ///< the room's return level
    float plateReturn_ = 1.0f;   ///< the plate's return level
    float hallReturn_ = 1.0f;   ///< the hall's return level
    float drumSat_ = 0.2f;   ///< the drum bus's saturation, 0 .. 1 (mix.drum_sat)
    // The track bus.
    Svf groupHp_[2][2];              ///< the group high pass (mix.low_cut), fourth order, per channel
    bool groupHpOn_ = false;   ///< the group high pass is in (mix.low_cut above 20.5 Hz)
    float tiltHigh_ = 1.0f;   ///< the master tilt's gain on the highs (master.tilt)
    float tiltCoef_ = 0.1f;   ///< the tilt's one-pole coefficient (1 kHz)
    float tiltState_[2] = {};   ///< the tilt's low pass, per channel
    BusCompressor glue_;   ///< the bus compressor on the track
    float trimGain_ = 1.0f;   ///< the loudness trim as it stands, gliding to trimTarget_
    float trimTarget_ = 1.0f;   ///< the trim of the track that plays now
    float trimCoef_ = 0.0f;   ///< the trim's glide (about a second)
    std::vector<float> lateTrims_;   ///< setLevelTrims: the corrections found while playing
    float balGain_[kBalParts] = {};   ///< (1 from prepare() on)
    float balTarget_[kBalParts] = {};   ///< the parts' gains of the track that plays now
    bool watch_ = false;   ///< the loudest samples are kept (watchPeaks)
    MeterSink* meter_ = nullptr;     ///< setMeterSink
    float kickPeak_ = 0.0f;   ///< the kick's loudest sample since watchPeaks(true)
    float partPeak_[kBalParts] = {};   ///< every part's loudest sample since watchPeaks(true)
    std::vector<BalanceDb> lateBal_;   ///< setLevelBalance: the parts' corrections found while playing

    std::vector<float> kickBuf_;   ///< the kick's click and body, mono
    std::vector<float> bodyBuf_;   ///< the kick's body alone, mono
    std::vector<float> subBuf_;   ///< the sub bass, mono
    std::vector<float> bassL_;   ///< the bass, left
    std::vector<float> bassR_;   ///< the bass, right
    std::vector<float> acidL_;   ///< the 303, left
    std::vector<float> acidR_;   ///< the 303, right
    std::vector<float> polyL_[kPolyInstances];   ///< per voice: its block, left
    std::vector<float> polyR_[kPolyInstances];   ///< per voice: its block, right
    std::vector<float> pianoL_;   ///< the piano, left
    std::vector<float> pianoR_;   ///< the piano, right
    std::vector<float> orchL_[kOrch];   ///< per instrument: its block, left
    std::vector<float> orchR_[kOrch];   ///< per instrument: its block, right
    std::vector<float> cloudInL_;   ///< the cloud's input, left
    std::vector<float> cloudInR_;   ///< the cloud's input, right
    std::vector<float> cloudL_;   ///< the cloud's return, left
    std::vector<float> cloudR_;   ///< the cloud's return, right
    std::vector<float> roomInL_;   ///< the room's input, left
    std::vector<float> roomInR_;   ///< the room's input, right
    std::vector<float> plateInL_;   ///< the plate's input, left
    std::vector<float> plateInR_;   ///< the plate's input, right
    std::vector<float> hallInL_;   ///< the hall's input, left
    std::vector<float> hallInR_;   ///< the hall's input, right
    std::vector<float> roomL_;   ///< the room's return, left
    std::vector<float> roomR_;   ///< the room's return, right
    std::vector<float> plateL_;   ///< the plate's return, left
    std::vector<float> plateR_;   ///< the plate's return, right
    std::vector<float> hallL_;   ///< the hall's return, left
    std::vector<float> hallR_;   ///< the hall's return, right
    std::vector<float> drumL_;   ///< the drum bus after its saturation, left
    std::vector<float> drumR_;   ///< the drum bus after its saturation, right
    std::vector<float> synthL_;   ///< the synths summed, left
    std::vector<float> synthR_;   ///< the synths summed, right
    std::vector<float> fxL_;   ///< the effects, left
    std::vector<float> fxR_;   ///< the effects, right
    std::vector<float> fxSub_;   ///< the effects' sub layer, mono (the sub drop)
    std::vector<float> fxWetL_;   ///< the effects' share for the reverb, left
    std::vector<float> fxWetR_;   ///< the effects' share for the reverb, right

    // The stems' own copies of the linear stages (render() with stems), and the gains of the nonlinear ones.
    /** @brief A stem's own copy of the track bus's linear stages. */
    struct StemBus {
        Svf hp[2][2];          ///< the group high pass: two stages per channel
        float tilt[2] = {};    ///< the tilt's low pass, per channel
    };
    StemBus stemBus_[kStems];   ///< per stem: its copy of the group high pass and the tilt
    float satGain_[2][kRaster] = {};   ///< per channel and sample of a raster cell: the drum bus saturation's gain
    float glueGains_[kRaster] = {};   ///< the glue's gain per sample of a cell
    float trimGains_[kRaster] = {};   ///< the trim per sample of a cell
    /** @brief Clears the stems' copies of the linear stages (a seek, a load). */
    void resetStems();
};

} // namespace parh
