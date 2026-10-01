/**
 * @file Kick.cpp
 * @brief Kick drum implementation.
 * @note Adapted from Phosphene `Core/src/Kick.cpp` at 76f7100 (27.09.2026); see Kick.h for what is new.
 * @note Copied from Totality `Core/src/synth/Kick.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#include "parh/synth/Kick.h"
#include "parh/Params.h"
#include <cmath>
#include <complex>

namespace parh {

namespace {
constexpr double kPiD = 3.141592653589793;   ///< pi
constexpr double kLn1000 = 6.907755278982137;   ///< ln 1000: a decay of 60 dB
constexpr double kDcHz = 3.0;                 ///< DC blocker corner
constexpr float kClickGain = 4.0f;            ///< gain of the click layer at Click = 1 (Phosphene, 18.09.2026)
constexpr float kDipQ = 0.8f;                 ///< the EQ dip's quality: broad, "-3 dB at 500 Hz, wide" (Dok. 3)
constexpr float kSqrt2 = 1.41421356f;   ///< sqrt 2

/** @brief Drive knob to saturator gain. */
float driveGain(float drive) { return 0.2f + 7.8f * drive; }

/** @brief Small-signal gain of the normalised saturator relative to full scale, in dB. */
double saturationLiftDb(float drive, int clip)
{
    const double g = driveGain(drive);
    if (clip == 0) return 20.0 * std::log10(g / std::tanh(g));
    return 20.0 * std::log10(g > 1.0 ? g : 1.0);
}

/** @brief A triangle whose fundamental is in phase with sin(2 pi phi): 0 at phi = 0, rising to 1 at a quarter. */
inline double triangle(double phi)
{
    const double u = phi + 0.25 - std::floor(phi + 0.25);
    return 1.0 - 4.0 * std::fabs(u - 0.5);
}

/** @brief Phase in radians of the trapezoidal SVF's band pass (normalised by k) at @p w, for corner @p fc and damping @p k. */
std::complex<double> svfBandNorm(double w, double fc, double k, double sr)
{
    const double x = std::tan(0.5 * w) / std::tan(kPiD * std::min(fc, 0.45 * sr) / sr);
    const std::complex<double> s(0.0, x);
    return k * s / (s * s + k * s + 1.0);
}
} // namespace

void Kick::prepare(double sampleRate)
{
    sr_ = sampleRate;
    fadeStep_ = static_cast<float>(1.0 / (0.003 * sr_));
    dc_.prepare(sr_, static_cast<float>(kDcHz));
    reset();
}

void Kick::reset()
{
    voice_ = Voice{};
    fade_ = Voice{};
    top_ = Top{};
    fadeGain_ = 0.0f;
    noise_.seed(0x4B49434Bull);
    clickFilter_.reset();
    toneFilter_.reset();
    tanh_.reset();
    topTanh_.reset();
    hard_.reset();
    dc_.reset();
    lowCut_.reset();
    dip_.reset();
    topHp1_.reset();
    topHp2_.reset();
}

float Kick::tuneToKey(int keyRoot, int rule, float targetHz)
{
    const int root = ((keyRoot % 12) + 12) % 12;
    int degree = 0;
    if (rule == static_cast<int>(KickTune::Fifth)) degree = 7;
    else if (rule == static_cast<int>(KickTune::FlatSeventh)) degree = 10;
    else {
        // The tonic if one of its octaves lies in 41 .. 62 Hz; else the fifth.
        bool fits = false;
        for (int oct = 0; oct <= 2; ++oct) {
            const double f = midiToHz(12.0 * (oct + 1) + root);
            fits = fits || (f >= 41.0 && f <= 62.0);
        }
        degree = fits ? 0 : 7;
    }
    const double base = midiToHz(12.0 + ((root + degree) % 12));   // octave 0
    double best = targetHz, bestDist = 1e9;
    for (int oct = -1; oct <= 5; ++oct) {
        const double f = base * std::pow(2.0, oct);
        const double dist = std::fabs(std::log2(f / targetHz));
        if (dist < bestDist) { bestDist = dist; best = f; }
    }
    return static_cast<float>(best);
}

