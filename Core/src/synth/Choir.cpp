/**
 * @file Choir.cpp
 * @brief The choir (Choir.h).
 */
#include "parh/synth/Choir.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr double kRdLo = 0.3, kRdHi = 2.7;

/** @brief Formants: frequency (Hz), level (dB), bandwidth (Hz) of five, for a voice on a vowel. */
struct Formants { double f[5], db[5], bw[5]; };
// [voice: bass, tenor, alto, soprano][vowel: a, o, u]
const Formants kFormants[4][3] = {
    { { { 600, 1040, 2250, 2450, 2750 }, { 0, -7, -9, -9, -20 }, { 60, 70, 110, 120, 130 } },
      { { 400, 750, 2400, 2600, 2900 }, { 0, -11, -21, -20, -40 }, { 40, 80, 100, 120, 120 } },
      { { 350, 600, 2400, 2675, 2950 }, { 0, -20, -32, -28, -36 }, { 40, 80, 100, 120, 120 } } },
    { { { 650, 1080, 2650, 2900, 3250 }, { 0, -6, -7, -8, -22 }, { 80, 90, 120, 130, 140 } },
      { { 400, 800, 2600, 2800, 3000 }, { 0, -10, -12, -12, -26 }, { 40, 80, 100, 120, 120 } },
      { { 350, 600, 2700, 2900, 3300 }, { 0, -20, -17, -14, -26 }, { 40, 60, 100, 120, 120 } } },
    { { { 800, 1150, 2800, 3500, 4950 }, { 0, -4, -20, -36, -60 }, { 80, 90, 120, 130, 140 } },
      { { 450, 800, 2830, 3500, 4950 }, { 0, -9, -16, -28, -55 }, { 70, 80, 100, 130, 135 } },
      { { 325, 700, 2530, 3500, 4950 }, { 0, -12, -30, -40, -64 }, { 50, 60, 170, 180, 200 } } },
    { { { 800, 1150, 2900, 3900, 4950 }, { 0, -6, -32, -20, -50 }, { 80, 90, 120, 130, 140 } },
      { { 450, 800, 2830, 3800, 4950 }, { 0, -11, -22, -22, -50 }, { 70, 80, 100, 130, 135 } },
      { { 325, 700, 2700, 3800, 4950 }, { 0, -16, -35, -40, -60 }, { 50, 60, 170, 180, 200 } } },
};

} // namespace

int Choir::voiceOf(int pitch)
{
    if (pitch < 52) return 0;
    if (pitch < 60) return 1;
    if (pitch < 67) return 2;
    return 3;
}

Choir::LfShape Choir::solveLf(double rd)
{
    // Fant 1995: the R parameters from Rd, the timing in a period of 1.
    const double ra = (-1.0 + 4.8 * rd) / 100.0, rk = (22.4 + 11.8 * rd) / 100.0;
    const double rg = rk / (4.0 * (0.11 * rd / (0.5 + 1.2 * rk) - ra));
    LfShape s;
    s.tp = 1.0 / (2.0 * rg);
    s.te = s.tp * (1.0 + rk);
    s.ta = std::max(ra, 1e-4);
    if (s.te > 0.98) s.te = 0.98;
    // eps: eps ta = 1 - e^(-eps (1 - te)) (Newton from 1 / ta).
    double eps = 1.0 / s.ta;
    for (int i = 0; i < 40; ++i) {
        const double e = std::exp(-eps * (1.0 - s.te));
        const double f = eps * s.ta - 1.0 + e, df = s.ta - (1.0 - s.te) * e;
        eps -= f / df;
    }
    s.eps = eps;
    // alpha: the flow back to zero over the period (bisection on the areas), E0 so that E(te) = -Ee = -1.
    const double w = kPiD / s.tp;
    const double retArea = -(1.0 / (eps * s.ta)) * ((1.0 - std::exp(-eps * (1.0 - s.te))) / eps - (1.0 - s.te) * std::exp(-eps * (1.0 - s.te)));
    auto total = [&](double a) {
        const double e0 = -1.0 / (std::exp(a * s.te) * std::sin(w * s.te));
        const double open = e0 * (std::exp(a * s.te) * (a * std::sin(w * s.te) - w * std::cos(w * s.te)) + w) / (a * a + w * w);
        return open + retArea;
    };
    double lo = -50.0, hi = 50.0;
    for (int i = 0; i < 100; ++i) {
        const double mid = 0.5 * (lo + hi);
        if ((total(lo) < 0.0) == (total(mid) < 0.0)) lo = mid; else hi = mid;
    }
    s.alpha = 0.5 * (lo + hi);
    s.e0 = -1.0 / (std::exp(s.alpha * s.te) * std::sin(w * s.te));
    return s;
}

