/**
 * @file Brass.cpp
 * @brief The brass (Brass.h).
 */
#include "parh/synth/Brass.h"
#include "parh/Params.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {
constexpr double kPiD = 3.14159265358979323846;   ///< pi
constexpr int kLineMax = 4096;   ///< the bore's longest delay, samples
constexpr double kRho = 1.2;   ///< air's density, kg/m^3
constexpr double kC = 343.0;   ///< the speed of sound, m/s
constexpr double kBoreArea = 1.5e-4;             ///< m^2 (a horn's or a trombone's bore)
constexpr double kZc = kRho * kC / kBoreArea;    ///< the bore's characteristic impedance
constexpr double kLipWidth = 0.01;   ///< the lips' width, m
constexpr double kLipMass = 1.5;   ///< the lips' mass per area, kg/m^2
constexpr double kLipRest = 1e-4;   ///< the lips' rest opening, m
constexpr double kLipQ = 7.0;   ///< the lips' quality factor
constexpr double kLipRatio = 0.8;   ///< the lips' own frequency over the note's
constexpr double kBellReflection = 0.95;   ///< the bell's reflection
/** @brief sub-steps of the lips per sample */
constexpr int kLipSteps = 2;}

void Brass::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate;
    seed_ = seed;
    mod_.prepare(sampleRate, mixSeed(seed, 0x4D4F44ull));   // "MOD"
    for (Player& p : player_) p.line.assign(kLineMax, 0.0f);
    for (Svf& f : lowCut_) f.setQ(lowCutHz_, 0.707f, static_cast<float>(sr_));
    reset();
}

void Brass::update(const float* v)
{
    level_ = dbToGain(v[brass::Level]);
    setPlayers(static_cast<int>(std::lround(v[brass::Players])));
    pressure_ = v[brass::Pressure];
    brassiness_ = v[brass::Brassiness];
    attackMs_ = v[brass::Attack];
    releaseMs_ = v[brass::Release];
    vibratoCents_ = v[brass::Vibrato];
    width_ = v[brass::Width];
    if (v[brass::LowCut] != lowCutHz_) {
        lowCutHz_ = v[brass::LowCut];
        for (Svf& f : lowCut_) f.setQ(lowCutHz_, 0.707f, static_cast<float>(sr_));
    }
    mod_.set(readModCore(v + brass::ModFirst, kBrassModDests, static_cast<int>(std::size(kBrassModDests))));
    for (int s = 0; s < kBrassNotes; ++s) {
        if (!mod_.targets(ModDest::Brassiness)) noteBrass_[s] = brassiness_;
        if (!mod_.targets(ModDest::VibDepth)) noteVib_[s] = vibratoCents_;
    }
}

void Brass::modNote(int s)
{
    mod_.evaluate(s, pos_);
    noteBrass_[s] = std::clamp(brassiness_ + mod_.get(s, ModDest::Brassiness), 0.0f, 1.0f);
    noteVib_[s] = vibratoCents_ * std::clamp(1.0f + mod_.get(s, ModDest::VibDepth), 0.0f, 4.0f);
    const double breath = std::exp2(std::clamp(static_cast<double>(mod_.get(s, ModDest::Pressure)), -2.0, 1.0));
    const double bend = std::pow(2.0, std::clamp(static_cast<double>(mod_.get(s, ModDest::Pitch)), -2.0, 2.0) / 12.0);
    const bool pan = mod_.targets(ModDest::Pan) || mod_.targets(ModDest::Level);
    const float gain = std::clamp(1.0f + mod_.get(s, ModDest::Level), 0.0f, 2.0f);
    for (int k = 0; k < kBrassPlayers; ++k) {
        Player& p = player_[s * kBrassPlayers + k];
        if (!p.on) continue;
        p.target = p.target0 * breath;
        if (mod_.targets(ModDest::Pitch)) {
            // The lips set to the bent note, the bore's delay moved with it (the ear refines it).
            const double f = p.f0 * bend, period = sr_ / f;
            p.delay = std::clamp(p.delay * period / p.period, 4.0, static_cast<double>(kLineMax - 4));
            p.period = period;
            p.wl = 2.0 * kPiD * f * kLipRatio;
        }
        if (pan) {
            const float x = std::clamp(p.pan0 + mod_.get(s, ModDest::Pan), -1.0f, 1.0f);
            p.panL = gain * std::cos((x + 1.0f) * kPi / 4.0f);
            p.panR = gain * std::sin((x + 1.0f) * kPi / 4.0f);
        }
    }
}

