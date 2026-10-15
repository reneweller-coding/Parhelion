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
 * @author Rene Weller
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Vec.h"
#include "parh/synth/Modulation.h"
#include <cstdint>

namespace parh {

constexpr int StringNotes = 6;                                         ///< notes at once
constexpr int StringPlayers = 6;                                       ///< players on a note at most
constexpr int StringLanes = paddedLanes(StringNotes * StringPlayers);   ///< 40
constexpr int StringModes = 24;                                        ///< partials of a string
constexpr int StringFamilies = 4;                                      ///< violins, violas, cellos, basses
constexpr int BodyModes = 24;                                          ///< resonators of a body

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)   // padded to its 32-byte lanes on purpose (the bodies)
#endif
/** @brief The string section: bowed modal strings, a player each, through the four families' bodies. */
class StringSection {
public:
    static constexpr int MaxBlock = 32;   ///< samples per decision cell (vibrato, envelopes' targets)

    /**
     * @brief Rate and seed (the players' intonation, vibrato and onsets, the bodies' high modes).
     * @param[in] sampleRate  the sample rate
     * @param[in] seed        the seed of the players' streams
     */
    void prepare(double sampleRate, uint64_t seed);
    /**
     * @brief The knobs (strings:: parameters).
     * @param[in] v  the effective parameter values
     */
    void update(const float* v);
    /**
     * @brief Players take up @p pitch at @p velocity; @p staccato: a short bitten stroke.
     * @param[in] pitch     the MIDI note
     * @param[in] velocity  the velocity, 0..1
     * @param[in] staccato  true for a short bitten stroke
     * @param[in] late      samples after the ideal start
     */
    void noteOn(int pitch, float velocity, bool staccato, double late);
    /**
     * @brief The players on @p pitch lift their bows.
     * @param[in] pitch  the MIDI note
     */
    void noteOff(int pitch);
    /** @brief Silence. */
    void reset();
    /**
     * @brief Players on a note (1 .. StringPlayers, at most the quality's limit): the knob, through update().
     * @param[in] n  the players per note
     */
    void setPlayers(int n) { m_players = n < 1 ? 1 : (n > m_playerLimit ? m_playerLimit : n); }
    /**
     * @brief The quality's limit on the players (Deck::setQuest; the Quest's three). Until 29.09.2026 the Quest set the
     *        players themselves, and the knob's value from update() overwrote them at the next cell: the Quest played
     *        all six. It takes effect with the next update().
     * @param[in] n  the most players per note
     */
    void setPlayerLimit(int n) { m_playerLimit = n < 1 ? 1 : (n > StringPlayers ? StringPlayers : n); }
    /**
     * @brief Renders @p n samples, replacing @p L and @p R.
     * @param[out] L  the left channel
     * @param[out] R  the right channel
     * @param[in]  n  the block's samples
     */
    void process(float* L, float* R, int n);
    /**
     * @brief With a chosen lane type (float: the scalar reference), for the vector tests.
     * @param[out] L  the left channel
     * @param[out] R  the right channel
     * @param[in]  n  the block's samples
     */
    template <class VT> void processWith(float* L, float* R, int n);
    /**
     * @brief Lanes sounding.
     * @return the lanes sounding
     */
    int activeLanes() const;
    /**
     * @brief The family a pitch is played by (0 violins .. 3 basses).
     * @param[in] pitch  the MIDI note
     * @return 0 violins .. 3 basses
     */
    static int familyOf(int pitch);
    /**
     * @brief The deck's shared modulation sources (Modulation.h).
     * @param[in] g  the shared sources
     */
    void setModGlobals(const ModGlobals& g) { m_mod.setGlobals(g); }
    /**
     * @brief The beat now and the beats per sample (the synced LFOs).
     * @param[in] beat            the beat now
     * @param[in] beatsPerSample  the beats per sample
     */
    void setClock(double beat, double beatsPerSample) { m_mod.setClock(m_pos, beat, beatsPerSample); }

private:
    /**
     * @brief A lane's bow point moved to @p beta: its modes' response there and its admittance.
     * @param[in] lane  the lane
     * @param[in] beta  the bow point, a share of the string
     */
    void placeBow(int lane, double beta);
    /**
     * @brief @p n samples of every lane's string and bow within one cell (see the file comment).
     * @param[in] n  the samples
     */
    template <class VT> void bowSegment(int n);
    /**
     * @brief Sets lane @p lane to play @p pitch at @p velocity as player @p player (@p staccato: a bitten stroke).
     * @param[in] lane      the lane
     * @param[in] pitch     the MIDI note
     * @param[in] velocity  the velocity, 0..1
     * @param[in] staccato  true for a bitten stroke
     * @param[in] player    the player
     */
    void startLane(int lane, int pitch, float velocity, bool staccato, int player);
    /** @brief The decisions of a cell: the envelopes' targets, the vibrato, the modulation, the lanes that end. */
    void cellUpdate();

