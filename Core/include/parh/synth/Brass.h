/**
 * @file Brass.h
 * @brief The brass (PLAN 5.9, Cinematic): players' lips on a bore -- horns and trombones for the chords and the braam.
 *
 * **The lips** are an outward-striking valve of one mass (Adachi and Sato, "Trumpet sound simulation using a
 * two-dimensional lip vibration model", JASA 99(2), 1996, reduced to its one-dimensional form; Vergez and Rodet 1997):
 * y'' + (w_l / Q_l) y' + w_l^2 (y - y0) = (p_m - p) / mu_l, the opening's area w max(y, 0), the flow through it after
 * Bernoulli, U = w h sqrt(2 |p_m - p| / rho) sgn(p_m - p). The pressure in the mouthpiece is p = 2 p- + Z_c U (the
 * wave coming back from the bore, the closed end's reflection plus what the flow injects), so U solves a quadratic in
 * closed form. The lips run on two sub-steps a sample (one was not enough to keep them stable at forte). An
 * outward-striking valve plays above the bore's resonance, the more so the higher its own frequency: the lips start at
 * 0.8 of the note (Q_l = 7), where they lock within about 10 to 45 cents of it across the range (scanned in a
 * simulation, PLAN Phase 4b), and the player's ear below takes the rest. A tuning of 1.02, as first tried, and the
 * scattering-junction reduction of the Synthesis ToolKit both failed to lock onto the bore in the scan.
 *
 * **The bore**: a delay of one period there and back and the bell's reflection, a low pass that lets the high
 * partials out (the bell's cut-off) and returns the low ones; the round trip is not inverted -- the flared bore of a
 * brass instrument has the whole harmonic series, as a cone has, where a cylinder closed at one end would have the
 * odd ones only. What leaves the bell (the outgoing wave less the returning one) is the sound.
 *
 * **The blare**: the fortissimo's brassiness is the wave steepening as it travels (nonlinear propagation, Hirschberg
 * et al., JASA 99(3), 1996); here as a quadratic term on the wave that leaves the bell, stronger the louder
 * (brass.brassiness) -- inside the loop it would feed itself.
 *
 * **The player's ear**: what the lips and the bore agree on is not quite the note; a player lips it into tune. Here
 * the period of the outgoing wave is measured at its upward zero crossings, and the bore's delay follows the
 * difference to the note's period, a third of it per period. Higher notes need more breath (the threshold rises with
 * the frequency): the mouth pressure grows with the note's frequency to the power 0.3.
 *
 * **The section**: three players a note (their tuning, onset and breath their own), up to six notes.
 * @author Rene Weller
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/synth/Modulation.h"
#include <cstdint>
#include <vector>

namespace parh
{

constexpr int BrassNotes = 6;     ///< notes at once
constexpr int BrassPlayers = 4;   ///< players on a note at most

/** @brief The brass section: lips on a bore, a player each, a few players a note (see the file comment). */
class Brass
{
public:
    static constexpr int MaxBlock = 32;   ///< the longest block process() renders at once

