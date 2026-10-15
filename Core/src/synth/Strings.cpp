/**
 * @file Strings.cpp
 * @brief The orchestra's string section (Strings.h).
 */
#include "parh/synth/Strings.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace parh {

namespace {

constexpr double PiD = 3.14159265358979323846;   ///< pi
constexpr float MuS = 0.8f;   ///< the rosin's static friction
constexpr float MuD = 0.3f;   ///< the rosin's dynamic friction
constexpr float V0 = 0.2f;   ///< the friction curve's velocity scale, m/s (Smith and Woodhouse 2000)

/** @brief A family: its open strings (MIDI), their tensions (N), the string length (m), inharmonicity, pan. */
struct Family {
    int m_open[4];   ///< its open strings, MIDI
    float m_tension[4];   ///< their tensions, N
    double m_length;   ///< the string length, m
    double m_B;   ///< the inharmonicity
    float m_pan;   ///< where it sits, -1 .. 1
    double m_bodyScale;     ///< the body's modes relative to a violin's
    double m_hillHz;        ///< the bridge hill
};

/** @brief violins, violas, cellos, basses */
const Family Families[StringFamilies] = {
    { { 55, 62, 69, 76 }, { 45.0f, 45.0f, 50.0f, 75.0f }, 0.325, 2e-5, -0.5f, 1.0, 2500.0 },    // violins
    { { 48, 55, 62, 69 }, { 55.0f, 55.0f, 60.0f, 70.0f }, 0.37, 3e-5, -0.15f, 0.82, 2100.0 },    // violas
    { { 36, 43, 50, 57 }, { 130.0f, 130.0f, 140.0f, 160.0f }, 0.69, 5e-5, 0.2f, 0.37, 1400.0 },   // cellos
    { { 28, 33, 38, 43 }, { 270.0f, 280.0f, 300.0f, 310.0f }, 1.05, 1e-4, 0.45f, 0.24, 1000.0 },  // basses
};

using cd = std::complex<double>;   ///< A complex number.

} // namespace

int StringSection::familyOf(int pitch)
{
    if (pitch < 40) return 3;
    if (pitch < 55) return 2;
    if (pitch < 62) return 1;
    return 0;
}

void StringSection::prepare(double sampleRate, uint64_t seed)
{
    m_sr = sampleRate;
    m_seed = seed;
    for (Svf& f : m_lowCut) f.setQ(m_lowCutHz, 0.707f, static_cast<float>(m_sr));
    // The bodies: the signature modes (Bissinger 2008) scaled per family, statistical modes above with the bridge hill.
    Rng r;
    r.seed(mixSeed(seed, 0x424F4459ull));   // "BODY"
    const double T = 1.0 / m_sr;
    for (int f = 0; f < StringFamilies; ++f) {
        Body& b = m_body[f];
        const Family& fam = Families[f];
        static const double Sig[4][3] = { { 275.0, 20.0, 0.6 }, { 405.0, 30.0, 0.3 }, { 470.0, 35.0, 1.0 }, { 545.0, 35.0, 1.0 } };
        for (int m = 0; m < BodyModes; ++m) {
            double hz, q, w;
            if (m < 4) {
                hz = Sig[m][0] * fam.m_bodyScale;
                q = Sig[m][1];
                w = Sig[m][2];
            } else {
                const double lo = 620.0 * fam.m_bodyScale, hi = std::min(9000.0 * std::sqrt(fam.m_bodyScale), 0.45 * m_sr);
                hz = lo * std::pow(hi / lo, (m - 4 + 0.5 * r.uniform()) / (BodyModes - 4));
                q = 15.0 + 12.0 * r.uniform();
                // The bridge hill: a broad rise around its frequency, falling above.
                const double x = std::log2(hz / fam.m_hillHz);
                w = (0.35 + 0.4 * r.uniform()) * (0.4 + 1.2 * std::exp(-x * x / 0.5)) * (r.uniform() < 0.5f ? -1.0 : 1.0);
            }
            const double wn = 2.0 * PiD * hz, sigma = wn / (2.0 * q);
            const cd lam(-sigma, wn), p = std::exp(lam * T), g = (p - 1.0) / lam;
            for (int c = 0; c < 2; ++c) {
                const int lane = 2 * f + c;
                m_bpr[m][lane] = static_cast<float>(p.real());
                m_bpi[m][lane] = static_cast<float>(p.imag());
                m_bgr[m][lane] = static_cast<float>(g.real());
                m_bgi[m][lane] = static_cast<float>(g.imag());
                // A resonator's peak on a unit force is 1 / (2 sigma): 2 sigma w makes its peak w.
                m_bw[m][lane] = static_cast<float>(2.0 * sigma * w);
            }
        }
        static const float Gain[StringFamilies] = { 1.0f, 0.35f, 0.2f, 0.1f };
        b.m_gain = Gain[f];
        b.m_direct = 0.3f;
    }
    reset();
}

