/**
 * @file Synth.cpp
 * @brief The mono synth.
 * @note Copied from Totality `Core/src/synth/Synth.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#include "parh/synth/Synth.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {
constexpr double kPiD = 3.141592653589793;   ///< pi
constexpr double kLn1000 = 6.907755278982137;   ///< ln 1000: a decay of 60 dB
}

void MonoSynth::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate;
    mod_.prepare(sampleRate, seed);
    amp_.setSampleRate(sampleRate);
    duck_.prepare(sampleRate);
    hb_ = designHalfband(90.0, 0.06);
    down_.setup(hb_);
    reset();
}

void MonoSynth::reset()
{
    amp_.kill();
    filt_.clear();
    down_.reset();
    duck_.reset();
    hp_.reset();
    lp_.reset();
    fenv_ = 0.0;
    subPhase_ = 0.0;
    slidePending_ = false;
    pos_ = 0;
    mod_.reset();
}

void MonoSynth::update(const float* v, float minLowCut)
{
    const float fs = static_cast<float>(sr_);
    level_ = v[synth::Level] <= -59.9f ? 0.0f : dbToGain(v[synth::Level]);
    levelKnob_ = level_;
    panKnob_ = v[synth::Pan];
    const float th = (clampv(v[synth::Pan], -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    gl_ = std::cos(th) * 1.41421356f;
    gr_ = std::sin(th) * 1.41421356f;
    wave_ = v[synth::Wave];
    pw_ = v[synth::PulseWidth];
    sub_ = v[synth::SubOsc];
    model_ = static_cast<FilterModel>(std::clamp(static_cast<int>(std::lround(v[synth::Filter])), 0, kFilterModels - 2));
    cutoff_ = v[synth::Cutoff];
    res_ = v[synth::Resonance];
    envAmt_ = v[synth::EnvAmount];
    filtDecayS_ = std::max(0.002, static_cast<double>(v[synth::Decay]) * 0.001);
    fenvDecay_ = std::exp(-kLn1000 / (filtDecayS_ * 2.0 * sr_));
    accent_ = v[synth::Accent];
    attack_ = v[synth::AmpAttack] * 0.001f;
    decay_ = v[synth::AmpDecay] * 0.001f;
    sustain_ = v[synth::AmpSustain];
    release_ = v[synth::AmpRelease] * 0.001f;
    glideMs_ = v[synth::Glide];
    drive_ = 0.5f + 5.0f * v[synth::Drive];
    driveNorm_ = 1.0f / std::tanh(drive_);
    driveKnob_ = v[synth::Drive];
    keyTrack_ = v[synth::KeyTrack];
    hp_.setK(std::max(v[synth::LowCut], minLowCut), 1.41421356f, fs);
    lp_.setK(std::min(v[synth::HighCut], 0.45f * fs), 1.41421356f, fs);
    duck_.set(1.0f - dbToGain(-v[synth::Duck]), 1.0f, 30.0f, v[synth::DuckRelease]);
    // The modulation's settings; where no slot is live the voice plays its knobs as they are.
    mod_.set(readModCore(v + synth::ModFirst, kSynthModDests, static_cast<int>(std::size(kSynthModDests))));
    refreshModulation();
}

void MonoSynth::refreshModulation()
{
    pwNow_ = pw_;
    resNow_ = res_;
    kNow_ = FilterVoicing::feedback(model_, res_);
    cutOct_ = envAdd_ = 0.0f;
    pitchMul_ = 1.0;
    if (mod_.on() && amp_.isActive()) applyModulation();
}

void MonoSynth::applyModulation()
{
    mod_.evaluate(0, pos_, static_cast<float>(fenv_));
    pitchMul_ = std::pow(2.0, static_cast<double>(mod_.get(0, ModDest::Pitch)) / 12.0);
    pwNow_ = std::clamp(pw_ + mod_.get(0, ModDest::PulseWidth), 0.05f, 0.95f);
    cutOct_ = mod_.get(0, ModDest::Cutoff);
    envAdd_ = mod_.get(0, ModDest::EnvAmount);
    if (mod_.targets(ModDest::Resonance)) {
        resNow_ = std::clamp(res_ + mod_.get(0, ModDest::Resonance), 0.0f, 1.0f);
        kNow_ = FilterVoicing::feedback(model_, resNow_);
    }
    if (mod_.targets(ModDest::Drive)) {
        drive_ = 0.5f + 5.0f * std::clamp(driveKnob_ + mod_.get(0, ModDest::Drive), 0.0f, 1.0f);
        driveNorm_ = 1.0f / std::tanh(drive_);
    }
    if (mod_.targets(ModDest::Level)) level_ = levelKnob_ * std::clamp(1.0f + mod_.get(0, ModDest::Level), 0.0f, 2.0f);
    if (mod_.targets(ModDest::Pan)) {
        const float th = (clampv(panKnob_ + mod_.get(0, ModDest::Pan), -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        gl_ = std::cos(th) * 1.41421356f;
        gr_ = std::sin(th) * 1.41421356f;
    }
}

void MonoSynth::noteOn(int pitch, float velocity, double late, bool accent, bool slide)
{
    targetHz_ = midiToHz(pitch);
    const bool legato = slidePending_ && amp_.isActive();
    const double glide = legato ? (glideMs_ > 0.0f ? glideMs_ : 60.0) : glideMs_;
    // The glide: an exponential approach of the pitch, a time constant of a third of the glide time.
    glideCoef_ = glide > 0.0 ? 1.0 - std::exp(-1.0 / (glide * 0.001 / 3.0 * 2.0 * sr_)) : 1.0;
    if (glide <= 0.0 || !amp_.isActive()) hz_ = targetHz_;
    velocity_ = clampv(velocity, 0.0f, 1.0f);
    noteAccent_ = accent;
    if (!legato) {
        amp_.setTimes(attack_, decay_, sustain_, std::max(release_, static_cast<float>(0.5 / targetHz_)));
        amp_.noteOn();
        amp_.advanceAttack(late);
        fenv_ = std::pow(fenvDecay_, 2.0 * late);
        mod_.noteOn(0, pos_, pitch, velocity_);
        if (mod_.on()) applyModulation();
    }
    slidePending_ = slide;
}

void MonoSynth::noteOff()
{
    // A sliding note's off comes after the next note has begun (the rack makes it 0.3 beats long), and the engine drops
    // an off whose note is no longer the latest: an off that arrives is a rest after the slide, so the gate closes.
    amp_.noteOff();
    mod_.noteOff(0);
}

void MonoSynth::process(float* L, float* R, int n)
{
    const double sr2 = 2.0 * sr_;
    const float accentOct = noteAccent_ ? 1.5f * accent_ : 0.0f;
    const float accentGain = noteAccent_ ? dbToGain(4.0f * accent_) : 1.0f;
    const bool modded = mod_.on();
    if (!modded) kNow_ = FilterVoicing::feedback(model_, res_);
    for (int i = 0; i < n; ++i) {
        if (!amp_.isActive()) {
            // Silent from the sample its envelope ended: only the duck's curve runs on (sample-exact, so the host's
            // blocks cannot move where the voice rests).
            for (; i < n; ++i) { duck_.next(); L[i] = 0.0f; R[i] = 0.0f; ++pos_; }
            break;
        }
        if (modded && (pos_ & 15) == 0) applyModulation();
        ++pos_;
        const float k = kNow_;
        float pair[2];
        for (int h = 0; h < 2; ++h) {
            hz_ += (targetHz_ - hz_) * glideCoef_;
            const double hzm = hz_ * pitchMul_;
            osc_.set(hzm, sr2, wave_, pwNow_);
            float x = osc_.next();
            // The sub: a square an octave down, naive but low (its first alias lies far under its own level at bass
            // pitches, and the filter follows).
            subPhase_ += 0.5 * hzm / sr2;
            subPhase_ -= std::floor(subPhase_);
            x += sub_ * (subPhase_ < 0.5 ? 1.0f : -1.0f);
            x = std::tanh(x * drive_) * driveNorm_;
            // The key tracking's factor, taken again only where the pitch or the knob moved: the same pow of the same
            // numbers, one of three transcendental calls a sample fewer (the optimisation pass, 29.09.2026).
            if (hzm != trackHz_ || keyTrack_ != trackKt_) {
                trackHz_ = hzm;
                trackKt_ = keyTrack_;
                track_ = std::pow(hzm / 110.0, static_cast<double>(keyTrack_));
            }
            const double track = track_;
            const double fc = std::min(0.42 * sr2, cutoff_ * track * std::pow(2.0, static_cast<double>(cutOct_)
                                                                                  + (envAmt_ + envAdd_ + accentOct) * fenv_));
            const float g = static_cast<float>(std::tan(kPiD * fc / sr2));
            // The knee: transparent under 1, then towards 2. A cutoff moved fast by the envelope pumps the integrators of
            // the diode ladder, the Polivoks and the Wasp at full resonance far beyond any input (peaks of 27 in the self
            // test); at 2x, so the knee's harmonics are filtered by the downsampler.
            const float f = filt_.tick(model_, x * 0.5f, g, k, 0.0f);
            const float a = std::fabs(f);
            pair[h] = a <= 1.0f ? f : std::copysign(1.0f + std::tanh(a - 1.0f), f);
            fenv_ *= fenvDecay_;
        }
        float y = down_.process(pair[0], pair[1]);
        float lpo, bp, hp;
        hp_.tick(y, lpo, bp, hp);
        y = lp_.lp(hp);
        y *= amp_.process() * velocity_ * accentGain * duck_.next() * level_;
        L[i] = y * gl_;
        R[i] = y * gr_;
    }
}

} // namespace parh
