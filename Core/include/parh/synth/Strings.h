/**
 * @file Strings.h
 * @brief The orchestra's string section (PLAN 5.9, Cinematic): bowed strings, every player his own string and bow,
 *        radiated through the bodies of violins, violas, cellos and basses.
 *
 * **The string** of a player is modal, as the piano's (Piano.h): its partials k f0 sqrt(1 + B k^2) as complex one-poles
 * driven by one real force at the bow point, their losses b1 + b3 w^2 (the fingers, the air, the wood). The bow point's
 * velocity is a real part of the states, the bridge force another (Demoucron, "On the control of virtual violins",
 * thesis, IRCAM/KTH 2008: a modal string under a bow in real time).
 *
 * **The bow** (Smith 1986; Woodhouse 2004): friction mu(dv) = mu_d + (mu_s - mu_d) v0 / (v0 + |dv|) between the bow
 * hair and the string, at a bow speed and a bow force. Each sample the modes run free first; their velocity at the bow
 * point v_h and the string's instantaneous admittance there Y (the modes' response within the sample) say what force
 * the bow needs to hold the string: F = (v_bow - v_h) / Y. If static friction can give it (|F| <= mu_s F_bow), the
 * string sticks; else it slips, and the slip speed u solves u^2 + (v0 - |c| + Y F_bow mu_d) u + v0 (Y F_bow mu_s - |c|)
 * = 0 (c = v_h - v_bow), whose one positive root exists exactly when sticking is impossible -- a closed form, no
 * iteration, no ambiguity between intersections. That is the Helmholtz motion's stick and slip, the corner travelling
 * the string, the saw-like bridge force, and what goes wrong at too little or too much force (Schelleng's limits): the
 * bow force follows the bow speed inside them (half the upper limit, 2 Z0 v / (beta (mu_s - mu_d)); a third left the
 * cellos' low notes in a surface sound with missing partials, measured with parh_orchprobe).
 *
 * **The player**: a speed and a force that rise at the note's start (a legato stroke, or a short bite for staccato) and
 * lift at its end, rosin noise on the force, vibrato (5 to 6.5 Hz, 10 to 25 cents, starting after a fifth of a second),
 * an intonation and an onset of his own. **The section**: six players on a note (three on the Quest), up to six notes;
 * a note goes to the family whose register it lies in (basses under E2, cellos under G3, violas under D4, violins
 * above), on the highest open string under it, stopped to its pitch.
 *
 * **The bodies**: per family a bank of resonators after the measured signature modes (violin: A0 275 Hz, CBR 405 Hz,
 * B1- 460 Hz, B1+ 530 Hz; the others scaled by size, Bissinger 2008) and 44 statistical modes above with the bridge
 * hill (about 2.5 kHz on a violin), plus the broadband share the top plate radiates between them; fed by the players'
 * bridge forces, left and right by where they sit, each family levelled to the violins (a bass's bridge force is some
 * twenty times a violin's).
 *
 * **Lanes** (Vec.h): the players run eight side by side (every lane its own string and bow, the friction solved with
 * selects), bit-identical to the scalar path; the envelopes, the noise and the bodies are scalar.
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Vec.h"
#include "parh/synth/Modulation.h"
#include <cstdint>

namespace parh {

constexpr int kStringNotes = 6;                                         ///< notes at once
constexpr int kStringPlayers = 6;                                       ///< players on a note at most
constexpr int kStringLanes = paddedLanes(kStringNotes * kStringPlayers);   ///< 40
constexpr int kStringModes = 24;                                        ///< partials of a string
constexpr int kStringFamilies = 4;                                      ///< violins, violas, cellos, basses
constexpr int kBodyModes = 24;                                          ///< resonators of a body

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)   // padded to its 32-byte lanes on purpose (the bodies)
#endif
/** @brief The string section: bowed modal strings, a player each, through the four families' bodies. */
class StringSection {
public:
    static constexpr int kMaxBlock = 32;   ///< samples per decision cell (vibrato, envelopes' targets)

