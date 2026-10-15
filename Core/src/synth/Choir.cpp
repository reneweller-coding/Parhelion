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

constexpr double PiD = 3.14159265358979323846;   ///< pi
constexpr double RdLo = 0.3;   ///< Rd's low end: pressed
constexpr double RdHi = 2.7;   ///< Rd's high end: lax and breathy

/** @brief Formants: frequency (Hz), level (dB), bandwidth (Hz) of five, for a voice on a vowel. */
struct Formants {
    double m_f[5];    ///< the frequencies, Hz
    double m_db[5];   ///< the levels, dB
    double m_bw[5];   ///< the bandwidths, Hz
};
/// [voice: bass, tenor, alto, soprano][vowel: a, o, u]
const Formants FormantTable[4][3] = {
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
    s.m_tp = 1.0 / (2.0 * rg);
    s.m_te = s.m_tp * (1.0 + rk);
    s.m_ta = std::max(ra, 1e-4);
    if (s.m_te > 0.98) s.m_te = 0.98;
    // eps: eps ta = 1 - e^(-eps (1 - te)) (Newton from 1 / ta).
    double eps = 1.0 / s.m_ta;
    for (int i = 0; i < 40; ++i) {
        const double e = std::exp(-eps * (1.0 - s.m_te));
        const double f = eps * s.m_ta - 1.0 + e, df = s.m_ta - (1.0 - s.m_te) * e;
        eps -= f / df;
    }
    s.m_eps = eps;
    // alpha: the flow back to zero over the period (bisection on the areas), E0 so that E(te) = -Ee = -1.
    const double w = PiD / s.m_tp;
    const double retArea = -(1.0 / (eps * s.m_ta)) * ((1.0 - std::exp(-eps * (1.0 - s.m_te))) / eps - (1.0 - s.m_te) * std::exp(-eps * (1.0 - s.m_te)));
    auto total = [&](double a) {
        const double e0 = -1.0 / (std::exp(a * s.m_te) * std::sin(w * s.m_te));
        const double open = e0 * (std::exp(a * s.m_te) * (a * std::sin(w * s.m_te) - w * std::cos(w * s.m_te)) + w) / (a * a + w * w);
        return open + retArea;
    };
    double lo = -50.0, hi = 50.0;
    for (int i = 0; i < 100; ++i) {
        const double mid = 0.5 * (lo + hi);
        if ((total(lo) < 0.0) == (total(mid) < 0.0)) lo = mid; else hi = mid;
    }
    s.m_alpha = 0.5 * (lo + hi);
    s.m_e0 = -1.0 / (std::exp(s.m_alpha * s.m_te) * std::sin(w * s.m_te));
    return s;
}

void Choir::lfPeriod(double rd, int n, float* out) const
{
    const int i = std::clamp(static_cast<int>(std::lround((rd - RdLo) / (RdHi - RdLo) * (LfTable - 1))), 0, LfTable - 1);
    const LfShape& s = m_lf[i];
    const double w = PiD / s.m_tp;
    for (int k = 0; k < n; ++k) {
        const double t = static_cast<double>(k) / n;
        out[k] = static_cast<float>(t < s.m_te ? s.m_e0 * std::exp(s.m_alpha * t) * std::sin(w * t)
                                             : -(1.0 / (s.m_eps * s.m_ta)) * (std::exp(-s.m_eps * (t - s.m_te)) - std::exp(-s.m_eps * (1.0 - s.m_te))));
    }
}

void Choir::prepare(double sampleRate, uint64_t seed)
{
    m_sr = sampleRate;
    m_seed = seed;
    m_mod.prepare(sampleRate, mixSeed(seed, 0x4D4F44ull));   // "MOD"
    for (int i = 0; i < LfTable; ++i) m_lf[i] = solveLf(RdLo + (RdHi - RdLo) * i / (LfTable - 1.0));
    for (Svf& f : m_lowCut) f.setQ(m_lowCutHz, 0.707f, static_cast<float>(m_sr));
    reset();
}

void Choir::update(const float* v)
{
    m_level = dbToGain(v[choir::LEVEL]);
    setSingers(static_cast<int>(std::lround(v[choir::SINGERS])));
    const float vowel = v[choir::VOWEL];
    m_vibrato = v[choir::VIBRATO];
    m_breath = v[choir::BREATH];
    m_tension = v[choir::TENSION];
    m_attackMs = v[choir::ATTACK];
    m_releaseMs = v[choir::RELEASE];
    m_width = v[choir::WIDTH];
    if (v[choir::LOW_CUT] != m_lowCutHz) {
        m_lowCutHz = v[choir::LOW_CUT];
        for (Svf& f : m_lowCut) f.setQ(m_lowCutHz, 0.707f, static_cast<float>(m_sr));
    }
    m_mod.set(readModCore(v + choir::MOD_FIRST, ChoirModDests, static_cast<int>(std::size(ChoirModDests))));
    // A note no slot moves takes the knobs as they are.
    for (Note& n : m_note) {
        if (!m_mod.targets(ModDestE::TENSION)) n.m_tension = m_tension;
        if (!m_mod.targets(ModDestE::BREATH)) n.m_breath = m_breath;
    }
    if (vowel != m_vowel) {
        m_vowel = vowel;
        for (Note& n : m_note) {
            if (m_mod.targets(ModDestE::VOWEL) || m_mod.targets(ModDestE::FORMANT)) continue;   // (the cell sets it)
            n.m_vowel = m_vowel;
            if (n.m_on) setFormants(n);
        }
    }
}

