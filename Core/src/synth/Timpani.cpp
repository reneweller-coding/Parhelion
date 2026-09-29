/**
 * @file Timpani.cpp
 * @brief The timpani (Timpani.h).
 */
#include "parh/synth/Timpani.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace parh {

namespace {

constexpr double kPiD = 3.14159265358979323846;
using cd = std::complex<double>;

/** @brief A mode: (m, n), the zero j_mn of J_m, its frequency relative to (1,1) with the air, its decay (s, T60). */
struct Mode { int m; double j, ratio, t60, radiation; };
// Rossing's measurements on a 65 cm kettle drum, rounded; the (0,n) modes short (they radiate as monopoles).
const Mode kMeasured[12] = {
    { 1, 3.832, 1.00, 3.0, 0.8 }, { 2, 5.136, 1.50, 2.2, 0.6 }, { 3, 6.380, 1.98, 1.6, 0.5 }, { 4, 7.588, 2.44, 1.2, 0.4 },
    { 5, 8.771, 2.90, 0.9, 0.3 }, { 6, 9.936, 3.35, 0.7, 0.25 }, { 0, 2.405, 0.85, 0.35, 1.0 }, { 0, 5.520, 1.70, 0.25, 0.6 },
    { 1, 7.016, 2.10, 0.45, 0.3 }, { 2, 8.417, 2.66, 0.4, 0.25 }, { 0, 8.654, 2.80, 0.2, 0.4 }, { 3, 9.761, 3.10, 0.35, 0.2 },
};

double besselJ(int m, double x);

/** @brief The whole set: the measured twelve and the ideal membrane's next modes, by frequency. */
struct ModeTable {
    Mode modes[kTimpaniModes];
    ModeTable()
    {
        int n = 0;
        for (const Mode& md : kMeasured) modes[n++] = md;
        // The zeros of J_m for m = 0 .. 12 (bisection on a scan), those not measured, ascending.
        struct Z { int m; double j; };
        Z zs[200];
        int nz = 0;
        for (int m = 0; m <= 12; ++m) {
            double prev = besselJ(m, 0.5 + m), x0 = 0.5 + m;
            for (double x = 0.6 + m; x < 24.0 && nz < 200; x += 0.1) {
                const double v = besselJ(m, x);
                if ((v < 0.0) != (prev < 0.0)) {
                    double lo = x0, hi = x;
                    for (int it = 0; it < 60; ++it) {
                        const double mid = 0.5 * (lo + hi);
                        if ((besselJ(m, mid) < 0.0) == (besselJ(m, lo) < 0.0)) lo = mid; else hi = mid;
                    }
                    zs[nz++] = { m, 0.5 * (lo + hi) };
                }
                prev = v;
                x0 = x;
            }
        }
        std::sort(zs, zs + nz, [](const Z& a, const Z& b) { return a.j < b.j; });
        for (int k = 0; k < nz && n < kTimpaniModes; ++k) {
            bool known = false;
            for (const Mode& md : kMeasured) known = known || (md.m == zs[k].m && std::fabs(md.j - zs[k].j) < 0.01);
            if (known || zs[k].j < 10.0) continue;
            const double ratio = zs[k].j / 3.832 * 1.12;
            modes[n++] = { zs[k].m, zs[k].j, ratio, 0.35 / std::sqrt(ratio / 3.0), 0.3 };
        }
        for (; n < kTimpaniModes; ++n) modes[n] = { 0, 2.405, 0.85, 0.0, 0.0 };
    }
};

const ModeTable& table()
{
    static const ModeTable t;
    return t;
}

/** @brief J_m(x), the Bessel function of the first kind, by its series (x < 12 here). */
double besselJ(int m, double x)
{
    double term = 1.0;
    for (int k = 1; k <= m; ++k) term *= x / 2.0 / k;
    double sum = term;
    for (int k = 1; k < 60; ++k) {
        term *= -(x * x / 4.0) / (k * (k + m));
        sum += term;
        if (std::fabs(term) < 1e-15 * std::fabs(sum)) break;
    }
    return sum;
}

constexpr double kMalletMass = 0.02, kMalletP = 2.3, kMembraneMass = 0.12;   ///< kg, exponent, kg (the membrane's modal mass)
constexpr int kOs = 4;

} // namespace

double Timpani::ratio(int i) { return table().modes[std::clamp(i, 0, kTimpaniModes - 1)].ratio; }

void Timpani::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate;
    seed_ = seed;
    mod_.prepare(sampleRate, mixSeed(seed, 0x4D4F44ull));   // "MOD"
    for (Svf& f : lowCut_) f.setQ(lowCutHz_, 0.707f, static_cast<float>(sr_));
    reset();
}

