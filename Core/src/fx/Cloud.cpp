/**
 * @file Cloud.cpp
 * @brief The granular cloud (Cloud.h).
 */
#include "parh/fx/Cloud.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {
constexpr double kTwoPiD = 6.283185307179586;   ///< 2 pi
/** Totality raised its percussive ping by 12 dB; a pad is continuous, so Parhelion's cloud needs none. */
constexpr float kMakeupDb = 0.0f;
}

void GrainCloud::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate;
    seed_ = seed;
    int frames = 1;
    while (frames < static_cast<int>(5.5 * sampleRate)) frames <<= 1;
    histL_.assign(static_cast<size_t>(frames), 0.0f);
    histR_.assign(static_cast<size_t>(frames), 0.0f);
    mask_ = frames - 1;
    reset();
}

void GrainCloud::reset()
{
    std::fill(histL_.begin(), histL_.end(), 0.0f);
    std::fill(histR_.begin(), histR_.end(), 0.0f);
    write_ = 0;
    for (Grain& g : g_) g.live = false;
    live_ = 0;
    rng_.seed(seed_);
}

void GrainCloud::update(const float* v)
{
    on_ = v[cloud::Level] > -59.9f;
    level_ = on_ ? dbToGain(v[cloud::Level] + kMakeupDb) : 0.0f;
    rate_ = static_cast<float>(v[cloud::Density] / sr_);
    sizeS_ = v[cloud::Size] * 0.001f;
    pitch_ = v[cloud::Pitch];
    spray_ = v[cloud::Spray];
}

void GrainCloud::spawn()
{
    int k = 0;
    while (k < kMaxGrains && g_[k].live) ++k;
    // The draws are made whether or not a slot is free, so the stream does not depend on how many grains sound.
    const float uLen = rng_.uniform(), uBack = rng_.uniform(), uPitch = rng_.uniform(), uWhich = rng_.uniform(), uPan = rng_.uniform();
    if (k == kMaxGrains) return;
    Grain& g = g_[k];
    g.length = std::max(64, static_cast<int>(sizeS_ * (0.7f + 0.6f * uLen) * static_cast<float>(sr_)));
    g.rate = 1.0;
    if (uPitch < pitch_) g.rate = uWhich < 0.4f ? 0.5 : (uWhich < 0.7f ? 2.0 : 1.5);
    // It starts Spray seconds back at most, and never reads past the write head (a grain an octave up reads twice as fast).
    const double reach = g.length * g.rate + 2.0;
    const double back = std::max(reach, static_cast<double>(uBack) * spray_ * sr_);
    g.pos = static_cast<double>(write_) - std::min(back, static_cast<double>(mask_) - 4.0);
    const float th = 0.25f * kPi * (1.0f + 0.8f * (2.0f * uPan - 1.0f));
    g.gl = std::cos(th) * 1.41421356f;
    g.gr = std::sin(th) * 1.41421356f;
    g.age = 0;
    g.live = true;
    ++live_;
}

void GrainCloud::process(const float* inL, const float* inR, float* L, float* R, int n)
{
    for (int i = 0; i < n; ++i) {
        histL_[static_cast<size_t>(write_ & mask_)] = inL[i];
        histR_[static_cast<size_t>(write_ & mask_)] = inR[i];
        ++write_;
        if (on_ && rng_.uniform() < rate_) spawn();
        float l = 0.0f, r = 0.0f;
        if (live_ > 0) {
            for (Grain& g : g_) {
                if (!g.live) continue;
                const double p = g.pos;
                const int i0 = static_cast<int>(std::floor(p));
                const float fr = static_cast<float>(p - i0);
                const size_t a = static_cast<size_t>(i0 & mask_), b = static_cast<size_t>((i0 + 1) & mask_);
                const float xl = histL_[a] + fr * (histL_[b] - histL_[a]);
                const float xr = histR_[a] + fr * (histR_[b] - histR_[a]);
                const float w = 0.5f - 0.5f * static_cast<float>(std::cos(kTwoPiD * g.age / g.length));
                l += xl * w * g.gl;
                r += xr * w * g.gr;
                g.pos += g.rate;
                if (++g.age >= g.length) { g.live = false; --live_; }
            }
        }
        L[i] = l * level_;
        R[i] = r * level_;
    }
}

} // namespace parh