void StringSection::update(const float* v)
{
    m_level = dbToGain(v[strings::LEVEL]);
    setPlayers(static_cast<int>(std::lround(v[strings::PLAYERS])));
    m_vibrato = v[strings::VIBRATO];
    m_pressure = v[strings::PRESSURE];
    m_position = v[strings::POSITION];
    m_speed = v[strings::SPEED];
    m_attackMs = v[strings::ATTACK];
    m_releaseMs = v[strings::RELEASE];
    m_width = v[strings::WIDTH];
    m_mod.set(readModCore(v + strings::MOD_FIRST, StringsModDests, static_cast<int>(std::size(StringsModDests))));
    if (v[strings::LOW_CUT] != m_lowCutHz) {
        m_lowCutHz = v[strings::LOW_CUT];
        for (Svf& f : m_lowCut) f.setQ(m_lowCutHz, 0.707f, static_cast<float>(m_sr));
    }
}

void StringSection::reset()
{
    for (int m = 0; m < StringModes; ++m)
        for (int l = 0; l < StringLanes; ++l) {
            m_zr[m][l] = m_zi[m][l] = 0.0f;
            m_pr[m][l] = m_pi[m][l] = m_p0r[m][l] = m_p0i[m][l] = m_gr[m][l] = m_gi[m][l] = 0.0f;
            m_hr[m][l] = m_hi[m][l] = m_br[m][l] = m_bi[m][l] = m_wT[m][l] = 0.0f;
            m_release[m][l] = 1.0f;
        }
    for (int l = 0; l < StringLanes; ++l) { m_lane[l] = Lane{}; m_y[l] = 1.0f; }
    for (int m = 0; m < BodyModes; ++m)
        for (int lane = 0; lane < BodyLanes; ++lane) m_bzr[m][lane] = m_bzi[m][lane] = 0.0f;
    for (Body& b : m_body) {
        b.m_active = false;
        b.m_quiet = 0;
    }
    for (Svf& f : m_lowCut) f.m_ic1 = f.m_ic2 = 0.0f;
    m_pos = 0;
    m_mod.reset();
}

int StringSection::activeLanes() const
{
    int n = 0;
    for (const Lane& l : m_lane) n += l.m_on;
    return n;
}

void StringSection::noteOn(int pitch, float velocity, bool staccato, double late)
{
    (void)late;
    // A note's players take the lanes of a free note slot (the oldest slot when none is free).
    int slot = -1;
    for (int s = 0; s < StringNotes && slot < 0; ++s) {
        bool free = true;
        for (int p = 0; p < StringPlayers; ++p) free = free && !m_lane[s * StringPlayers + p].m_on;
        if (free) slot = s;
    }
    if (slot < 0) {
        double oldest = -1.0;
        for (int s = 0; s < StringNotes; ++s) {
            const Lane& l = m_lane[s * StringPlayers];
            if (l.m_t > oldest) { oldest = l.m_t; slot = s; }
        }
    }
    for (int p = 0; p < StringPlayers; ++p) {
        const int lane = slot * StringPlayers + p;
        if (p < m_players) startLane(lane, pitch, velocity, staccato, p);
        else m_lane[lane].m_on = false;
    }
    m_mod.noteOn(slot, m_pos, pitch, velocity);
}

void StringSection::noteOff(int pitch)
{
    for (int s = 0; s < StringNotes; ++s) {
        const Lane& l = m_lane[s * StringPlayers];
        if (l.m_on && l.m_held && l.m_pitch == pitch) m_mod.noteOff(s);
    }
    for (Lane& l : m_lane) if (l.m_on && l.m_held && l.m_pitch == pitch) { l.m_held = false; l.m_lift = 0.0; }
}

