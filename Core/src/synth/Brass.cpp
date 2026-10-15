/**
 * @file Brass.cpp
 * @brief The brass (Brass.h).
 * @author Rene Weller
 */
#include "parh/synth/Brass.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {
constexpr double PiD = 3.14159265358979323846;   ///< pi
constexpr int LineMax = 4096;   ///< the bore's longest delay, samples
constexpr double Rho = 1.2;   ///< air's density, kg/m^3
constexpr double C = 343.0;   ///< the speed of sound, m/s
constexpr double BoreArea = 1.5e-4;             ///< m^2 (a horn's or a trombone's bore)
constexpr double Zc = Rho * C / BoreArea;    ///< the bore's characteristic impedance
constexpr double LipWidth = 0.01;   ///< the lips' width, m
constexpr double LipMass = 1.5;   ///< the lips' mass per area, kg/m^2
constexpr double LipRest = 1e-4;   ///< the lips' rest opening, m
constexpr double LipQ = 7.0;   ///< the lips' quality factor
constexpr double LipRatio = 0.8;   ///< the lips' own frequency over the note's
constexpr double BellReflection = 0.95;   ///< the bell's reflection
/** @brief sub-steps of the lips per sample */
constexpr int LipSteps = 2;}

void Brass::prepare(double sampleRate, uint64_t seed)
{
    m_sr = sampleRate;
    m_seed = seed;
    m_mod.prepare(sampleRate, mixSeed(seed, 0x4D4F44ull));   // "MOD"
    for (Player& p : m_player) p.m_line.assign(LineMax, 0.0f);
    for (Svf& f : m_lowCut) f.setQ(m_lowCutHz, 0.707f, static_cast<float>(m_sr));
    reset();
}

void Brass::update(const float* v)
{
    m_level = dbToGain(v[brass::LEVEL]);
    setPlayers(static_cast<int>(std::lround(v[brass::PLAYERS])));
    m_pressure = v[brass::PRESSURE];
    m_brassiness = v[brass::BRASSINESS];
    m_attackMs = v[brass::ATTACK];
    m_releaseMs = v[brass::RELEASE];
    m_vibratoCents = v[brass::VIBRATO];
    m_width = v[brass::WIDTH];
    if (v[brass::LOW_CUT] != m_lowCutHz) {
        m_lowCutHz = v[brass::LOW_CUT];
        for (Svf& f : m_lowCut) f.setQ(m_lowCutHz, 0.707f, static_cast<float>(m_sr));
    }
    m_mod.set(readModCore(v + brass::MOD_FIRST, BrassModDests, static_cast<int>(std::size(BrassModDests))));
    for (int s = 0; s < BrassNotes; ++s) {
        if (!m_mod.targets(ModDestE::BRASSINESS)) m_noteBrass[s] = m_brassiness;
        if (!m_mod.targets(ModDestE::VIB_DEPTH)) m_noteVib[s] = m_vibratoCents;
    }
}

void Brass::modNote(int s)
{
    m_mod.evaluate(s, m_pos);
    m_noteBrass[s] = std::clamp(m_brassiness + m_mod.get(s, ModDestE::BRASSINESS), 0.0f, 1.0f);
    m_noteVib[s] = m_vibratoCents * std::clamp(1.0f + m_mod.get(s, ModDestE::VIB_DEPTH), 0.0f, 4.0f);
    const double breath = std::exp2(std::clamp(static_cast<double>(m_mod.get(s, ModDestE::PRESSURE)), -2.0, 1.0));
    const double bend = std::pow(2.0, std::clamp(static_cast<double>(m_mod.get(s, ModDestE::PITCH)), -2.0, 2.0) / 12.0);
    const bool pan = m_mod.targets(ModDestE::PAN) || m_mod.targets(ModDestE::LEVEL);
    const float gain = std::clamp(1.0f + m_mod.get(s, ModDestE::LEVEL), 0.0f, 2.0f);
    for (int k = 0; k < BrassPlayers; ++k) {
        Player& p = m_player[s * BrassPlayers + k];
        if (!p.m_on) continue;
        p.m_target = p.m_target0 * breath;
        if (m_mod.targets(ModDestE::PITCH)) {
            // The lips set to the bent note, the bore's delay moved with it (the ear refines it).
            const double f = p.m_f0 * bend, period = m_sr / f;
            p.m_delay = std::clamp(p.m_delay * period / p.m_period, 4.0, static_cast<double>(LineMax - 4));
            p.m_period = period;
            p.m_wl = 2.0 * PiD * f * LipRatio;
        }
        if (pan) {
            const float x = std::clamp(p.m_pan0 + m_mod.get(s, ModDestE::PAN), -1.0f, 1.0f);
            p.m_panL = gain * std::cos((x + 1.0f) * Pi / 4.0f);
            p.m_panR = gain * std::sin((x + 1.0f) * Pi / 4.0f);
        }
    }
}

