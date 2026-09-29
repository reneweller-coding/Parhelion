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

constexpr double kPiD = 3.14159265358979323846;
constexpr float kMuS = 0.8f, kMuD = 0.3f, kV0 = 0.2f;   ///< the rosin's friction (Smith and Woodhouse 2000)

/** @brief A family: its open strings (MIDI), their tensions (N), the string length (m), inharmonicity, pan. */
struct Family {
    int open[4];
    float tension[4];
    double length;
    double B;
    float pan;
    double bodyScale;     ///< the body's modes relative to a violin's
    double hillHz;        ///< the bridge hill
};

const Family kFamilies[kStringFamilies] = {
    { { 55, 62, 69, 76 }, { 45.0f, 45.0f, 50.0f, 75.0f }, 0.325, 2e-5, -0.5f, 1.0, 2500.0 },    // violins
    { { 48, 55, 62, 69 }, { 55.0f, 55.0f, 60.0f, 70.0f }, 0.37, 3e-5, -0.15f, 0.82, 2100.0 },    // violas
    { { 36, 43, 50, 57 }, { 130.0f, 130.0f, 140.0f, 160.0f }, 0.69, 5e-5, 0.2f, 0.37, 1400.0 },   // cellos
    { { 28, 33, 38, 43 }, { 270.0f, 280.0f, 300.0f, 310.0f }, 1.05, 1e-4, 0.45f, 0.24, 1000.0 },  // basses
};

using cd = std::complex<double>;

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
    sr_ = sampleRate;
    seed_ = seed;
    for (Svf& f : lowCut_) f.setQ(lowCutHz_, 0.707f, static_cast<float>(sr_));
    // The bodies: the signature modes (Bissinger 2008) scaled per family, statistical modes above with the bridge hill.
    Rng r;
    r.seed(mixSeed(seed, 0x424F4459ull));   // "BODY"
    const double T = 1.0 / sr_;
    for (int f = 0; f < kStringFamilies; ++f) {
        Body& b = body_[f];
        const Family& fam = kFamilies[f];
        static const double kSig[4][3] = { { 275.0, 20.0, 0.6 }, { 405.0, 30.0, 0.3 }, { 470.0, 35.0, 1.0 }, { 545.0, 35.0, 1.0 } };
        for (int m = 0; m < kBodyModes; ++m) {
            double hz, q, w;
            if (m < 4) {
                hz = kSig[m][0] * fam.bodyScale;
                q = kSig[m][1];
                w = kSig[m][2];
            } else {
                const double lo = 620.0 * fam.bodyScale, hi = std::min(9000.0 * std::sqrt(fam.bodyScale), 0.45 * sr_);
                hz = lo * std::pow(hi / lo, (m - 4 + 0.5 * r.uniform()) / (kBodyModes - 4));
                q = 15.0 + 12.0 * r.uniform();
                // The bridge hill: a broad rise around its frequency, falling above.
                const double x = std::log2(hz / fam.hillHz);
                w = (0.35 + 0.4 * r.uniform()) * (0.4 + 1.2 * std::exp(-x * x / 0.5)) * (r.uniform() < 0.5f ? -1.0 : 1.0);
            }
            const double wn = 2.0 * kPiD * hz, sigma = wn / (2.0 * q);
            const cd lam(-sigma, wn), p = std::exp(lam * T), g = (p - 1.0) / lam;
            b.pr[m] = static_cast<float>(p.real());
            b.pi[m] = static_cast<float>(p.imag());
            b.gr[m] = static_cast<float>(g.real());
            b.gi[m] = static_cast<float>(g.imag());
            // A resonator's peak on a unit force is 1 / (2 sigma): 2 sigma w makes its peak w.
            b.w[m] = static_cast<float>(2.0 * sigma * w);
        }
        static const float kGain[kStringFamilies] = { 1.0f, 0.35f, 0.2f, 0.1f };
        b.gain = kGain[f];
        b.direct = 0.3f;
    }
    reset();
}

void StringSection::update(const float* v)
{
    level_ = dbToGain(v[strings::Level]);
    setPlayers(static_cast<int>(std::lround(v[strings::Players])));
    vibrato_ = v[strings::Vibrato];
    pressure_ = v[strings::Pressure];
    position_ = v[strings::Position];
    speed_ = v[strings::Speed];
    attackMs_ = v[strings::Attack];
    releaseMs_ = v[strings::Release];
    width_ = v[strings::Width];
    if (v[strings::LowCut] != lowCutHz_) {
        lowCutHz_ = v[strings::LowCut];
        for (Svf& f : lowCut_) f.setQ(lowCutHz_, 0.707f, static_cast<float>(sr_));
    }
}

