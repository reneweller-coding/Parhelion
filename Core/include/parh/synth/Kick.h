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
 * @author Rene Weller
 */
#pragma once
#include "parh/Adaa.h"
#include "parh/Dsp.h"

namespace parh {

/** @brief The kick drum synthesizer (one voice plus a fade slot, and the top layer). */
class Kick {
public:
    /**
     * @brief Prepares for a sample rate.
     * @param[in] sampleRate  the sample rate in Hz
     */
    void prepare(double sampleRate);
    /** @brief Silences and clears all state. */
    void reset();
    /**
     * @brief Reads the effective parameter values.
     * @param[in] v        values indexed by kick:: (after offsets and constraints)
     * @param[in] keyRoot  pitch class of the key (0 = C), used when Tune is not Free
     */
    void update(const float* v, int keyRoot);
    /**
     * @brief Starts a kick.
     * @param[in] velocity  0..1
     * @param[in] late      how many samples ago the kick ideally started (0 <= late < 1)
     */
    void trigger(float velocity, double late = 0.0);
    /**
     * @brief Renders @p n samples.
     * @param[out] out   receives the kick (replaced)
     * @param[out] body  receives what feeds the rumble: body and click after the EQ, without the top layer (may be
     *                   null)
     * @param[in]  n     samples
     */
    void process(float* out, float* body, int n);

    /**
     * @brief The end pitch actually used, after tuning.
     * @return the end pitch in Hz after tuning
     */
    float tunedEndHz() const { return m_endHz; }
    /**
     * @brief The body time constant the next kick uses, after the phase trim, in seconds.
     * @return the body time constant in seconds
     */
    double bodyTau() const { return m_tau2Trimmed; }
    /**
     * @brief Whether anything is sounding.
     * @return true while anything sounds
     */
    bool active() const { return m_voice.m_active || m_fade.m_active || m_top.m_active; }

    /**
     * @brief Output phase of a kick triggered with the current settings, @p t seconds after its ideal start.
     * @param[in] t  seconds after the kick's ideal start
     * @return phase in cycles (not wrapped)
     */
    double outputPhaseAt(double t) const;
    /**
     * @brief The constant c with outputPhaseAt(t) -> f_e t + c for large t, in cycles (see the file comment).
     * @return the constant c in cycles
     */
    double asymptoticPhase() const;
    /**
     * @brief Instantaneous frequency of the current settings at @p t seconds.
     * @param[in] t  seconds after the kick's start
     * @return the frequency in Hz
     */
    double frequencyAt(double t) const;
    /**
     * @brief Phase in cycles the chain after the oscillator adds at @p hz (saturator, tone, DC blocker, EQ).
     * @param[in] hz  the frequency in Hz
     * @return the phase the chain adds there, in cycles
     */
    double chainPhase(double hz) const;
    /**
     * @brief Trims tau_2 so the output phase at @p t is @p targetCycles modulo whole cycles (Phosphene's lock).
     * @param[in] t             seconds after the trigger; <= 0 disables the trim
     * @param[in] targetCycles  wanted phase in cycles
     */
    void setPhaseTarget(double t, double targetCycles);

    /**
     * @brief Applies the amplitude constraints of Phosphene: the body floor (two periods of the end pitch above
     *        -20 dB), then the tail limit, if Tail Limit is below 0.
     * @param[in,out] v            kick values indexed by kick:: (modified in place)
     * @param[in]     slotSeconds  time from the kick to the point the tail limit concerns
     * @param[in]     keyRoot      pitch class of the key, for the tuned end pitch
     */
    static void constrain(float* v, double slotSeconds, int keyRoot);

    /**
     * @brief The end pitch for a key (PLAN 5.1, Dok. 4 and 8.1).
     *
     * KickTune::Key: the tonic, if an octave of it lies in 41 .. 62 Hz (E1 .. B1), else the fifth (C -> G1, C# ->
     * G#1, D -> A1, D# -> A#1). Fifth and FlatSeventh take that degree. Of a degree's octaves, the one nearest
     * @p targetHz.
     * @param[in] keyRoot   the key's root, 0 .. 11
     * @param[in] rule      the tuning rule (KickTune)
     * @param[in] targetHz  the end pitch the free rule keeps, in Hz
     * @return the end pitch in Hz
     */
    static float tuneToKey(int keyRoot, int rule, float targetHz);

private:
    /** @brief Everything a kick needs, frozen at its trigger. */
    struct Shape {
        int    m_engine = 0;   ///< kick.engine
        double m_fe = 50.0;   ///< end pitch, Hz
        double m_fs = 330.0;   ///< start pitch, Hz
        double m_tau1 = 0.004;   ///< the punch's sweep time constant, s
        double m_tau2 = 0.022;   ///< the body's sweep time constant, s
        double m_punch = 0.5;   ///< the punch's share
        double m_attack = 10.0;   ///< the attack, samples
        double m_hold = 576.0;   ///< the hold, samples
        double m_decayRate = -1e-4;               ///< ln(amplitude) per sample after the hold
        double m_damping = 0.9999;                ///< resonant engine radius per sample
    };
    /** @brief One sounding kick: its shape, its envelopes and where it stands. */
    struct Voice {
        bool   m_active = false;   ///< sounding
        Shape  m_s;                ///< the shape it was triggered with
        double m_late = 0.0;       ///< sub-sample start offset
        int    m_n = 0;            ///< samples since the trigger
        double m_e1 = 1.0;   ///< e^(-t/tau_1)
        double m_e2 = 1.0;   ///< e^(-t/tau_2)
        double m_d1 = 1.0;   ///< e1's per-sample factor
        double m_d2 = 1.0;   ///< e2's per-sample factor
        float  m_click = 0.0f;     ///< the click layer's envelope
        float  m_velocity = 1.0f;  ///< 0..1
        double m_zRe = 0.0;   ///< the resonant phasor's real part
        double m_zIm = 0.0;   ///< the resonant phasor's imaginary part
    };
    /** @brief The top layer's voice: a sweep through a triangle, with its own decay. */
    struct Top {
        bool   m_active = false;   ///< sounding
        double m_fe = 35.0;   ///< end pitch, Hz
        double m_fs = 200.0;   ///< start pitch, Hz
        double m_tau1 = 0.003;   ///< the punch's sweep time constant, s
        double m_tau2 = 0.02;   ///< the body's sweep time constant, s
        double m_punch = 0.5;   ///< its sweep
        double m_late = 0.0;       ///< sub-sample start offset
        int    m_n = 0;            ///< samples since the trigger
        double m_e1 = 1.0;   ///< e^(-t/tau_1)
        double m_e2 = 1.0;   ///< e^(-t/tau_2)
        double m_d1 = 1.0;   ///< e1's per-sample factor
        double m_d2 = 1.0;   ///< sweep envelopes
        double m_amp = 1.0;   ///< the amplitude
        double m_decay = 0.999;   ///< the amplitude's per-sample factor
        float  m_velocity = 1.0f;  ///< 0..1
    };

