/**
 * @file Synth.h
 * @brief The mono synth (PLAN 5.3): the bass in the SH-101 idiom and the 303 line, one voice each, one parameter table.
 *
 * Signal path, at twice the sample rate where anything is nonlinear:
 * @code
 *   PolyBLEP saw..pulse + square sub an octave down -> tanh drive -> circuit filter (Filters.h, Ephemeris' models)
 *   -> half-band decimator -> high pass (Low Cut) -> low pass (High Cut) -> amp envelope -> duck -> level, pan
 * @endcode
 * **The filter** is one of Ephemeris' circuit models, solved sample by sample: the Juno's IR3109 cascade by default
 * (the SH-101 has the same chip), the diode ladder for the 303 line. Its cutoff is Cutoff times 2^(Env Amount x env) times
 * the key tracking, the envelope an exponential Decay retriggered by every note that does not slide.
 *
 * **Accent** (the 303's): an accented note opens the envelope further (Accent times an octave and a half) and plays
 * louder (Accent times 4 dB). **Slide**: the note after a slid one does not retrigger either envelope and glides to its
 * pitch in Glide ms (or 60 ms when Glide is 0); a plain Glide above 0 glides every note (portamento).
 *
 * **Low end** (PLAN 5.2): where the rumble owns the band under the split, the engine raises the voice's Low Cut to at
 * least 100 Hz, so the bass carries its harmonics above the kick's fundamental and never its own fundamental under it.
 * @note Copied from Totality `Core/include/tot/synth/Synth.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Halfband.h"
#include "parh/mix/Ducker.h"
#include "parh/synth/Filters.h"
#include "parh/synth/Oscillator.h"

namespace parh {

/** @brief One monophonic synth voice with its strip. */
class MonoSynth {
public:
    /** @brief Prepares for a sample rate. */
    void prepare(double sampleRate);
    /** @brief Silence. */
    void reset();
    /**
     * @brief Reads the effective parameter values (indexed by synth::).
     * @param minLowCut the lowest Low Cut the engine allows now (0: none; 100 Hz where the rumble owns the low end)
     */
    void update(const float* v, float minLowCut);
    /**
     * @brief Starts a note.
     * @param pitch    MIDI note
     * @param velocity 0..1
     * @param late     samples since its ideal start (0 <= late < 1)
     * @param accent   the 303's accent
     * @param slide    this note slides into the next (the next neither retriggers nor jumps)
     */
    void noteOn(int pitch, float velocity, double late, bool accent, bool slide);
    /** @brief Releases the note (unless a slid note has taken over). */
    void noteOff();
    /** @brief A kick starts: the duck. */
    void kick(double late) { duck_.trigger(late); }
    /** @brief Renders @p n samples into @p L and @p R (replaced). */
    void process(float* L, float* R, int n);
    /** @brief Whether anything sounds. */
    bool active() const { return amp_.isActive(); }

private:
    double sr_ = 48000.0;
    VaOscillator osc_;                 ///< at twice the rate
    double subPhase_ = 0.0;            ///< the square sub's phase, cycles
    FilterLane filt_;
    HalfbandDesign hb_;
    HalfbandDown<float> down_;
    Envelope amp_;
    double fenv_ = 0.0, fenvDecay_ = 0.999;   ///< the filter envelope and its per-sample factor (at the high rate)
    double hz_ = 110.0, targetHz_ = 110.0, glideCoef_ = 1.0;   ///< the pitch, its target, the glide's per-sample step
    bool slidePending_ = false;        ///< the last note slides into the next
    bool noteAccent_ = false;
    float velocity_ = 1.0f;
    Ducker duck_;
    Svf hp_, lp_;
    // Settings.
    FilterModel model_ = FilterModel::Juno;
    float wave_ = 0.0f, pw_ = 0.5f, sub_ = 0.3f, cutoff_ = 350.0f, res_ = 0.2f, envAmt_ = 2.0f, accent_ = 0.3f;
    float drive_ = 1.0f, driveNorm_ = 1.0f, keyTrack_ = 0.5f, level_ = 0.3f, gl_ = 0.7f, gr_ = 0.7f;
    float glideMs_ = 0.0f, attack_ = 0.003f, decay_ = 0.25f, sustain_ = 0.6f, release_ = 0.3f;
    double filtDecayS_ = 0.18;
};

} // namespace parh