void Brass::reset()
{
    for (Player& p : m_player) {
        std::vector<float> line = std::move(p.m_line);
        p = Player{};
        p.m_line = std::move(line);
        std::fill(p.m_line.begin(), p.m_line.end(), 0.0f);
    }
    for (bool& b : m_noteOn) b = false;
    for (Svf& f : m_lowCut) f.m_ic1 = f.m_ic2 = 0.0f;
    for (int s = 0; s < BrassNotes; ++s) { m_noteBrass[s] = m_brassiness; m_noteVib[s] = m_vibratoCents; }
    m_pos = 0;
    m_mod.reset();
}

int Brass::activePlayers() const
{
    int n = 0;
    for (const Player& p : m_player) n += p.m_on;
    return n;
}

void Brass::noteOn(int pitch, float velocity, bool, double)
{
    int slot = -1;
    for (int s = 0; s < BrassNotes && slot < 0; ++s) if (!m_noteOn[s]) slot = s;
    if (slot < 0) {
        double oldest = -1.0;
        for (int s = 0; s < BrassNotes; ++s) if (m_noteAge[s] > oldest) { oldest = m_noteAge[s]; slot = s; }
    }
    m_noteOn[slot] = true;
    m_notePitch[slot] = pitch;
    m_noteAge[slot] = 0.0;
    for (int k = 0; k < BrassPlayers; ++k) {
        Player& p = m_player[slot * BrassPlayers + k];
        std::vector<float> line = std::move(p.m_line);
        p = Player{};
        p.m_line = std::move(line);
        std::fill(p.m_line.begin(), p.m_line.end(), 0.0f);
        if (k >= m_players) continue;
        p.m_on = true;
        p.m_held = true;
        p.m_note = slot;
        p.m_rng.seed(mixSeed(m_seed, (static_cast<uint64_t>(m_counter++) << 4) + static_cast<uint64_t>(k)));
        const double cents = 4.0 * p.m_rng.bipolar();
        const double f0 = midiToHz(pitch) * std::pow(2.0, cents / 1200.0);
        // The bell's low pass (its cut-off rises with the register) and the loop's delay for f0 (less the low pass's).
        const double fc = std::clamp(4.0 * f0, 600.0, 4000.0);
        p.m_bellA = std::exp(-2.0 * PiD * fc / m_sr);
        p.m_delay = std::clamp(m_sr / f0 - p.m_bellA / (1.0 - p.m_bellA), 4.0, static_cast<double>(LineMax - 4));
        p.m_period = m_sr / f0;
        p.m_wl = 2.0 * PiD * f0 * LipRatio;
        p.m_y = LipRest;
        p.m_target = m_pressure * (2500.0 + 6500.0 * velocity) * std::pow(f0 / 100.0, 0.3) * (1.0 + 0.05 * p.m_rng.bipolar());
        p.m_target0 = p.m_target;
        p.m_f0 = f0;
        p.m_start = 0.03 * p.m_rng.uniform();
        p.m_vibHz = 4.5 + p.m_rng.uniform();
        p.m_vibPhase = 2.0 * PiD * p.m_rng.uniform();
        const float pan = std::clamp((0.35f * static_cast<float>(k) - 0.35f + 0.2f * p.m_rng.bipolar()) * m_width, -1.0f, 1.0f);
        p.m_panL = std::cos((pan + 1.0f) * Pi / 4.0f);
        p.m_panR = std::sin((pan + 1.0f) * Pi / 4.0f);
        p.m_pan0 = pan;
    }
    m_noteBrass[slot] = m_brassiness;
    m_noteVib[slot] = m_vibratoCents;
    m_mod.noteOn(slot, m_pos, pitch, velocity);
    if (m_mod.on()) modNote(slot);
}

void Brass::noteOff(int pitch)
{
    for (int s = 0; s < BrassNotes; ++s) {
        if (!m_noteOn[s] || m_notePitch[s] != pitch) continue;
        m_mod.noteOff(s);
        for (int k = 0; k < BrassPlayers; ++k) {
            Player& p = m_player[s * BrassPlayers + k];
            if (p.m_on && p.m_held) { p.m_held = false; p.m_released = 0.0; }
        }
    }
}