void StringSection::startLane(int lane, int pitch, float velocity, bool staccato, int player)
{
    Lane& l = m_lane[lane];
    const int family = familyOf(pitch);
    const Family& fam = Families[family];
    int s = 0;
    for (int k = 0; k < 4; ++k) if (fam.m_open[k] <= pitch) s = k;
    const int played = std::max(pitch, fam.m_open[0]);
    l = Lane{};
    l.m_on = true;
    l.m_held = true;
    l.m_staccato = staccato;
    l.m_pitch = pitch;
    l.m_family = family;
    l.m_velocity = velocity;
    l.m_noise.seed(mixSeed(m_seed, (static_cast<uint64_t>(m_counter) << 8) + static_cast<uint64_t>(lane)));
    ++m_counter;
    // The player: an intonation, an onset, a vibrato of his own.
    l.m_cents = static_cast<float>(2.5 * l.m_noise.gaussian());
    l.m_t = -0.025 * l.m_noise.uniform() * (player > 0 ? 1.0 : 0.3);
    l.m_vibHz = 5.0f + 1.5f * l.m_noise.uniform();
    l.m_vibCents = (10.0f + 15.0f * l.m_noise.uniform()) * m_vibrato;
    l.m_vibPhase = TwoPi * l.m_noise.uniform();
    const float pan = std::clamp(fam.m_pan * m_width + 0.15f * m_width * l.m_noise.bipolar(), -1.0f, 1.0f);
    l.m_panL = std::cos((pan + 1.0f) * Pi / 4.0f);
    l.m_panR = std::sin((pan + 1.0f) * Pi / 4.0f);
    // The string: open string s, stopped to the pitch.
    const double fOpen = midiToHz(fam.m_open[s]), f = midiToHz(played) * std::pow(2.0, l.m_cents / 1200.0);
    const double T = fam.m_tension[s], mu = T / std::pow(2.0 * fam.m_length * fOpen, 2.0), L = fam.m_length * fOpen / f;
    const double z0 = std::sqrt(T * mu);
    const double beta = 0.1 * std::pow(2.0, -1.3 * static_cast<double>(m_position));
    l.m_beta0 = l.m_beta = beta;
    const double b1 = 1.5, b3 = 6e-9, dt = 1.0 / m_sr;
    double y = 0.0;
    for (int k = 1; k <= StringModes; ++k) {
        const size_t m = static_cast<size_t>(k - 1);
        const double fk = k * f * std::sqrt(1.0 + fam.m_B * k * k);
        if (fk > std::min(0.45 * m_sr, 12000.0)) {
            m_zr[m][lane] = m_zi[m][lane] = m_pr[m][lane] = m_pi[m][lane] = m_p0r[m][lane] = m_p0i[m][lane] = 0.0f;
            m_gr[m][lane] = m_gi[m][lane] = m_hr[m][lane] = m_hi[m][lane] = m_br[m][lane] = m_bi[m][lane] = m_wT[m][lane] = 0.0f;
            m_hur[m][lane] = m_hui[m][lane] = 0.0f;
            continue;
        }
        const double w = 2.0 * PiD * fk, sigma = b1 + b3 * w * w;
        const cd lam(-sigma, w), p = std::exp(lam * dt), g = (p - 1.0) / lam;
        const double sk = std::sin(k * PiD * beta), gk = 2.0 / (mu * L) * sk;
        const cd hv = sk * gk * cd(1.0, sigma / w);                                           // velocity at the bow
        const cd hu = 2.0 / (mu * L) * cd(1.0, sigma / w);                                    // the same over sin^2
        m_hur[m][lane] = static_cast<float>(hu.real());
        m_hui[m][lane] = static_cast<float>(hu.imag());
        const cd bf = (k & 1 ? -1.0 : 1.0) * k * PiD * T / L * cd(0.0, -1.0 / w) * gk;       // bridge force
        m_zr[m][lane] = m_zi[m][lane] = 0.0f;
        m_p0r[m][lane] = m_pr[m][lane] = static_cast<float>(p.real());
        m_p0i[m][lane] = m_pi[m][lane] = static_cast<float>(p.imag());
        m_gr[m][lane] = static_cast<float>(g.real());
        m_gi[m][lane] = static_cast<float>(g.imag());
        m_hr[m][lane] = static_cast<float>(hv.real());
        m_hi[m][lane] = static_cast<float>(hv.imag());
        m_br[m][lane] = static_cast<float>(bf.real());
        m_bi[m][lane] = static_cast<float>(bf.imag());
        m_wT[m][lane] = static_cast<float>(w * dt);
        // After the stroke the player's finger and the bow's rest take the string down within a second.
        m_release[m][lane] = static_cast<float>(std::exp(-12.0 * dt));
        y += (hv * g).real();
    }
    m_y[lane] = static_cast<float>(std::max(y, 1e-9));
    l.m_admittance = m_y[lane];
    // The bow: a speed from the velocity, the force a third of Schelleng's upper limit for it.
    l.m_vbow = static_cast<float>(m_speed * (staccato ? 0.12 + 0.35 * velocity : 0.06 + 0.24 * velocity));
    const double fmax = 2.0 * z0 * l.m_vbow / (beta * (MuS - MuD));
    l.m_fbow = static_cast<float>(m_pressure * 0.5 * fmax);
    l.m_vbow0 = l.m_vbow;
    l.m_fbow0 = l.m_fbow;
    l.m_pan0 = pan;
    m_body[family].m_active = true;
    m_body[family].m_quiet = 0;
}