void Brass::reset()
{
    for (Player& p : player_) {
        std::vector<float> line = std::move(p.line);
        p = Player{};
        p.line = std::move(line);
        std::fill(p.line.begin(), p.line.end(), 0.0f);
    }
    for (bool& b : noteOn_) b = false;
    for (Svf& f : lowCut_) f.ic1 = f.ic2 = 0.0f;
    for (int s = 0; s < kBrassNotes; ++s) { noteBrass_[s] = brassiness_; noteVib_[s] = vibratoCents_; }
    pos_ = 0;
    mod_.reset();
}

int Brass::activePlayers() const
{
    int n = 0;
    for (const Player& p : player_) n += p.on;
    return n;
}

void Brass::noteOn(int pitch, float velocity, bool, double)
{
    int slot = -1;
    for (int s = 0; s < kBrassNotes && slot < 0; ++s) if (!noteOn_[s]) slot = s;
    if (slot < 0) {
        double oldest = -1.0;
        for (int s = 0; s < kBrassNotes; ++s) if (noteAge_[s] > oldest) { oldest = noteAge_[s]; slot = s; }
    }
    noteOn_[slot] = true;
    notePitch_[slot] = pitch;
    noteAge_[slot] = 0.0;
    for (int k = 0; k < kBrassPlayers; ++k) {
        Player& p = player_[slot * kBrassPlayers + k];
        std::vector<float> line = std::move(p.line);
        p = Player{};
        p.line = std::move(line);
        std::fill(p.line.begin(), p.line.end(), 0.0f);
        if (k >= players_) continue;
        p.on = true;
        p.held = true;
        p.note = slot;
        p.rng.seed(mixSeed(seed_, (static_cast<uint64_t>(counter_++) << 4) + static_cast<uint64_t>(k)));
        const double cents = 4.0 * p.rng.bipolar();
        const double f0 = midiToHz(pitch) * std::pow(2.0, cents / 1200.0);
        // The bell's low pass (its cut-off rises with the register) and the loop's delay for f0 (less the low pass's).
        const double fc = std::clamp(4.0 * f0, 600.0, 4000.0);
        p.bellA = std::exp(-2.0 * kPiD * fc / sr_);
        p.delay = std::clamp(sr_ / f0 - p.bellA / (1.0 - p.bellA), 4.0, static_cast<double>(kLineMax - 4));
        p.period = sr_ / f0;
        p.wl = 2.0 * kPiD * f0 * kLipRatio;
        p.y = kLipRest;
        p.target = pressure_ * (2500.0 + 6500.0 * velocity) * std::pow(f0 / 100.0, 0.3) * (1.0 + 0.05 * p.rng.bipolar());
        p.target0 = p.target;
        p.f0 = f0;
        p.start = 0.03 * p.rng.uniform();
        p.vibHz = 4.5 + p.rng.uniform();
        p.vibPhase = 2.0 * kPiD * p.rng.uniform();
        const float pan = std::clamp((0.35f * static_cast<float>(k) - 0.35f + 0.2f * p.rng.bipolar()) * width_, -1.0f, 1.0f);
        p.panL = std::cos((pan + 1.0f) * kPi / 4.0f);
        p.panR = std::sin((pan + 1.0f) * kPi / 4.0f);
        p.pan0 = pan;
    }
    noteBrass_[slot] = brassiness_;
    noteVib_[slot] = vibratoCents_;
    mod_.noteOn(slot, pos_, pitch, velocity);
    if (mod_.on()) modNote(slot);
}

void Brass::noteOff(int pitch)
{
    for (int s = 0; s < kBrassNotes; ++s) {
        if (!noteOn_[s] || notePitch_[s] != pitch) continue;
        mod_.noteOff(s);
        for (int k = 0; k < kBrassPlayers; ++k) {
            Player& p = player_[s * kBrassPlayers + k];
            if (p.on && p.held) { p.held = false; p.released = 0.0; }
        }
    }
}