void Timpani::update(const float* v)
{
    level_ = dbToGain(v[timpani::Level]);
    hardness_ = v[timpani::Hardness];
    decay_ = v[timpani::Decay];
    strike_ = v[timpani::Strike];
    width_ = v[timpani::Width];
    if (v[timpani::LowCut] != lowCutHz_) {
        lowCutHz_ = v[timpani::LowCut];
        for (Svf& f : lowCut_) f.setQ(lowCutHz_, 0.707f, static_cast<float>(sr_));
    }
    mod_.set(readModCore(v + timpani::ModFirst, kTimpaniModDests, static_cast<int>(std::size(kTimpaniModDests))));
}

void Timpani::modDrum(int k, bool strike)
{
    Drum& d = drum_[k];
    mod_.evaluate(k, pos_);
    if (strike) {
        d.K = 8e7 * static_cast<double>(hardness_) * std::exp2(std::clamp(static_cast<double>(mod_.get(k, ModDest::Hardness)), -3.0, 2.0));
        if (mod_.targets(ModDest::Position)) {
            const double strike01 = std::clamp(static_cast<double>(strike_ + mod_.get(k, ModDest::Position)), 0.05, 0.95);
            for (int m = 0; m < kTimpaniModes; ++m) {
                const Mode& md = table().modes[m];
                d.shape[m] = besselJ(md.m, md.j * strike01) / 0.58;
            }
        }
    }
    // The pedal and the decay: the modes tuned again where they have moved.
    const double bend = std::pow(2.0, std::clamp(static_cast<double>(mod_.get(k, ModDest::Pitch)), -5.0, 5.0) / 12.0);
    const double decayMul = std::exp2(std::clamp(static_cast<double>(mod_.get(k, ModDest::Decay)), -3.0, 2.0));
    if (std::fabs(bend - d.bend) > 1e-6 || std::fabs(decayMul - d.decayMul) > 1e-6) {
        double shape[kTimpaniModes];
        for (int m = 0; m < kTimpaniModes; ++m) shape[m] = d.shape[m];
        tune(d, d.pitch, bend, decayMul);
        for (int m = 0; m < kTimpaniModes; ++m) d.shape[m] = shape[m];   // (the strike's place stays the strike's)
    }
    if (mod_.targets(ModDest::Level) || mod_.targets(ModDest::Pan)) {
        const double gain = std::clamp(1.0 + static_cast<double>(mod_.get(k, ModDest::Level)), 0.0, 2.0);
        const double pan = std::clamp(static_cast<double>(mod_.get(k, ModDest::Pan)), -1.0, 1.0);
        d.gainL = gain * std::cos((pan + 1.0) * kPiD / 4.0) * 1.41421356;
        d.gainR = gain * std::sin((pan + 1.0) * kPiD / 4.0) * 1.41421356;
    }
}

void Timpani::reset()
{
    for (Drum& d : drum_) d = Drum{};
    for (Svf& f : lowCut_) f.ic1 = f.ic2 = 0.0f;
    pos_ = 0;
    mod_.reset();
}

void Timpani::tune(Drum& d, int pitch, double bend, double decayMul)
{
    d.pitch = pitch;
    d.bend = bend;
    d.decayMul = decayMul;
    const double f0 = midiToHz(pitch) * bend, T = 1.0 / sr_;
    d.f0 = f0;
    for (int k = 0; k < kTimpaniModes; ++k) {
        const Mode& md = table().modes[k];
        double w = 2.0 * kPiD * f0 * md.ratio;
        if (w > 0.9 * kPiD * sr_) w = 0.9 * kPiD * sr_;
        const double sigma = md.t60 > 0.0 ? 6.91 / (md.t60 * static_cast<double>(decay_) * decayMul) * std::sqrt(110.0 / f0) : 1e4;   // a smaller drum rings shorter
        const cd lam(-sigma, w);
        const cd p = std::exp(lam * T), g = (p - 1.0) / lam, ps = std::exp(lam * (T / kOs)), gs = (ps - 1.0) / lam;
        d.pr[k] = p.real(); d.pi[k] = p.imag(); d.gr[k] = g.real(); d.gi[k] = g.imag();
        d.spr[k] = ps.real(); d.spi[k] = ps.imag(); d.sgr[k] = gs.real(); d.sgi[k] = gs.imag();
        // The shape at the strike point (r / a = strike_), at angle 0; normalised by the mode's own peak (J_m's first
        // maximum is under 0.6 for every m here).
        d.shape[k] = besselJ(md.m, md.j * static_cast<double>(strike_)) / 0.58;
        // Listening at +-40 degrees: cos(m theta) sets the image.
        const double th = 40.0 * kPiD / 180.0 * static_cast<double>(width_);
        d.outL[k] = md.radiation * std::cos(md.m * th);
        d.outR[k] = md.radiation * std::cos(md.m * -th + (md.m % 2 == 1 ? kPiD * static_cast<double>(width_) * 0.25 : 0.0));
    }
}