void StringSection::placeBow(int lane, double beta)
{
    double y = 0.0;
    for (int m = 0; m < StringModes; ++m) {
        const double sk = std::sin((m + 1) * PiD * beta), s2 = sk * sk;
        m_hr[m][lane] = static_cast<float>(s2 * m_hur[m][lane]);
        m_hi[m][lane] = static_cast<float>(s2 * m_hui[m][lane]);
        y += static_cast<double>(m_hr[m][lane]) * m_gr[m][lane] - static_cast<double>(m_hi[m][lane]) * m_gi[m][lane];
    }
    m_y[lane] = static_cast<float>(std::max(y, 1e-9));
    m_lane[lane].m_admittance = m_y[lane];
    m_lane[lane].m_beta = beta;
}

void StringSection::cellUpdate()
{
    const float dtCell = static_cast<float>(MaxBlock / m_sr);
    // The modulation of every note that sounds (Phase 5b): its sums at this cell.
    const bool modded = m_mod.on();
    if (modded)
        for (int s = 0; s < StringNotes; ++s) {
            bool on = false;
            for (int p = 0; p < StringPlayers; ++p) on = on || m_lane[s * StringPlayers + p].m_on;
            if (on) m_mod.evaluate(s, m_pos);
        }
    for (int lane = 0; lane < StringLanes; ++lane) {
        Lane& l = m_lane[lane];
        if (!l.m_on) continue;
        float depth = 1.0f, rate = 1.0f;
        double pitchSt = 0.0;
        if (modded) {
            const int s = lane / StringPlayers;
            pitchSt = std::clamp(static_cast<double>(m_mod.get(s, ModDestE::PITCH)), -12.0, 2.0);
            depth = std::clamp(1.0f + m_mod.get(s, ModDestE::VIB_DEPTH), 0.0f, 4.0f);
            rate = std::exp2(std::clamp(m_mod.get(s, ModDestE::VIB_RATE), -3.0f, 3.0f));
            // The bow: its speed, its force (Schelleng's limit goes with the speed and against the distance from the
            // bridge, so the force keeps its place under it), its place.
            const float speed = std::exp2(std::clamp(m_mod.get(s, ModDestE::SPEED), -2.0f, 2.0f));
            const float press = std::exp2(std::clamp(m_mod.get(s, ModDestE::PRESSURE), -3.0f, 2.0f));
            if (m_mod.targets(ModDestE::POSITION)) {
                const double place = std::clamp(static_cast<double>(m_position + m_mod.get(s, ModDestE::POSITION)), -1.0, 1.0);
                const double beta = 0.1 * std::pow(2.0, -1.3 * place);
                if (std::fabs(beta - l.m_beta) > 1e-6) placeBow(lane, beta);
            }
            l.m_vbow = l.m_vbow0 * speed;
            l.m_fbow = static_cast<float>(l.m_fbow0 * speed * press * (l.m_beta0 / l.m_beta));
            if (m_mod.targets(ModDestE::LEVEL) || m_mod.targets(ModDestE::PAN)) {
                const float gain = std::clamp(1.0f + m_mod.get(s, ModDestE::LEVEL), 0.0f, 2.0f);
                const float pan = std::clamp(l.m_pan0 + m_mod.get(s, ModDestE::PAN), -1.0f, 1.0f);
                l.m_panL = gain * std::cos((pan + 1.0f) * Pi / 4.0f);
                l.m_panR = gain * std::sin((pan + 1.0f) * Pi / 4.0f);
            }
        }
        // Vibrato: in after a fifth of a second, over another; the poles turn by the pitch's offset.
        l.m_vibPhase += TwoPi * (l.m_vibHz * rate) * dtCell;
        if (l.m_vibPhase > TwoPi) l.m_vibPhase -= TwoPi;
        const double ramp = std::clamp((l.m_t - 0.2) / 0.3, 0.0, 1.0) * (l.m_staccato ? 0.3 : 1.0);
        const double cents = (l.m_vibCents * depth) * ramp * std::sin(static_cast<double>(l.m_vibPhase)) + 100.0 * pitchSt;
        const double rel = std::pow(2.0, cents / 1200.0) - 1.0;
        const bool damping = l.m_lift > static_cast<double>(m_releaseMs) * 1e-3 + 0.05;
        const bool exact = pitchSt != 0.0;   // (a bend: the turn exactly; the vibrato's few cents: the series)
        for (int m = 0; m < StringModes; ++m) {
            const double a = static_cast<double>(m_wT[m][lane]) * rel;
            double c, s;
            if (exact) { c = std::cos(a); s = std::sin(a); }
            else { c = 1.0 - 0.5 * a * a + a * a * a * a / 24.0; s = a - a * a * a / 6.0; }
            // A mode bent towards Nyquist falls silent rather than folding.
            const double g = (damping ? m_release[m][lane] : 1.0) * (exact && m_wT[m][lane] * (1.0 + rel) > 2.9 ? 0.0 : 1.0);
            m_pr[m][lane] = static_cast<float>((m_p0r[m][lane] * c - m_p0i[m][lane] * s) * g);
            m_pi[m][lane] = static_cast<float>((m_p0r[m][lane] * s + m_p0i[m][lane] * c) * g);
        }
    }
}