    /**
     * @brief The body of one voice for one sample; the click layer's sample goes to @p click.
     * @param[in,out] v      the voice
     * @param[out]    click  receives the click layer's sample
     * @return the body's sample
     */
    float voiceSample(Voice& v, float& click);
    /**
     * @brief The top layer's sample (before its drive and high pass).
     * @return the top layer's sample
     */
    float topSample();
    /**
     * @brief The shape a kick triggered now would get, from the current parameters.
     * @return the shape a kick triggered now would get
     */
    Shape currentShape() const;
    /**
     * @brief The sweep's phase at @p t seconds with body time constant @p tau2, in cycles.
     * @param[in] t     seconds after the kick's start
     * @param[in] tau2  the body time constant in seconds
     * @return the sweep's phase in cycles
     */
    double phaseWith(double t, double tau2) const;

    double m_sr = 48000.0;   ///< sample rate
    Voice  m_voice;   ///< the sounding kick
    Voice  m_fade;   ///< the kick fading out under it
    Top    m_top;            ///< the top layer
    float  m_fadeGain = 0.0f;   ///< the fading kick's gain
    float  m_fadeStep = 0.0f;   ///< its per-sample step

    // Settings from update().
    int    m_engine = 0;   ///< kick.engine
    float  m_endHz = 50.0f;   ///< end pitch (tuned), Hz
    float  m_startHz = 330.0f;   ///< start pitch, Hz
    float  m_punch = 0.5f;   ///< the punch's share
    double m_tau1 = 0.004;   ///< the punch's time constant, s
    double m_tau2 = 0.022;   ///< the body's time constant, s
    double m_tau2Trimmed = 0.022;   ///< the body's time constant after the phase trim, s
    double m_attackSamples = 10.0;   ///< the amplitude envelope's attack, samples
    double m_holdSamples = 576.0;   ///< its hold, samples
    double m_decaySeconds = 0.15;   ///< its decay, seconds
    float  m_drive = 2.0f;   ///< drive into the clip
    float  m_driveNorm = 1.0f;   ///< the gain that keeps the level after the clip
    int    m_clip = 0;   ///< kick.clip: soft (tanh) or hard
    float  m_clickLevel = 0.2f;   ///< the click layer's level
    float  m_clickDecay = 0.99f;   ///< the click layer's per-sample decay
    float  m_toneHz = 9000.0f;   ///< the tone low pass
    float  m_level = 1.0f;       ///< kick.level, linear
    float  m_lowCutHz = 30.0f;   ///< the EQ's high pass
    float  m_dipHz = 500.0f;   ///< the EQ's peaking dip: its centre, Hz
    float  m_dipGain = 1.0f;   ///< the EQ's peaking dip: its linear gain at the centre
    float  m_topLevel = 0.0f;   ///< the top layer's level (0 = off)
    float  m_topShift = 1.0f;   ///< the top layer's pitch factor
    float  m_topDrive = 2.0f;   ///< the top layer's drive
    float  m_topNorm = 1.0f;   ///< the top layer's normaliser after its drive
    double m_topDecay = 0.09;    ///< the top layer's decay (to -60 dB), s
    double m_lockT = 0.0;   ///< the phase lock: seconds from the trigger to the slot
    double m_lockTarget = 0.0;   ///< the phase lock: the phase wanted at the slot, cycles
    double m_trimKey[10] = { -1.0 };           ///< inputs of the last trim solve

    Rng          m_noise;   ///< the click's noise
    Svf          m_clickFilter;   ///< the click's band
    Svf          m_toneFilter;   ///< the tone low pass
    TanhAdaa     m_tanh;   ///< the body's soft clip
    TanhAdaa     m_topTanh;   ///< the top layer's soft clip
    HardClipAdaa m_hard;    ///< the hard clip
    DcBlocker    m_dc;      ///< after the clip
    Svf          m_lowCut;   ///< the EQ's high pass
    Svf          m_dip;   ///< the EQ's peaking dip
    Svf          m_topHp1;   ///< the top layer's high pass, first stage
    Svf          m_topHp2;   ///< the top layer's high pass, second stage
};

} // namespace parh
