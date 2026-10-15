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
constexpr double PiD = 3.141592653589793;   ///< pi
constexpr double Ln1000 = 6.907755278982137;   ///< ln 1000: a decay of 60 dB
constexpr double DcHz = 3.0;                 ///< DC blocker corner
constexpr float ClickGain = 4.0f;            ///< gain of the click layer at Click = 1 (Phosphene, 18.09.2026)
constexpr float DipQ = 0.8f;                 ///< the EQ dip's quality: broad, "-3 dB at 500 Hz, wide" (Dok. 3)
constexpr float Sqrt2 = 1.41421356f;   ///< sqrt 2

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
    const double x = std::tan(0.5 * w) / std::tan(PiD * std::min(fc, 0.45 * sr) / sr);
    const std::complex<double> s(0.0, x);
    return k * s / (s * s + k * s + 1.0);
}
} // namespace

void Kick::prepare(double sampleRate)
{
    m_sr = sampleRate;
    m_fadeStep = static_cast<float>(1.0 / (0.003 * m_sr));
    m_dc.prepare(m_sr, static_cast<float>(DcHz));
    reset();
}

void Kick::reset()
{
    m_voice = Voice{};
    m_fade = Voice{};
    m_top = Top{};
    m_fadeGain = 0.0f;
    m_noise.seed(0x4B49434Bull);
    m_clickFilter.reset();
    m_toneFilter.reset();
    m_tanh.reset();
    m_topTanh.reset();
    m_hard.reset();
    m_dc.reset();
    m_lowCut.reset();
    m_dip.reset();
    m_topHp1.reset();
    m_topHp2.reset();
}

float Kick::tuneToKey(int keyRoot, int rule, float targetHz)
{
    const int root = ((keyRoot % 12) + 12) % 12;
    int degree = 0;
    if (rule == static_cast<int>(KickTuneE::FIFTH)) degree = 7;
    else if (rule == static_cast<int>(KickTuneE::FLAT_SEVENTH)) degree = 10;
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
    const bool resonator = std::lround(v[kick::ENGINE]) == static_cast<long>(KickEngineE::RESONATOR);
    const int tune = static_cast<int>(std::lround(v[kick::TUNE]));
    const double endHz = tune != 0 ? tuneToKey(keyRoot, tune, v[kick::PITCH_END]) : v[kick::PITCH_END];
    const double body = 2.0 / endHz;
    const double holdNow = resonator ? 0.0 : v[kick::AMP_HOLD] * 0.001;
    const double minDecay = 3.0 * std::max(0.0, body - holdNow);
    if (v[kick::AMP_DECAY] * 0.001 < minDecay) v[kick::AMP_DECAY] = static_cast<float>(minDecay * 1000.0);

    // The tail limit (off at 0 dB): the tail at the slot, in dB below the peak, plus what the saturation adds.
    const double limit = v[kick::TAIL_LIMIT];
    if (limit >= -0.01 || slotSeconds <= 0.0) return;
    const double lift = saturationLiftDb(v[kick::DRIVE], static_cast<int>(std::lround(v[kick::CLIP])));
    const double allowed = limit - lift;
    const double attack = resonator ? 0.0 : v[kick::AMP_ATTACK] * 0.001;
    double hold = resonator ? 0.0 : v[kick::AMP_HOLD] * 0.001;
    if (hold > 0.3 * slotSeconds) { hold = 0.3 * slotSeconds; v[kick::AMP_HOLD] = static_cast<float>(hold * 1000.0); }
    const double run = slotSeconds - attack - hold;
    if (run <= 0.0) return;
    const double maxDecay = 60.0 * run / -allowed;
    if (v[kick::AMP_DECAY] * 0.001 > maxDecay) v[kick::AMP_DECAY] = static_cast<float>(std::max(20.0, maxDecay * 1000.0));
}