template <class VT>
void StringSection::bowSegment(int n)
{
    constexpr int W = laneWidth<VT>();
    const VT mus = lanes<VT>(MuS), mud = lanes<VT>(MuD), v0 = lanes<VT>(V0), half = lanes<VT>(0.5f), four = lanes<VT>(4.0f);
    const VT zero = lanes<VT>(0.0f), one = lanes<VT>(1.0f), minus = lanes<VT>(-1.0f);
    for (int g = 0; g < StringLanes; g += 8) {
        bool any = false;
        for (int l = g; l < g + 8; ++l) any = any || m_lane[l].m_on;
        if (!any) {
            for (int i = 0; i < n; ++i) for (int l = g; l < g + 8; ++l) m_out[i][l] = 0.0f;
            continue;
        }
        for (int h = 0; h < 8; h += W) {
            const int l0 = g + h;
            const VT Y = loadLanes<VT>(m_y + l0);
            for (int i = 0; i < n; ++i) {
                // The modes run free; their velocity at the bow point.
                VT vh = zero;
                for (int m = 0; m < StringModes; ++m) {
                    const VT Pr = loadLanes<VT>(m_pr[m] + l0), Pi = loadLanes<VT>(m_pi[m] + l0);
                    const VT Zr = loadLanes<VT>(m_zr[m] + l0), Zi = loadLanes<VT>(m_zi[m] + l0);
                    const VT nr = vfnmadd(Pi, Zi, Pr * Zr), ni = vfmadd(Pi, Zr, Pr * Zi);
                    vstore(m_zr[m] + l0, nr);
                    vstore(m_zi[m] + l0, ni);
                    vh = vfnmadd(loadLanes<VT>(m_hi[m] + l0), ni, vfmadd(loadLanes<VT>(m_hr[m] + l0), nr, vh));
                }
                // The bow: stick if static friction can hold the string, else slip at the one root.
                const VT vb = loadLanes<VT>(m_vb[i] + l0), fb = loadLanes<VT>(m_fb[i] + l0);
                const VT c = vh - vb, ac = vabs(c), yf = Y * fb;
                const VT fStick = (vb - vh) / Y;
                const VT bq = vfmadd(yf, mud, v0 - ac), cq = v0 * vfnmadd(lanes<VT>(1.0f), ac, yf * mus);
                const VT disc = vmax(vfnmadd(four, cq, bq * bq), zero);
                const VT u = vmax((vsqrt(disc) - bq) * half, zero);
                const VT mu = vfmadd(mus - mud, v0 / (v0 + u), mud);
                const VT fSlip = vselect(vlt(c, zero), one, minus) * (fb * mu);
                const VT F = vselect(vge(yf * mus, ac), fStick, fSlip);
                // The force into the modes; the bridge force out.
                VT o = zero;
                for (int m = 0; m < StringModes; ++m) {
                    const VT nr = vfmadd(loadLanes<VT>(m_gr[m] + l0), F, loadLanes<VT>(m_zr[m] + l0));
                    const VT ni = vfmadd(loadLanes<VT>(m_gi[m] + l0), F, loadLanes<VT>(m_zi[m] + l0));
                    vstore(m_zr[m] + l0, nr);
                    vstore(m_zi[m] + l0, ni);
                    o = vfnmadd(loadLanes<VT>(m_bi[m] + l0), ni, vfmadd(loadLanes<VT>(m_br[m] + l0), nr, o));
                }
                vstore(m_out[i] + l0, o);
            }
        }
    }
}

