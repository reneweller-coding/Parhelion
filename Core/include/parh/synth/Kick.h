/**
 * @file Kick.h
 * @brief The kick (PLAN 5.1): three engines with closed-form phase, a 909 top layer, the kick's EQ, and the phase the
 *        rumble and the sub continue.
 *
 * **Sweep engine** (Phosphene). A sine chirp y(t) = A(t) sin(2 pi phi(t)). The frequency falls from a start to an end
 * pitch along two exponential segments -- a fast punch segment (tau_1) and a slower body segment (tau_2):
 *
 *   f(t)   = f_e + (f_s - f_e) ((1 - p) e^(-t/tau_2) + p e^(-t/tau_1))
 *   phi(t) = f_e t + (f_s - f_e) ((1 - p) tau_2 (1 - e^(-t/tau_2)) + p tau_1 (1 - e^(-t/tau_1)))
 *
 * phi is the exact integral of f, evaluated in closed form at every sample; a kick that should have started 0.37
 * samples before the grid begins at t = 0.37 / fs. Amplitude: linear attack, hold, exponential decay.
 *
 * **Resonator engine** (Phosphene). A damped rotating phasor whose amplitude does not depend on its tuning, so the
 * same pitch envelope sweeps it; a trigger adds energy to the ring instead of restarting it, which is how the
 * TR-808's bridged-T bass drum behaves (Werner, Abel and Smith, DAFx 2014).
 *
 * **909 engine** (Totality). The TR-909's bass drum is a swept oscillator whose wave a shaper turns almost into a sine,
 * with filtered noise and a click beside it (Reid, Synth Secrets 34). Here: the sweep's closed-form phase drives a
 * triangle, tri(phi) = 1 - 4 |frac(phi + 1/4) - 1/2|, whose fundamental is in phase with sin(2 pi phi) (its series is
 * 8/pi^2 sum (-1)^k sin((2k+1) 2 pi phi)/(2k+1)^2), so the phase lock and the rumble's continuation hold unchanged;
 * the saturator after it rounds the corners, more with more drive. The odd harmonics a triangle carries (-12 dB per
 * octave) are what the ear reads as the 909's knock.
 *
 * **The top layer** (the Berghain recipe, Dok. 3: a second kick, a 909 pitched eight semitones down, high-passed at
 * 400 Hz, under the body). Its own sweep with the body's time constants, shifted by Top Pitch, a triangle through its
 * own drive, a fourth-order high pass at Top Cut, its own short decay. It lives above the rumble's split and plays no
 * part in any phase relation.
 *
 * **The EQ** (Dok. 8.4): a second-order high pass at Low Cut (a subsonic filter; the body's fundamental sits a little
 * over an octave above it) and a peaking dip at Dip Freq. Both are in chainPhase(), because the rumble's sub and the
 * sub bass sum with the kick *after* them.
 *
 * **Output phase.** outputPhaseAt() returns the phase of the kick as it leaves the module at time t: phi(t) plus the
 * phase responses of the chain at the instantaneous frequency -- half a sample for the first-order ADAA saturator, the
 * tone low pass, the DC blocker, the low cut, the dip. asymptoticPhase() is the constant c with outputPhase(t) ->
 * f_e t + c: the phase a sine at f_e must have to continue the kick's tail exactly (the rumble's sub, PLAN 5.2).
 *
 * **Latching.** Every trigger freezes the pitch and amplitude settings for that kick; a parameter ramp acts on the
 * next kick, never inside one.
 *
 * @note Adapted from Phosphene `Core/include/phos/Kick.h` at 76f7100 (27.09.2026): the sweep and resonator engines,
 *       the phase trim, the tail limit and the click are Phosphene's; the 909 engine, the top layer, the EQ, the
 *       tuning rules and asymptoticPhase() are Totality's.
 * @note Copied from Totality `Core/include/tot/synth/Kick.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#pragma once
#include "parh/Adaa.h"
#include "parh/Dsp.h"

namespace parh {

/** @brief The kick drum synthesizer (one voice plus a fade slot, and the top layer). */
class Kick {
public:
    /** @brief Prepares for a sample rate. */
    void prepare(double sampleRate);
    /** @brief Silences and clears all state. */
    void reset();
    /**
     * @brief Reads the effective parameter values.
     * @param v       values indexed by kick:: (after offsets and constraints)
     * @param keyRoot pitch class of the key (0 = C), used when Tune is not Free
     */
    void update(const float* v, int keyRoot);
    /**
     * @brief Starts a kick.
     * @param velocity 0..1
     * @param late     how many samples ago the kick ideally started (0 <= late < 1)
     */
    void trigger(float velocity, double late = 0.0);
    /**
     * @brief Renders @p n samples.
     * @param out  receives the kick (replaced)
     * @param body receives what feeds the rumble: body and click after the EQ, without the top layer (may be null)
     * @param n    samples
     */
    void process(float* out, float* body, int n);