void Choir::lfPeriod(double rd, int n, float* out) const
{
    const int i = std::clamp(static_cast<int>(std::lround((rd - kRdLo) / (kRdHi - kRdLo) * (kLfTable - 1))), 0, kLfTable - 1);
    const LfShape& s = lf_[i];
    const double w = kPiD / s.tp;
    for (int k = 0; k < n; ++k) {
        const double t = static_cast<double>(k) / n;
        out[k] = static_cast<float>(t < s.te ? s.e0 * std::exp(s.alpha * t) * std::sin(w * t)
                                             : -(1.0 / (s.eps * s.ta)) * (std::exp(-s.eps * (t - s.te)) - std::exp(-s.eps * (1.0 - s.te))));
    }
}

void Choir::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate;
    seed_ = seed;
    for (int i = 0; i < kLfTable; ++i) lf_[i] = solveLf(kRdLo + (kRdHi - kRdLo) * i / (kLfTable - 1.0));
    for (Svf& f : lowCut_) f.setQ(lowCutHz_, 0.707f, static_cast<float>(sr_));
    reset();
}

void Choir::update(const float* v)
{
    level_ = dbToGain(v[choir::Level]);
    setSingers(static_cast<int>(std::lround(v[choir::Singers])));
    const float vowel = v[choir::Vowel];
    vibrato_ = v[choir::Vibrato];
    breath_ = v[choir::Breath];
    tension_ = v[choir::Tension];
    attackMs_ = v[choir::Attack];
    releaseMs_ = v[choir::Release];
    width_ = v[choir::Width];
    if (v[choir::LowCut] != lowCutHz_) {
        lowCutHz_ = v[choir::LowCut];
        for (Svf& f : lowCut_) f.setQ(lowCutHz_, 0.707f, static_cast<float>(sr_));
    }
    if (vowel != vowel_) {
        vowel_ = vowel;
        for (Note& n : note_) if (n.on) setFormants(n);
    }
}

void Choir::reset()
{
    for (Singer& g : singer_) g = Singer{};
    for (Note& n : note_) {
        n.on = false;
        for (auto& bank : n.formant) for (Svf& f : bank) f.ic1 = f.ic2 = 0.0f;
    }
    for (Svf& f : lowCut_) f.ic1 = f.ic2 = 0.0f;
}

int Choir::activeSingers() const
{
    int n = 0;
    for (const Singer& g : singer_) n += g.on;
    return n;
}

void Choir::setFormants(Note& n)
{
    // The vowel glides a -> o -> u; frequencies geometric, levels in dB, bandwidths linear.
    const double x = std::clamp(static_cast<double>(vowel_), 0.0, 1.0) * 2.0;
    const int a = std::min(static_cast<int>(x), 1);
    const double u = x - a;
    const Formants& p = kFormants[n.voice][a];
    const Formants& q = kFormants[n.voice][a + 1];
    for (int k = 0; k < 5; ++k) {
        const double f = p.f[k] * std::pow(q.f[k] / p.f[k], u), bw = p.bw[k] + u * (q.bw[k] - p.bw[k]), db = p.db[k] + u * (q.db[k] - p.db[k]);
        n.gain[k] = dbToGain(static_cast<float>(db));
        for (int g = 0; g < 2; ++g) {
            // The two tracts: 3 % shorter and 3 % longer.
            const double fg = std::min(f * (g == 0 ? 1.03 : 0.97), 0.45 * sr_);
            n.formant[g][k].setQ(static_cast<float>(fg), static_cast<float>(fg / bw), static_cast<float>(sr_));
        }
    }
}

