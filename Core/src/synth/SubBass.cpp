/**
 * @file SubBass.cpp
 * @brief The sub bass voice.
 * @note Copied from Totality `Core/src/synth/SubBass.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 * @author Rene Weller
 */
#include "parh/synth/SubBass.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {
constexpr double TwoPiD = 6.283185307179586;   ///< 2 pi
}

void SubBass::prepare(double sampleRate)
{
    m_sr = sampleRate;
    m_env.setSampleRate(sampleRate);
    m_duck.prepare(sampleRate);
    reset();
}

void SubBass::reset()
{
    m_env.kill();
    m_duck.reset();
    m_sat.reset();
    m_lp.reset();
    m_phase = 0.0;
}

double SubBass::noteHz(int pitch) const
{
    double hz = midiToHz(pitch + 12 * m_octave);
    while (hz < 35.0) hz *= 2.0;   // never under 35 Hz (Dok. 8.4)
    return hz;
}

void SubBass::update(const float* v)
{
    m_level = v[sub::LEVEL] <= -59.9f ? 0.0f : dbToGain(v[sub::LEVEL]);
    m_octave = static_cast<int>(std::lround(v[sub::OCTAVE]));
    m_attackS = v[sub::ATTACK] * 0.001f;
    m_decayS = v[sub::DECAY] * 0.001f;
    m_sustain = v[sub::SUSTAIN];
    m_releaseS = v[sub::RELEASE] * 0.001f;
    m_drive = 0.3f + 4.0f * v[sub::DRIVE];
    m_driveNorm = 1.0f / std::tanh(m_drive);
    m_lpHz = v[sub::LOW_PASS];
    m_lp.setQ(m_lpHz, 0.7071f, static_cast<float>(m_sr));
    m_lock = std::lround(v[sub::LOCK]) == 1;
    m_duck.set(1.0f - dbToGain(-v[sub::DUCK]), 1.0f, v[sub::DUCK_HOLD], v[sub::DUCK_RELEASE]);
}

double SubBass::chainPhase(double hz) const
{
    const double w = TwoPiD * hz / m_sr;
    const double x = std::tan(0.5 * w) / std::tan(0.5 * TwoPiD * std::min<double>(m_lpHz, 0.45 * m_sr) / m_sr);
    const double phase = -0.5 * w - std::atan2(std::sqrt(2.0) * x, 1.0 - x * x);
    return phase / TwoPiD;
}

void SubBass::noteOn(int pitch, float velocity, double late, double phase)
{
    const double hz = noteHz(pitch);
    m_inc = hz / m_sr;
    // The release floor: at least half a period (Phosphene's rule).
    const float release = std::max(m_releaseS, static_cast<float>(0.5 / hz));
    m_env.setTimes(m_attackS, m_decayS, m_sustain, release);
    if (phase >= 0.0) {
        // The phase at the ideal start, carried to this sample.
        const double p = phase + m_inc * late;
        m_phase = p - std::floor(p);
    }
    m_velocity = clampv(velocity, 0.0f, 1.0f);
    m_env.noteOn();
    m_env.advanceAttack(late);
}

void SubBass::noteOff()
{
    m_env.noteOff();
}

void SubBass::process(float* out, int n)
{
    for (int i = 0; i < n; ++i) {
        const float e = m_env.process();
        const float s = static_cast<float>(std::sin(TwoPiD * m_phase)) * e * m_velocity;
        m_phase += m_inc;
        m_phase -= std::floor(m_phase);
        const float y = m_lp.lp(m_sat(s * m_drive) * m_driveNorm);
        out[i] = y * m_level * m_duck.next();
    }
}

} // namespace parh
