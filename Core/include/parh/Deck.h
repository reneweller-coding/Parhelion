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
#include <cstdint>
#include <limits>
#include <vector>

namespace parh {

/** @brief One deck. */
class Deck {
public:
    /** @brief The stems, in the order Engine::setStems() takes them. */
    enum Stem : int { kStemKick = 0, kStemSub, kStemBass, kStemAcid, kStemHats, kStemPerc, kStemLead, kStemCounter, kStemPluck,
                      kStemArp, kStemPad, kStemStab, kStemPiano, kStemStrings, kStemChoir, kStemBrass, kStemTimpani, kStemRoom,
                      kStemPlate, kStemHall, kStemCloud, kStemFx, kStems };
    static constexpr int kRaster = 32;   ///< the engine's parameter raster, samples
    static constexpr int64_t kNever = std::numeric_limits<int64_t>::max();

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
    struct Sends { float room = 0.0f, plate = 0.0f, hall = 0.0f; };

    void dispatch(const Ev& e);

    const ParamStore* params_ = nullptr;
    int index_ = 0;
    double sampleRate_ = 48000.0;
    bool loaded_ = false;
    bool live_ = false;
    std::vector<float> base_;
    size_t knobCursor_ = 0;
    double knobGroup_ = -1.0;
    bool newGroup_ = false;
    const float* shown_ = nullptr;
    /** @brief Takes the knob settings due up to @p beat (a group's replaces the last group's). */
    void applyKnobs(double beat);
    uint32_t mutes_ = 0;   ///< the performer's muted groups (perform::MuteKick ..), bit k for group k
    bool muted(int param) const { return ((mutes_ >> (param - perform::MuteKick)) & 1u) != 0; }
    Score score_;
    std::vector<Ev> events_;
    size_t evCursor_ = 0;
    std::vector<Track> tracks_;
    std::vector<int> trackOf_;   ///< parameter id -> index into tracks_, or -1
    std::vector<int64_t> mixerSteps_;
    std::vector<std::pair<int64_t, int64_t>> plays_;   ///< where it plays: [from, to) in samples, sorted

    Kick kick_;
    SubBass sub_;
    PercKit kit_;
    MonoSynth bass_, acid_;
    Poly poly_[kPolyInstances];
    TranceGate gate_[kPolyInstances];
    Ducker polyDuck_[kPolyInstances], retDuck_;
    Piano piano_;              ///< the physical piano (PLAN 5.8)
    Ducker pianoDuck_;
    Sends pianoSends_;
    // The orchestra (PLAN 5.9): four instruments, each with its duck and sends, in BalPart order from Strings.
    StringSection strings_;
    Choir choir_;
    Brass brass_;
    Timpani timpani_;
    static constexpr int kOrch = 4;
    Ducker orchDuck_[kOrch];
    Sends orchSends_[kOrch];
    GrainCloud cloud_;         ///< the granular cloud (Deep)
    float cloudPad_ = 0.8f, cloudKeys_ = 0.5f, cloudPlate_ = 0.5f;
    Sfx sfx_;                  ///< the effects (PLAN 5.10)
    Ducker fxDuck_, subDropDuck_;   ///< the effects' pump and the sub drop's own (sfx.sub_duck)
    Sends fxSends_;
    int keyRoot_ = 9, scale_ = 0;
    double beat_ = 0.0, beatsPerSample_ = 0.0;   ///< the beat at the cell's first sample and its step (the gates, the poly clock)
    int64_t cellSample_ = 0;                     ///< the cell's first sample: a span that starts inside a cell counts from it
    // The last kick, for the sub's lock.
    bool haveKick_ = false;
    double kickTime_ = 0.0;   ///< its ideal start, in samples
    double kickC_ = 0.0, kickF0_ = 50.0;
    int subNote_ = -1, bassNote_ = -1, acidNote_ = -1;   ///< the ids of the notes sounding

    // The voices' strips.
    struct PolyStrip {
        bool gate = false;
        int pattern = 0;
        float depth = 0.85f, duty = 0.5f, attackBeats = 0.0f, releaseBeats = 0.0f, tone = 0.4f;
        Sends sends;
    };
    PolyStrip strip_[kPolyInstances];
    float lastDuck_[kPolyInstances] = {};
    Sends bassSends_, acidSends_;

    // Buses, sends, rooms.
    Svf hatsLp_[2], percLp_[2];
    float hatsGain_ = 1.0f, percGain_ = 1.0f, synthGain_ = 1.0f, synthTarget_ = 1.0f, fxGain_ = 1.0f, fxTarget_ = 1.0f;
    std::vector<std::pair<double, double>> breaks_;   ///< the beats of the breakdowns and breaks (LevelMark::breakDb)
    std::vector<std::pair<double, double>> builds_;   ///< the beats of the builds (LevelMark::buildDb)
    float percBuild_ = 1.0f;                          ///< the build's correction on the percussion bus
    bool snapFaders_ = true;   ///< after a seek the faders start where the score has them, not gliding there
    bool laneIsHat_[kPercLanes] = {};
    float hatsSend_ = 0.0f, percSend_ = 0.0f;
    Reverb room_, hall_;
    Plate plate_;
    float roomReturn_ = 1.0f, plateReturn_ = 1.0f, hallReturn_ = 1.0f;
    float drumSat_ = 0.2f;
    // The track bus.
    Svf groupHp_[2][2];              ///< the group high pass (mix.low_cut), fourth order, per channel
    bool groupHpOn_ = false;
    float tiltHigh_ = 1.0f, tiltCoef_ = 0.1f, tiltState_[2] = {};
    BusCompressor glue_;
    float trimGain_ = 1.0f, trimTarget_ = 1.0f, trimCoef_ = 0.0f;
    std::vector<float> lateTrims_;
    float balGain_[kBalParts] = {};
    float balTarget_[kBalParts] = {};
    bool watch_ = false;
    float kickPeak_ = 0.0f, partPeak_[kBalParts] = {};
    std::vector<BalanceDb> lateBal_;

    std::vector<float> kickBuf_, bodyBuf_, subBuf_, bassL_, bassR_, acidL_, acidR_;
    std::vector<float> polyL_[kPolyInstances], polyR_[kPolyInstances], pianoL_, pianoR_;
    std::vector<float> orchL_[kOrch], orchR_[kOrch];
    std::vector<float> cloudInL_, cloudInR_, cloudL_, cloudR_;
    std::vector<float> roomInL_, roomInR_, plateInL_, plateInR_, hallInL_, hallInR_, roomL_, roomR_, plateL_, plateR_, hallL_, hallR_;
    std::vector<float> drumL_, drumR_, synthL_, synthR_, fxL_, fxR_, fxSub_, fxWetL_, fxWetR_;

    // The stems' own copies of the linear stages (render() with stems), and the gains of the nonlinear ones.
    struct StemBus { Svf hp[2][2]; float tilt[2] = {}; };
    StemBus stemBus_[kStems];
    float satGain_[2][kRaster] = {}, glueGains_[kRaster] = {}, trimGains_[kRaster] = {};
    void resetStems();
};

} // namespace parh
