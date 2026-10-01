/**
 * @file SubBass.h
 * @brief The sub bass (PLAN 5.3): a sine that starts every note in the kick's phase, under a duck from the kick.
 *
 * In a track whose low end the sub owns (compose.low_owner = Sub, the Dub profile's default), the rumble loses its
 * sine and this voice plays the bass line's fundamental: a sine, never below 35 Hz, low-passed at 80 to 120 Hz, mono
 * (Dok. 8.4). The kick's tail and the sub overlap in the first sixteenths after every kick, and there they must add.
 *
 * **Kick lock.** The engine hands every note the kick's phase at the note's ideal start -- f_k t + c, from
 * Kick::asymptoticPhase() -- and the note starts at r times it, r = f_note / f_k. For a note at the kick's pitch (the
 * tonic under a tonic kick, the most common case) the two are then one sine; for its octaves the zero crossings meet
 * at the start; for other intervals only the start is aligned, which is all a phase can do.
 *
 * **Duck** event-driven (Ducker.h): 6 to 12 dB, a short hold, a raised-cosine return of 150 to 350 ms (Dok. 8.7).
 * **Envelope:** Dsp.h's ADSR with the sub-sample onset of Phosphene; the release never shorter than half a period
 * (Phosphene's release floor), so a note's end does not click.
 * @note Copied from Totality `Core/include/tot/synth/SubBass.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#pragma once
#include "parh/Adaa.h"
#include "parh/Dsp.h"
#include "parh/mix/Ducker.h"

namespace parh {

/** @brief The sub bass voice (monophonic). */
class SubBass {
public:
    /** @brief Prepares for a sample rate. */
    void prepare(double sampleRate);
    /** @brief Silence. */
    void reset();
    /** @brief Reads the effective parameter values (indexed by sub::). */
    void update(const float* v);
    /**
     * @brief Starts a note.
     * @param pitch    MIDI note (before sub.octave)
     * @param velocity 0..1
     * @param late     samples since its ideal start (0 <= late < 1)
     * @param phase    the phase to start at, in cycles, or a negative value for "free" (the running phase)
     */
    void noteOn(int pitch, float velocity, double late, double phase);
    /** @brief Releases the note. */
    void noteOff();
    /** @brief A kick starts: the duck. */
    void kick(double late) { duck_.trigger(late); }
    /** @brief Renders @p n samples (replacing @p out, mono). */
    void process(float* out, int n);
    /** @brief The frequency a note of @p pitch plays at, after the octave setting. */
    double noteHz(int pitch) const;
    /** @brief Whether sub.lock asks for the kick's phase. */
    bool locked() const { return lock_; }
    /**
     * @brief Phase in cycles the voice's chain adds at @p hz (half a sample of ADAA, the low pass): a locked note starts
     *        this much earlier in its cycle, so that what leaves the voice is in the kick's phase.
     */
    double chainPhase(double hz) const;
    /** @brief Whether anything sounds. */
    bool active() const { return env_.isActive(); }

private:
    double sr_ = 48000.0;   ///< the sample rate, Hz
    Envelope env_;           ///< the amplitude envelope
    Ducker duck_;            ///< the kick's duck
    TanhAdaa sat_;           ///< the drive
    Svf lp_;                 ///< the low pass
    double phase_ = 0.0;   ///< the phase, cycles
    double inc_ = 0.0;   ///< the phase step per sample, cycles
    float velocity_ = 1.0f;   ///< the note's velocity
    float level_ = 0.5f;   ///< sub.level, linear
    float drive_ = 1.0f;   ///< the drive
    float driveNorm_ = 1.0f;   ///< the gain that keeps the level after it
    float attackS_ = 0.003f;   ///< the envelope's attack, seconds
    float decayS_ = 0.25f;   ///< its decay, seconds
    float sustain_ = 0.7f;   ///< its sustain, 0..1
    float releaseS_ = 0.09f;   ///< its release, seconds
    float lpHz_ = 110.0f;    ///< the low pass's corner
    int octave_ = 0;   ///< octaves the notes are moved by
    bool lock_ = true;   ///< the phase is locked to the kick's (sub.lock)
};

} // namespace parh
