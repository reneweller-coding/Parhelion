/**
 * @file SubBass.cpp
 * @brief The sub bass voice.
 * @note Copied from Totality `Core/src/synth/SubBass.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#include "parh/synth/SubBass.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {
constexpr double kTwoPiD = 6.283185307179586;   ///< 2 pi
}

void SubBass::prepare(double sampleRate)
{
    sr_ = sampleRate;
    env_.setSampleRate(sampleRate);
    duck_.prepare(sampleRate);
    reset();
}

void SubBass::reset()
{
    env_.kill();
    duck_.reset();
    sat_.reset();
    lp_.reset();
    phase_ = 0.0;
}

double SubBass::noteHz(int pitch) const
{
    double hz = midiToHz(pitch + 12 * octave_);
    while (hz < 35.0) hz *= 2.0;   // never under 35 Hz (Dok. 8.4)
    return hz;
}

void SubBass::update(const float* v)
{
    level_ = v[sub::Level] <= -59.9f ? 0.0f : dbToGain(v[sub::Level]);
    octave_ = static_cast<int>(std::lround(v[sub::Octave]));
    attackS_ = v[sub::Attack] * 0.001f;
    decayS_ = v[sub::Decay] * 0.001f;
    sustain_ = v[sub::Sustain];
    releaseS_ = v[sub::Release] * 0.001f;
    drive_ = 0.3f + 4.0f * v[sub::Drive];
    driveNorm_ = 1.0f / std::tanh(drive_);
    lpHz_ = v[sub::LowPass];
    lp_.setQ(lpHz_, 0.7071f, static_cast<float>(sr_));
    lock_ = std::lround(v[sub::Lock]) == 1;
    duck_.set(1.0f - dbToGain(-v[sub::Duck]), 1.0f, v[sub::DuckHold], v[sub::DuckRelease]);
}

double SubBass::chainPhase(double hz) const
{
    const double w = kTwoPiD * hz / sr_;
    const double x = std::tan(0.5 * w) / std::tan(0.5 * kTwoPiD * std::min<double>(lpHz_, 0.45 * sr_) / sr_);
    const double phase = -0.5 * w - std::atan2(std::sqrt(2.0) * x, 1.0 - x * x);
    return phase / kTwoPiD;
}

void SubBass::noteOn(int pitch, float velocity, double late, double phase)
{
    const double hz = noteHz(pitch);
    inc_ = hz / sr_;
    // The release floor: at least half a period (Phosphene's rule).
    const float release = std::max(releaseS_, static_cast<float>(0.5 / hz));
    env_.setTimes(attackS_, decayS_, sustain_, release);
    if (phase >= 0.0) {
        // The phase at the ideal start, carried to this sample.
        const double p = phase + inc_ * late;
        phase_ = p - std::floor(p);
    }
    velocity_ = clampv(velocity, 0.0f, 1.0f);
    env_.noteOn();
    env_.advanceAttack(late);
}

void SubBass::noteOff()
{
    env_.noteOff();
}

void SubBass::process(float* out, int n)
{
    for (int i = 0; i < n; ++i) {
        const float e = env_.process();
        const float s = static_cast<float>(std::sin(kTwoPiD * phase_)) * e * velocity_;
        phase_ += inc_;
        phase_ -= std::floor(phase_);
        const float y = lp_.lp(sat_(s * drive_) * driveNorm_);
        out[i] = y * level_ * duck_.next();
    }
}

} // namespace parh