void Kick::constrain(float* v, double slotSeconds, int keyRoot)
{
    // Body floor first: at least two periods of the end pitch above -20 dB. The envelope falls 20 dB in a third of
    // the decay time, so the sweep kick needs hold + decay/3 >= 2/f_end and the resonator decay/3 >= 2/f_end.
    const bool resonator = std::lround(v[kick::Engine]) == static_cast<long>(KickEngine::Resonator);
    const int tune = static_cast<int>(std::lround(v[kick::Tune]));
    const double endHz = tune != 0 ? tuneToKey(keyRoot, tune, v[kick::PitchEnd]) : v[kick::PitchEnd];
    const double body = 2.0 / endHz;
    const double holdNow = resonator ? 0.0 : v[kick::AmpHold] * 0.001;
    const double minDecay = 3.0 * std::max(0.0, body - holdNow);
    if (v[kick::AmpDecay] * 0.001 < minDecay) v[kick::AmpDecay] = static_cast<float>(minDecay * 1000.0);

    // The tail limit (off at 0 dB): the tail at the slot, in dB below the peak, plus what the saturation adds.
    const double limit = v[kick::TailLimit];
    if (limit >= -0.01 || slotSeconds <= 0.0) return;
    const double lift = saturationLiftDb(v[kick::Drive], static_cast<int>(std::lround(v[kick::Clip])));
    const double allowed = limit - lift;
    const double attack = resonator ? 0.0 : v[kick::AmpAttack] * 0.001;
    double hold = resonator ? 0.0 : v[kick::AmpHold] * 0.001;
    if (hold > 0.3 * slotSeconds) { hold = 0.3 * slotSeconds; v[kick::AmpHold] = static_cast<float>(hold * 1000.0); }
    const double run = slotSeconds - attack - hold;
    if (run <= 0.0) return;
    const double maxDecay = 60.0 * run / -allowed;
    if (v[kick::AmpDecay] * 0.001 > maxDecay) v[kick::AmpDecay] = static_cast<float>(std::max(20.0, maxDecay * 1000.0));
}

void Kick::update(const float* v, int keyRoot)
{
    engine_ = static_cast<int>(std::lround(v[kick::Engine]));
    const int tune = static_cast<int>(std::lround(v[kick::Tune]));
    endHz_ = tune != 0 ? tuneToKey(keyRoot, tune, v[kick::PitchEnd]) : v[kick::PitchEnd];
    startHz_ = std::max(v[kick::PitchStart], endHz_);
    punch_ = v[kick::Punch];
    tau2_ = v[kick::PitchDecay] * 0.001;
    tau1_ = std::min(static_cast<double>(v[kick::PunchDecay]) * 0.001, tau2_);
    attackSamples_ = std::max(1.0, v[kick::AmpAttack] * 0.001 * sr_);
    holdSamples_ = v[kick::AmpHold] * 0.001 * sr_;
    decaySeconds_ = std::max(1.0e-3, static_cast<double>(v[kick::AmpDecay]) * 0.001);
    clip_ = static_cast<int>(std::lround(v[kick::Clip]));
    drive_ = driveGain(v[kick::Drive]);
    driveNorm_ = clip_ == 0 ? 1.0f / std::tanh(drive_) : 1.0f / std::min(drive_, 1.0f);
    clickLevel_ = v[kick::ClickLevel];
    clickFilter_.setQ(v[kick::ClickTone], 0.9f, static_cast<float>(sr_));
    clickDecay_ = static_cast<float>(std::exp(-kLn1000 / (std::max(0.5e-3, v[kick::ClickDecay] * 0.001) * sr_)));
    toneHz_ = v[kick::Tone];
    toneFilter_.setQ(toneHz_, 0.7071f, static_cast<float>(sr_));
    level_ = dbToGain(v[kick::Level]);
    lowCutHz_ = v[kick::LowCut];
    lowCut_.setK(lowCutHz_, kSqrt2, static_cast<float>(sr_));
    dipHz_ = v[kick::DipFreq];
    dipGain_ = dbToGain(v[kick::Dip]);
    dip_.setQ(dipHz_, kDipQ, static_cast<float>(sr_));
    topLevel_ = v[kick::TopLevel] <= -59.9f ? 0.0f : dbToGain(v[kick::TopLevel]);
    topShift_ = static_cast<float>(std::pow(2.0, v[kick::TopPitch] / 12.0));
    topDecay_ = std::max(0.005, static_cast<double>(v[kick::TopDecay]) * 0.001);
    topDrive_ = driveGain(v[kick::TopDrive]);
    topNorm_ = 1.0f / std::tanh(topDrive_);
    topHp1_.setK(v[kick::TopCut], 1.8477590f, static_cast<float>(sr_));   // fourth-order Butterworth: 2 cos(pi/8)
    topHp2_.setK(v[kick::TopCut], 0.7653669f, static_cast<float>(sr_));   // and 2 cos(3 pi/8)
    setPhaseTarget(lockT_, lockTarget_);
}