void Kick::update(const float* v, int keyRoot)
{
    m_engine = static_cast<int>(std::lround(v[kick::ENGINE]));
    const int tune = static_cast<int>(std::lround(v[kick::TUNE]));
    m_endHz = tune != 0 ? tuneToKey(keyRoot, tune, v[kick::PITCH_END]) : v[kick::PITCH_END];
    m_startHz = std::max(v[kick::PITCH_START], m_endHz);
    m_punch = v[kick::PUNCH];
    m_tau2 = v[kick::PITCH_DECAY] * 0.001;
    m_tau1 = std::min(static_cast<double>(v[kick::PUNCH_DECAY]) * 0.001, m_tau2);
    m_attackSamples = std::max(1.0, v[kick::AMP_ATTACK] * 0.001 * m_sr);
    m_holdSamples = v[kick::AMP_HOLD] * 0.001 * m_sr;
    m_decaySeconds = std::max(1.0e-3, static_cast<double>(v[kick::AMP_DECAY]) * 0.001);
    m_clip = static_cast<int>(std::lround(v[kick::CLIP]));
    m_drive = driveGain(v[kick::DRIVE]);
    m_driveNorm = m_clip == 0 ? 1.0f / std::tanh(m_drive) : 1.0f / std::min(m_drive, 1.0f);
    m_clickLevel = v[kick::CLICK_LEVEL];
    m_clickFilter.setQ(v[kick::CLICK_TONE], 0.9f, static_cast<float>(m_sr));
    m_clickDecay = static_cast<float>(std::exp(-Ln1000 / (std::max(0.5e-3, v[kick::CLICK_DECAY] * 0.001) * m_sr)));
    m_toneHz = v[kick::TONE];
    m_toneFilter.setQ(m_toneHz, 0.7071f, static_cast<float>(m_sr));
    m_level = dbToGain(v[kick::LEVEL]);
    m_lowCutHz = v[kick::LOW_CUT];
    m_lowCut.setK(m_lowCutHz, Sqrt2, static_cast<float>(m_sr));
    m_dipHz = v[kick::DIP_FREQ];
    m_dipGain = dbToGain(v[kick::DIP]);
    m_dip.setQ(m_dipHz, DipQ, static_cast<float>(m_sr));
    m_topLevel = v[kick::TOP_LEVEL] <= -59.9f ? 0.0f : dbToGain(v[kick::TOP_LEVEL]);
    m_topShift = static_cast<float>(std::pow(2.0, v[kick::TOP_PITCH] / 12.0));
    m_topDecay = std::max(0.005, static_cast<double>(v[kick::TOP_DECAY]) * 0.001);
    m_topDrive = driveGain(v[kick::TOP_DRIVE]);
    m_topNorm = 1.0f / std::tanh(m_topDrive);
    m_topHp1.setK(v[kick::TOP_CUT], 1.8477590f, static_cast<float>(m_sr));   // fourth-order Butterworth: 2 cos(pi/8)
    m_topHp2.setK(v[kick::TOP_CUT], 0.7653669f, static_cast<float>(m_sr));   // and 2 cos(3 pi/8)
    setPhaseTarget(m_lockT, m_lockTarget);
}

double Kick::phaseWith(double t, double tau2) const
{
    const double a = static_cast<double>(m_startHz) - m_endHz;
    return m_endHz * t + a * ((1.0 - m_punch) * tau2 * -std::expm1(-t / tau2) + m_punch * m_tau1 * -std::expm1(-t / m_tau1));
}

double Kick::frequencyAt(double t) const
{
    return m_endHz + (static_cast<double>(m_startHz) - m_endHz) * ((1.0 - m_punch) * std::exp(-t / m_tau2Trimmed) + m_punch * std::exp(-t / m_tau1));
}