void Choir::noteOn(int pitch, float velocity, bool, double)
{
    int slot = -1;
    for (int s = 0; s < kChoirNotes && slot < 0; ++s) if (!note_[s].on) slot = s;
    if (slot < 0) {
        double oldest = -1.0;
        for (int s = 0; s < kChoirNotes; ++s) if (note_[s].t > oldest) { oldest = note_[s].t; slot = s; }
    }
    Note& n = note_[slot];
    n.on = true;
    n.held = true;
    n.pitch = pitch;
    n.voice = voiceOf(pitch);
    n.velocity = velocity;
    n.t = 0.0;
    n.released = -1.0;
    n.env = 0.0f;
    n.quiet = 0;
    for (auto& bank : n.formant) for (Svf& f : bank) f.ic1 = f.ic2 = 0.0f;
    setFormants(n);
    for (int k = 0; k < kChoirSingers; ++k) {
        Singer& g = singer_[slot * kChoirSingers + k];
        g = Singer{};
        if (k >= singers_) continue;
        g.on = true;
        g.note = slot;
        g.group = k & 1;
        g.rng.seed(mixSeed(seed_, (static_cast<uint64_t>(counter_++) << 4) + static_cast<uint64_t>(k)));
        g.cents = 5.0 * g.rng.gaussian() * 0.6;
        g.start = 0.06 * g.rng.uniform();
        g.vibHz = 4.8 + g.rng.uniform();
        g.vibCents = (15.0 + 20.0 * g.rng.uniform()) * vibrato_;
        g.vibPhase = 2.0 * kPiD * g.rng.uniform();
        g.wanderTo = 6.0 * g.rng.bipolar();
        g.amp = 0.8 + 0.4 * g.rng.uniform();
        startPeriod(g, n, 0.0);
    }
}

void Choir::noteOff(int pitch)
{
    for (Note& n : note_) if (n.on && n.held && n.pitch == pitch) { n.held = false; n.released = 0.0; }
}

void Choir::startPeriod(Singer& g, const Note& n, double tInto)
{
    // The pitch of this period: the note, the singer's intonation, vibrato (after a third of a second) and wander,
    // jitter on the length and shimmer on the amplitude.
    const double ramp = std::clamp((g.age - 0.3) / 0.4, 0.0, 1.0);
    const double cents = g.cents + g.wander + ramp * g.vibCents * std::sin(g.vibPhase);
    const double f0 = midiToHz(n.pitch) * std::pow(2.0, cents / 1200.0);
    g.T0 = (1.0 / f0) * (1.0 + 0.004 * g.rng.gaussian());
    g.wander += 0.05 * (g.wanderTo - g.wander);
    if (g.rng.uniform() < 0.01) g.wanderTo = 6.0 * g.rng.bipolar();
    // The shape at this period's Rd (a little per singer), scaled to the period.
    const double rd = std::clamp(2.7 - 1.9 * static_cast<double>(tension_) + 0.1 * g.rng.bipolar(), kRdLo, kRdHi);
    const double x = (rd - kRdLo) / (kRdHi - kRdLo) * (kLfTable - 1);
    const int i = std::min(static_cast<int>(x), kLfTable - 2);
    const double u = x - i;
    const LfShape& a = lf_[i];
    const LfShape& b = lf_[i + 1];
    g.s.tp = (a.tp + u * (b.tp - a.tp)) * g.T0;
    g.s.te = (a.te + u * (b.te - a.te)) * g.T0;
    g.s.ta = (a.ta + u * (b.ta - a.ta)) * g.T0;
    g.s.alpha = (a.alpha + u * (b.alpha - a.alpha)) / g.T0;
    g.s.eps = (a.eps + u * (b.eps - a.eps)) / g.T0;
    g.s.e0 = a.e0 + u * (b.e0 - a.e0);
    g.w = kPiD / g.s.tp;
    const double dt = 1.0 / sr_;
    g.t = tInto;
    // The open phase's phasor at tInto and its step.
    const double ea = std::exp(g.s.alpha * tInto);
    g.zr = ea * std::cos(g.w * tInto);
    g.zi = ea * std::sin(g.w * tInto);
    const double es = std::exp(g.s.alpha * dt);
    g.sr = es * std::cos(g.w * dt);
    g.si = es * std::sin(g.w * dt);
    g.retStep = std::exp(-g.s.eps * dt);
    g.retEnd = std::exp(-g.s.eps * (g.T0 - g.s.te));
    g.returning = false;
    g.amp = std::max(0.2, g.amp * (1.0 + 0.03 * g.rng.gaussian()) * 0.9 + 0.1);
}