double Kick::phaseWith(double t, double tau2) const
{
    const double a = static_cast<double>(startHz_) - endHz_;
    return endHz_ * t + a * ((1.0 - punch_) * tau2 * -std::expm1(-t / tau2) + punch_ * tau1_ * -std::expm1(-t / tau1_));
}

double Kick::frequencyAt(double t) const
{
    return endHz_ + (static_cast<double>(startHz_) - endHz_) * ((1.0 - punch_) * std::exp(-t / tau2Trimmed_) + punch_ * std::exp(-t / tau1_));
}

double Kick::chainPhase(double hz) const
{
    const double w = 2.0 * kPiD * hz / sr_;
    // First-order ADAA: half a sample late.
    double phase = -0.5 * w;
    // Tone low pass: the trapezoidal SVF is the bilinear image of 1 / (1 + k s + s^2), k = sqrt 2.
    const double x = std::tan(0.5 * w) / std::tan(kPiD * std::min<double>(toneHz_, 0.45 * sr_) / sr_);
    phase += -std::atan2(std::sqrt(2.0) * x, 1.0 - x * x);
    // DC blocker (1 - z^-1) / (1 - r z^-1).
    const double r = 1.0 - 2.0 * kPiD * kDcHz / sr_;
    const std::complex<double> zi = std::polar(1.0, -w);
    phase += std::arg((1.0 - zi) / (1.0 - r * zi));
    // The low cut: s^2 / (s^2 + sqrt2 s + 1), s = j x.
    const double xl = std::tan(0.5 * w) / std::tan(kPiD * std::min<double>(lowCutHz_, 0.45 * sr_) / sr_);
    phase += kPiD - std::atan2(std::sqrt(2.0) * xl, 1.0 - xl * xl);
    // The dip: 1 + (g - 1) times the normalised band pass.
    const std::complex<double> dip = 1.0 + (static_cast<double>(dipGain_) - 1.0) * svfBandNorm(w, dipHz_, 1.0 / kDipQ, sr_);
    phase += std::arg(dip);
    return phase / (2.0 * kPiD);
}

double Kick::outputPhaseAt(double t) const
{
    double phi = phaseWith(t, tau2Trimmed_);
    if (engine_ == static_cast<int>(KickEngine::Resonator)) {
        // The phasor turns by f(t_k)/fs at every sample k up to and including the one output, so its phase is the
        // trapezoidal sum of f: the integral plus (f(0) + f(t)) / (2 fs).
        phi += (static_cast<double>(startHz_) + frequencyAt(t)) / (2.0 * sr_);
    }
    return phi + chainPhase(frequencyAt(t));
}

double Kick::asymptoticPhase() const
{
    const double a = static_cast<double>(startHz_) - endHz_;
    double c = a * ((1.0 - punch_) * tau2Trimmed_ + punch_ * tau1_);
    if (engine_ == static_cast<int>(KickEngine::Resonator)) c += (static_cast<double>(startHz_) + endHz_) / (2.0 * sr_);
    return c + chainPhase(endHz_);
}

