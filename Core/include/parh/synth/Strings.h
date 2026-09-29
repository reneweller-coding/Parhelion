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
    template <class V> void bowSegment(int n);
    void startLane(int lane, int pitch, float velocity, bool staccato, int player);
    void cellUpdate();

    double sr_ = 48000.0;
    uint64_t seed_ = 1;
    int players_ = kStringPlayers;
    int playerLimit_ = kStringPlayers;   ///< setPlayerLimit()
    int64_t pos_ = 0;
    uint32_t counter_ = 0;
    // Knobs.
    float level_ = 1.0f, vibrato_ = 1.0f, pressure_ = 1.0f, position_ = 0.0f, speed_ = 1.0f, attackMs_ = 80.0f, releaseMs_ = 120.0f;
    float width_ = 0.8f;
    Svf lowCut_[2];
    float lowCutHz_ = 60.0f;
    // The modes, [mode][lane].
    float zr_[kStringModes][kStringLanes] = {}, zi_[kStringModes][kStringLanes] = {};
    float pr_[kStringModes][kStringLanes] = {}, pi_[kStringModes][kStringLanes] = {};
    float p0r_[kStringModes][kStringLanes] = {}, p0i_[kStringModes][kStringLanes] = {};
    float gr_[kStringModes][kStringLanes] = {}, gi_[kStringModes][kStringLanes] = {};
    float hr_[kStringModes][kStringLanes] = {}, hi_[kStringModes][kStringLanes] = {};   ///< bow-point velocity
    float br_[kStringModes][kStringLanes] = {}, bi_[kStringModes][kStringLanes] = {};   ///< bridge force
    float wT_[kStringModes][kStringLanes] = {};                                        ///< angle per sample
    float release_[kStringModes][kStringLanes] = {};                                   ///< decay factor after the stroke
    float hur_[kStringModes][kStringLanes] = {}, hui_[kStringModes][kStringLanes] = {}; ///< bow-point velocity over sin^2 (the place moved)
    // The lanes.
    struct Lane {
        bool on = false, held = false, staccato = false;
        int pitch = 60, family = 0, note = -1;
        float velocity = 0.7f;
        double t = 0.0;            ///< seconds since the stroke's start (negative: the onset's delay)
        double lift = -1.0;        ///< seconds since the bow lifted, -1 while it plays
        float vbow = 0.0f, fbow = 0.0f;      ///< targets
        float vNow = 0.0f, fNow = 0.0f;      ///< the envelope's current values
        float admittance = 1.0f;
        float cents = 0.0f;        ///< the player's intonation
        float vibHz = 5.5f, vibCents = 15.0f, vibPhase = 0.0f;
        float panL = 0.7f, panR = 0.7f;
        // What the stroke began with (the modulation moves from these; Phase 5b).
        float vbow0 = 0.0f, fbow0 = 0.0f, pan0 = 0.0f;
        double beta0 = 0.1, beta = 0.1;
        float peak = 0.0f;
        int quiet = 0;
        Rng noise;
    };
    Lane lane_[kStringLanes];
    float vb_[kMaxBlock][kStringLanes] = {}, fb_[kMaxBlock][kStringLanes] = {}, y_[kStringLanes] = {};
    float out_[kMaxBlock][kStringLanes] = {};
    // The bodies: per family two inputs (left, right), a bank each.
    struct Body {
        float direct = 0.3f;       ///< the broadband share
        float gain = 1.0f;         ///< the family levelled to the violins
        bool active = false;
        int quiet = 0;
    };
    Body body_[kStringFamilies];
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
    // The modulation (Phase 5b, Modulation.h): a Modulator per note slot.
    NoteModulation<kStringNotes> mod_;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace parh
