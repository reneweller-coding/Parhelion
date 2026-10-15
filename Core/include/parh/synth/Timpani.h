/**
 * @file Timpani.h
 * @brief The timpani (PLAN 5.9, Cinematic): a kettle drum's membrane in modes, struck by a felt mallet.
 *
 * **The membrane**: the modes (m, n) of a circular membrane, their frequencies as the kettle's air and the air around
 * make them (Rossing, "Science of Percussion Instruments", 2000; Fletcher and Rossing 1998): the preferred modes
 * (1,1), (2,1), (3,1), (4,1), (5,1) near 1 : 1.5 : 1.98 : 2.44 : 2.9 -- the near-harmonic series that gives the drum
 * its pitch, the note being (1,1) -- and the others ((0,1), (0,2), (1,2), (2,2), (0,3), (3,2), (6,1)) where the air
 * leaves them, short-lived because they radiate well or couple to the kettle; above them the ideal membrane's modes up
 * to about six times the note (the air lowers (1,1) by about 11 % and the high modes hardly, so their ratios are the
 * ideal ones times 1.12), short and radiating little, which is where the mallet's hardness shows. Every mode is a
 * complex one-pole as in
 * the piano (Piano.h); its shape at the strike point is J_m(j_mn r / a) (Bessel functions of the first kind, the zeros
 * j_mn), its radiation a monopole's for m = 0 and a dipole's and above for the rest, left and right by the angle.
 *
 * **The mallet** (Rhaouti, Chaigne and Joly, JASA 105(6), 1999, for the physics): a felt head of 20 g on the
 * membrane, the felt a power-law spring F = K u^2.3 (timpani.hardness), on four sub-steps while it touches, as the
 * piano's hammer; a harder mallet or a stronger stroke is brighter by the contact's shortness, not by a filter.
 *
 * **The drums**: four (the timpanist's set), each tuned to the note it plays last; a roll is strokes in quick
 * succession on the ringing membrane.
 * @author Rene Weller
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/synth/Modulation.h"
#include <cstdint>

namespace parh {

constexpr int TimpaniDrums = 4;   ///< drums in the set
constexpr int TimpaniModes = 32;   ///< modes of a drum's membrane

/** @brief The timpani: four kettle drums, modal membranes struck by a felt mallet (see the file comment). */
class Timpani {
public:
    /**
     * @brief Sets the sample rate and the seed, and falls silent.
     * @param[in] sampleRate  the sample rate
     * @param[in] seed        the seed
     */
    void prepare(double sampleRate, uint64_t seed);
    /**
     * @brief Reads the effective parameter values (indexed by timpani::).
     * @param[in] v  the effective parameter values
     */
    void update(const float* v);
    /**
     * @brief Strikes the drum for @p pitch (tuned to it) at @p velocity, @p late samples ago; @p unused is not read.
     * @param[in] pitch     the MIDI note
     * @param[in] velocity  the velocity, 0..1
     * @param[in] unused    not read
     * @param[in] late      samples ago
     */
    void noteOn(int pitch, float velocity, bool unused, double late);
    /** @brief Nothing: a drum rings out. */
    void noteOff(int) {}
    /** @brief Silence: every drum still. */
    void reset();
    /**
     * @brief Renders @p n samples, replacing @p L and @p R.
     * @param[out] L  the left channel
     * @param[out] R  the right channel
     * @param[in]  n  the block's samples
     */
    void process(float* L, float* R, int n);
    /**
     * @brief The frequency ratio of mode @p i to the note (the tests).
     * @param[in] i  the mode
     * @return its frequency ratio to the note
     */
    static double ratio(int i);
    /**
     * @brief The deck's shared modulation sources (Modulation.h).
     * @param[in] g  the shared sources
     */
    void setModGlobals(const ModGlobals& g) { m_mod.setGlobals(g); }
    /**
     * @brief The beat now and the beats per sample (the synced LFOs).
     * @param[in] beat            the beat now
     * @param[in] beatsPerSample  the beats per sample
     */
    void setClock(double beat, double beatsPerSample) { m_mod.setClock(m_pos, beat, beatsPerSample); }

private:
    /** @brief One drum: its membrane's modes, its mallet, its modulation. */
    struct Drum {
        bool m_on = false;   ///< it rings
        int m_pitch = 45;   ///< the note it is tuned to
        double m_zr[TimpaniModes] = {};   ///< the modes' states: real part
        double m_zi[TimpaniModes] = {};   ///< ... imaginary part
        double m_pr[TimpaniModes] = {};   ///< their poles per sample: real part
        double m_pi[TimpaniModes] = {};   ///< ... imaginary part
        double m_gr[TimpaniModes] = {};   ///< their input gains per sample: real part
        double m_gi[TimpaniModes] = {};   ///< ... imaginary part
        double m_spr[TimpaniModes] = {};   ///< their poles per sub-step of the contact: real part
        double m_spi[TimpaniModes] = {};   ///< ... imaginary part
        double m_sgr[TimpaniModes] = {};   ///< their input gains per sub-step: real part
        double m_sgi[TimpaniModes] = {};   ///< ... imaginary part
        double m_shape[TimpaniModes] = {};   ///< the modes' shapes at the strike point
        double m_outL[TimpaniModes] = {};   ///< the modes' radiation, left
        double m_outR[TimpaniModes] = {};   ///< the modes' radiation, right
        bool m_contact = false;   ///< the mallet touches the membrane
        double m_yh = 0.0;   ///< the mallet's displacement
        double m_vh = 0.0;   ///< the mallet's velocity
        int m_steps = 0;   ///< sub-steps since the contact began
        int m_apart = 0;   ///< sub-steps the felt has been apart in a row
        double m_age = 0.0;   ///< seconds since the stroke
        int m_quiet = 0;   ///< cells it has been silent (it stops after a few)
        // The modulation's values (Phase 5b): the pedal's bend, the decay's factor, the felt, the drum's place.
        double m_bend = 1.0;   ///< the pedal's bend, a factor on the pitch
        double m_decayMul = 1.0;   ///< the decay's factor
        double m_K = 8e7;   ///< the felt's stiffness
        double m_f0 = 110.0;   ///< the pitch, Hz
        double m_gainL = 1.0;   ///< the drum's place: gain left
        double m_gainR = 1.0;   ///< ... gain right
    };
    /**
     * @brief Tunes drum @p d to @p pitch, times @p bend, its decays times @p decayMul: the poles, the gains, the
     *        shapes.
     * @param[in,out] d         the drum
     * @param[in]     pitch     the MIDI note
     * @param[in]     bend      the frequency factor
     * @param[in]     decayMul  the decays' factor
     */
    void tune(Drum& d, int pitch, double bend = 1.0, double decayMul = 1.0);
    /**
     * @brief Drum @p k's modulation now; @p strike: the mallet is about to meet it (the felt and the place).
     * @param[in] k       the drum
     * @param[in] strike  true when the mallet is about to meet it
     */
    void modDrum(int k, bool strike);
    double m_sr = 48000.0;   ///< the sample rate, Hz
    uint64_t m_seed = 1;   ///< the seed
    float m_level = 1.0f;   ///< timpani.level, linear
    float m_hardness = 1.0f;   ///< the felt's hardness
    float m_decay = 1.0f;   ///< the decay's factor
    float m_strike = 0.68f;   ///< the strike point, r / a
    float m_width = 0.5f;   ///< the width
    float m_lowCutHz = 35.0f;   ///< the low cut, Hz
    Svf m_lowCut[2];   ///< the low cut, per channel
    Drum m_drum[TimpaniDrums];   ///< the drums
    NoteModulation<TimpaniDrums> m_mod;   ///< the modulation, a Modulator per drum (Phase 5b)
    int64_t m_pos = 0;                     ///< the timpani's absolute sample count
};

} // namespace parh
