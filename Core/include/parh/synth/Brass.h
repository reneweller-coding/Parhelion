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

constexpr int kBrassNotes = 6;
constexpr int kBrassPlayers = 4;

class Brass {
public:
    static constexpr int kMaxBlock = 32;

    void prepare(double sampleRate, uint64_t seed);
    void update(const float* v);
    void noteOn(int pitch, float velocity, bool unused, double late);
    void noteOff(int pitch);
    void reset();
    void setPlayers(int n) { players_ = n < 1 ? 1 : (n > kBrassPlayers ? kBrassPlayers : n); }
    void process(float* L, float* R, int n);
    int activePlayers() const;
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { mod_.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { mod_.setClock(pos_, beat, beatsPerSample); }

private:
    /** @brief Note @p s's modulation now, onto its players. */
    void modNote(int s);
    struct Player {
        bool on = false, held = false;
        int note = -1;
        double y = 0.0, v = 0.0;          ///< lip displacement (m) and velocity
        double wl = 0.0;                  ///< lip angular frequency
        double bell = 0.0, bellA = 0.7;   ///< the bell's low pass: state, coefficient
        double delay = 100.0;             ///< the bore's round trip, samples
        double pm = 0.0, target = 5000.0; ///< mouth pressure now and the note's
        double t = 0.0, released = -1.0, start = 0.0;
        double vibHz = 5.0, vibPhase = 0.0;
        float panL = 0.7f, panR = 0.7f;
        float pan0 = 0.0f;                ///< where the player sits (the modulation moves from here; Phase 5b)
        double target0 = 5000.0, f0 = 100.0;   ///< the note's pressure and pitch as they began
        float out = 0.0f, dc = 0.0f;
        std::vector<float> line;          ///< the outgoing wave's delay
        int write = 0;
        double period = 200.0;            ///< the note's period, samples
        double lastCross = -1.0, mean = 0.0, prevX = 0.0;
        int64_t clock = 0;
        int crossings = 0;
        Rng rng;
        int quiet = 0;
    };
    double sr_ = 48000.0;
    uint64_t seed_ = 1;
    int players_ = 3;
    uint32_t counter_ = 0;
    float level_ = 1.0f, pressure_ = 1.0f, brassiness_ = 0.35f, attackMs_ = 40.0f, releaseMs_ = 150.0f, vibratoCents_ = 6.0f;
    float width_ = 0.6f, lowCutHz_ = 50.0f;
    Svf lowCut_[2];
    Player player_[kBrassNotes * kBrassPlayers];
    int notePitch_[kBrassNotes] = {};
    bool noteOn_[kBrassNotes] = {};
    double noteAge_[kBrassNotes] = {};
    // The modulation (Phase 5b): a Modulator per note; a note's blare and vibrato depth as the matrix leaves them.
    NoteModulation<kBrassNotes> mod_;
    float noteBrass_[kBrassNotes] = {}, noteVib_[kBrassNotes] = {};
    int64_t pos_ = 0;
};

} // namespace parh