void Brass::process(float* L, float* R, int n)
{
    const double dt = 1.0 / m_sr;
    const double att = static_cast<double>(m_attackMs) * 1e-3, rel = static_cast<double>(m_releaseMs) * 1e-3;
    const bool modded = m_mod.on();
    for (int i = 0; i < n; ++i) {
        if (modded && m_pos % MaxBlock == 0)
            for (int s = 0; s < BrassNotes; ++s) if (m_noteOn[s]) modNote(s);
        ++m_pos;
        float outL = 0.0f, outR = 0.0f;
        for (int s = 0; s < BrassNotes; ++s) {
            if (!m_noteOn[s]) continue;
            m_noteAge[s] += dt;
            bool any = false;
            for (int k = 0; k < BrassPlayers; ++k) {
                Player& p = m_player[s * BrassPlayers + k];
                if (!p.m_on) continue;
                any = true;
                p.m_t += dt;
                // The breath: tongued (the pressure is there within 10 ms), the fall after the note; the attack knob swells
                // the sound that leaves the bell.
                double breath = 0.0, swell = 0.0;
                const double t = p.m_t - p.m_start;
                if (t > 0.0) {
                    breath = t < 0.01 ? t / 0.01 : 1.0;
                    swell = t < att ? std::sin(0.5 * PiD * t / att) * (1.0 + 0.15 * std::sin(PiD * t / att)) : 1.0;
                }
                if (!p.m_held) {
                    const double fall = std::max(0.0, 1.0 - p.m_released / rel);
                    breath *= fall;
                    swell *= fall;
                    p.m_released += dt;
                }
                const double vib = 1.0 + (std::pow(2.0, m_noteVib[s] * std::sin(p.m_vibPhase) / 1200.0) - 1.0) * std::clamp((t - 0.25) / 0.3, 0.0, 1.0);
                p.m_vibPhase += 2.0 * PiD * p.m_vibHz * dt;
                p.m_pm = p.m_target * breath;
                // The returning wave: the bell's reflection of what left one round trip ago.
                const double rd = static_cast<double>(p.m_write) - p.m_delay;
                const double pos = rd < 0.0 ? rd + LineMax : rd;
                const int i0 = static_cast<int>(pos), i1 = (i0 + 1) % LineMax;
                const double fr = pos - i0;
                const double back = p.m_line[static_cast<size_t>(i0)] * (1.0 - fr) + p.m_line[static_cast<size_t>(i1)] * fr;
                p.m_bell = (1.0 - p.m_bellA) * back + p.m_bellA * p.m_bell;
                const double pMinus = BellReflection * p.m_bell;
                // The lips on two sub-steps, the returning wave held: the flow in closed form, the valve pushed open.
                const double h2 = 0.5 * dt, wl = p.m_wl * vib;
                double pMouth = 2.0 * pMinus;
                for (int step = 0; step < LipSteps; ++step) {
                    const double A = LipWidth * std::max(p.m_y, 0.0) * std::sqrt(2.0 / Rho);
                    const double dp0 = p.m_pm - 2.0 * pMinus;
                    double U = 0.0;
                    if (A > 0.0) {
                        const double a2 = A * A;
                        U = 0.5 * (-a2 * Zc + std::sqrt(a2 * a2 * Zc * Zc + 4.0 * a2 * std::fabs(dp0)));
                        if (dp0 < 0.0) U = -U;
                    }
                    pMouth = 2.0 * pMinus + Zc * U;
                    const double acc = (p.m_pm - pMouth) / LipMass - (wl / LipQ) * p.m_v - wl * wl * (p.m_y - LipRest);
                    p.m_v += acc * h2;
                    p.m_y += p.m_v * h2;
                }
                const double pPlus = pMouth - pMinus;
                p.m_line[static_cast<size_t>(p.m_write)] = static_cast<float>(pPlus);
                p.m_write = (p.m_write + 1) % LineMax;
                // The player's ear: the period at the upward zero crossings (interpolated), the delay pulled a third of
                // the way towards the note each period.
                p.m_mean += 0.001 * (pPlus - p.m_mean);
                const double xc = pPlus - p.m_mean;
                if (p.m_prevX < 0.0 && xc >= 0.0) {
                    const double at = static_cast<double>(p.m_clock) - 1.0 + (-p.m_prevX) / (xc - p.m_prevX);
                    if (p.m_lastCross >= 0.0) {
                        const double measured = at - p.m_lastCross;
                        if (++p.m_crossings > 2 && measured > 0.5 * p.m_period && measured < 2.0 * p.m_period)
                            p.m_delay = std::clamp(p.m_delay - 0.33 * (measured - p.m_period), 4.0, static_cast<double>(LineMax - 4));
                    }
                    p.m_lastCross = at;
                }
                p.m_prevX = xc;
                ++p.m_clock;
                // What leaves the bell (the outgoing wave less what returns), steepened when loud, no constant part.
                double bell = (back - pMinus) / 4000.0;
                bell += static_cast<double>(m_noteBrass[s]) * 1.5 * bell * std::fabs(bell);
                const float o = static_cast<float>(bell * swell);
                p.m_dc += 0.002f * (o - p.m_dc);
                p.m_out = o - p.m_dc;
                outL += p.m_out * p.m_panL;
                outR += p.m_out * p.m_panR;
                if (!p.m_held && p.m_released > rel + 0.2) p.m_on = false;
            }
            if (!any) m_noteOn[s] = false;
        }
        float lp, bp, hp;
        m_lowCut[0].tick(outL * m_level, lp, bp, hp);
        L[i] = hp;
        m_lowCut[1].tick(outR * m_level, lp, bp, hp);
        R[i] = hp;
    }
}

} // namespace parh