    /** @brief The end pitch actually used, after tuning. */
    float tunedEndHz() const { return endHz_; }
    /** @brief The body time constant the next kick uses, after the phase trim, in seconds. */
    double bodyTau() const { return tau2Trimmed_; }
    /** @brief Whether anything is sounding. */
    bool active() const { return voice_.active || fade_.active || top_.active; }

    /**
     * @brief Output phase of a kick triggered with the current settings, @p t seconds after its ideal start.
     * @return phase in cycles (not wrapped)
     */
    double outputPhaseAt(double t) const;
    /** @brief The constant c with outputPhaseAt(t) -> f_e t + c for large t, in cycles (see the file comment). */
    double asymptoticPhase() const;
    /** @brief Instantaneous frequency of the current settings at @p t seconds. */
    double frequencyAt(double t) const;
    /** @brief Phase in cycles the chain after the oscillator adds at @p hz (saturator, tone, DC blocker, EQ). */
    double chainPhase(double hz) const;
    /**
     * @brief Trims tau_2 so the output phase at @p t is @p targetCycles modulo whole cycles (Phosphene's lock).
     * @param t            seconds after the trigger; <= 0 disables the trim
     * @param targetCycles wanted phase in cycles
     */
    void setPhaseTarget(double t, double targetCycles);

    /**
     * @brief Applies the amplitude constraints of Phosphene: the body floor (two periods of the end pitch above
     *        -20 dB), then the tail limit, if Tail Limit is below 0.
     * @param v           kick values indexed by kick:: (modified in place)
     * @param slotSeconds time from the kick to the point the tail limit concerns
     * @param keyRoot     pitch class of the key, for the tuned end pitch
     */
    static void constrain(float* v, double slotSeconds, int keyRoot);

    /**
     * @brief The end pitch for a key (PLAN 5.1, Dok. 4 and 8.1).
     *
     * KickTune::Key: the tonic, if an octave of it lies in 41 .. 62 Hz (E1 .. B1), else the fifth (C -> G1, C# ->
     * G#1, D -> A1, D# -> A#1). Fifth and FlatSeventh take that degree. Of a degree's octaves, the one nearest
     * @p targetHz.
     */
    static float tuneToKey(int keyRoot, int rule, float targetHz);

private:
    /** @brief Everything a kick needs, frozen at its trigger. */
    struct Shape {
        int    engine = 0;   ///< kick.engine
        double fe = 50.0, fs = 330.0, tau1 = 0.004, tau2 = 0.022, punch = 0.5;   ///< end and start pitch (Hz), the two sweep time constants (s), the punch share
        double attack = 10.0, hold = 576.0;     ///< samples
        double decayRate = -1e-4;               ///< ln(amplitude) per sample after the hold
        double damping = 0.9999;                ///< resonant engine radius per sample
    };
    /** @brief One sounding kick: its shape, its envelopes and where it stands. */
    struct Voice {
        bool   active = false;   ///< sounding
        Shape  s;                ///< the shape it was triggered with
        double late = 0.0;       ///< sub-sample start offset
        int    n = 0;            ///< samples since the trigger
        double e1 = 1.0, e2 = 1.0;   ///< e^(-t/tau_1), e^(-t/tau_2)
        double d1 = 1.0, d2 = 1.0;   ///< their per-sample factors
        float  click = 0.0f;     ///< the click layer's envelope
        float  velocity = 1.0f;  ///< 0..1
        double zRe = 0.0, zIm = 0.0;   ///< resonant phasor
    };
    /** @brief The top layer's voice: a sweep through a triangle, with its own decay. */
    struct Top {
        bool   active = false;   ///< sounding
        double fe = 35.0, fs = 200.0, tau1 = 0.003, tau2 = 0.02, punch = 0.5;   ///< its sweep
        double late = 0.0;       ///< sub-sample start offset
        int    n = 0;            ///< samples since the trigger
        double e1 = 1.0, e2 = 1.0, d1 = 1.0, d2 = 1.0;   ///< sweep envelopes
        double amp = 1.0, decay = 0.999;   ///< amplitude and its per-sample factor
        float  velocity = 1.0f;  ///< 0..1
    };