void StringSection::reset()
{
    for (int m = 0; m < kStringModes; ++m)
        for (int l = 0; l < kStringLanes; ++l) {
            zr_[m][l] = zi_[m][l] = 0.0f;
            pr_[m][l] = pi_[m][l] = p0r_[m][l] = p0i_[m][l] = gr_[m][l] = gi_[m][l] = 0.0f;
            hr_[m][l] = hi_[m][l] = br_[m][l] = bi_[m][l] = wT_[m][l] = 0.0f;
            release_[m][l] = 1.0f;
        }
    for (int l = 0; l < kStringLanes; ++l) { lane_[l] = Lane{}; y_[l] = 1.0f; }
    for (Body& b : body_) {
        for (int c = 0; c < 2; ++c) for (int m = 0; m < kBodyModes; ++m) b.zr[c][m] = b.zi[c][m] = 0.0f;
        b.active = false;
        b.quiet = 0;
    }
    for (Svf& f : lowCut_) f.ic1 = f.ic2 = 0.0f;
    pos_ = 0;
}

int StringSection::activeLanes() const
{
    int n = 0;
    for (const Lane& l : lane_) n += l.on;
    return n;
}

void StringSection::noteOn(int pitch, float velocity, bool staccato, double late)
{
    (void)late;
    // A note's players take the lanes of a free note slot (the oldest slot when none is free).
    int slot = -1;
    for (int s = 0; s < kStringNotes && slot < 0; ++s) {
        bool free = true;
        for (int p = 0; p < kStringPlayers; ++p) free = free && !lane_[s * kStringPlayers + p].on;
        if (free) slot = s;
    }
    if (slot < 0) {
        double oldest = -1.0;
        for (int s = 0; s < kStringNotes; ++s) {
            const Lane& l = lane_[s * kStringPlayers];
            if (l.t > oldest) { oldest = l.t; slot = s; }
        }
    }
    for (int p = 0; p < kStringPlayers; ++p) {
        const int lane = slot * kStringPlayers + p;
        if (p < players_) startLane(lane, pitch, velocity, staccato, p);
        else lane_[lane].on = false;
    }
}

void StringSection::noteOff(int pitch)
{
    for (Lane& l : lane_) if (l.on && l.held && l.pitch == pitch) { l.held = false; l.lift = 0.0; }
}