void Kick::setPhaseTarget(double t, double targetCycles)
{
    // update() runs every 32 samples; the solve only when something it depends on has changed.
    const double key[] = { t, targetCycles, tau1_, tau2_, endHz_, startHz_, punch_, toneHz_, static_cast<double>(engine_), sr_ };
    bool same = true;
    for (int i = 0; i < 10; ++i) same = same && key[i] == trimKey_[i];
    if (same) return;
    for (int i = 0; i < 10; ++i) trimKey_[i] = key[i];
    lockT_ = t;
    lockTarget_ = targetCycles;
    tau2Trimmed_ = tau2_;
    if (t <= 0.0) return;
    // Output phase minus target as a function of tau_2; monotonic increasing in tau_2.
    auto g = [&](double tau2) {
        const double saved = tau2Trimmed_;
        const_cast<Kick*>(this)->tau2Trimmed_ = tau2;
        const double v = outputPhaseAt(t) - targetCycles;
        const_cast<Kick*>(this)->tau2Trimmed_ = saved;
        return v;
    };
    const double g0 = g(tau2_);
    const double lo = std::max(tau1_ * 1.01, tau2_ * 0.5), hi = tau2_ * 2.0;
    const double gLo = g(lo), gHi = g(hi);
    double k = std::round(g0);
    if (k < gLo) k = std::ceil(gLo);
    if (k > gHi) k = std::floor(gHi);
    if (k < gLo || k > gHi) return;   // no whole cycle inside the range: leave tau_2 alone
    double a = lo, b = hi;
    for (int i = 0; i < 60; ++i) {
        const double m = 0.5 * (a + b);
        if (g(m) < k) a = m; else b = m;
    }
    const double down = 0.5 * (a + b);
    const double k2 = g0 >= k ? k + 1.0 : k - 1.0;
    double best = down;
    if (k2 >= gLo && k2 <= gHi) {
        a = lo; b = hi;
        for (int i = 0; i < 60; ++i) {
            const double m = 0.5 * (a + b);
            if (g(m) < k2) a = m; else b = m;
        }
        const double other = 0.5 * (a + b);
        if (std::fabs(other - tau2_) < std::fabs(down - tau2_)) best = other;
    }
    tau2Trimmed_ = best;
}

Kick::Shape Kick::currentShape() const
{
    Shape s;
    s.engine = engine_;
    s.fe = endHz_;
    s.fs = startHz_;
    s.tau1 = tau1_;
    s.tau2 = tau2Trimmed_;
    s.punch = punch_;
    s.attack = attackSamples_;
    s.hold = holdSamples_;
    s.decayRate = -kLn1000 / (decaySeconds_ * sr_);
    s.damping = std::exp(-kLn1000 / (decaySeconds_ * sr_));
    return s;
}

void Kick::trigger(float velocity, double late)
{
    const Shape shape = currentShape();
    const bool resonator = shape.engine == static_cast<int>(KickEngine::Resonator);
    // The resonator keeps its ring: a new trigger adds to it rather than replacing it. Everything else hands the old
    // voice to the 3 ms fade.
    const bool keepRing = resonator && voice_.s.engine == shape.engine && voice_.active;
    if (voice_.active && !keepRing) { fade_ = voice_; fadeGain_ = 1.0f; }
    const double zRe = keepRing ? voice_.zRe : 0.0, zIm = keepRing ? voice_.zIm : 0.0;
    Voice v;
    v.active = true;
    v.s = shape;
    v.late = late;
    v.n = 0;
    v.d1 = std::exp(-1.0 / (shape.tau1 * sr_));
    v.d2 = std::exp(-1.0 / (shape.tau2 * sr_));
    v.e1 = std::exp(-late / (shape.tau1 * sr_));
    v.e2 = std::exp(-late / (shape.tau2 * sr_));
    v.click = 1.0f;
    v.velocity = clampv(velocity, 0.0f, 1.0f);
    v.zRe = zRe;
    v.zIm = zIm;
    if (resonator) {
        // Energy into the real part, rotated to where the phasor is `late` samples after an ideal start: Im(z) then
        // rings as a sine of the injected amplitude starting at phase zero.
        const double w0 = 2.0 * kPiD * shape.fs * late / sr_;
        v.zRe += v.velocity * std::cos(w0);
        v.zIm += v.velocity * std::sin(w0);
    }
    voice_ = v;

    if (topLevel_ > 0.0f) {
        Top t;
        t.active = true;
        t.fe = endHz_ * topShift_;
        t.fs = startHz_ * topShift_;
        t.tau1 = tau1_;
        t.tau2 = tau2_;
        t.punch = punch_;
        t.late = late;
        t.d1 = std::exp(-1.0 / (t.tau1 * sr_));
        t.d2 = std::exp(-1.0 / (t.tau2 * sr_));
        t.e1 = std::exp(-late / (t.tau1 * sr_));
        t.e2 = std::exp(-late / (t.tau2 * sr_));
        t.decay = std::exp(-kLn1000 / (topDecay_ * sr_));
        t.amp = std::pow(t.decay, late);
        t.velocity = v.velocity;
        top_ = t;
    }
}