void Choir::modCell()
{
    for (int s = 0; s < ChoirNotes; ++s) if (m_note[s].m_on) modNote(s);
}

void Choir::modNote(int s)
{
    {
        Note& n = m_note[s];
        m_mod.evaluate(s, m_pos);
        n.m_pitchSt = std::clamp(static_cast<double>(m_mod.get(s, ModDestE::PITCH)), -12.0, 12.0);
        n.m_vib = std::clamp(1.0f + m_mod.get(s, ModDestE::VIB_DEPTH), 0.0f, 4.0f);
        n.m_tension = std::clamp(m_tension + m_mod.get(s, ModDestE::TENSION), 0.0f, 1.0f);
        n.m_breath = std::clamp(m_breath + m_mod.get(s, ModDestE::BREATH), 0.0f, 1.0f);
        n.m_level = std::clamp(1.0f + m_mod.get(s, ModDestE::LEVEL), 0.0f, 2.0f);
        const float pan = std::clamp(m_mod.get(s, ModDestE::PAN), -1.0f, 1.0f);
        n.m_panL = m_mod.targets(ModDestE::PAN) ? std::cos((pan + 1.0f) * Pi / 4.0f) * 1.41421356f : 1.0f;
        n.m_panR = m_mod.targets(ModDestE::PAN) ? std::sin((pan + 1.0f) * Pi / 4.0f) * 1.41421356f : 1.0f;
        // The vowel and the tract's length: new formants where they have moved.
        const float vowel = std::clamp(m_vowel + m_mod.get(s, ModDestE::VOWEL), 0.0f, 1.0f);
        const float fm = std::exp2(0.5f * std::clamp(m_mod.get(s, ModDestE::FORMANT), -1.0f, 1.0f));
        if (std::fabs(vowel - n.m_vowel) > 1e-3f || std::fabs(fm - n.m_formantMul) > 1e-3f) {
            n.m_vowel = vowel;
            n.m_formantMul = fm;
            setFormants(n);
        }
    }
}

void Choir::reset()
{
    for (Singer& g : m_singer) g = Singer{};
    for (Note& n : m_note) {
        n.m_on = false;
        for (auto& bank : n.m_formant) for (Svf& f : bank) f.m_ic1 = f.m_ic2 = 0.0f;
    }
    for (Svf& f : m_lowCut) f.m_ic1 = f.m_ic2 = 0.0f;
    m_pos = 0;
    m_mod.reset();
}

int Choir::activeSingers() const
{
    int n = 0;
    for (const Singer& g : m_singer) n += g.m_on;
    return n;
}

void Choir::setFormants(Note& n)
{
    // The vowel glides a -> o -> u; frequencies geometric, levels in dB, bandwidths linear.
    const double x = std::clamp(static_cast<double>(n.m_vowel), 0.0, 1.0) * 2.0;
    const int a = std::min(static_cast<int>(x), 1);
    const double u = x - a;
    const Formants& p = FormantTable[n.m_voice][a];
    const Formants& q = FormantTable[n.m_voice][a + 1];
    for (int k = 0; k < 5; ++k) {
        const double f = p.m_f[k] * std::pow(q.m_f[k] / p.m_f[k], u), bw = p.m_bw[k] + u * (q.m_bw[k] - p.m_bw[k]), db = p.m_db[k] + u * (q.m_db[k] - p.m_db[k]);
        n.m_gain[k] = dbToGain(static_cast<float>(db));
        for (int g = 0; g < 2; ++g) {
            // The two tracts: 3 % shorter and 3 % longer.
            const double fg = std::min(f * static_cast<double>(n.m_formantMul) * (g == 0 ? 1.03 : 0.97), 0.45 * m_sr);
            n.m_formant[g][k].setQ(static_cast<float>(fg), static_cast<float>(fg / bw), static_cast<float>(m_sr));
        }
    }
}