void StringSection::startLane(int lane, int pitch, float velocity, bool staccato, int player)
{
    Lane& l = lane_[lane];
    const int family = familyOf(pitch);
    const Family& fam = kFamilies[family];
    int s = 0;
    for (int k = 0; k < 4; ++k) if (fam.open[k] <= pitch) s = k;
    const int played = std::max(pitch, fam.open[0]);
    l = Lane{};
    l.on = true;
    l.held = true;
    l.staccato = staccato;
    l.pitch = pitch;
    l.family = family;
    l.velocity = velocity;
    l.noise.seed(mixSeed(seed_, (static_cast<uint64_t>(counter_) << 8) + static_cast<uint64_t>(lane)));
    ++counter_;
    // The player: an intonation, an onset, a vibrato of his own.
    l.cents = static_cast<float>(2.5 * l.noise.gaussian());
    l.t = -0.025 * l.noise.uniform() * (player > 0 ? 1.0 : 0.3);
    l.vibHz = 5.0f + 1.5f * l.noise.uniform();
    l.vibCents = (10.0f + 15.0f * l.noise.uniform()) * vibrato_;
    l.vibPhase = kTwoPi * l.noise.uniform();
    const float pan = std::clamp(fam.pan * width_ + 0.15f * width_ * l.noise.bipolar(), -1.0f, 1.0f);
    l.panL = std::cos((pan + 1.0f) * kPi / 4.0f);
    l.panR = std::sin((pan + 1.0f) * kPi / 4.0f);
    // The string: open string s, stopped to the pitch.
    const double fOpen = midiToHz(fam.open[s]), f = midiToHz(played) * std::pow(2.0, l.cents / 1200.0);
    const double T = fam.tension[s], mu = T / std::pow(2.0 * fam.length * fOpen, 2.0), L = fam.length * fOpen / f;
    const double z0 = std::sqrt(T * mu);
    const double beta = 0.1 * std::pow(2.0, -1.3 * static_cast<double>(position_));
    const double b1 = 1.5, b3 = 6e-9, dt = 1.0 / sr_;
    double y = 0.0;
    for (int k = 1; k <= kStringModes; ++k) {
        const size_t m = static_cast<size_t>(k - 1);
        const double fk = k * f * std::sqrt(1.0 + fam.B * k * k);
        if (fk > std::min(0.45 * sr_, 12000.0)) {
            zr_[m][lane] = zi_[m][lane] = pr_[m][lane] = pi_[m][lane] = p0r_[m][lane] = p0i_[m][lane] = 0.0f;
            gr_[m][lane] = gi_[m][lane] = hr_[m][lane] = hi_[m][lane] = br_[m][lane] = bi_[m][lane] = wT_[m][lane] = 0.0f;
            continue;
        }
        const double w = 2.0 * kPiD * fk, sigma = b1 + b3 * w * w;
        const cd lam(-sigma, w), p = std::exp(lam * dt), g = (p - 1.0) / lam;
        const double sk = std::sin(k * kPiD * beta), gk = 2.0 / (mu * L) * sk;
        const cd hv = sk * gk * cd(1.0, sigma / w);                                           // velocity at the bow
        const cd bf = (k & 1 ? -1.0 : 1.0) * k * kPiD * T / L * cd(0.0, -1.0 / w) * gk;       // bridge force
        zr_[m][lane] = zi_[m][lane] = 0.0f;
        p0r_[m][lane] = pr_[m][lane] = static_cast<float>(p.real());
        p0i_[m][lane] = pi_[m][lane] = static_cast<float>(p.imag());
        gr_[m][lane] = static_cast<float>(g.real());
        gi_[m][lane] = static_cast<float>(g.imag());
        hr_[m][lane] = static_cast<float>(hv.real());
        hi_[m][lane] = static_cast<float>(hv.imag());
        br_[m][lane] = static_cast<float>(bf.real());
        bi_[m][lane] = static_cast<float>(bf.imag());
        wT_[m][lane] = static_cast<float>(w * dt);
        // After the stroke the player's finger and the bow's rest take the string down within a second.
        release_[m][lane] = static_cast<float>(std::exp(-12.0 * dt));
        y += (hv * g).real();
    }
    y_[lane] = static_cast<float>(std::max(y, 1e-9));
    l.admittance = y_[lane];
    // The bow: a speed from the velocity, the force a third of Schelleng's upper limit for it.
    l.vbow = static_cast<float>(speed_ * (staccato ? 0.12 + 0.35 * velocity : 0.06 + 0.24 * velocity));
    const double fmax = 2.0 * z0 * l.vbow / (beta * (kMuS - kMuD));
    l.fbow = static_cast<float>(pressure_ * 0.5 * fmax);
    body_[family].active = true;
    body_[family].quiet = 0;
}

void StringSection::cellUpdate()
{
    const float dtCell = static_cast<float>(kMaxBlock / sr_);
    for (int lane = 0; lane < kStringLanes; ++lane) {
        Lane& l = lane_[lane];
        if (!l.on) continue;
        // Vibrato: in after a fifth of a second, over another; the poles turn by the pitch's offset.
        l.vibPhase += kTwoPi * l.vibHz * dtCell;
        if (l.vibPhase > kTwoPi) l.vibPhase -= kTwoPi;
        const double ramp = std::clamp((l.t - 0.2) / 0.3, 0.0, 1.0) * (l.staccato ? 0.3 : 1.0);
        const double cents = l.vibCents * ramp * std::sin(static_cast<double>(l.vibPhase));
        const double rel = std::pow(2.0, cents / 1200.0) - 1.0;
        const bool damping = l.lift > static_cast<double>(releaseMs_) * 1e-3 + 0.05;
        for (int m = 0; m < kStringModes; ++m) {
            const double a = static_cast<double>(wT_[m][lane]) * rel;
            const double c = 1.0 - 0.5 * a * a + a * a * a * a / 24.0, s = a - a * a * a / 6.0;
            const double g = damping ? release_[m][lane] : 1.0;
            pr_[m][lane] = static_cast<float>((p0r_[m][lane] * c - p0i_[m][lane] * s) * g);
            pi_[m][lane] = static_cast<float>((p0r_[m][lane] * s + p0i_[m][lane] * c) * g);
        }
    }
}