void Brass::process(float* L, float* R, int n)
{
    const double dt = 1.0 / sr_;
    const double att = static_cast<double>(attackMs_) * 1e-3, rel = static_cast<double>(releaseMs_) * 1e-3;
    const bool modded = mod_.on();
    for (int i = 0; i < n; ++i) {
        if (modded && pos_ % kMaxBlock == 0)
            for (int s = 0; s < kBrassNotes; ++s) if (noteOn_[s]) modNote(s);
        ++pos_;
        float outL = 0.0f, outR = 0.0f;
        for (int s = 0; s < kBrassNotes; ++s) {
            if (!noteOn_[s]) continue;
            noteAge_[s] += dt;
            bool any = false;
            for (int k = 0; k < kBrassPlayers; ++k) {
                Player& p = player_[s * kBrassPlayers + k];
                if (!p.on) continue;
                any = true;
                p.t += dt;
                // The breath: tongued (the pressure is there within 10 ms), the fall after the note; the attack knob swells
                // the sound that leaves the bell.
                double breath = 0.0, swell = 0.0;
                const double t = p.t - p.start;
                if (t > 0.0) {
                    breath = t < 0.01 ? t / 0.01 : 1.0;
                    swell = t < att ? std::sin(0.5 * kPiD * t / att) * (1.0 + 0.15 * std::sin(kPiD * t / att)) : 1.0;
                }
                if (!p.held) {
                    const double fall = std::max(0.0, 1.0 - p.released / rel);
                    breath *= fall;
                    swell *= fall;
                    p.released += dt;
                }
                const double vib = 1.0 + (std::pow(2.0, noteVib_[s] * std::sin(p.vibPhase) / 1200.0) - 1.0) * std::clamp((t - 0.25) / 0.3, 0.0, 1.0);
                p.vibPhase += 2.0 * kPiD * p.vibHz * dt;
                p.pm = p.target * breath;
                // The returning wave: the bell's reflection of what left one round trip ago.
                const double rd = static_cast<double>(p.write) - p.delay;
                const double pos = rd < 0.0 ? rd + kLineMax : rd;
                const int i0 = static_cast<int>(pos), i1 = (i0 + 1) % kLineMax;
                const double fr = pos - i0;
                const double back = p.line[static_cast<size_t>(i0)] * (1.0 - fr) + p.line[static_cast<size_t>(i1)] * fr;
                p.bell = (1.0 - p.bellA) * back + p.bellA * p.bell;
                const double pMinus = kBellReflection * p.bell;
                // The lips on two sub-steps, the returning wave held: the flow in closed form, the valve pushed open.
                const double h2 = 0.5 * dt, wl = p.wl * vib;
                double pMouth = 2.0 * pMinus;
                for (int step = 0; step < kLipSteps; ++step) {
                    const double A = kLipWidth * std::max(p.y, 0.0) * std::sqrt(2.0 / kRho);
                    const double dp0 = p.pm - 2.0 * pMinus;
                    double U = 0.0;
                    if (A > 0.0) {
                        const double a2 = A * A;
                        U = 0.5 * (-a2 * kZc + std::sqrt(a2 * a2 * kZc * kZc + 4.0 * a2 * std::fabs(dp0)));
                        if (dp0 < 0.0) U = -U;
                    }
                    pMouth = 2.0 * pMinus + kZc * U;
                    const double acc = (p.pm - pMouth) / kLipMass - (wl / kLipQ) * p.v - wl * wl * (p.y - kLipRest);
                    p.v += acc * h2;
                    p.y += p.v * h2;
                }
                const double pPlus = pMouth - pMinus;
                p.line[static_cast<size_t>(p.write)] = static_cast<float>(pPlus);
                p.write = (p.write + 1) % kLineMax;
                // The player's ear: the period at the upward zero crossings (interpolated), the delay pulled a third of
                // the way towards the note each period.
                p.mean += 0.001 * (pPlus - p.mean);
                const double xc = pPlus - p.mean;
                if (p.prevX < 0.0 && xc >= 0.0) {
                    const double at = static_cast<double>(p.clock) - 1.0 + (-p.prevX) / (xc - p.prevX);
                    if (p.lastCross >= 0.0) {
                        const double measured = at - p.lastCross;
                        if (++p.crossings > 2 && measured > 0.5 * p.period && measured < 2.0 * p.period)
                            p.delay = std::clamp(p.delay - 0.33 * (measured - p.period), 4.0, static_cast<double>(kLineMax - 4));
                    }
                    p.lastCross = at;
                }
                p.prevX = xc;
                ++p.clock;
                // What leaves the bell (the outgoing wave less what returns), steepened when loud, no constant part.
                double bell = (back - pMinus) / 4000.0;
                bell += static_cast<double>(noteBrass_[s]) * 1.5 * bell * std::fabs(bell);
                const float o = static_cast<float>(bell * swell);
                p.dc += 0.002f * (o - p.dc);
                p.out = o - p.dc;
                outL += p.out * p.panL;
                outR += p.out * p.panR;
                if (!p.held && p.released > rel + 0.2) p.on = false;
            }
            if (!any) noteOn_[s] = false;
        }
        float lp, bp, hp;
        lowCut_[0].tick(outL * level_, lp, bp, hp);
        L[i] = hp;
        lowCut_[1].tick(outR * level_, lp, bp, hp);
        R[i] = hp;
    }
}

} // namespace parh