double Kick::chainPhase(double hz) const
{
    const double w = 2.0 * PiD * hz / m_sr;
    // First-order ADAA: half a sample late.
    double phase = -0.5 * w;
    // Tone low pass: the trapezoidal SVF is the bilinear image of 1 / (1 + k s + s^2), k = sqrt 2.
    const double x = std::tan(0.5 * w) / std::tan(PiD * std::min<double>(m_toneHz, 0.45 * m_sr) / m_sr);
    phase += -std::atan2(std::sqrt(2.0) * x, 1.0 - x * x);
    // DC blocker (1 - z^-1) / (1 - r z^-1).
    const double r = 1.0 - 2.0 * PiD * DcHz / m_sr;
    const std::complex<double> zi = std::polar(1.0, -w);
    phase += std::arg((1.0 - zi) / (1.0 - r * zi));
    // The low cut: s^2 / (s^2 + sqrt2 s + 1), s = j x.
    const double xl = std::tan(0.5 * w) / std::tan(PiD * std::min<double>(m_lowCutHz, 0.45 * m_sr) / m_sr);
    phase += PiD - std::atan2(std::sqrt(2.0) * xl, 1.0 - xl * xl);
    // The dip: 1 + (g - 1) times the normalised band pass.
    const std::complex<double> dip = 1.0 + (static_cast<double>(m_dipGain) - 1.0) * svfBandNorm(w, m_dipHz, 1.0 / DipQ, m_sr);
    phase += std::arg(dip);
    return phase / (2.0 * PiD);
}

double Kick::outputPhaseAt(double t) const
{
    double phi = phaseWith(t, m_tau2Trimmed);
    if (m_engine == static_cast<int>(KickEngineE::RESONATOR)) {
        // The phasor turns by f(t_k)/fs at every sample k up to and including the one output, so its phase is the
        // trapezoidal sum of f: the integral plus (f(0) + f(t)) / (2 fs).
        phi += (static_cast<double>(m_startHz) + frequencyAt(t)) / (2.0 * m_sr);
    }
    return phi + chainPhase(frequencyAt(t));
}

double Kick::asymptoticPhase() const
{
    const double a = static_cast<double>(m_startHz) - m_endHz;
    double c = a * ((1.0 - m_punch) * m_tau2Trimmed + m_punch * m_tau1);
    if (m_engine == static_cast<int>(KickEngineE::RESONATOR)) c += (static_cast<double>(m_startHz) + m_endHz) / (2.0 * m_sr);
    return c + chainPhase(m_endHz);
}

void Kick::setPhaseTarget(double t, double targetCycles)
{
    // update() runs every 32 samples; the solve only when something it depends on has changed.
    const double key[] = { t, targetCycles, m_tau1, m_tau2, m_endHz, m_startHz, m_punch, m_toneHz, static_cast<double>(m_engine), m_sr };
    bool same = true;
    for (int i = 0; i < 10; ++i) same = same && key[i] == m_trimKey[i];
    if (same) return;
    for (int i = 0; i < 10; ++i) m_trimKey[i] = key[i];
    m_lockT = t;
    m_lockTarget = targetCycles;
    m_tau2Trimmed = m_tau2;
    if (t <= 0.0) return;
    // Output phase minus target as a function of tau_2; monotonic increasing in tau_2.
    auto g = [&](double tau2) {
        const double saved = m_tau2Trimmed;
        const_cast<Kick*>(this)->m_tau2Trimmed = tau2;
        const double v = outputPhaseAt(t) - targetCycles;
        const_cast<Kick*>(this)->m_tau2Trimmed = saved;
        return v;
    };
    const double g0 = g(m_tau2);
    const double lo = std::max(m_tau1 * 1.01, m_tau2 * 0.5), hi = m_tau2 * 2.0;
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
        if (std::fabs(other - m_tau2) < std::fabs(down - m_tau2)) best = other;
    }
    m_tau2Trimmed = best;
}

Kick::Shape Kick::currentShape() const
{
    Shape s;
    s.m_engine = m_engine;
    s.m_fe = m_endHz;
    s.m_fs = m_startHz;
    s.m_tau1 = m_tau1;
    s.m_tau2 = m_tau2Trimmed;
    s.m_punch = m_punch;
    s.m_attack = m_attackSamples;
    s.m_hold = m_holdSamples;
    s.m_decayRate = -Ln1000 / (m_decaySeconds * m_sr);
    s.m_damping = std::exp(-Ln1000 / (m_decaySeconds * m_sr));
    return s;
}