template <class V>
void StringSection::bowSegment(int n)
{
    constexpr int W = laneWidth<V>();
    const V mus = lanes<V>(kMuS), mud = lanes<V>(kMuD), v0 = lanes<V>(kV0), half = lanes<V>(0.5f), four = lanes<V>(4.0f);
    const V zero = lanes<V>(0.0f), one = lanes<V>(1.0f), minus = lanes<V>(-1.0f);
    for (int g = 0; g < kStringLanes; g += 8) {
        bool any = false;
        for (int l = g; l < g + 8; ++l) any = any || lane_[l].on;
        if (!any) {
            for (int i = 0; i < n; ++i) for (int l = g; l < g + 8; ++l) out_[i][l] = 0.0f;
            continue;
        }
        for (int h = 0; h < 8; h += W) {
            const int l0 = g + h;
            const V Y = loadLanes<V>(y_ + l0);
            for (int i = 0; i < n; ++i) {
                // The modes run free; their velocity at the bow point.
                V vh = zero;
                for (int m = 0; m < kStringModes; ++m) {
                    const V Pr = loadLanes<V>(pr_[m] + l0), Pi = loadLanes<V>(pi_[m] + l0);
                    const V Zr = loadLanes<V>(zr_[m] + l0), Zi = loadLanes<V>(zi_[m] + l0);
                    const V nr = vfnmadd(Pi, Zi, Pr * Zr), ni = vfmadd(Pi, Zr, Pr * Zi);
                    vstore(zr_[m] + l0, nr);
                    vstore(zi_[m] + l0, ni);
                    vh = vfnmadd(loadLanes<V>(hi_[m] + l0), ni, vfmadd(loadLanes<V>(hr_[m] + l0), nr, vh));
                }
                // The bow: stick if static friction can hold the string, else slip at the one root.
                const V vb = loadLanes<V>(vb_[i] + l0), fb = loadLanes<V>(fb_[i] + l0);
                const V c = vh - vb, ac = vabs(c), yf = Y * fb;
                const V fStick = (vb - vh) / Y;
                const V bq = vfmadd(yf, mud, v0 - ac), cq = v0 * vfnmadd(lanes<V>(1.0f), ac, yf * mus);
                const V disc = vmax(vfnmadd(four, cq, bq * bq), zero);
                const V u = vmax((vsqrt(disc) - bq) * half, zero);
                const V mu = vfmadd(mus - mud, v0 / (v0 + u), mud);
                const V fSlip = vselect(vlt(c, zero), one, minus) * (fb * mu);
                const V F = vselect(vge(yf * mus, ac), fStick, fSlip);
                // The force into the modes; the bridge force out.
                V o = zero;
                for (int m = 0; m < kStringModes; ++m) {
                    const V nr = vfmadd(loadLanes<V>(gr_[m] + l0), F, loadLanes<V>(zr_[m] + l0));
                    const V ni = vfmadd(loadLanes<V>(gi_[m] + l0), F, loadLanes<V>(zi_[m] + l0));
                    vstore(zr_[m] + l0, nr);
                    vstore(zi_[m] + l0, ni);
                    o = vfnmadd(loadLanes<V>(bi_[m] + l0), ni, vfmadd(loadLanes<V>(br_[m] + l0), nr, o));
                }
                vstore(out_[i] + l0, o);
            }
        }
    }
}