void Choir::process(float* L, float* R, int n)
{
    const double dt = 1.0 / sr_;
    const float wl = 0.5f * (1.0f + width_), wr = 0.5f * (1.0f - width_);
    for (int i = 0; i < n; ++i) {
        float outL = 0.0f, outR = 0.0f;
        for (int s = 0; s < kChoirNotes; ++s) {
            Note& nt = note_[s];
            if (!nt.on) continue;
            // The note's envelope: a swell in, a fall after the release.
            const double att = static_cast<double>(attackMs_) * 1e-3, rel = static_cast<double>(releaseMs_) * 1e-3;
            double env = nt.t < att ? 0.5 - 0.5 * std::cos(kPiD * nt.t / att) : 1.0;
            if (!nt.held) { env *= std::max(0.0, 1.0 - nt.released / rel); nt.released += dt; }
            nt.t += dt;
            nt.env = static_cast<float>(env);
            float src[2] = { 0.0f, 0.0f };
            for (int k = 0; k < kChoirSingers; ++k) {
                Singer& g = singer_[s * kChoirSingers + k];
                if (!g.on) continue;
                g.age += dt;
                g.vibPhase += 2.0 * kPiD * g.vibHz * dt;
                if (g.age < g.start) continue;
                double e;
                if (g.t < g.s.te) {
                    e = g.s.e0 * g.zi;
                    const double zr = g.zr * g.sr - g.zi * g.si;
                    g.zi = g.zr * g.si + g.zi * g.sr;
                    g.zr = zr;
                } else {
                    if (!g.returning) { g.returning = true; g.ret = std::exp(-g.s.eps * (g.t - g.s.te)); }
                    e = -(1.0 / (g.s.eps * g.s.ta)) * (g.ret - g.retEnd);
                    g.ret *= g.retStep;
                }
                // Aspiration: noise, strongest while the glottis is open.
                const double open = g.t < g.s.te ? 1.0 : 0.3;
                e += static_cast<double>(breath_) * 0.35 * open * g.rng.bipolar();
                src[g.group] += static_cast<float>(e * g.amp);
                g.t += dt;
                if (g.t >= g.T0) startPeriod(g, nt, g.t - g.T0);
            }
            // The two tracts.
            const float amp = static_cast<float>(env) * (0.4f + 0.6f * nt.velocity);
            for (int grp = 0; grp < 2; ++grp) {
                float y = 0.0f;
                for (int f = 0; f < 5; ++f) {
                    float lp, bp, hp;
                    nt.formant[grp][f].tick(src[grp], lp, bp, hp);
                    y += nt.gain[f] * bp * nt.formant[grp][f].k;
                }
                y *= amp;
                outL += y * (grp == 0 ? wl : wr);
                outR += y * (grp == 0 ? wr : wl);
            }
            if (!nt.held && nt.released > rel + 0.3) {
                nt.on = false;
                for (int k = 0; k < kChoirSingers; ++k) singer_[s * kChoirSingers + k].on = false;
            }
        }
        float lp, bp, hp;
        lowCut_[0].tick(outL * level_ * 1.5f, lp, bp, hp);
        L[i] = hp;
        lowCut_[1].tick(outR * level_ * 1.5f, lp, bp, hp);
        R[i] = hp;
    }
}

} // namespace parh