void Choir::noteOn(int pitch, float velocity, bool, double)
{
    int slot = -1;
    for (int s = 0; s < ChoirNotes && slot < 0; ++s) if (!m_note[s].m_on) slot = s;
    if (slot < 0) {
        double oldest = -1.0;
        for (int s = 0; s < ChoirNotes; ++s) if (m_note[s].m_t > oldest) { oldest = m_note[s].m_t; slot = s; }
    }
    Note& n = m_note[slot];
    n.m_on = true;
    n.m_held = true;
    n.m_pitch = pitch;
    n.m_voice = voiceOf(pitch);
    n.m_velocity = velocity;
    n.m_t = 0.0;
    n.m_released = -1.0;
    n.m_env = 0.0f;
    n.m_quiet = 0;
    n.m_vowel = m_vowel;
    n.m_formantMul = 1.0f;
    n.m_tension = m_tension;
    n.m_breath = m_breath;
    n.m_vib = 1.0f;
    n.m_level = n.m_panL = n.m_panR = 1.0f;
    n.m_pitchSt = 0.0;
    m_mod.noteOn(slot, m_pos, pitch, velocity);
    if (m_mod.on()) modNote(slot);   // (the note's first values before its first period)
    for (auto& bank : n.m_formant) for (Svf& f : bank) f.m_ic1 = f.m_ic2 = 0.0f;
    setFormants(n);
    for (int k = 0; k < ChoirSingers; ++k) {
        Singer& g = m_singer[slot * ChoirSingers + k];
        g = Singer{};
        if (k >= m_singers) continue;
        g.m_on = true;
        g.m_note = slot;
        g.m_group = k & 1;
        g.m_rng.seed(mixSeed(m_seed, (static_cast<uint64_t>(m_counter++) << 4) + static_cast<uint64_t>(k)));
        g.m_cents = 5.0 * g.m_rng.gaussian() * 0.6;
        g.m_start = 0.06 * g.m_rng.uniform();
        g.m_vibHz = 4.8 + g.m_rng.uniform();
        g.m_vibCents = (15.0 + 20.0 * g.m_rng.uniform()) * m_vibrato;
        g.m_vibPhase = 2.0 * PiD * g.m_rng.uniform();
        g.m_wanderTo = 6.0 * g.m_rng.bipolar();
        g.m_amp = 0.8 + 0.4 * g.m_rng.uniform();
        startPeriod(g, n, 0.0);
    }
}

void Choir::noteOff(int pitch)
{
    for (int s = 0; s < ChoirNotes; ++s) {
        Note& n = m_note[s];
        if (n.m_on && n.m_held && n.m_pitch == pitch) { n.m_held = false; n.m_released = 0.0; m_mod.noteOff(s); }
    }
}

void Choir::startPeriod(Singer& g, const Note& n, double tInto)
{
    // The pitch of this period: the note, the singer's intonation, vibrato (after a third of a second) and wander,
    // jitter on the length and shimmer on the amplitude.
    const double ramp = std::clamp((g.m_age - 0.3) / 0.4, 0.0, 1.0);
    const double cents = g.m_cents + g.m_wander + ramp * (g.m_vibCents * n.m_vib) * std::sin(g.m_vibPhase) + 100.0 * n.m_pitchSt;
    const double f0 = midiToHz(n.m_pitch) * std::pow(2.0, cents / 1200.0);
    g.m_T0 = (1.0 / f0) * (1.0 + 0.004 * g.m_rng.gaussian());
    g.m_wander += 0.05 * (g.m_wanderTo - g.m_wander);
    if (g.m_rng.uniform() < 0.01) g.m_wanderTo = 6.0 * g.m_rng.bipolar();
    // The shape at this period's Rd (a little per singer), scaled to the period.
    const double rd = std::clamp(2.7 - 1.9 * static_cast<double>(n.m_tension) + 0.1 * g.m_rng.bipolar(), RdLo, RdHi);
    const double x = (rd - RdLo) / (RdHi - RdLo) * (LfTable - 1);
    const int i = std::min(static_cast<int>(x), LfTable - 2);
    const double u = x - i;
    const LfShape& a = m_lf[i];
    const LfShape& b = m_lf[i + 1];
    g.m_s.m_tp = (a.m_tp + u * (b.m_tp - a.m_tp)) * g.m_T0;
    g.m_s.m_te = (a.m_te + u * (b.m_te - a.m_te)) * g.m_T0;
    g.m_s.m_ta = (a.m_ta + u * (b.m_ta - a.m_ta)) * g.m_T0;
    g.m_s.m_alpha = (a.m_alpha + u * (b.m_alpha - a.m_alpha)) / g.m_T0;
    g.m_s.m_eps = (a.m_eps + u * (b.m_eps - a.m_eps)) / g.m_T0;
    g.m_s.m_e0 = a.m_e0 + u * (b.m_e0 - a.m_e0);
    g.m_w = PiD / g.m_s.m_tp;
    const double dt = 1.0 / m_sr;
    g.m_t = tInto;
    // The open phase's phasor at tInto and its step.
    const double ea = std::exp(g.m_s.m_alpha * tInto);
    g.m_zr = ea * std::cos(g.m_w * tInto);
    g.m_zi = ea * std::sin(g.m_w * tInto);
    const double es = std::exp(g.m_s.m_alpha * dt);
    g.m_sr = es * std::cos(g.m_w * dt);
    g.m_si = es * std::sin(g.m_w * dt);
    g.m_retStep = std::exp(-g.m_s.m_eps * dt);
    g.m_retEnd = std::exp(-g.m_s.m_eps * (g.m_T0 - g.m_s.m_te));
    g.m_returning = false;
    g.m_amp = std::max(0.2, g.m_amp * (1.0 + 0.03 * g.m_rng.gaussian()) * 0.9 + 0.1);
}