template <class V>
void StringSection::processWith(float* L, float* R, int n)
{
    int done = 0;
    const double dt = 1.0 / sr_;
    while (done < n) {
        const int into = static_cast<int>(pos_ % kMaxBlock);
        const int seg = std::min(n - done, kMaxBlock - into);
        if (into == 0) cellUpdate();
        // The players' bows, sample by sample (scalar): speed and force along their envelopes, rosin noise on the force.
        for (int lane = 0; lane < kStringLanes; ++lane) {
            Lane& l = lane_[lane];
            for (int i = 0; i < seg; ++i) {
                if (!l.on) { vb_[i][lane] = fb_[i][lane] = 0.0f; continue; }
                float env = 0.0f;
                if (l.t >= 0.0) {
                    const double att = (l.staccato ? 12.0 : static_cast<double>(attackMs_)) * 1e-3;
                    env = l.t < att ? static_cast<float>(0.5 - 0.5 * std::cos(kPiD * l.t / att)) : 1.0f;
                    if (l.staccato && l.held && l.t > 0.14) { l.held = false; l.lift = 0.0; }   // the bite is short
                    if (!l.held) {
                        const double rel = (l.staccato ? 30.0 : static_cast<double>(releaseMs_)) * 1e-3;
                        env *= static_cast<float>(std::max(0.0, 1.0 - l.lift / rel));
                        l.lift += dt;
                    }
                }
                l.t += dt;
                vb_[i][lane] = l.vbow * env;
                fb_[i][lane] = l.fbow * env * (1.0f + 0.08f * l.noise.bipolar());
            }
        }
        bowSegment<V>(seg);
        // Each lane's peak over the whole cell, whoever cuts it (the lanes stop at the cell's end by it).
        for (int lane = 0; lane < kStringLanes; ++lane) {
            Lane& l = lane_[lane];
            if (!l.on) continue;
            float peak = into == 0 ? 0.0f : l.peak;
            for (int i = 0; i < seg; ++i) peak = std::max(peak, std::fabs(out_[i][lane]));
            l.peak = peak;
        }
        // The bodies: each family's players, left and right by where they sit.
        for (int i = 0; i < seg; ++i) {
            float outL = 0.0f, outR = 0.0f;
            for (int f = 0; f < kStringFamilies; ++f) {
                Body& b = body_[f];
                if (!b.active) continue;
                float inL = 0.0f, inR = 0.0f;
                for (int lane = 0; lane < kStringLanes; ++lane) {
                    const Lane& l = lane_[lane];
                    if (!l.on || l.family != f) continue;
                    inL += out_[i][lane] * l.panL;
                    inR += out_[i][lane] * l.panR;
                }
                float yl = 0.0f, yr = 0.0f;
                for (int m = 0; m < kBodyModes; ++m) {
                    const float rl = b.pr[m] * b.zr[0][m] - b.pi[m] * b.zi[0][m] + b.gr[m] * inL;
                    const float il = b.pr[m] * b.zi[0][m] + b.pi[m] * b.zr[0][m] + b.gi[m] * inL;
                    const float rr = b.pr[m] * b.zr[1][m] - b.pi[m] * b.zi[1][m] + b.gr[m] * inR;
                    const float ir = b.pr[m] * b.zi[1][m] + b.pi[m] * b.zr[1][m] + b.gi[m] * inR;
                    b.zr[0][m] = rl; b.zi[0][m] = il; b.zr[1][m] = rr; b.zi[1][m] = ir;
                    yl += b.w[m] * rl;
                    yr += b.w[m] * rr;
                }
                outL += (yl + b.direct * inL) * b.gain;
                outR += (yr + b.direct * inR) * b.gain;
            }
            float lp, bp, hp;
            lowCut_[0].tick(outL * level_ * 0.24f, lp, bp, hp);
            L[done + i] = hp;
            lowCut_[1].tick(outR * level_ * 0.24f, lp, bp, hp);
            R[done + i] = hp;
        }
        // At the cell's end: lanes whose bow has lifted and whose string is quiet stop; a body with no player rests.
        if (into + seg == kMaxBlock) {
            for (int lane = 0; lane < kStringLanes; ++lane) {
                Lane& l = lane_[lane];
                if (!l.on) continue;
                l.quiet = !l.held && l.peak < 1e-6f ? l.quiet + 1 : 0;
                if (l.quiet > 20) l.on = false;
            }
            for (int f = 0; f < kStringFamilies; ++f) {
                Body& b = body_[f];
                if (!b.active) continue;
                bool players = false;
                for (const Lane& l : lane_) players = players || (l.on && l.family == f);
                double e = 0.0;
                for (int m = 0; m < kBodyModes; ++m) e += static_cast<double>(b.zr[0][m]) * b.zr[0][m] + static_cast<double>(b.zr[1][m]) * b.zr[1][m];
                b.quiet = !players && e < 1e-18 ? b.quiet + 1 : 0;
                if (b.quiet > 20) {
                    b.active = false;
                    for (int c = 0; c < 2; ++c) for (int m = 0; m < kBodyModes; ++m) b.zr[c][m] = b.zi[c][m] = 0.0f;
                }
            }
        }
        pos_ += seg;
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
