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
    void kick(double late) { m_duck.trigger(late); }
    /** @brief Renders @p n samples into @p L and @p R (replaced). */
    void process(float* L, float* R, int n);
    /** @brief Whether anything sounds. */
    bool active() const { return m_amp.isActive(); }
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { m_mod.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { m_mod.setClock(m_pos, beat, beatsPerSample); }

private:
    /** @brief The matrix's sums at this sample (every 16th), onto the voice's settings. */
    void applyModulation();

    double m_sr = 48000.0;   ///< the sample rate, Hz
    VaOscillator m_osc;                 ///< at twice the rate
    double m_subPhase = 0.0;            ///< the square sub's phase, cycles
    FilterLane m_filt;   ///< the filter, at twice the rate
    HalfbandDesign m_hb;   ///< the half-band filter back down to the rate
    HalfbandDown<float> m_down;   ///< its state
    Envelope m_amp;   ///< the amp envelope
    double m_fenv = 0.0;   ///< the filter envelope (at the high rate)
    double m_fenvDecay = 0.999;   ///< its per-sample factor
    double m_hz = 110.0;   ///< the pitch, Hz
    double m_targetHz = 110.0;   ///< its target (a glide), Hz
    double m_glideCoef = 1.0;   ///< the glide's per-sample step
    bool m_slidePending = false;        ///< the last note slides into the next
    bool m_noteAccent = false;   ///< the note sounding is accented
    float m_velocity = 1.0f;   ///< the note's velocity
    Ducker m_duck;   ///< the duck under the kick
    Svf m_hp;   ///< the strip's low cut
    Svf m_lp;   ///< the strip's high cut
    /// Settings.
    FilterModelE m_model = FilterModelE::JUNO;
    float m_wave = 0.0f;   ///< the wave: 0 saw .. 1 pulse
    float m_pw = 0.5f;   ///< the pulse's width
    float m_sub = 0.3f;   ///< the square sub's level
    float m_cutoff = 350.0f;   ///< the filter's cutoff, Hz
    float m_res = 0.2f;   ///< the filter's resonance
    float m_envAmt = 2.0f;   ///< how far the filter envelope opens it, octaves
    float m_accent = 0.3f;   ///< the accent's amount
    float m_drive = 1.0f;   ///< the drive into the filter
    float m_driveNorm = 1.0f;   ///< the gain that keeps the level after it
    float m_keyTrack = 0.5f;   ///< the cutoff's key tracking, 0..1
    float m_level = 0.3f;   ///< the level, linear
    float m_gl = 0.7f;   ///< the pan's gain, left
    float m_gr = 0.7f;   ///< the pan's gain, right
    float m_glideMs = 0.0f;   ///< the glide, ms
    float m_attack = 0.003f;   ///< the amp envelope's attack, seconds
    float m_decay = 0.25f;   ///< its decay, seconds
    float m_sustain = 0.6f;   ///< its sustain, 0..1
    float m_release = 0.3f;   ///< its release, seconds
    double m_filtDecayS = 0.18;   ///< the filter envelope's decay, seconds
    /// The modulation (Phase 5b): the knobs' values and what the matrix makes of them now.
    NoteModulation<1> m_mod;
    int64_t m_pos = 0;                  ///< the voice's absolute sample count (the control grid)
    float m_driveKnob = 0.25f;   ///< the drive's knob (the matrix moves from it)
    float m_panKnob = 0.0f;   ///< the pan's knob
    float m_levelKnob = 0.3f;   ///< the level's knob
    float m_pwNow = 0.5f;   ///< the pulse width as the matrix leaves it
    float m_resNow = 0.2f;   ///< the resonance as the matrix leaves it
    float m_kNow = 0.0f;   ///< the filter's damping now
    float m_cutOct = 0.0f;   ///< the cutoff's offset from the matrix, octaves
    float m_envAdd = 0.0f;   ///< the filter envelope's depth added by the matrix
    double m_pitchMul = 1.0;   ///< the pitch's factor from the matrix
    double m_trackHz = -1.0;   ///< the pitch the key tracking was taken for (kept while the pitch stands)
    double m_track = 1.0;   ///< the key tracking's factor for that pitch
    float m_trackKt = -1.0f;                 ///< and the key tracking it was taken at
};

} // namespace parh