void Choir::process(float* L, float* R, int n)
{
    const double dt = 1.0 / m_sr;
    const float wl = 0.5f * (1.0f + m_width), wr = 0.5f * (1.0f - m_width);
    const bool modded = m_mod.on();
    for (int i = 0; i < n; ++i) {
        if (modded && m_pos % MaxBlock == 0) modCell();
        ++m_pos;
        float outL = 0.0f, outR = 0.0f;
        for (int s = 0; s < ChoirNotes; ++s) {
            Note& nt = m_note[s];
            if (!nt.m_on) continue;
            // The note's envelope: a swell in, a fall after the release.
            const double att = static_cast<double>(m_attackMs) * 1e-3, rel = static_cast<double>(m_releaseMs) * 1e-3;
            double env = nt.m_t < att ? 0.5 - 0.5 * std::cos(PiD * nt.m_t / att) : 1.0;
            if (!nt.m_held) { env *= std::max(0.0, 1.0 - nt.m_released / rel); nt.m_released += dt; }
            nt.m_t += dt;
            nt.m_env = static_cast<float>(env);
            float src[2] = { 0.0f, 0.0f };
            for (int k = 0; k < ChoirSingers; ++k) {
                Singer& g = m_singer[s * ChoirSingers + k];
                if (!g.m_on) continue;
                g.m_age += dt;
                g.m_vibPhase += 2.0 * PiD * g.m_vibHz * dt;
                if (g.m_age < g.m_start) continue;
                double e;
                if (g.m_t < g.m_s.m_te) {
                    e = g.m_s.m_e0 * g.m_zi;
                    const double zr = g.m_zr * g.m_sr - g.m_zi * g.m_si;
                    g.m_zi = g.m_zr * g.m_si + g.m_zi * g.m_sr;
                    g.m_zr = zr;
                } else {
                    if (!g.m_returning) { g.m_returning = true; g.m_ret = std::exp(-g.m_s.m_eps * (g.m_t - g.m_s.m_te)); }
                    e = -(1.0 / (g.m_s.m_eps * g.m_s.m_ta)) * (g.m_ret - g.m_retEnd);
                    g.m_ret *= g.m_retStep;
                }
                // Aspiration: noise, strongest while the glottis is open.
                const double open = g.m_t < g.m_s.m_te ? 1.0 : 0.3;
                e += static_cast<double>(nt.m_breath) * 0.35 * open * g.m_rng.bipolar();
                src[g.m_group] += static_cast<float>(e * g.m_amp);
                g.m_t += dt;
                if (g.m_t >= g.m_T0) startPeriod(g, nt, g.m_t - g.m_T0);
            }
            // The two tracts.
            const float amp = static_cast<float>(env) * (0.4f + 0.6f * nt.m_velocity) * nt.m_level;
            for (int grp = 0; grp < 2; ++grp) {
                float y = 0.0f;
                for (int f = 0; f < 5; ++f) {
                    float lp, bp, hp;
                    nt.m_formant[grp][f].tick(src[grp], lp, bp, hp);
                    y += nt.m_gain[f] * bp * nt.m_formant[grp][f].m_k;
                }
                y *= amp;
                outL += y * (grp == 0 ? wl : wr) * nt.m_panL;
                outR += y * (grp == 0 ? wr : wl) * nt.m_panR;
            }
            if (!nt.m_held && nt.m_released > rel + 0.3) {
                nt.m_on = false;
                for (int k = 0; k < ChoirSingers; ++k) m_singer[s * ChoirSingers + k].m_on = false;
            }
        }
        float lp, bp, hp;
        m_lowCut[0].tick(outL * m_level * 1.5f, lp, bp, hp);
        L[i] = hp;
        m_lowCut[1].tick(outR * m_level * 1.5f, lp, bp, hp);
        R[i] = hp;
    }
}

} // namespace parh