void Kick::trigger(float velocity, double late)
{
    const Shape shape = currentShape();
    const bool resonator = shape.m_engine == static_cast<int>(KickEngineE::RESONATOR);
    // The resonator keeps its ring: a new trigger adds to it rather than replacing it. Everything else hands the old
    // voice to the 3 ms fade.
    const bool keepRing = resonator && m_voice.m_s.m_engine == shape.m_engine && m_voice.m_active;
    if (m_voice.m_active && !keepRing) { m_fade = m_voice; m_fadeGain = 1.0f; }
    const double zRe = keepRing ? m_voice.m_zRe : 0.0, zIm = keepRing ? m_voice.m_zIm : 0.0;
    Voice v;
    v.m_active = true;
    v.m_s = shape;
    v.m_late = late;
    v.m_n = 0;
    v.m_d1 = std::exp(-1.0 / (shape.m_tau1 * m_sr));
    v.m_d2 = std::exp(-1.0 / (shape.m_tau2 * m_sr));
    v.m_e1 = std::exp(-late / (shape.m_tau1 * m_sr));
    v.m_e2 = std::exp(-late / (shape.m_tau2 * m_sr));
    v.m_click = 1.0f;
    v.m_velocity = clampv(velocity, 0.0f, 1.0f);
    v.m_zRe = zRe;
    v.m_zIm = zIm;
    if (resonator) {
        // Energy into the real part, rotated to where the phasor is `late` samples after an ideal start: Im(z) then
        // rings as a sine of the injected amplitude starting at phase zero.
        const double w0 = 2.0 * PiD * shape.m_fs * late / m_sr;
        v.m_zRe += v.m_velocity * std::cos(w0);
        v.m_zIm += v.m_velocity * std::sin(w0);
    }
    m_voice = v;

    if (m_topLevel > 0.0f) {
        Top t;
        t.m_active = true;
        t.m_fe = m_endHz * m_topShift;
        t.m_fs = m_startHz * m_topShift;
        t.m_tau1 = m_tau1;
        t.m_tau2 = m_tau2;
        t.m_punch = m_punch;
        t.m_late = late;
        t.m_d1 = std::exp(-1.0 / (t.m_tau1 * m_sr));
        t.m_d2 = std::exp(-1.0 / (t.m_tau2 * m_sr));
        t.m_e1 = std::exp(-late / (t.m_tau1 * m_sr));
        t.m_e2 = std::exp(-late / (t.m_tau2 * m_sr));
        t.m_decay = std::exp(-Ln1000 / (m_topDecay * m_sr));
        t.m_amp = std::pow(t.m_decay, late);
        t.m_velocity = v.m_velocity;
        m_top = t;
    }
}