    /** @brief Rate and seed (the players' intonation, vibrato and onsets, the bodies' high modes). */
    void prepare(double sampleRate, uint64_t seed);
    /** @brief The knobs (strings:: parameters). */
    void update(const float* v);
    /** @brief Players take up @p pitch at @p velocity; @p staccato: a short bitten stroke. */
    void noteOn(int pitch, float velocity, bool staccato, double late);
    /** @brief The players on @p pitch lift their bows. */
    void noteOff(int pitch);
    /** @brief Silence. */
    void reset();
    /** @brief Players on a note (1 .. kStringPlayers, at most the quality's limit): the knob, through update(). */
    void setPlayers(int n) { players_ = n < 1 ? 1 : (n > playerLimit_ ? playerLimit_ : n); }
    /**
     * @brief The quality's limit on the players (Deck::setQuest; the Quest's three). Until 29.09.2026 the Quest set the
     *        players themselves, and the knob's value from update() overwrote them at the next cell: the Quest played
     *        all six. It takes effect with the next update().
     */
    void setPlayerLimit(int n) { playerLimit_ = n < 1 ? 1 : (n > kStringPlayers ? kStringPlayers : n); }
    /** @brief Renders @p n samples, replacing @p L and @p R. */
    void process(float* L, float* R, int n);
    /** @brief With a chosen lane type (float: the scalar reference), for the vector tests. */
    template <class V> void processWith(float* L, float* R, int n);
    /** @brief Lanes sounding. */
    int activeLanes() const;
    /** @brief The family a pitch is played by (0 violins .. 3 basses). */
    static int familyOf(int pitch);
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { mod_.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { mod_.setClock(pos_, beat, beatsPerSample); }

private:
    /** @brief A lane's bow point moved to @p beta: its modes' response there and its admittance. */
    void placeBow(int lane, double beta);
    /** @brief @p n samples of every lane's string and bow within one cell (see the file comment). */
    template <class V> void bowSegment(int n);
    /** @brief Sets lane @p lane to play @p pitch at @p velocity as player @p player (@p staccato: a bitten stroke). */
    void startLane(int lane, int pitch, float velocity, bool staccato, int player);
    /** @brief The decisions of a cell: the envelopes' targets, the vibrato, the modulation, the lanes that end. */
    void cellUpdate();

    double sr_ = 48000.0;   ///< the sample rate, Hz
    uint64_t seed_ = 1;   ///< the players' streams' seed
    int players_ = kStringPlayers;   ///< players on a note
    int playerLimit_ = kStringPlayers;   ///< setPlayerLimit()
    int64_t pos_ = 0;   ///< samples since the last reset (the cells)
    uint32_t counter_ = 0;   ///< counts the lanes started (each player's stream)
    // Knobs.
    float level_ = 1.0f;   ///< strings.level, linear
    float vibrato_ = 1.0f;   ///< the vibrato's depth knob
    float pressure_ = 1.0f;   ///< the bow force's factor
    float position_ = 0.0f;   ///< the bow point knob
    float speed_ = 1.0f;   ///< the bow speed's factor
    float attackMs_ = 80.0f;   ///< the attack, ms
    float releaseMs_ = 120.0f;   ///< the release, ms
    float width_ = 0.8f;   ///< the width
    Svf lowCut_[2];   ///< the low cut, per channel
    float lowCutHz_ = 60.0f;   ///< the low cut, Hz
    // The modes, [mode][lane].
    float zr_[kStringModes][kStringLanes] = {};   ///< the modes' states: real part
    float zi_[kStringModes][kStringLanes] = {};   ///< ... imaginary part
    float pr_[kStringModes][kStringLanes] = {};   ///< their poles this cell (with the vibrato): real part
    float pi_[kStringModes][kStringLanes] = {};   ///< ... imaginary part
    float p0r_[kStringModes][kStringLanes] = {};   ///< their poles at the stopped pitch: real part
    float p0i_[kStringModes][kStringLanes] = {};   ///< ... imaginary part
    float gr_[kStringModes][kStringLanes] = {};   ///< their input gains at the bow point: real part
    float gi_[kStringModes][kStringLanes] = {};   ///< ... imaginary part
    float hr_[kStringModes][kStringLanes] = {};   ///< the bow-point velocity's weights: real part
    float hi_[kStringModes][kStringLanes] = {};   ///< ... imaginary part
    float br_[kStringModes][kStringLanes] = {};   ///< the bridge force's weights: real part
    float bi_[kStringModes][kStringLanes] = {};   ///< ... imaginary part
    float wT_[kStringModes][kStringLanes] = {};                                        ///< angle per sample
    float release_[kStringModes][kStringLanes] = {};                                   ///< decay factor after the stroke
    float hur_[kStringModes][kStringLanes] = {};   ///< the bow-point velocity over sin^2 (the place moved): real part
    float hui_[kStringModes][kStringLanes] = {};   ///< ... imaginary part
    // The lanes.
    /** @brief One player on one note: his stroke, his vibrato, his place. */
    struct Lane {
        bool on = false;   ///< it sounds
        bool held = false;   ///< its key is held
        bool staccato = false;   ///< a short bitten stroke
        int pitch = 60;   ///< MIDI note
        int family = 0;   ///< the family that plays it (0 violins .. 3 basses)
        int note = -1;   ///< the note slot it belongs to, -1 none
        float velocity = 0.7f;   ///< its velocity, 0..1
        double t = 0.0;            ///< seconds since the stroke's start (negative: the onset's delay)
        double lift = -1.0;        ///< seconds since the bow lifted, -1 while it plays
        float vbow = 0.0f;   ///< the bow speed to go to
        float fbow = 0.0f;   ///< the bow force to go to
        float vNow = 0.0f;   ///< the bow speed now
        float fNow = 0.0f;   ///< the bow force now
        float admittance = 1.0f;   ///< the string's admittance at the bow point
        float cents = 0.0f;        ///< the player's intonation
        float vibHz = 5.5f;   ///< the vibrato's rate, Hz
        float vibCents = 15.0f;   ///< the vibrato's depth, cents
        float vibPhase = 0.0f;   ///< the vibrato's phase, radians
        float panL = 0.7f;   ///< the pan's gain, left
        float panR = 0.7f;   ///< the pan's gain, right
        // What the stroke began with (the modulation moves from these; Phase 5b).
        float vbow0 = 0.0f;   ///< the bow speed the stroke began with
        float fbow0 = 0.0f;   ///< the bow force the stroke began with
        float pan0 = 0.0f;   ///< the place it began with
        double beta0 = 0.1;   ///< the bow point the stroke began with (a share of the string)
        double beta = 0.1;   ///< the bow point now
        float peak = 0.0f;   ///< the bridge force's peak in this cell
        int quiet = 0;   ///< cells it has been silent (it ends after a few)
        Rng noise;   ///< its rosin noise stream
    };
    Lane lane_[kStringLanes];   ///< the lanes
    float vb_[kMaxBlock][kStringLanes] = {};   ///< per sample of the cell and lane: the bow speed
    float fb_[kMaxBlock][kStringLanes] = {};   ///< per sample and lane: the bow force
    float y_[kStringLanes] = {};   ///< per lane: the string's admittance at the bow point
    float out_[kMaxBlock][kStringLanes] = {};   ///< per sample and lane: the bridge force
    /// The bodies: per family two inputs (left, right), a bank each.
    struct Body {
        float direct = 0.3f;       ///< the broadband share
        float gain = 1.0f;         ///< the family levelled to the violins
        bool active = false;   ///< it rings
        int quiet = 0;   ///< cells it has been silent
    };
    Body body_[kStringFamilies];   ///< the bodies, per family
    /**
     * @name The bodies' resonators on eight lanes (the optimisation pass, 29.09.2026)
     * Lane 2 f + c is family f's channel c (0 left, 1 right), mode by mode: the four families' two channels are the
     * eight lanes of one AVX2 register (two NEON ones), and each lane still sums its modes in their order -- the scalar
     * loop's arithmetic, lane by lane. A family at rest holds zeros and is fed zero, so it stays zero.
     * @{ */
    static constexpr int kBodyLanes = 2 * kStringFamilies;
    alignas(32) float bpr_[kBodyModes][kBodyLanes] = {}, bpi_[kBodyModes][kBodyLanes] = {};   ///< the poles
    alignas(32) float bgr_[kBodyModes][kBodyLanes] = {}, bgi_[kBodyModes][kBodyLanes] = {};   ///< the input gains
    alignas(32) float bw_[kBodyModes][kBodyLanes] = {};                                       ///< the output weights
    alignas(32) float bzr_[kBodyModes][kBodyLanes] = {}, bzi_[kBodyModes][kBodyLanes] = {};   ///< the states
    /** @} */
    /// The modulation (Phase 5b, Modulation.h): a Modulator per note slot.
    NoteModulation<kStringNotes> mod_;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace parh
