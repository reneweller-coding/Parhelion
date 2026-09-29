/**
 * @file Kit.h
 * @brief The percussion kit (PLAN 5.4): twelve lanes of Phosphene's universal percussion voice (PercKernel.h), each
 *        with its own output, and the 909's metal table as a noise source.
 *
 * The kit turns lane parameters into kernel coefficients, starts hits (with sub-sample onsets, pitch shifts, chokes
 * and clap bursts) and renders the lanes in registers: two AVX2 registers or three NEON registers for the twelve
 * lanes. Everything that is not a per-sample operation on all lanes -- noise generation, the metal table, burst
 * schedules, triggers -- is scalar and shared by every vector path.
 *
 * **Lane outputs** (Totality). Phosphene summed the lanes; here every lane's stereo output stays readable after
 * processLanes(), so the engine can route the hats and the percussion to buses of their own (the perc bus's low pass
 * is a ramp target, PLAN 7.3) and send single lanes to the dub delay later.
 *
 * **The 909's metal table** (Totality, PLAN 5.4). The TR-909's hats and cymbals are six-bit samples of real cymbals,
 * played through an analogue VCA and filter, with a tuning range of 0.43 to 1.7 times (Dok. 3). Totality samples nothing:
 * the table is made in prepare() from a fixed seed -- 60 inharmonic partials, log-uniform between 2.8 and 17 kHz, each
 * with its own decay of a quarter to one and a quarter seconds, a little decaying white noise under them -- normalised
 * and quantised to 64 levels, the six bits that give the 909 its grit. A lane with Noise Type = 909 Metal reads it
 * instead of white noise, from its start at every hit, at Metal Scale times its own rate (48 kHz), linearly
 * interpolated; the lane's noise envelope is the 909's VCA and its filter the 909's filter.
 *
 * **Tuning.** A lane with Tune to Key moves its pitch to the nearest note of the current key and scale.
 *
 * **The auto-pan** (Phosphene, 16.09.2026): each lane swings around its position with a period of three sixteenths,
 * the two phase groups by role; see Phosphene's Perc.h for the algebra that keeps the kit's width.
 *
 * @note Adapted from Phosphene `Core/include/phos/Perc.h` and `Core/src/Perc.cpp` at 76f7100 (27.09.2026): roles,
 *       lane outputs and the metal table are Totality's; the coefficients, triggers and the kernel are Phosphene's.
 * @note Copied from Totality `Core/include/tot/synth/Kit.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Params.h"
#include "parh/synth/PercKernel.h"
#include <vector>

namespace parh {

/** @brief General MIDI drum note of each role (closed hat 42, pedal hat 44 for the rolling hat, open hat 46 ...). */
inline constexpr int kPercRoleNote[kNumPercRoles] = { 42, 44, 46, 51, 39, 39, 38, 37, 70, 45, 63, 49 };

/** @brief The percussion kit. */
class PercKit {
public:
    static constexpr int kMaxBlock = 64;         ///< longest block processLanes() renders
    static constexpr int kStride = kPercLaneSlots;   ///< the lane outputs' stride: sample i of lane l is at i * kStride + l
    static constexpr double kMetalRate = 48000.0;    ///< the metal table's own sample rate

    /** @brief Prepares for a sample rate (and builds the metal table). */
    void prepare(double sampleRate);
    /** @brief Silences every lane and clears all state. */
    void reset();
    /**
     * @brief Reads one lane's effective parameters.
     * @param lane    0..11
     * @param v       values indexed by perc::
     * @param keyRoot pitch class of the key, for Tune to Key
     * @param scale   compose.scale, for Tune to Key
     */
    void update(int lane, const float* v, int keyRoot, int scale);
    /** @brief The tempo, the time base of the auto-pan. */
    void setTempo(double bpm);
    /**
     * @brief Starts a hit.
     * @param lane     0..11
     * @param velocity 0..1
     * @param shift    pitch shift in semitones
     * @param late     how many samples ago the hit ideally started (0 <= late < 1)
     */
    void trigger(int lane, float velocity, int shift, double late);
    /**
     * @brief Renders @p n samples (n <= kMaxBlock) of every lane; read them with laneL() and laneR().
     */
    void processLanes(int n);
    /** @brief Renders with a chosen lane type (float = scalar reference), for the vector tests. */
    template <class V> void processLanesWith(int n);
    /** @brief Lane outputs of the last processLanes(), left: sample i of lane l at [i * kStride + l]. */
    const float* laneL() const { return outL_.data(); }
    /** @brief Lane outputs of the last processLanes(), right. */
    const float* laneR() const { return outR_.data(); }
    /** @brief Renders @p n samples of all lanes summed (any length), replacing @p L and @p R. */
    void process(float* L, float* R, int n);

    /** @brief Role of a lane as last updated. */
    PercRole role(int lane) const { return static_cast<PercRole>(role_[lane]); }
    /** @brief Frequency a lane's tone and modes are tuned to (after Tune to Key, before a hit's shift). */
    double laneHz(int lane) const { return tunedHz_[lane]; }
    /** @brief Nearest note of the key and scale to @p hz, as a frequency. */
    static double tuneToScale(double hz, int keyRoot, int scale);
    /** @brief The metal table (for the tests). */
    const std::vector<float>& metalTable() const { return metal_; }

private:
    void computeCoefs(int lane);
    void updatePanRate(int lane);
    void assignPanGroups();
    void buildMetalTable();

    double sr_ = 48000.0;
    PercState s_;
    PercCoefs c_;
    float values_[kPercLanes][perc::Count] = {};
    bool  valid_[kPercLanes] = {};
    int   keyRoot_[kPercLanes] = {}, scale_[kPercLanes] = {};
    int   role_[kPercLanes] = {};
    int   engine_[kPercLanes] = {};
    int   choke_[kPercLanes] = {};
    bool  metalNoise_[kPercLanes] = {};   ///< the lane's noise is the 909 table
    double tunedHz_[kPercLanes] = {};
    double shiftMul_[kPercLanes] = {};
    double bpm_ = 130.0;
    bool   panning_ = false;
    float modeAmp_[kPercModes][kPercLanes] = {};
    double modeW_[kPercModes][kPercLanes] = {};
    double modeR_[kPercModes][kPercLanes] = {};
    Rng   noiseRng_[kPercLanes];
    float noiseTail_[kPercLanes] = {}, noiseFast_[kPercLanes] = {};
    int   burstsLeft_[kPercLanes] = {};
    double burstTimer_[kPercLanes] = {}, burstSpacing_[kPercLanes] = {};
    float burstVel_[kPercLanes] = {};
    float chokeFactor_ = 0.999f;
    double metalPos_[kPercLanes] = {}, metalStep_[kPercLanes] = {};   ///< where each lane reads the metal table, and how fast
    std::vector<float> metal_;   ///< the 909 metal table at kMetalRate
    std::vector<float> noise_, reset_, dNoise_, outL_, outR_;
};

} // namespace parh
