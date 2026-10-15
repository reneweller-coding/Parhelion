/**
 * @file Cloud.cpp
 * @brief The granular cloud (Cloud.h).
 * @author Rene Weller
 */
#include "parh/fx/Cloud.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {
constexpr double TwoPiD = 6.283185307179586;   ///< 2 pi
/** Totality raised its percussive ping by 12 dB; a pad is continuous, so Parhelion's cloud needs none. */
constexpr float MakeupDb = 0.0f;
}

void GrainCloud::prepare(double sampleRate, uint64_t seed)
{
    m_sr = sampleRate;
    m_seed = seed;
    int frames = 1;
    while (frames < static_cast<int>(5.5 * sampleRate)) frames <<= 1;
    m_histL.assign(static_cast<size_t>(frames), 0.0f);
    m_histR.assign(static_cast<size_t>(frames), 0.0f);
    m_mask = frames - 1;
    reset();
}

void GrainCloud::reset()
{
    std::fill(m_histL.begin(), m_histL.end(), 0.0f);
    std::fill(m_histR.begin(), m_histR.end(), 0.0f);
    m_write = 0;
    for (Grain& g : m_g) g.m_live = false;
    m_live = 0;
    m_rng.seed(m_seed);
}

void GrainCloud::update(const float* v)
{
    m_on = v[cloud::LEVEL] > -59.9f;
    m_level = m_on ? dbToGain(v[cloud::LEVEL] + MakeupDb) : 0.0f;
    m_rate = static_cast<float>(v[cloud::DENSITY] / m_sr);
    m_sizeS = v[cloud::SIZE] * 0.001f;
    m_pitch = v[cloud::PITCH];
    m_spray = v[cloud::SPRAY];
}

void GrainCloud::spawn()
{
    int k = 0;
    while (k < MaxGrains && m_g[k].m_live) ++k;
    // The draws are made whether or not a slot is free, so the stream does not depend on how many grains sound.
    const float uLen = m_rng.uniform(), uBack = m_rng.uniform(), uPitch = m_rng.uniform(), uWhich = m_rng.uniform(), uPan = m_rng.uniform();
    if (k == MaxGrains) return;
    Grain& g = m_g[k];
    g.m_length = std::max(64, static_cast<int>(m_sizeS * (0.7f + 0.6f * uLen) * static_cast<float>(m_sr)));
    g.m_rate = 1.0;
    if (uPitch < m_pitch) g.m_rate = uWhich < 0.4f ? 0.5 : (uWhich < 0.7f ? 2.0 : 1.5);
    // It starts Spray seconds back at most, and never reads past the write head (a grain an octave up reads twice as fast).
    const double reach = g.m_length * g.m_rate + 2.0;
    const double back = std::max(reach, static_cast<double>(uBack) * m_spray * m_sr);
    g.m_pos = static_cast<double>(m_write) - std::min(back, static_cast<double>(m_mask) - 4.0);
    const float th = 0.25f * Pi * (1.0f + 0.8f * (2.0f * uPan - 1.0f));
    g.m_gl = std::cos(th) * 1.41421356f;
    g.m_gr = std::sin(th) * 1.41421356f;
    g.m_age = 0;
    g.m_live = true;
    ++m_live;
}

void GrainCloud::process(const float* inL, const float* inR, float* L, float* R, int n)
{
    for (int i = 0; i < n; ++i) {
        m_histL[static_cast<size_t>(m_write & m_mask)] = inL[i];
        m_histR[static_cast<size_t>(m_write & m_mask)] = inR[i];
        ++m_write;
        if (m_on && m_rng.uniform() < m_rate) spawn();
        float l = 0.0f, r = 0.0f;
        if (m_live > 0) {
            for (Grain& g : m_g) {
                if (!g.m_live) continue;
                const double p = g.m_pos;
                const int i0 = static_cast<int>(std::floor(p));
                const float fr = static_cast<float>(p - i0);
                const size_t a = static_cast<size_t>(i0 & m_mask), b = static_cast<size_t>((i0 + 1) & m_mask);
                const float xl = m_histL[a] + fr * (m_histL[b] - m_histL[a]);
                const float xr = m_histR[a] + fr * (m_histR[b] - m_histR[a]);
                const float w = 0.5f - 0.5f * static_cast<float>(std::cos(TwoPiD * g.m_age / g.m_length));
                l += xl * w * g.m_gl;
                r += xr * w * g.m_gr;
                g.m_pos += g.m_rate;
                if (++g.m_age >= g.m_length) { g.m_live = false; --m_live; }
            }
        }
        L[i] = l * m_level;
        R[i] = r * m_level;
    }
}

} // namespace parh
