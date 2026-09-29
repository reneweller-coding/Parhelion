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

constexpr int kTimpaniDrums = 4;
constexpr int kTimpaniModes = 32;

class Timpani {
public:
    void prepare(double sampleRate, uint64_t seed);
    void update(const float* v);
    void noteOn(int pitch, float velocity, bool unused, double late);
    void noteOff(int) {}
    void reset();
    void process(float* L, float* R, int n);
    /** @brief The frequency ratio of mode @p i to the note (the tests). */
    static double ratio(int i);
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { mod_.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { mod_.setClock(pos_, beat, beatsPerSample); }

private:
    struct Drum {
        bool on = false;
        int pitch = 45;
        double zr[kTimpaniModes] = {}, zi[kTimpaniModes] = {};
        double pr[kTimpaniModes] = {}, pi[kTimpaniModes] = {}, gr[kTimpaniModes] = {}, gi[kTimpaniModes] = {};
        double spr[kTimpaniModes] = {}, spi[kTimpaniModes] = {}, sgr[kTimpaniModes] = {}, sgi[kTimpaniModes] = {};
        double shape[kTimpaniModes] = {}, outL[kTimpaniModes] = {}, outR[kTimpaniModes] = {};
        bool contact = false;
        double yh = 0.0, vh = 0.0;
        int steps = 0, apart = 0;
        double age = 0.0;
        int quiet = 0;
        // The modulation's values (Phase 5b): the pedal's bend, the decay's factor, the felt, the drum's place.
        double bend = 1.0, decayMul = 1.0, K = 8e7, f0 = 110.0;
        double gainL = 1.0, gainR = 1.0;
    };
    void tune(Drum& d, int pitch, double bend = 1.0, double decayMul = 1.0);
    /** @brief Drum @p k's modulation now; @p strike: the mallet is about to meet it (the felt and the place). */
    void modDrum(int k, bool strike);
    double sr_ = 48000.0;
    uint64_t seed_ = 1;
    float level_ = 1.0f, hardness_ = 1.0f, decay_ = 1.0f, strike_ = 0.68f, width_ = 0.5f, lowCutHz_ = 35.0f;
    Svf lowCut_[2];
    Drum drum_[kTimpaniDrums];
    NoteModulation<kTimpaniDrums> mod_;   ///< the modulation, a Modulator per drum (Phase 5b)
    int64_t pos_ = 0;                     ///< the timpani's absolute sample count
};

} // namespace parh