    /** @brief The body of one voice for one sample; the click layer's sample goes to @p click. */
    float voiceSample(Voice& v, float& click);
    /** @brief The top layer's sample (before its drive and high pass). */
    float topSample();
    /** @brief The shape a kick triggered now would get, from the current parameters. */
    Shape currentShape() const;
    /** @brief The sweep's phase at @p t seconds with body time constant @p tau2, in cycles. */
    double phaseWith(double t, double tau2) const;

    double sr_ = 48000.0;   ///< sample rate
    Voice  voice_, fade_;   ///< the sounding kick and the one fading out under it
    Top    top_;            ///< the top layer
    float  fadeGain_ = 0.0f, fadeStep_ = 0.0f;   ///< the fading kick's gain and its per-sample step

    // Settings from update().
    int    engine_ = 0;   ///< kick.engine
    float  endHz_ = 50.0f, startHz_ = 330.0f, punch_ = 0.5f;   ///< end pitch (tuned), start pitch, punch share
    double tau1_ = 0.004, tau2_ = 0.022, tau2Trimmed_ = 0.022;   ///< punch and body time constants, the body's after the trim
    double attackSamples_ = 10.0, holdSamples_ = 576.0, decaySeconds_ = 0.15;   ///< the amplitude envelope
    float  drive_ = 2.0f, driveNorm_ = 1.0f;   ///< drive into the clip and the gain that keeps the level
    int    clip_ = 0;   ///< kick.clip: soft (tanh) or hard
    float  clickLevel_ = 0.2f, clickDecay_ = 0.99f;   ///< the click layer's level and per-sample decay
    float  toneHz_ = 9000.0f;   ///< the tone low pass
    float  level_ = 1.0f;       ///< kick.level, linear
    float  lowCutHz_ = 30.0f;   ///< the EQ's high pass
    float  dipHz_ = 500.0f, dipGain_ = 1.0f;   ///< the EQ's peaking dip: centre and linear gain at it
    float  topLevel_ = 0.0f, topShift_ = 1.0f, topDrive_ = 2.0f, topNorm_ = 1.0f;   ///< the top layer's level (0 = off), pitch factor, drive and its normaliser
    double topDecay_ = 0.09;    ///< the top layer's decay (to -60 dB), s
    double lockT_ = 0.0, lockTarget_ = 0.0;   ///< the phase lock: seconds to the slot and the phase wanted there
    double trimKey_[10] = { -1.0 };           ///< inputs of the last trim solve

    Rng          noise_;   ///< the click's noise
    Svf          clickFilter_, toneFilter_;   ///< the click's band and the tone low pass
    TanhAdaa     tanh_, topTanh_;   ///< the soft clips of the body and of the top
    HardClipAdaa hard_;    ///< the hard clip
    DcBlocker    dc_;      ///< after the clip
    Svf          lowCut_, dip_;   ///< the EQ
    Svf          topHp1_, topHp2_;   ///< the top layer's fourth-order high pass
};

} // namespace parh