float Kick::voiceSample(Voice& v, float& click)
{
    const Shape& s = v.m_s;
    const double tSamples = v.m_n + v.m_late;
    const double t = tSamples / m_sr;
    float body;
    if (s.m_engine != static_cast<int>(KickEngineE::RESONATOR)) {
        const double a = s.m_fs - s.m_fe;
        const double phi = s.m_fe * t + a * ((1.0 - s.m_punch) * s.m_tau2 * (1.0 - v.m_e2) + s.m_punch * s.m_tau1 * (1.0 - v.m_e1));
        double amp;
        if (tSamples < s.m_attack) amp = tSamples / s.m_attack;
        else if (tSamples < s.m_attack + s.m_hold) amp = 1.0;
        else amp = std::exp(s.m_decayRate * (tSamples - s.m_attack - s.m_hold));
        const double wave = s.m_engine == static_cast<int>(KickEngineE::TR909) ? triangle(phi)
                                                                             : std::sin(2.0 * PiD * (phi - std::floor(phi)));
        body = static_cast<float>(wave * amp) * v.m_velocity;
        if (tSamples > s.m_attack + s.m_hold && amp < 1.0e-5) v.m_active = false;
    } else {
        const double f = s.m_fe + (s.m_fs - s.m_fe) * ((1.0 - s.m_punch) * v.m_e2 + s.m_punch * v.m_e1);
        const double w = 2.0 * PiD * std::min(f, 0.45 * m_sr) / m_sr;
        const double c = s.m_damping * std::cos(w), sn = s.m_damping * std::sin(w);
        const double re = v.m_zRe * c - v.m_zIm * sn;
        v.m_zIm = v.m_zRe * sn + v.m_zIm * c;
        v.m_zRe = re;
        body = static_cast<float>(v.m_zIm);
        if (v.m_n > 64 && v.m_zRe * v.m_zRe + v.m_zIm * v.m_zIm < 1.0e-12) v.m_active = false;
    }
    v.m_e1 *= v.m_d1;
    v.m_e2 *= v.m_d2;
    click = 0.0f;
    if (v.m_click > 1.0e-5f) {
        const float nz = m_noise.bipolar() * v.m_click + (v.m_n == 0 ? 1.0f : 0.0f);
        float lp, bp, hp;
        m_clickFilter.tick(nz, lp, bp, hp);
        click = bp * m_clickFilter.m_k * m_clickLevel * ClickGain * v.m_velocity;
        v.m_click *= m_clickDecay;
    }
    ++v.m_n;
    return body;
}

float Kick::topSample()
{
    Top& t = m_top;
    const double time = (t.m_n + t.m_late) / m_sr;
    const double a = t.m_fs - t.m_fe;
    const double phi = t.m_fe * time + a * ((1.0 - t.m_punch) * t.m_tau2 * (1.0 - t.m_e2) + t.m_punch * t.m_tau1 * (1.0 - t.m_e1));
    const float y = static_cast<float>(triangle(phi) * t.m_amp) * t.m_velocity;
    t.m_e1 *= t.m_d1;
    t.m_e2 *= t.m_d2;
    t.m_amp *= t.m_decay;
    ++t.m_n;
    if (t.m_amp < 1.0e-5) t.m_active = false;
    return y;
}

void Kick::process(float* out, float* body, int n)
{
    for (int i = 0; i < n; ++i) {
        float x = 0.0f, click = 0.0f, c = 0.0f;
        if (m_voice.m_active) { x += voiceSample(m_voice, c); click += c; }
        if (m_fade.m_active && m_fadeGain > 0.0f) {
            x += voiceSample(m_fade, c) * m_fadeGain;
            click += c * m_fadeGain;
            m_fadeGain -= m_fadeStep;
            if (m_fadeGain <= 0.0f) { m_fadeGain = 0.0f; m_fade.m_active = false; }
        }
        const float driven = x * m_drive;
        // The click joins after the saturator (Phosphene, 18.09.2026): inside it, it rode on the body's crest and was
        // flattened with it.
        float y = (m_clip == 0 ? m_tanh(driven) : m_hard(driven)) * m_driveNorm + click;
        y = m_toneFilter.lp(y);
        y = m_dc.process(y);
        float lp, bp, hp;
        m_lowCut.tick(y, lp, bp, hp);
        y = hp;
        m_dip.tick(y, lp, bp, hp);
        y += (m_dipGain - 1.0f) * bp * m_dip.m_k;
        y *= m_level;
        float top = 0.0f;
        if (m_top.m_active) {
            float tl, tb, th;
            const float s = m_topTanh(topSample() * m_topDrive) * m_topNorm;
            m_topHp1.tick(s, tl, tb, th);
            m_topHp2.tick(th, tl, tb, th);
            top = th * m_topLevel * m_level;
        } else {
            // Keep the high pass's state moving towards rest so a new hit starts from silence.
            float tl, tb, th;
            m_topHp1.tick(0.0f, tl, tb, th);
            m_topHp2.tick(th, tl, tb, th);
        }
        out[i] = y + top;
        if (body != nullptr) body[i] = y;
    }
}

} // namespace parh
