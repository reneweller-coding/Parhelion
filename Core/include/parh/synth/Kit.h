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
 * @author Rene Weller
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Params.h"
#include "parh/synth/PercKernel.h"
#include <vector>

namespace parh
{

/** @brief General MIDI drum note of each role (closed hat 42, pedal hat 44 for the rolling hat, open hat 46 ...). */
inline constexpr int PercRoleNote[NumPercRoles] = { 42, 44, 46, 51, 39, 39, 38, 37, 70, 45, 63, 49 };

/** @brief The percussion kit. */
class PercKit
{
public:
    static constexpr int MaxBlock = 64;   ///< longest block processLanes() renders
    static constexpr int Stride =
        PercLaneSlots;   ///< the lane outputs' stride: sample i of lane l is at i * Stride + l
    static constexpr double MetalRate = 48000.0;   ///< the metal table's own sample rate

    /**
     * @brief Prepares for a sample rate (and builds the metal table).
     * @param[in] sampleRate  the sample rate in Hz
     */
    void prepare( double sampleRate );
    /** @brief Silences every lane and clears all state. */
    void reset();
    /**
     * @brief Reads one lane's effective parameters.
     * @param[in] lane     0..11
     * @param[in] v        values indexed by perc::
     * @param[in] keyRoot  pitch class of the key, for Tune to Key
     * @param[in] scale    compose.scale, for Tune to Key
     */
    void update( int lane, const float *v, int keyRoot, int scale );
    /**
     * @brief The tempo, the time base of the auto-pan.
     * @param[in] bpm  the tempo in beats per minute
     */
    void setTempo( double bpm );
    /**
     * @brief Starts a hit.
     * @param[in] lane      0..11
     * @param[in] velocity  0..1
     * @param[in] shift     pitch shift in semitones
     * @param[in] late      how many samples ago the hit ideally started (0 <= late < 1)
     */
    void trigger( int lane, float velocity, int shift, double late );
    /**
     * @brief Renders @p n samples (n <= MaxBlock) of every lane; read them with laneL() and laneR().
     * @param[in] n  how many samples, at most MaxBlock
     */
    void processLanes( int n );
    /**
     * @brief Renders with a chosen lane type (float = scalar reference), for the vector tests.
     * @param[in] n  how many samples, at most MaxBlock
     */
    template <class VT>
    void processLanesWith( int n );
    /**
     * @brief Lane outputs of the last processLanes(), left: sample i of lane l at [i * Stride + l].
     * @return the left lane outputs, sample i of lane l at [i * Stride + l]
     */
    const float *laneL() const { return m_outL.data(); }
    /**
     * @brief Lane outputs of the last processLanes(), right.
     * @return the right lane outputs, laid out as laneL()'s
     */
    const float *laneR() const { return m_outR.data(); }
    /**
     * @brief Renders @p n samples of all lanes summed (any length), replacing @p L and @p R.
     * @param[out] L  receives the left sum (replaced)
     * @param[out] R  receives the right sum (replaced)
     * @param[in]  n  how many samples
     */
    void process( float *L, float *R, int n );

    /**
     * @brief Role of a lane as last updated.
     * @param[in] lane  the lane
     * @return its role as last updated
     */
    PercRoleE role( int lane ) const { return static_cast<PercRoleE>( m_role[lane] ); }
    /**
     * @brief Frequency a lane's tone and modes are tuned to (after Tune to Key, before a hit's shift).
     * @param[in] lane  the lane
     * @return the frequency its tone and modes are tuned to, in Hz
     */
    double laneHz( int lane ) const { return m_tunedHz[lane]; }
    /**
     * @brief Nearest note of the key and scale to @p hz, as a frequency.
     * @param[in] hz       the frequency in Hz
     * @param[in] keyRoot  the key's root, 0 .. 11
     * @param[in] scale    the scale
     * @return the nearest note of the key and scale, in Hz
     */
    static double tuneToScale( double hz, int keyRoot, int scale );
    /**
     * @brief The metal table (for the tests).
     * @return the metal table
     */
    const std::vector<float> &metalTable() const { return m_metal; }

private:
    /**
     * @brief Computes lane @p lane's coefficients from its values (and its shift).
     * @param[in] lane  the lane
     */
    void computeCoefs( int lane );
    /**
     * @brief Sets lane @p lane's auto-pan rate from its period in bars and the tempo.
     * @param[in] lane  the lane
     */
    void updatePanRate( int lane );
    /** @brief Sets every lane's auto-pan offset and direction, so the lanes of a role move as a group. */
    void assignPanGroups();
    /** @brief Builds the 909 metal table (six square waves at their ratios, band-passed). */
    void buildMetalTable();