float Kick::voiceSample(Voice& v, float& click)
{
    const Shape& s = v.s;
    const double tSamples = v.n + v.late;
    const double t = tSamples / sr_;
    float body;
    if (s.engine != static_cast<int>(KickEngine::Resonator)) {
        const double a = s.fs - s.fe;
        const double phi = s.fe * t + a * ((1.0 - s.punch) * s.tau2 * (1.0 - v.e2) + s.punch * s.tau1 * (1.0 - v.e1));
        double amp;
        if (tSamples < s.attack) amp = tSamples / s.attack;
        else if (tSamples < s.attack + s.hold) amp = 1.0;
        else amp = std::exp(s.decayRate * (tSamples - s.attack - s.hold));
        const double wave = s.engine == static_cast<int>(KickEngine::Tr909) ? triangle(phi)
                                                                             : std::sin(2.0 * kPiD * (phi - std::floor(phi)));
        body = static_cast<float>(wave * amp) * v.velocity;
        if (tSamples > s.attack + s.hold && amp < 1.0e-5) v.active = false;
    } else {
        const double f = s.fe + (s.fs - s.fe) * ((1.0 - s.punch) * v.e2 + s.punch * v.e1);
        const double w = 2.0 * kPiD * std::min(f, 0.45 * sr_) / sr_;
        const double c = s.damping * std::cos(w), sn = s.damping * std::sin(w);
        const double re = v.zRe * c - v.zIm * sn;
        v.zIm = v.zRe * sn + v.zIm * c;
        v.zRe = re;
        body = static_cast<float>(v.zIm);
        if (v.n > 64 && v.zRe * v.zRe + v.zIm * v.zIm < 1.0e-12) v.active = false;
    }
    v.e1 *= v.d1;
    v.e2 *= v.d2;
    click = 0.0f;
    if (v.click > 1.0e-5f) {
        const float nz = noise_.bipolar() * v.click + (v.n == 0 ? 1.0f : 0.0f);
        float lp, bp, hp;
        clickFilter_.tick(nz, lp, bp, hp);
        click = bp * clickFilter_.k * clickLevel_ * kClickGain * v.velocity;
        v.click *= clickDecay_;
    }
    ++v.n;
    return body;
}

float Kick::topSample()
{
    Top& t = top_;
    const double time = (t.n + t.late) / sr_;
    const double a = t.fs - t.fe;
    const double phi = t.fe * time + a * ((1.0 - t.punch) * t.tau2 * (1.0 - t.e2) + t.punch * t.tau1 * (1.0 - t.e1));
    const float y = static_cast<float>(triangle(phi) * t.amp) * t.velocity;
    t.e1 *= t.d1;
    t.e2 *= t.d2;
    t.amp *= t.decay;
    ++t.n;
    if (t.amp < 1.0e-5) t.active = false;
    return y;
}

void Kick::process(float* out, float* body, int n)
{
    for (int i = 0; i < n; ++i) {
        float x = 0.0f, click = 0.0f, c = 0.0f;
        if (voice_.active) { x += voiceSample(voice_, c); click += c; }
        if (fade_.active && fadeGain_ > 0.0f) {
            x += voiceSample(fade_, c) * fadeGain_;
            click += c * fadeGain_;
            fadeGain_ -= fadeStep_;
            if (fadeGain_ <= 0.0f) { fadeGain_ = 0.0f; fade_.active = false; }
        }
        const float driven = x * drive_;
        // The click joins after the saturator (Phosphene, 18.09.2026): inside it, it rode on the body's crest and was
        // flattened with it.
        float y = (clip_ == 0 ? tanh_(driven) : hard_(driven)) * driveNorm_ + click;
        y = toneFilter_.lp(y);
        y = dc_.process(y);
        float lp, bp, hp;
        lowCut_.tick(y, lp, bp, hp);
        y = hp;
        dip_.tick(y, lp, bp, hp);
        y += (dipGain_ - 1.0f) * bp * dip_.k;
        y *= level_;
        float top = 0.0f;
        if (top_.active) {
            float tl, tb, th;
            const float s = topTanh_(topSample() * topDrive_) * topNorm_;
            topHp1_.tick(s, tl, tb, th);
            topHp2_.tick(th, tl, tb, th);
            top = th * topLevel_ * level_;
        } else {
            // Keep the high pass's state moving towards rest so a new hit starts from silence.
            float tl, tb, th;
            topHp1_.tick(0.0f, tl, tb, th);
            topHp2_.tick(th, tl, tb, th);
        }
        out[i] = y + top;
        if (body != nullptr) body[i] = y;
    }
}

} // namespace parh
