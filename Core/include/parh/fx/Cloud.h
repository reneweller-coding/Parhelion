/**
 * @file Cloud.h
 * @brief The granular cloud (PLAN 5.7): grains of the pad's and the keys' recent past, the texture of Deep and
 *        Ambient trance (Dok. 5: "granulare Texturen aus dem eigenen Pad-Bus").
 *
 * A history of the last five seconds of what the pad and the keys (the piano, the pluck) send into it; grains read out of it -- a Poisson
 * stream of Density a second, each Size ms long (0.7 .. 1.3 of it), starting up to Spray seconds back, at the pitch it was
 * played or, with the chance Pitch, an octave down (0.4), an octave up (0.3) or a fifth up (0.3), under a Hann window,
 * panned across the field. Its output goes into the plate as well as into the mix, so the grains sit in a room.
 *
 * Noctuary's GrainCloud (Core/include/ambient/Cloud.h at 7a48fdd) is the model, not the source: its Aetherizer half --
 * the feedback spiral through a spectral shifter, the resonators on the scale, the Hawkes swarm -- serves a far plane
 * that Totality does not have, and it needs the tuning system and the FFT shifter with it. What a reduction wants is the
 * plain cloud, written anew here in a tenth of the lines.
 *
 * @note Copied from Totality `Core/include/tot/fx/Cloud.h` at 4d3c0d2 (29.09.2026); namespace parh, the sources the pad
 *       and the keys instead of the ping and the chord.
 *
 * Every decision is per sample (the onsets from one stream, the grains in a fixed pool), so the host's blocks change no
 * bit; with Level at -60 dB no grain starts, and once the last has ended the pool is skipped.
 */
#pragma once
#include "parh/Dsp.h"
#include <cstdint>
#include <vector>

namespace parh {

/** @brief The cloud. */
class GrainCloud {
public:
    static constexpr int kMaxGrains = 24;   ///< grains at once; an onset beyond them is dropped
    /** @brief Allocates the history and seeds the stream (not for the audio thread). */
    void prepare(double sampleRate, uint64_t seed);
    /** @brief Silence, the stream from its seed again. */
    void reset();
    /** @brief Reads the effective parameter values (indexed by cloud::). */
    void update(const float* v);
    /**
     * @brief Writes @p n samples of @p inL / @p inR into the history and renders the grains into @p L / @p R (replaced).
     */
    void process(const float* inL, const float* inR, float* L, float* R, int n);

private:
    /** @brief One grain: where it reads, how fast, how long, where it sits. */
    struct Grain {
        double pos = 0.0;     ///< read position in the history, frames
        double rate = 1.0;    ///< frames per sample
        int length = 0;       ///< samples
        int age = 0;          ///< samples played
        float gl = 0.0f;   ///< the pan's gain, left
        float gr = 0.0f;   ///< the pan's gain, right
        bool live = false;   ///< it sounds
    };
    /** @brief Starts a grain at a random place in the history (Spray), pitch and side. */
    void spawn();

    double sr_ = 48000.0;   ///< the sample rate, Hz
    uint64_t seed_ = 1;   ///< the stream's seed
    Rng rng_;   ///< the random stream
    std::vector<float> histL_;   ///< the history the grains read, left
    std::vector<float> histR_;   ///< ... right
    int mask_ = 0;   ///< its size - 1
    int write_ = 0;   ///< where the next sample goes
    Grain g_[kMaxGrains];   ///< the grains
    int live_ = 0;   ///< how many grains sound
    float rate_ = 0.0f;        ///< onsets per sample
    float sizeS_ = 0.2f;   ///< a grain's length, s
    float pitch_ = 0.3f;   ///< the grains' pitch spread, semitones
    float spray_ = 1.5f;   ///< how far back in the history they read, s
    float level_ = 0.0f;   ///< the level, linear
    bool on_ = false;   ///< the cloud plays (level above zero)
};

} // namespace parh
