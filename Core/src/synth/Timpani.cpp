/**
 * @file Timpani.cpp
 * @brief The timpani (Timpani.h).
 * @author Rene Weller
 */
#include "parh/synth/Timpani.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace parh {

namespace {

constexpr double PiD = 3.14159265358979323846;   ///< pi
using cd = std::complex<double>;   ///< A complex number.

/** @brief A mode: (m, n), the zero j_mn of J_m, its frequency relative to (1,1) with the air, its decay (s, T60). */
struct Mode {
    int m_m;              ///< the nodal diameters
    double m_j;           ///< the zero j_mn of J_m
    double m_ratio;       ///< its frequency relative to (1,1), with the air
    double m_t60;         ///< its decay to -60 dB, s
    double m_radiation;   ///< how well it radiates
};
/// Rossing's measurements on a 65 cm kettle drum, rounded; the (0,n) modes short (they radiate as monopoles).
const Mode Measured[12] = {
    { 1, 3.832, 1.00, 3.0, 0.8 }, { 2, 5.136, 1.50, 2.2, 0.6 }, { 3, 6.380, 1.98, 1.6, 0.5 }, { 4, 7.588, 2.44, 1.2, 0.4 },
    { 5, 8.771, 2.90, 0.9, 0.3 }, { 6, 9.936, 3.35, 0.7, 0.25 }, { 0, 2.405, 0.85, 0.35, 1.0 }, { 0, 5.520, 1.70, 0.25, 0.6 },
    { 1, 7.016, 2.10, 0.45, 0.3 }, { 2, 8.417, 2.66, 0.4, 0.25 }, { 0, 8.654, 2.80, 0.2, 0.4 }, { 3, 9.761, 3.10, 0.35, 0.2 },
};

double besselJ(int m, double x);

/** @brief The whole set: the measured twelve and the ideal membrane's next modes, by frequency. */
struct ModeTable {
    Mode m_modes[TimpaniModes];   ///< the modes, by frequency
    ModeTable()
    {
        int n = 0;
        for (const Mode& md : Measured) m_modes[n++] = md;
        // The zeros of J_m for m = 0 .. 12 (bisection on a scan), those not measured, ascending.
        struct Z { int m_m; double m_j; };
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
        std::sort(zs, zs + nz, [](const Z& a, const Z& b) { return a.m_j < b.m_j; });
        for (int k = 0; k < nz && n < TimpaniModes; ++k) {
            bool known = false;
            for (const Mode& md : Measured) known = known || (md.m_m == zs[k].m_m && std::fabs(md.m_j - zs[k].m_j) < 0.01);
            if (known || zs[k].m_j < 10.0) continue;
            const double ratio = zs[k].m_j / 3.832 * 1.12;
            m_modes[n++] = { zs[k].m_m, zs[k].m_j, ratio, 0.35 / std::sqrt(ratio / 3.0), 0.3 };
        }
        for (; n < TimpaniModes; ++n) m_modes[n] = { 0, 2.405, 0.85, 0.0, 0.0 };
    }
};

/**
 * @brief The mode table, built on first use.
 * @return the mode table
 */
const ModeTable& table()
{
    static const ModeTable t;
    return t;
}

/**
 * @brief J_m(x), the Bessel function of the first kind, by its series (x < 12 here).
 * @param[in] m  the order
 * @param[in] x  the argument, below 12
 * @return J_m(x)
 */
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

constexpr double MalletMass = 0.02;   ///< the mallet's mass, kg
constexpr double MalletP = 2.3;   ///< the felt's exponent
constexpr double MembraneMass = 0.12;   ///< the membrane's modal mass, kg
constexpr int Os = 4;   ///< sub-steps per sample while the mallet touches

} // namespace

double Timpani::ratio(int i) { return table().m_modes[std::clamp(i, 0, TimpaniModes - 1)].m_ratio; }

void Timpani::prepare(double sampleRate, uint64_t seed)
{
    m_sr = sampleRate;
    m_seed = seed;
    m_mod.prepare(sampleRate, mixSeed(seed, 0x4D4F44ull));   // "MOD"
    for (Svf& f : m_lowCut) f.setQ(m_lowCutHz, 0.707f, static_cast<float>(m_sr));
    reset();
}

void Timpani::update(const float* v)
{
    m_level = dbToGain(v[timpani::LEVEL]);
    m_hardness = v[timpani::HARDNESS];
    m_decay = v[timpani::DECAY];
    m_strike = v[timpani::STRIKE];
    m_width = v[timpani::WIDTH];
    if (v[timpani::LOW_CUT] != m_lowCutHz) {
        m_lowCutHz = v[timpani::LOW_CUT];
        for (Svf& f : m_lowCut) f.setQ(m_lowCutHz, 0.707f, static_cast<float>(m_sr));
    }
    m_mod.set(readModCore(v + timpani::MOD_FIRST, TimpaniModDests, static_cast<int>(std::size(TimpaniModDests))));
}