template <class VT>
void StringSection::processWith(float* L, float* R, int n)
{
    int done = 0;
    const double dt = 1.0 / m_sr;
    while (done < n) {
        const int into = static_cast<int>(m_pos % MaxBlock);
        const int seg = std::min(n - done, MaxBlock - into);
        // At rest (the optimisation pass, 29.09.2026): no player, no body ringing, the low cut's states at zero. Then
        // the section's output is exactly zero and nothing in it moves -- the cell has no lane to update, the bow no
        // group, the bodies nothing, and the low cut of zero is zero -- so the segment is only counted. It cost 0.3 %
        // of a core in every track without an orchestra.
        bool rest = m_lowCut[0].m_ic1 == 0.0f && m_lowCut[0].m_ic2 == 0.0f && m_lowCut[1].m_ic1 == 0.0f && m_lowCut[1].m_ic2 == 0.0f;
        for (int lane = 0; rest && lane < StringLanes; ++lane) rest = !m_lane[lane].m_on;
        for (int f = 0; rest && f < StringFamilies; ++f) rest = !m_body[f].m_active;
        if (rest) {
            std::fill(L + done, L + done + seg, 0.0f);
            std::fill(R + done, R + done + seg, 0.0f);
            m_pos += seg;
            done += seg;
            continue;
        }
        if (into == 0) cellUpdate();
        // The players' bows, sample by sample (scalar): speed and force along their envelopes, rosin noise on the force.
        for (int lane = 0; lane < StringLanes; ++lane) {
            Lane& l = m_lane[lane];
            for (int i = 0; i < seg; ++i) {
                if (!l.m_on) { m_vb[i][lane] = m_fb[i][lane] = 0.0f; continue; }
                float env = 0.0f;
                if (l.m_t >= 0.0) {
                    const double att = (l.m_staccato ? 12.0 : static_cast<double>(m_attackMs)) * 1e-3;
                    env = l.m_t < att ? static_cast<float>(0.5 - 0.5 * std::cos(PiD * l.m_t / att)) : 1.0f;
                    if (l.m_staccato && l.m_held && l.m_t > 0.14) { l.m_held = false; l.m_lift = 0.0; }   // the bite is short
                    if (!l.m_held) {
                        const double rel = (l.m_staccato ? 30.0 : static_cast<double>(m_releaseMs)) * 1e-3;
                        env *= static_cast<float>(std::max(0.0, 1.0 - l.m_lift / rel));
                        l.m_lift += dt;
                    }
                }
                l.m_t += dt;
                m_vb[i][lane] = l.m_vbow * env;
                m_fb[i][lane] = l.m_fbow * env * (1.0f + 0.08f * l.m_noise.bipolar());
            }
        }
        bowSegment<VT>(seg);
        // Each lane's peak over the whole cell, whoever cuts it (the lanes stop at the cell's end by it).
        for (int lane = 0; lane < StringLanes; ++lane) {
            Lane& l = m_lane[lane];
            if (!l.m_on) continue;
            float peak = into == 0 ? 0.0f : l.m_peak;
            for (int i = 0; i < seg; ++i) peak = std::max(peak, std::fabs(m_out[i][lane]));
            l.m_peak = peak;
        }
        // The bodies: each family's players, left and right by where they sit -- the eight (family, channel) lanes
        // mode by mode, each lane summing its modes in order (see m_bzr).
        constexpr int W = laneWidth<VT>();
        for (int i = 0; i < seg; ++i) {
            alignas(32) float in8[BodyLanes] = {}, y8[BodyLanes];
            for (int f = 0; f < StringFamilies; ++f) {
                if (!m_body[f].m_active) continue;
                float inL = 0.0f, inR = 0.0f;
                for (int lane = 0; lane < StringLanes; ++lane) {
                    const Lane& l = m_lane[lane];
                    if (!l.m_on || l.m_family != f) continue;
                    inL += m_out[i][lane] * l.m_panL;
                    inR += m_out[i][lane] * l.m_panR;
                }
                in8[2 * f] = inL;
                in8[2 * f + 1] = inR;
            }
            for (int h = 0; h < BodyLanes; h += W) {
                const VT in = loadLanes<VT>(in8 + h);
                VT y = lanes<VT>(0.0f);
                for (int m = 0; m < BodyModes; ++m) {
                    const VT pr = loadLanes<VT>(m_bpr[m] + h), pi = loadLanes<VT>(m_bpi[m] + h);
                    const VT zr = loadLanes<VT>(m_bzr[m] + h), zi = loadLanes<VT>(m_bzi[m] + h);
                    const VT r = pr * zr - pi * zi + loadLanes<VT>(m_bgr[m] + h) * in;
                    const VT im = pr * zi + pi * zr + loadLanes<VT>(m_bgi[m] + h) * in;
                    vstore(m_bzr[m] + h, r);
                    vstore(m_bzi[m] + h, im);
                    y = y + loadLanes<VT>(m_bw[m] + h) * r;
                }
                vstore(y8 + h, y);
            }
            float outL = 0.0f, outR = 0.0f;
            for (int f = 0; f < StringFamilies; ++f) {
                const Body& b = m_body[f];
                if (!b.m_active) continue;
                outL += (y8[2 * f] + b.m_direct * in8[2 * f]) * b.m_gain;
                outR += (y8[2 * f + 1] + b.m_direct * in8[2 * f + 1]) * b.m_gain;
            }
            float lp, bp, hp;
            m_lowCut[0].tick(outL * m_level * 0.24f, lp, bp, hp);
            L[done + i] = hp;
            m_lowCut[1].tick(outR * m_level * 0.24f, lp, bp, hp);
            R[done + i] = hp;
        }
        // At the cell's end: lanes whose bow has lifted and whose string is quiet stop; a body with no player rests.
        if (into + seg == MaxBlock) {
            for (int lane = 0; lane < StringLanes; ++lane) {
                Lane& l = m_lane[lane];
                if (!l.m_on) continue;
                l.m_quiet = !l.m_held && l.m_peak < 1e-6f ? l.m_quiet + 1 : 0;
                if (l.m_quiet > 20) l.m_on = false;
            }
            for (int f = 0; f < StringFamilies; ++f) {
                Body& b = m_body[f];
                if (!b.m_active) continue;
                bool players = false;
                for (const Lane& l : m_lane) players = players || (l.m_on && l.m_family == f);
                double e = 0.0;
                for (int m = 0; m < BodyModes; ++m)
                    e += static_cast<double>(m_bzr[m][2 * f]) * m_bzr[m][2 * f] + static_cast<double>(m_bzr[m][2 * f + 1]) * m_bzr[m][2 * f + 1];
                b.m_quiet = !players && e < 1e-18 ? b.m_quiet + 1 : 0;
                if (b.m_quiet > 20) {
                    b.m_active = false;
                    for (int c = 0; c < 2; ++c) for (int m = 0; m < BodyModes; ++m) m_bzr[m][2 * f + c] = m_bzi[m][2 * f + c] = 0.0f;
                }
            }
        }
        m_pos += seg;
        done += seg;
    }
}

void StringSection::process(float* L, float* R, int n)
{
    processWith<VecF>(L, R, n);
}

template void StringSection::processWith<float>(float*, float*, int);
#if PARH_VEC_PATH != 0
template void StringSection::processWith<VecF>(float*, float*, int);
#endif

} // namespace parh
