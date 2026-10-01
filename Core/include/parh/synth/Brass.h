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
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/synth/Modulation.h"
#include <cstdint>
#include <vector>

namespace parh {

constexpr int kBrassNotes = 6;   ///< notes at once
constexpr int kBrassPlayers = 4;   ///< players on a note at most

/** @brief The brass section: lips on a bore, a player each, a few players a note (see the file comment). */
class Brass {
public:
    static constexpr int kMaxBlock = 32;   ///< the longest block process() renders at once

    /** @brief Sets the sample rate and the seed of the players' streams, and falls silent. */
    void prepare(double sampleRate, uint64_t seed);
    /** @brief Reads the effective parameter values (indexed by brass::). */
    void update(const float* v);
    /** @brief Players take up @p pitch at @p velocity (0..1), @p late samples after its ideal start; @p unused is not read. */
    void noteOn(int pitch, float velocity, bool unused, double late);
    /** @brief The players of @p pitch stop blowing. */
    void noteOff(int pitch);
    /** @brief Silence: every player off. */
    void reset();
    /** @brief Players on a note (1 .. kBrassPlayers, at most the quality's limit): the knob, through update(). */
    void setPlayers(int n) { players_ = n < 1 ? 1 : (n > playerLimit_ ? playerLimit_ : n); }
    /** @brief The quality's limit on the players (Deck::setQuest), as StringSection::setPlayerLimit. */
    void setPlayerLimit(int n) { playerLimit_ = n < 1 ? 1 : (n > kBrassPlayers ? kBrassPlayers : n); }
    /** @brief Renders @p n samples, replacing @p L and @p R. */
    void process(float* L, float* R, int n);
    /** @brief How many players blow now (for the tests). */
    int activePlayers() const;
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { mod_.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { mod_.setClock(pos_, beat, beatsPerSample); }

private:
    /** @brief Note @p s's modulation now, onto its players. */
    void modNote(int s);
    /** @brief One player on one note: his lips, his bore, his ear. */
    struct Player {
        bool on = false;   ///< he plays
        bool held = false;   ///< the key is held
        int note = -1;   ///< the note slot he plays, -1 none
        double y = 0.0;   ///< the lip displacement, m
        double v = 0.0;   ///< the lip velocity, m/s
        double wl = 0.0;                  ///< lip angular frequency
        double bell = 0.0;   ///< the bell's low pass: its state
        double bellA = 0.7;   ///< its coefficient
        double delay = 100.0;             ///< the bore's round trip, samples
        double pm = 0.0;   ///< the mouth pressure now
        double target = 5000.0;   ///< the note's
        double t = 0.0;   ///< seconds since the note began
        double released = -1.0;   ///< seconds since its release, -1 while held
        double start = 0.0;   ///< seconds until he sets in
        double vibHz = 5.0;   ///< his vibrato's rate, Hz
        double vibPhase = 0.0;   ///< his vibrato's phase, radians
        float panL = 0.7f;   ///< the pan's gain, left
        float panR = 0.7f;   ///< the pan's gain, right
        float pan0 = 0.0f;                ///< where the player sits (the modulation moves from here; Phase 5b)
        double target0 = 5000.0;   ///< the note's mouth pressure as it began
        double f0 = 100.0;   ///< the note's pitch as it began, Hz
        float out = 0.0f;   ///< the last sample that left the bell
        float dc = 0.0f;   ///< the DC blocker's state
        std::vector<float> line;          ///< the outgoing wave's delay
        int write = 0;   ///< where the delay is written
        double period = 200.0;            ///< the note's period, samples
        double lastCross = -1.0;   ///< the last upward zero crossing, samples (-1 none)
        double mean = 0.0;   ///< the outgoing wave's slow mean (the crossings are taken around it)
        double prevX = 0.0;   ///< the last sample around the mean
        int64_t clock = 0;   ///< samples since the note began
        int crossings = 0;   ///< the crossings counted
        Rng rng;   ///< his own random stream
        int quiet = 0;   ///< cells he has been silent (he stops after a few)
    };
    double sr_ = 48000.0;   ///< the sample rate, Hz
    uint64_t seed_ = 1;   ///< the players' streams' seed
    int players_ = 3;   ///< players on a note
    int playerLimit_ = kBrassPlayers;   ///< setPlayerLimit()
    uint32_t counter_ = 0;   ///< counts the notes started (each player's stream)
    float level_ = 1.0f;   ///< brass.level, linear
    float pressure_ = 1.0f;   ///< the mouth pressure's factor
    float brassiness_ = 0.35f;   ///< the blare's amount
    float attackMs_ = 40.0f;   ///< the attack, ms
    float releaseMs_ = 150.0f;   ///< the release, ms
    float vibratoCents_ = 6.0f;   ///< the vibrato's depth, cents
    float width_ = 0.6f;   ///< the width
    float lowCutHz_ = 50.0f;   ///< the low cut, Hz
    Svf lowCut_[2];   ///< the low cut, per channel
    Player player_[kBrassNotes * kBrassPlayers];   ///< the players, kBrassPlayers per note
    int notePitch_[kBrassNotes] = {};   ///< per note slot: its pitch
    bool noteOn_[kBrassNotes] = {};   ///< per note slot: it sounds
    double noteAge_[kBrassNotes] = {};   ///< per note slot: seconds since it began
    /// The modulation (Phase 5b): a Modulator per note; a note's blare and vibrato depth as the matrix leaves them.
    NoteModulation<kBrassNotes> mod_;
    float noteBrass_[kBrassNotes] = {};   ///< per note slot: its blare as the matrix leaves it
    float noteVib_[kBrassNotes] = {};   ///< per note slot: its vibrato's depth as the matrix leaves it
    int64_t pos_ = 0;   ///< samples since the last reset (the cells)
};

} // namespace parh
