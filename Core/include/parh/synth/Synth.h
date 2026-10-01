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
 *
 * **Modulation** (Phase 5b, Modulation.h): the block's core (the modulation envelope, four LFOs, eight slots) on the
 * pitch, the pulse width, the cutoff, the resonance, the envelope's amount, the drive, the level and the pan, read every
 * 16 samples of the voice's own absolute count (so the host's blocks never move it); the filter envelope is a source.
 * @note Copied from Totality `Core/include/tot/synth/Synth.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Halfband.h"
#include "parh/mix/Ducker.h"
#include "parh/synth/Filters.h"
#include "parh/synth/Modulation.h"
#include "parh/synth/Oscillator.h"

namespace parh {

/** @brief One monophonic synth voice with its strip. */
class MonoSynth {
public:
    /** @brief Prepares for a sample rate; @p seed: the modulation's random stream. */
    void prepare(double sampleRate, uint64_t seed = 0x4D4F4E4Full);
    /** @brief Silence. */
    void reset();
    /**
     * @brief Reads the effective parameter values (indexed by synth::).
     * @param minLowCut the lowest Low Cut the engine allows now (0: none; 100 Hz where the rumble owns the low end)
     */
    void update(const float* v, float minLowCut);
    /**
     * @brief The part of update() that is not a function of the values: the modulated values back to the knobs', and
     *        the matrix applied again where a note sounds. update() ends with it; the deck calls it alone at a cell whose
     *        values did not change (Deck.h, the cell cache), so every cell does what it did before the cache.
     */
    void refreshModulation();
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
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { mod_.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { mod_.setClock(pos_, beat, beatsPerSample); }

private:
    /** @brief The matrix's sums at this sample (every 16th), onto the voice's settings. */
    void applyModulation();

    double sr_ = 48000.0;   ///< the sample rate, Hz
    VaOscillator osc_;                 ///< at twice the rate
    double subPhase_ = 0.0;            ///< the square sub's phase, cycles
    FilterLane filt_;   ///< the filter, at twice the rate
    HalfbandDesign hb_;   ///< the half-band filter back down to the rate
    HalfbandDown<float> down_;   ///< its state
    Envelope amp_;   ///< the amp envelope
    double fenv_ = 0.0;   ///< the filter envelope (at the high rate)
    double fenvDecay_ = 0.999;   ///< its per-sample factor
    double hz_ = 110.0;   ///< the pitch, Hz
    double targetHz_ = 110.0;   ///< its target (a glide), Hz
    double glideCoef_ = 1.0;   ///< the glide's per-sample step
    bool slidePending_ = false;        ///< the last note slides into the next
    bool noteAccent_ = false;   ///< the note sounding is accented
    float velocity_ = 1.0f;   ///< the note's velocity
    Ducker duck_;   ///< the duck under the kick
    Svf hp_;   ///< the strip's low cut
    Svf lp_;   ///< the strip's high cut
    /// Settings.
    FilterModel model_ = FilterModel::Juno;
    float wave_ = 0.0f;   ///< the wave: 0 saw .. 1 pulse
    float pw_ = 0.5f;   ///< the pulse's width
    float sub_ = 0.3f;   ///< the square sub's level
    float cutoff_ = 350.0f;   ///< the filter's cutoff, Hz
    float res_ = 0.2f;   ///< the filter's resonance
    float envAmt_ = 2.0f;   ///< how far the filter envelope opens it, octaves
    float accent_ = 0.3f;   ///< the accent's amount
    float drive_ = 1.0f;   ///< the drive into the filter
    float driveNorm_ = 1.0f;   ///< the gain that keeps the level after it
    float keyTrack_ = 0.5f;   ///< the cutoff's key tracking, 0..1
    float level_ = 0.3f;   ///< the level, linear
    float gl_ = 0.7f;   ///< the pan's gain, left
    float gr_ = 0.7f;   ///< the pan's gain, right
    float glideMs_ = 0.0f;   ///< the glide, ms
    float attack_ = 0.003f;   ///< the amp envelope's attack, seconds
    float decay_ = 0.25f;   ///< its decay, seconds
    float sustain_ = 0.6f;   ///< its sustain, 0..1
    float release_ = 0.3f;   ///< its release, seconds
    double filtDecayS_ = 0.18;   ///< the filter envelope's decay, seconds
    /// The modulation (Phase 5b): the knobs' values and what the matrix makes of them now.
    NoteModulation<1> mod_;
    int64_t pos_ = 0;                  ///< the voice's absolute sample count (the control grid)
    float driveKnob_ = 0.25f;   ///< the drive's knob (the matrix moves from it)
    float panKnob_ = 0.0f;   ///< the pan's knob
    float levelKnob_ = 0.3f;   ///< the level's knob
    float pwNow_ = 0.5f;   ///< the pulse width as the matrix leaves it
    float resNow_ = 0.2f;   ///< the resonance as the matrix leaves it
    float kNow_ = 0.0f;   ///< the filter's damping now
    float cutOct_ = 0.0f;   ///< the cutoff's offset from the matrix, octaves
    float envAdd_ = 0.0f;   ///< the filter envelope's depth added by the matrix
    double pitchMul_ = 1.0;   ///< the pitch's factor from the matrix
    double trackHz_ = -1.0;   ///< the pitch the key tracking was taken for (kept while the pitch stands)
    double track_ = 1.0;   ///< the key tracking's factor for that pitch
    float trackKt_ = -1.0f;                 ///< and the key tracking it was taken at
};

} // namespace parh