void Timpani::noteOn(int pitch, float velocity, bool, double)
{
    // The drum tuned to this pitch, else the one resting longest (retuned).
    int at = -1;
    for (int k = 0; k < kTimpaniDrums && at < 0; ++k) if (drum_[k].on && drum_[k].pitch == pitch) at = k;
    if (at < 0) {
        double oldest = -1.0;
        for (int k = 0; k < kTimpaniDrums; ++k) {
            const double age = drum_[k].on ? drum_[k].age : 1e9;
            if (age > oldest) { oldest = age; at = k; }
        }
    }
    Drum& d = drum_[at];
    if (!d.on || d.pitch != pitch) {
        for (int k = 0; k < kTimpaniModes; ++k) d.zr[k] = d.zi[k] = 0.0;
        tune(d, pitch);
    }
    d.on = true;
    d.age = 0.0;
    d.quiet = 0;
    d.K = 8e7 * static_cast<double>(hardness_);
    d.gainL = d.gainR = 1.0;
    mod_.noteOn(at, pos_, pitch, velocity);
    if (mod_.on()) modDrum(at, true);
    // The mallet meets the membrane where it is.
    double ym = 0.0;
    const double fm = mod_.on() ? d.f0 : midiToHz(pitch);
    for (int k = 0; k < kTimpaniModes; ++k) ym += d.shape[k] * d.zi[k] / (2.0 * kPiD * fm * table().modes[k].ratio);
    d.yh = ym;
    d.vh = 0.5 + 3.5 * static_cast<double>(velocity);
    d.contact = true;
    d.steps = 0;
    d.apart = 0;
}

void Timpani::process(float* L, float* R, int n)
{
    const double T = 1.0 / sr_, Ts = T / kOs;
    const bool modded = mod_.on();
    const double K0 = 8e7 * static_cast<double>(hardness_);
    for (int i = 0; i < n; ++i) {
        if (modded && pos_ % 32 == 0)
            for (int k = 0; k < kTimpaniDrums; ++k) if (drum_[k].on) modDrum(k, false);
        ++pos_;
        double outL = 0.0, outR = 0.0;
        for (Drum& d : drum_) {
            if (!d.on) continue;
            d.age += T;
            const double f0 = modded ? d.f0 : midiToHz(d.pitch);
            const double K = modded ? d.K : K0;
            if (d.contact) {
                for (int s = 0; s < kOs; ++s) {
                    // The membrane's displacement under the mallet: Re(-i / w z) per mode, times its shape.
                    double ym = 0.0;
                    for (int k = 0; k < kTimpaniModes; ++k) ym += d.shape[k] * d.zi[k] / (2.0 * kPiD * f0 * table().modes[k].ratio);
                    const double u = d.yh - ym;
                    const double F = u > 0.0 ? K * std::pow(u, kMalletP) : 0.0;
                    d.vh -= F / kMalletMass * Ts;
                    d.yh += d.vh * Ts;
                    for (int k = 0; k < kTimpaniModes; ++k) {
                        const double in = d.shape[k] * F / kMembraneMass;
                        const double r = d.spr[k] * d.zr[k] - d.spi[k] * d.zi[k] + d.sgr[k] * in;
                        const double q = d.spr[k] * d.zi[k] + d.spi[k] * d.zr[k] + d.sgi[k] * in;
                        d.zr[k] = r;
                        d.zi[k] = q;
                    }
                    d.apart = u > 0.0 ? 0 : d.apart + 1;
                    ++d.steps;
                }
                if ((d.apart > 16 * kOs && d.vh < 0.0) || d.steps > static_cast<int>(0.05 * sr_) * kOs) d.contact = false;
            } else {
                for (int k = 0; k < kTimpaniModes; ++k) {
                    const double r = d.pr[k] * d.zr[k] - d.pi[k] * d.zi[k];
                    const double q = d.pr[k] * d.zi[k] + d.pi[k] * d.zr[k];
                    d.zr[k] = r;
                    d.zi[k] = q;
                }
            }
            // Radiated: the modes' velocities (Re z), weighted.
            double peak = 0.0;
            if (!modded) {
                for (int k = 0; k < kTimpaniModes; ++k) {
                    outL += d.outL[k] * d.zr[k];
                    outR += d.outR[k] * d.zr[k];
                    peak = std::max(peak, std::fabs(d.zr[k]));
                }
            } else {
                double dl = 0.0, dr = 0.0;
                for (int k = 0; k < kTimpaniModes; ++k) {
                    dl += d.outL[k] * d.zr[k];
                    dr += d.outR[k] * d.zr[k];
                    peak = std::max(peak, std::fabs(d.zr[k]));
                }
                outL += dl * d.gainL;
                outR += dr * d.gainR;
            }
            d.quiet = !d.contact && peak < 1e-7 ? d.quiet + 1 : 0;
            if (d.quiet > 4800) d.on = false;
        }
        float lp, bp, hp;
        lowCut_[0].tick(static_cast<float>(outL) * level_ * 0.5f, lp, bp, hp);
        L[i] = hp;
        lowCut_[1].tick(static_cast<float>(outR) * level_ * 0.5f, lp, bp, hp);
        R[i] = hp;
    }
}

} // namespace parh
