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
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/synth/Modulation.h"
#include <cstdint>

namespace parh {

constexpr int kTimpaniDrums = 4;   ///< drums in the set
constexpr int kTimpaniModes = 32;   ///< modes of a drum's membrane

/** @brief The timpani: four kettle drums, modal membranes struck by a felt mallet (see the file comment). */
class Timpani {
public:
    /** @brief Sets the sample rate and the seed, and falls silent. */
    void prepare(double sampleRate, uint64_t seed);
    /** @brief Reads the effective parameter values (indexed by timpani::). */
    void update(const float* v);
    /** @brief Strikes the drum for @p pitch (tuned to it) at @p velocity, @p late samples ago; @p unused is not read. */
    void noteOn(int pitch, float velocity, bool unused, double late);
    /** @brief Nothing: a drum rings out. */
    void noteOff(int) {}
    /** @brief Silence: every drum still. */
    void reset();
    /** @brief Renders @p n samples, replacing @p L and @p R. */
    void process(float* L, float* R, int n);
    /** @brief The frequency ratio of mode @p i to the note (the tests). */
    static double ratio(int i);
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { mod_.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { mod_.setClock(pos_, beat, beatsPerSample); }

private:
    /** @brief One drum: its membrane's modes, its mallet, its modulation. */
    struct Drum {
        bool on = false;   ///< it rings
        int pitch = 45;   ///< the note it is tuned to
        double zr[kTimpaniModes] = {};   ///< the modes' states: real part
        double zi[kTimpaniModes] = {};   ///< ... imaginary part
        double pr[kTimpaniModes] = {};   ///< their poles per sample: real part
        double pi[kTimpaniModes] = {};   ///< ... imaginary part
        double gr[kTimpaniModes] = {};   ///< their input gains per sample: real part
        double gi[kTimpaniModes] = {};   ///< ... imaginary part
        double spr[kTimpaniModes] = {};   ///< their poles per sub-step of the contact: real part
        double spi[kTimpaniModes] = {};   ///< ... imaginary part
        double sgr[kTimpaniModes] = {};   ///< their input gains per sub-step: real part
        double sgi[kTimpaniModes] = {};   ///< ... imaginary part
        double shape[kTimpaniModes] = {};   ///< the modes' shapes at the strike point
        double outL[kTimpaniModes] = {};   ///< the modes' radiation, left
        double outR[kTimpaniModes] = {};   ///< the modes' radiation, right
        bool contact = false;   ///< the mallet touches the membrane
        double yh = 0.0;   ///< the mallet's displacement
        double vh = 0.0;   ///< the mallet's velocity
        int steps = 0;   ///< sub-steps since the contact began
        int apart = 0;   ///< sub-steps the felt has been apart in a row
        double age = 0.0;   ///< seconds since the stroke
        int quiet = 0;   ///< cells it has been silent (it stops after a few)
        // The modulation's values (Phase 5b): the pedal's bend, the decay's factor, the felt, the drum's place.
        double bend = 1.0;   ///< the pedal's bend, a factor on the pitch
        double decayMul = 1.0;   ///< the decay's factor
        double K = 8e7;   ///< the felt's stiffness
        double f0 = 110.0;   ///< the pitch, Hz
        double gainL = 1.0;   ///< the drum's place: gain left
        double gainR = 1.0;   ///< ... gain right
    };
    /** @brief Tunes drum @p d to @p pitch, times @p bend, its decays times @p decayMul: the poles, the gains, the shapes. */
    void tune(Drum& d, int pitch, double bend = 1.0, double decayMul = 1.0);
    /** @brief Drum @p k's modulation now; @p strike: the mallet is about to meet it (the felt and the place). */
    void modDrum(int k, bool strike);
    double sr_ = 48000.0;   ///< the sample rate, Hz
    uint64_t seed_ = 1;   ///< the seed
    float level_ = 1.0f;   ///< timpani.level, linear
    float hardness_ = 1.0f;   ///< the felt's hardness
    float decay_ = 1.0f;   ///< the decay's factor
    float strike_ = 0.68f;   ///< the strike point, r / a
    float width_ = 0.5f;   ///< the width
    float lowCutHz_ = 35.0f;   ///< the low cut, Hz
    Svf lowCut_[2];   ///< the low cut, per channel
    Drum drum_[kTimpaniDrums];   ///< the drums
    NoteModulation<kTimpaniDrums> mod_;   ///< the modulation, a Modulator per drum (Phase 5b)
    int64_t pos_ = 0;                     ///< the timpani's absolute sample count
};

} // namespace parh