    double m_sr = 48000.0;   ///< the sample rate, Hz
    uint64_t m_seed = 1;   ///< the players' streams' seed
    int m_players = StringPlayers;   ///< players on a note
    int m_playerLimit = StringPlayers;   ///< setPlayerLimit()
    int64_t m_pos = 0;   ///< samples since the last reset (the cells)
    uint32_t m_counter = 0;   ///< counts the lanes started (each player's stream)
    // Knobs.
    float m_level = 1.0f;   ///< strings.level, linear
    float m_vibrato = 1.0f;   ///< the vibrato's depth knob
    float m_pressure = 1.0f;   ///< the bow force's factor
    float m_position = 0.0f;   ///< the bow point knob
    float m_speed = 1.0f;   ///< the bow speed's factor
    float m_attackMs = 80.0f;   ///< the attack, ms
    float m_releaseMs = 120.0f;   ///< the release, ms
    float m_width = 0.8f;   ///< the width
    Svf m_lowCut[2];   ///< the low cut, per channel
    float m_lowCutHz = 60.0f;   ///< the low cut, Hz
    // The modes, [mode][lane].
    float m_zr[StringModes][StringLanes] = {};   ///< the modes' states: real part
    float m_zi[StringModes][StringLanes] = {};   ///< ... imaginary part
    float m_pr[StringModes][StringLanes] = {};   ///< their poles this cell (with the vibrato): real part
    float m_pi[StringModes][StringLanes] = {};   ///< ... imaginary part
    float m_p0r[StringModes][StringLanes] = {};   ///< their poles at the stopped pitch: real part
    float m_p0i[StringModes][StringLanes] = {};   ///< ... imaginary part
    float m_gr[StringModes][StringLanes] = {};   ///< their input gains at the bow point: real part
    float m_gi[StringModes][StringLanes] = {};   ///< ... imaginary part
    float m_hr[StringModes][StringLanes] = {};   ///< the bow-point velocity's weights: real part
    float m_hi[StringModes][StringLanes] = {};   ///< ... imaginary part
    float m_br[StringModes][StringLanes] = {};   ///< the bridge force's weights: real part
    float m_bi[StringModes][StringLanes] = {};   ///< ... imaginary part
    float m_wT[StringModes][StringLanes] = {};                                        ///< angle per sample
    float m_release[StringModes][StringLanes] = {};                                   ///< decay factor after the stroke
    float m_hur[StringModes][StringLanes] = {};   ///< the bow-point velocity over sin^2 (the place moved): real part
    float m_hui[StringModes][StringLanes] = {};   ///< ... imaginary part
    // The lanes.
    /** @brief One player on one note: his stroke, his vibrato, his place. */
    struct Lane {
        bool m_on = false;   ///< it sounds
        bool m_held = false;   ///< its key is held
        bool m_staccato = false;   ///< a short bitten stroke
        int m_pitch = 60;   ///< MIDI note
        int m_family = 0;   ///< the family that plays it (0 violins .. 3 basses)
        int m_note = -1;   ///< the note slot it belongs to, -1 none
        float m_velocity = 0.7f;   ///< its velocity, 0..1
        double m_t = 0.0;            ///< seconds since the stroke's start (negative: the onset's delay)
        double m_lift = -1.0;        ///< seconds since the bow lifted, -1 while it plays
        float m_vbow = 0.0f;   ///< the bow speed to go to
        float m_fbow = 0.0f;   ///< the bow force to go to
        float m_vNow = 0.0f;   ///< the bow speed now
        float m_fNow = 0.0f;   ///< the bow force now
        float m_admittance = 1.0f;   ///< the string's admittance at the bow point
        float m_cents = 0.0f;        ///< the player's intonation
        float m_vibHz = 5.5f;   ///< the vibrato's rate, Hz
        float m_vibCents = 15.0f;   ///< the vibrato's depth, cents
        float m_vibPhase = 0.0f;   ///< the vibrato's phase, radians
        float m_panL = 0.7f;   ///< the pan's gain, left
        float m_panR = 0.7f;   ///< the pan's gain, right
        // What the stroke began with (the modulation moves from these; Phase 5b).
        float m_vbow0 = 0.0f;   ///< the bow speed the stroke began with
        float m_fbow0 = 0.0f;   ///< the bow force the stroke began with
        float m_pan0 = 0.0f;   ///< the place it began with
        double m_beta0 = 0.1;   ///< the bow point the stroke began with (a share of the string)
        double m_beta = 0.1;   ///< the bow point now
        float m_peak = 0.0f;   ///< the bridge force's peak in this cell
        int m_quiet = 0;   ///< cells it has been silent (it ends after a few)
        Rng m_noise;   ///< its rosin noise stream
    };
    Lane m_lane[StringLanes];   ///< the lanes
    float m_vb[MaxBlock][StringLanes] = {};   ///< per sample of the cell and lane: the bow speed
    float m_fb[MaxBlock][StringLanes] = {};   ///< per sample and lane: the bow force
    float m_y[StringLanes] = {};   ///< per lane: the string's admittance at the bow point
    float m_out[MaxBlock][StringLanes] = {};   ///< per sample and lane: the bridge force
    /// The bodies: per family two inputs (left, right), a bank each.
    struct Body {
        float m_direct = 0.3f;       ///< the broadband share
        float m_gain = 1.0f;         ///< the family levelled to the violins
        bool m_active = false;   ///< it rings
        int m_quiet = 0;   ///< cells it has been silent
    };
    Body m_body[StringFamilies];   ///< the bodies, per family
    /**
     * @name The bodies' resonators on eight lanes (the optimisation pass, 29.09.2026)
     * Lane 2 f + c is family f's channel c (0 left, 1 right), mode by mode: the four families' two channels are the
     * eight lanes of one AVX2 register (two NEON ones), and each lane still sums its modes in their order -- the scalar
     * loop's arithmetic, lane by lane. A family at rest holds zeros and is fed zero, so it stays zero.
     * @{ */
    static constexpr int BodyLanes = 2 * StringFamilies;
    alignas(32) float m_bpr[BodyModes][BodyLanes] = {}, m_bpi[BodyModes][BodyLanes] = {};   ///< the poles
    alignas(32) float m_bgr[BodyModes][BodyLanes] = {}, m_bgi[BodyModes][BodyLanes] = {};   ///< the input gains
    alignas(32) float m_bw[BodyModes][BodyLanes] = {};                                       ///< the output weights
    alignas(32) float m_bzr[BodyModes][BodyLanes] = {}, m_bzi[BodyModes][BodyLanes] = {};   ///< the states
    /** @} */
    /// The modulation (Phase 5b, Modulation.h): a Modulator per note slot.
    NoteModulation<StringNotes> m_mod;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace parh