void Timpani::modDrum(int k, bool strike)
{
    Drum& d = m_drum[k];
    m_mod.evaluate(k, m_pos);
    if (strike) {
        d.m_K = 8e7 * static_cast<double>(m_hardness) * std::exp2(std::clamp(static_cast<double>(m_mod.get(k, ModDestE::HARDNESS)), -3.0, 2.0));
        if (m_mod.targets(ModDestE::POSITION)) {
            const double strike01 = std::clamp(static_cast<double>(m_strike + m_mod.get(k, ModDestE::POSITION)), 0.05, 0.95);
            for (int m = 0; m < TimpaniModes; ++m) {
                const Mode& md = table().m_modes[m];
                d.m_shape[m] = besselJ(md.m_m, md.m_j * strike01) / 0.58;
            }
        }
    }
    // The pedal and the decay: the modes tuned again where they have moved.
    const double bend = std::pow(2.0, std::clamp(static_cast<double>(m_mod.get(k, ModDestE::PITCH)), -5.0, 5.0) / 12.0);
    const double decayMul = std::exp2(std::clamp(static_cast<double>(m_mod.get(k, ModDestE::DECAY)), -3.0, 2.0));
    if (std::fabs(bend - d.m_bend) > 1e-6 || std::fabs(decayMul - d.m_decayMul) > 1e-6) {
        double shape[TimpaniModes];
        for (int m = 0; m < TimpaniModes; ++m) shape[m] = d.m_shape[m];
        tune(d, d.m_pitch, bend, decayMul);
        for (int m = 0; m < TimpaniModes; ++m) d.m_shape[m] = shape[m];   // (the strike's place stays the strike's)
    }
    if (m_mod.targets(ModDestE::LEVEL) || m_mod.targets(ModDestE::PAN)) {
        const double gain = std::clamp(1.0 + static_cast<double>(m_mod.get(k, ModDestE::LEVEL)), 0.0, 2.0);
        const double pan = std::clamp(static_cast<double>(m_mod.get(k, ModDestE::PAN)), -1.0, 1.0);
        d.m_gainL = gain * std::cos((pan + 1.0) * PiD / 4.0) * 1.41421356;
        d.m_gainR = gain * std::sin((pan + 1.0) * PiD / 4.0) * 1.41421356;
    }
}

void Timpani::reset()
{
    for (Drum& d : m_drum) d = Drum{};
    for (Svf& f : m_lowCut) f.m_ic1 = f.m_ic2 = 0.0f;
    m_pos = 0;
    m_mod.reset();
}

void Timpani::tune(Drum& d, int pitch, double bend, double decayMul)
{
    d.m_pitch = pitch;
    d.m_bend = bend;
    d.m_decayMul = decayMul;
    const double f0 = midiToHz(pitch) * bend, T = 1.0 / m_sr;
    d.m_f0 = f0;
    for (int k = 0; k < TimpaniModes; ++k) {
        const Mode& md = table().m_modes[k];
        double w = 2.0 * PiD * f0 * md.m_ratio;
        if (w > 0.9 * PiD * m_sr) w = 0.9 * PiD * m_sr;
        const double sigma = md.m_t60 > 0.0 ? 6.91 / (md.m_t60 * static_cast<double>(m_decay) * decayMul) * std::sqrt(110.0 / f0) : 1e4;   // a smaller drum rings shorter
        const cd lam(-sigma, w);
        const cd p = std::exp(lam * T), g = (p - 1.0) / lam, ps = std::exp(lam * (T / Os)), gs = (ps - 1.0) / lam;
        d.m_pr[k] = p.real(); d.m_pi[k] = p.imag(); d.m_gr[k] = g.real(); d.m_gi[k] = g.imag();
        d.m_spr[k] = ps.real(); d.m_spi[k] = ps.imag(); d.m_sgr[k] = gs.real(); d.m_sgi[k] = gs.imag();
        // The shape at the strike point (r / a = m_strike), at angle 0; normalised by the mode's own peak (J_m's first
        // maximum is under 0.6 for every m here).
        d.m_shape[k] = besselJ(md.m_m, md.m_j * static_cast<double>(m_strike)) / 0.58;
        // Listening at +-40 degrees: cos(m theta) sets the image.
        const double th = 40.0 * PiD / 180.0 * static_cast<double>(m_width);
        d.m_outL[k] = md.m_radiation * std::cos(md.m_m * th);
        d.m_outR[k] = md.m_radiation * std::cos(md.m_m * -th + (md.m_m % 2 == 1 ? PiD * static_cast<double>(m_width) * 0.25 : 0.0));
    }
}