    double             m_sr = 48000.0;                          ///< the sample rate, Hz
    PercState          m_s;                                     ///< the lanes' states
    PercCoefs          m_c;                                     ///< the lanes' coefficients
    float              m_values[PercLanes][perc::COUNT] = {};   ///< per lane: its values, indexed by perc::
    bool               m_valid[PercLanes] = {};                 ///< per lane: m_values has been set
    int                m_keyRoot[PercLanes] = {};               ///< per lane: the key's root it was tuned to
    int                m_scale[PercLanes] = {};                 ///< per lane: the scale it was tuned to
    int                m_role[PercLanes] = {};                  ///< per lane: its role (PercRole)
    int                m_engine[PercLanes] = {};                ///< per lane: its engine (PercEngine)
    int                m_choke[PercLanes] = {};                 ///< per lane: its choke group, 0 none
    bool               m_metalNoise[PercLanes] = {};            ///< the lane's noise is the 909 table
    double             m_tunedHz[PercLanes] = {};               ///< per lane: the frequency it is tuned to
    double             m_shiftMul[PercLanes] = {};              ///< per lane: the last hit's shift as a factor
    double             m_bpm = 130.0;                           ///< the tempo
    bool               m_panning = false;                       ///< any lane moves in the panorama
    float              m_modeAmp[PercModes][PercLanes] = {};    ///< per mode and lane: its amplitude
    double             m_modeW[PercModes][PercLanes] = {};      ///< per mode and lane: its angle per sample
    double             m_modeR[PercModes][PercLanes] = {};   ///< per mode and lane: its radius per sample (the decay)
    Rng                m_noiseRng[PercLanes];                ///< per lane: the noise's stream
    float              m_noiseTail[PercLanes] = {};    ///< per lane: the noise envelope's factor per sample in the tail
    float              m_noiseFast[PercLanes] = {};    ///< ... and between a clap's bursts
    int                m_burstsLeft[PercLanes] = {};   ///< per lane: the clap's bursts still to come
    double             m_burstTimer[PercLanes] = {};   ///< per lane: samples to the next burst
    double             m_burstSpacing[PercLanes] = {};   ///< per lane: samples between bursts
    float              m_burstVel[PercLanes] = {};       ///< per lane: the bursts' velocity
    float              m_chokeFactor = 0.999f;           ///< the per-sample factor of a choked lane (8 ms)
    double             m_metalPos[PercLanes] = {};       ///< per lane: where it reads the metal table
    double             m_metalStep[PercLanes] = {};      ///< per lane: how fast it reads the metal table
    std::vector<float> m_metal;                          ///< the 909 metal table at MetalRate
    std::vector<float> m_noise;                          ///< per sample and lane: the white noise of the block
    std::vector<float> m_reset;    ///< per sample and lane: the value the noise envelope restarts at, 0 none
    std::vector<float> m_dNoise;   ///< per sample and lane: the noise envelope's factor
    std::vector<float> m_outL;     ///< per sample and lane: the output, left
    std::vector<float> m_outR;     ///< per sample and lane: the output, right
};

}   // namespace parh
