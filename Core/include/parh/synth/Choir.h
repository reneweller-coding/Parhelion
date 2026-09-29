/**
 * @file Choir.h
 * @brief The choir (PLAN 5.9, Cinematic): singers from a glottal source and a vocal tract, "aah" to "uuh", no words.
 *
 * **The source** of a singer is the Liljencrants-Fant model of the glottal flow's derivative (Fant, Liljencrants and
 * Lin, "A four-parameter model of glottal flow", STL-QPSR 4, 1985): a growing sinusoid E0 e^(alpha t) sin(pi t / tp)
 * up to the main excitation at te, then an exponential return -(Ee / (eps ta)) (e^(-eps (t - te)) - e^(-eps (T0 - te)))
 * that closes the glottis by the period's end; eps from eps ta = 1 - e^(-eps (T0 - te)), alpha from a flow that
 * returns to zero over the period. Its shape follows one number, Rd (Fant, "The LF-model revisited", STL-QPSR 2-3,
 * 1995): Ra = (-1 + 4.8 Rd) / 100, Rk = (22.4 + 11.8 Rd) / 100, Rg = Rk / (4 (0.11 Rd / (0.5 + 1.2 Rk) - Ra)); lax
 * and breathy near 2.7, pressed near 0.8 (choir.tension). The model is solved once per Rd in time normalised to the
 * period (a table of 64) and scaled to each period; within the period it runs as a rotating phasor and a decaying
 * exponential, a few multiplications a sample.
 *
 * **The singer**: the period's length and amplitude perturbed from period to period (jitter about 0.4 %, shimmer about
 * 3 %), a vibrato of 4.8 to 5.8 Hz and 15 to 35 cents that sets in after a third of a second, a slow wander of the
 * pitch, an intonation and an onset of his own, aspiration noise in the open phase (choir.breath).
 *
 * **The vocal tract**: five formants in parallel as band passes of unit peak, their frequencies, bandwidths and levels
 * from the singers' tables of the formant-synthesis tradition (Csound's FOF tables after Peterson and Barney and
 * Sundberg) for bass, tenor, alto and soprano on "a", "o" and "u"; the vowel knob glides between them. A note's singers
 * share two tracts, one a little shorter and one a little longer (the section's spread, and its stereo).
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/synth/Modulation.h"
#include <cstdint>

namespace parh {

constexpr int kChoirNotes = 6;       ///< notes at once
constexpr int kChoirSingers = 6;     ///< singers on a note at most
constexpr int kLfTable = 64;         ///< the LF shapes over Rd

class Choir {
public:
    static constexpr int kMaxBlock = 32;

    void prepare(double sampleRate, uint64_t seed);
    void update(const float* v);
    void noteOn(int pitch, float velocity, bool unused, double late);
    void noteOff(int pitch);
    void reset();
    void setSingers(int n) { singers_ = n < 1 ? 1 : (n > kChoirSingers ? kChoirSingers : n); }
    /** @brief Renders @p n samples, replacing @p L and @p R (scalar: nothing to split into lanes). */
    void process(float* L, float* R, int n);
    int activeSingers() const;
    /** @brief The voice a pitch is sung by: 0 bass, 1 tenor, 2 alto, 3 soprano. */
    static int voiceOf(int pitch);
    /** @brief One period of the LF source at @p rd, @p n samples (normalised to Ee = 1; for the tests). */
    void lfPeriod(double rd, int n, float* out) const;
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { mod_.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { mod_.setClock(pos_, beat, beatsPerSample); }

private:
    struct LfShape { double tp, te, ta, alpha, eps, e0; };
    struct Singer {
        bool on = false;
        int note = -1, group = 0;
        double t = 0.0, T0 = 0.005;
        double start = 0.0;          ///< seconds until the singer sets in
        LfShape s{};                 ///< this period's, in seconds
        double w = 0.0;              ///< pi / tp
        double zr = 0.0, zi = 0.0, sr = 1.0, si = 0.0;   ///< e^((alpha + i w) t) and its step
        double ret = 0.0, retStep = 1.0, retEnd = 0.0;
        bool returning = false;
        double amp = 1.0, cents = 0.0, vibHz = 5.3, vibCents = 25.0, vibPhase = 0.0, wander = 0.0, wanderTo = 0.0;
        double age = 0.0;
        Rng rng;
    };
    struct Note {
        bool on = false, held = false;
        int pitch = 60, voice = 0;
        float velocity = 0.7f;
        double t = 0.0, released = -1.0;
        float env = 0.0f;
        Svf formant[2][5];
        float gain[5] = {};
        int quiet = 0;
        // The modulation's values for this note (Phase 5b); the knobs' where no slot reaches them.
        float vowel = 0.0f, formantMul = 1.0f, tension = 0.4f, breath = 0.25f, vib = 1.0f, level = 1.0f, panL = 1.0f, panR = 1.0f;
        double pitchSt = 0.0;
    };
    void startPeriod(Singer& g, const Note& n, double tInto);
    void setFormants(Note& n);
    /** @brief Every sounding note's modulation at this cell (every 32 samples of the choir's own count). */
    void modCell();
    /** @brief Note @p s's modulation now. */
    void modNote(int s);
    static LfShape solveLf(double rd);

    double sr_ = 48000.0;
    uint64_t seed_ = 1;
    int singers_ = kChoirSingers;
    uint32_t counter_ = 0;
    LfShape lf_[kLfTable];
    float level_ = 1.0f, vowel_ = 0.0f, vibrato_ = 1.0f, breath_ = 0.25f, tension_ = 0.4f, attackMs_ = 250.0f, releaseMs_ = 400.0f;
    float width_ = 0.8f, lowCutHz_ = 90.0f;
    Svf lowCut_[2];
    Singer singer_[kChoirNotes * kChoirSingers];
    Note note_[kChoirNotes];
    NoteModulation<kChoirNotes> mod_;   ///< the modulation, a Modulator per note (Phase 5b)
    int64_t pos_ = 0;                   ///< the choir's absolute sample count
};

} // namespace parh