void Timpani::noteOn(int pitch, float velocity, bool, double)
{
    // The drum tuned to this pitch, else the one resting longest (retuned).
    int at = -1;
    for (int k = 0; k < TimpaniDrums && at < 0; ++k) if (m_drum[k].m_on && m_drum[k].m_pitch == pitch) at = k;
    if (at < 0) {
        double oldest = -1.0;
        for (int k = 0; k < TimpaniDrums; ++k) {
            const double age = m_drum[k].m_on ? m_drum[k].m_age : 1e9;
            if (age > oldest) { oldest = age; at = k; }
        }
    }
    Drum& d = m_drum[at];
    if (!d.m_on || d.m_pitch != pitch) {
        for (int k = 0; k < TimpaniModes; ++k) d.m_zr[k] = d.m_zi[k] = 0.0;
        tune(d, pitch);
    }
    d.m_on = true;
    d.m_age = 0.0;
    d.m_quiet = 0;
    d.m_K = 8e7 * static_cast<double>(m_hardness);
    d.m_gainL = d.m_gainR = 1.0;
    m_mod.noteOn(at, m_pos, pitch, velocity);
    if (m_mod.on()) modDrum(at, true);
    // The mallet meets the membrane where it is.
    double ym = 0.0;
    const double fm = m_mod.on() ? d.m_f0 : midiToHz(pitch);
    for (int k = 0; k < TimpaniModes; ++k) ym += d.m_shape[k] * d.m_zi[k] / (2.0 * PiD * fm * table().m_modes[k].m_ratio);
    d.m_yh = ym;
    d.m_vh = 0.5 + 3.5 * static_cast<double>(velocity);
    d.m_contact = true;
    d.m_steps = 0;
    d.m_apart = 0;
}

void Timpani::process(float* L, float* R, int n)
{
    const double T = 1.0 / m_sr, Ts = T / Os;
    const bool modded = m_mod.on();
    const double K0 = 8e7 * static_cast<double>(m_hardness);
    for (int i = 0; i < n; ++i) {
        if (modded && m_pos % 32 == 0)
            for (int k = 0; k < TimpaniDrums; ++k) if (m_drum[k].m_on) modDrum(k, false);
        ++m_pos;
        double outL = 0.0, outR = 0.0;
        for (Drum& d : m_drum) {
            if (!d.m_on) continue;
            d.m_age += T;
            const double f0 = modded ? d.m_f0 : midiToHz(d.m_pitch);
            const double K = modded ? d.m_K : K0;
            if (d.m_contact) {
                for (int s = 0; s < Os; ++s) {
                    // The membrane's displacement under the mallet: Re(-i / w z) per mode, times its shape.
                    double ym = 0.0;
                    for (int k = 0; k < TimpaniModes; ++k) ym += d.m_shape[k] * d.m_zi[k] / (2.0 * PiD * f0 * table().m_modes[k].m_ratio);
                    const double u = d.m_yh - ym;
                    const double F = u > 0.0 ? K * std::pow(u, MalletP) : 0.0;
                    d.m_vh -= F / MalletMass * Ts;
                    d.m_yh += d.m_vh * Ts;
                    for (int k = 0; k < TimpaniModes; ++k) {
                        const double in = d.m_shape[k] * F / MembraneMass;
                        const double r = d.m_spr[k] * d.m_zr[k] - d.m_spi[k] * d.m_zi[k] + d.m_sgr[k] * in;
                        const double q = d.m_spr[k] * d.m_zi[k] + d.m_spi[k] * d.m_zr[k] + d.m_sgi[k] * in;
                        d.m_zr[k] = r;
                        d.m_zi[k] = q;
                    }
                    d.m_apart = u > 0.0 ? 0 : d.m_apart + 1;
                    ++d.m_steps;
                }
                if ((d.m_apart > 16 * Os && d.m_vh < 0.0) || d.m_steps > static_cast<int>(0.05 * m_sr) * Os) d.m_contact = false;
            } else {
                for (int k = 0; k < TimpaniModes; ++k) {
                    const double r = d.m_pr[k] * d.m_zr[k] - d.m_pi[k] * d.m_zi[k];
                    const double q = d.m_pr[k] * d.m_zi[k] + d.m_pi[k] * d.m_zr[k];
                    d.m_zr[k] = r;
                    d.m_zi[k] = q;
                }
            }
            // Radiated: the modes' velocities (Re z), weighted.
            double peak = 0.0;
            if (!modded) {
                for (int k = 0; k < TimpaniModes; ++k) {
                    outL += d.m_outL[k] * d.m_zr[k];
                    outR += d.m_outR[k] * d.m_zr[k];
                    peak = std::max(peak, std::fabs(d.m_zr[k]));
                }
            } else {
                double dl = 0.0, dr = 0.0;
                for (int k = 0; k < TimpaniModes; ++k) {
                    dl += d.m_outL[k] * d.m_zr[k];
                    dr += d.m_outR[k] * d.m_zr[k];
                    peak = std::max(peak, std::fabs(d.m_zr[k]));
                }
                outL += dl * d.m_gainL;
                outR += dr * d.m_gainR;
            }
            d.m_quiet = !d.m_contact && peak < 1e-7 ? d.m_quiet + 1 : 0;
            if (d.m_quiet > 4800) d.m_on = false;
        }
        float lp, bp, hp;
        m_lowCut[0].tick(static_cast<float>(outL) * m_level * 0.5f, lp, bp, hp);
        L[i] = hp;
        m_lowCut[1].tick(static_cast<float>(outR) * m_level * 0.5f, lp, bp, hp);
        R[i] = hp;
    }
}

} // namespace parh