    /**
     * @brief Sets the sample rate and the seed of the players' streams, and falls silent.
     * @param[in] sampleRate  the sample rate
     * @param[in] seed        the seed of the players' streams
     */
    void prepare( double sampleRate, uint64_t seed );
    /**
     * @brief Reads the effective parameter values (indexed by brass::).
     * @param[in] v  the effective parameter values
     */
    void update( const float *v );
    /**
     * @brief Players take up @p pitch at @p velocity (0..1), @p late samples after its ideal start; @p unused is not
     *        read.
     * @param[in] pitch     the MIDI note
     * @param[in] velocity  the velocity, 0..1
     * @param[in] unused    not read
     * @param[in] late      samples after the ideal start
     */
    void noteOn( int pitch, float velocity, bool unused, double late );
    /**
     * @brief The players of @p pitch stop blowing.
     * @param[in] pitch  the MIDI note
     */
    void noteOff( int pitch );
    /** @brief Silence: every player off. */
    void reset();
    /**
     * @brief Players on a note (1 .. BrassPlayers, at most the quality's limit): the knob, through update().
     * @param[in] n  the players per note
     */
    void setPlayers( int n ) { m_players = n < 1 ? 1 : ( n > m_playerLimit ? m_playerLimit : n ); }
    /**
     * @brief The quality's limit on the players (Deck::setQuest), as StringSection::setPlayerLimit.
     * @param[in] n  the most players per note
     */
    void setPlayerLimit( int n ) { m_playerLimit = n < 1 ? 1 : ( n > BrassPlayers ? BrassPlayers : n ); }
    /**
     * @brief Renders @p n samples, replacing @p L and @p R.
     * @param[out] L  the left channel
     * @param[out] R  the right channel
     * @param[in]  n  the block's samples
     */
    void process( float *L, float *R, int n );
    /**
     * @brief How many players blow now (for the tests).
     * @return the players blowing
     */
    int activePlayers() const;
    /**
     * @brief The deck's shared modulation sources (Modulation.h).
     * @param[in] g  the shared sources
     */
    void setModGlobals( const ModGlobals &g ) { m_mod.setGlobals( g ); }
    /**
     * @brief The beat now and the beats per sample (the synced LFOs).
     * @param[in] beat            the beat now
     * @param[in] beatsPerSample  the beats per sample
     */
    void setClock( double beat, double beatsPerSample ) { m_mod.setClock( m_pos, beat, beatsPerSample ); }

private:
    /**
     * @brief Note @p s's modulation now, onto its players.
     * @param[in] s  the note's slot
     */
    void modNote( int s );
    /** @brief One player on one note: his lips, his bore, his ear. */
    struct Player
    {
        bool               m_on = false;         ///< he plays
        bool               m_held = false;       ///< the key is held
        int                m_note = -1;          ///< the note slot he plays, -1 none
        double             m_y = 0.0;            ///< the lip displacement, m
        double             m_v = 0.0;            ///< the lip velocity, m/s
        double             m_wl = 0.0;           ///< lip angular frequency
        double             m_bell = 0.0;         ///< the bell's low pass: its state
        double             m_bellA = 0.7;        ///< its coefficient
        double             m_delay = 100.0;      ///< the bore's round trip, samples
        double             m_pm = 0.0;           ///< the mouth pressure now
        double             m_target = 5000.0;    ///< the note's
        double             m_t = 0.0;            ///< seconds since the note began
        double             m_released = -1.0;    ///< seconds since its release, -1 while held
        double             m_start = 0.0;        ///< seconds until he sets in
        double             m_vibHz = 5.0;        ///< his vibrato's rate, Hz
        double             m_vibPhase = 0.0;     ///< his vibrato's phase, radians
        float              m_panL = 0.7f;        ///< the pan's gain, left
        float              m_panR = 0.7f;        ///< the pan's gain, right
        float              m_pan0 = 0.0f;        ///< where the player sits (the modulation moves from here; Phase 5b)
        double             m_target0 = 5000.0;   ///< the note's mouth pressure as it began
        double             m_f0 = 100.0;         ///< the note's pitch as it began, Hz
        float              m_out = 0.0f;         ///< the last sample that left the bell
        float              m_dc = 0.0f;          ///< the DC blocker's state
        std::vector<float> m_line;               ///< the outgoing wave's delay
        int                m_write = 0;          ///< where the delay is written
        double             m_period = 200.0;     ///< the note's period, samples
        double             m_lastCross = -1.0;   ///< the last upward zero crossing, samples (-1 none)
        double             m_mean = 0.0;         ///< the outgoing wave's slow mean (the crossings are taken around it)
        double             m_prevX = 0.0;        ///< the last sample around the mean
        int64_t            m_clock = 0;          ///< samples since the note began
        int                m_crossings = 0;      ///< the crossings counted
        Rng                m_rng;                ///< his own random stream
        int                m_quiet = 0;          ///< cells he has been silent (he stops after a few)
    };
    double   m_sr = 48000.0;                        ///< the sample rate, Hz
    uint64_t m_seed = 1;                            ///< the players' streams' seed
    int      m_players = 3;                         ///< players on a note
    int      m_playerLimit = BrassPlayers;          ///< setPlayerLimit()
    uint32_t m_counter = 0;                         ///< counts the notes started (each player's stream)
    float    m_level = 1.0f;                        ///< brass.level, linear
    float    m_pressure = 1.0f;                     ///< the mouth pressure's factor
    float    m_brassiness = 0.35f;                  ///< the blare's amount
    float    m_attackMs = 40.0f;                    ///< the attack, ms
    float    m_releaseMs = 150.0f;                  ///< the release, ms
    float    m_vibratoCents = 6.0f;                 ///< the vibrato's depth, cents
    float    m_width = 0.6f;                        ///< the width
    float    m_lowCutHz = 50.0f;                    ///< the low cut, Hz
    Svf      m_lowCut[2];                           ///< the low cut, per channel
    Player   m_player[BrassNotes * BrassPlayers];   ///< the players, BrassPlayers per note
    int      m_notePitch[BrassNotes] = {};          ///< per note slot: its pitch
    bool     m_noteOn[BrassNotes] = {};             ///< per note slot: it sounds
    double   m_noteAge[BrassNotes] = {};            ///< per note slot: seconds since it began
    /// The modulation (Phase 5b): a Modulator per note; a note's blare and vibrato depth as the matrix leaves them.
    NoteModulation<BrassNotes> m_mod;
    float                      m_noteBrass[BrassNotes] = {};   ///< per note slot: its blare as the matrix leaves it
    float   m_noteVib[BrassNotes] = {};   ///< per note slot: its vibrato's depth as the matrix leaves it
    int64_t m_pos = 0;                    ///< samples since the last reset (the cells)
};

}   // namespace parh
