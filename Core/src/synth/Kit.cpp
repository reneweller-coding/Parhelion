/**
 * @file Kit.cpp
 * @brief Percussion kit: coefficients, hits, the metal table and rendering.
 * @note Adapted from Phosphene `Core/src/Perc.cpp` at 76f7100 (27.09.2026); see Kit.h for what changed.
 * @note Copied from Totality `Core/src/synth/Kit.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#include "parh/synth/Kit.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {

constexpr double kPiD = 3.141592653589793;   ///< pi
constexpr double kLn1000 = 6.907755278982137;   ///< ln 1000: a decay of 60 dB

/** @brief Mode frequency ratios: membrane (Bessel zeros), free-free bar, loaded membrane (tabla). */
constexpr double kModeRatios[3][kPercModes] = {
    { 1.0, 1.593, 2.136, 2.295 },
    { 1.0, 2.756, 5.404, 8.933 },
    { 1.0, 2.0, 3.0, 4.0 },
};
/** @brief Strike amplitudes of the modes, before normalisation. */
constexpr double kModeAmps[3][kPercModes] = {
    { 1.0, 0.7, 0.5, 0.4 },
    { 1.0, 0.5, 0.25, 0.12 },
    { 1.0, 0.5, 0.33, 0.25 },
};
/** @brief TR-808 cymbal oscillator frequencies. */
constexpr double kMetalHz[kPercMetalOsc] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
/** @brief How far a metal lane's noise pulls its squares down (Phosphene, 25.09.2026). */
constexpr float kMetalNoiseTrade = 0.8f;
/** @brief The auto-pan's swing amplitude and the largest angle the kernel's series may be asked for (Phosphene). */
constexpr double kPanRms = 1.4142135623730951;
constexpr double kPanMaxAngle = 1.6;   ///< the auto-pan's widest swing, radians

/** @brief The per-sample factor of a decay of 60 dB in @p seconds at rate @p sr. */
double decayFactor(double seconds, double sr) { return std::exp(-kLn1000 / (std::max(seconds, 1.0e-4) * sr)); }

/** @brief Trapezoidal SVF coefficients (Phosphene's Util.h). */
void svfCoefs(double fc, double damping, double sr, float& a1, float& a2, float& a3)
{
    const double g = std::tan(kPiD * std::clamp(fc, 10.0, 0.45 * sr) / sr);
    const double d1 = 1.0 / (1.0 + g * (g + damping));
    a1 = static_cast<float>(d1);
    a2 = static_cast<float>(g * d1);
    a3 = static_cast<float>(g * g * d1);
}

/**
 * @brief The phase group of each role (Phosphene's rule: a property of what a lane plays, never of a level). The
 *        closed hat against the rolling hat, the ride, the shaker and the rim; clap, snare and the hand drums with it.
 */
constexpr int8_t kRolePanGroup[kNumPercRoles] = {
    +1,   // Closed Hat
    -1,   // Rolling Hat
    +1,   // Open Hat
    -1,   // Ride
    +1,   // Clap (centred: no swing)
    +1,   // Clap Ghost
    +1,   // Snare
    -1,   // Rim
    -1,   // Shaker
    +1,   // Tom
    +1,   // Conga
    -1,   // Noise
};

} // namespace

double PercKit::tuneToScale(double hz, int keyRoot, int scale)
{
    const double note = 69.0 + 12.0 * std::log2(hz / 440.0);
    const int base = static_cast<int>(std::floor(note));
    double best = hz, bestDist = 1e9;
    for (int n = base - 2; n <= base + 3; ++n) {
        if (!inScale(scale, n - keyRoot)) continue;
        const double d = std::fabs(n - note);
        if (d < bestDist) { bestDist = d; best = midiToHz(n); }
    }
    return best;
}

void PercKit::setTempo(double bpm)
{
    const double b = std::clamp(bpm, 20.0, 400.0);
    if (b == bpm_) return;
    bpm_ = b;
    for (int l = 0; l < kPercLanes; ++l) if (valid_[l]) updatePanRate(l);
}

void PercKit::assignPanGroups()
{
    for (int l = 0; l < kPercLanes; ++l) {
        const double dir = values_[l][perc::Pan] < 0.0f ? -1.0 : 1.0;
        const int role = std::clamp(static_cast<int>(std::lround(values_[l][perc::Role])), 0, kNumPercRoles - 1);
        c_.panA[l] = static_cast<float>(std::fabs(static_cast<double>(c_.panA[l])) * dir * static_cast<double>(kRolePanGroup[role]));
    }
    panning_ = false;
    for (int l = 0; l < kPercLanes; ++l) panning_ = panning_ || c_.panA[l] != 0.0f || c_.panB[l] != 0.0f;
}

void PercKit::buildMetalTable()
{
    // A fixed seed: the table is part of the instrument, the same on every machine and in every render.
    Rng rng;
    rng.seed(0x3930394D4554414Cull);   // "909METAL"
    const int n = static_cast<int>(kMetalRate);   // one second
    std::vector<double> acc(static_cast<size_t>(n), 0.0);
    constexpr int kPartials = 60;
    for (int p = 0; p < kPartials; ++p) {
        const double f = 2800.0 * std::pow(17000.0 / 2800.0, static_cast<double>(rng.uniform()));
        const double amp = (0.5 + 0.5 * rng.uniform()) * std::pow(f / 2800.0, -0.3);
        const double tau = 0.25 + 1.0 * rng.uniform();
        const double w = 2.0 * kPiD * f / kMetalRate;
        const double r = std::exp(-1.0 / (tau * kMetalRate));
        const double ph = 2.0 * kPiD * rng.uniform();
        // A damped rotating phasor per partial: exact, and far cheaper than a sine per sample.
        double zr = amp * std::cos(ph), zi = amp * std::sin(ph);
        const double cr = r * std::cos(w), ci = r * std::sin(w);
        for (int i = 0; i < n; ++i) {
            acc[static_cast<size_t>(i)] += zi;
            const double nr = zr * cr - zi * ci;
            zi = zr * ci + zi * cr;
            zr = nr;
        }
    }
    const double noiseR = std::exp(-1.0 / (0.5 * kMetalRate));
    double noiseAmp = 0.15;
    double peak = 0.0;
    for (int i = 0; i < n; ++i) {
        acc[static_cast<size_t>(i)] += noiseAmp * rng.bipolar();
        noiseAmp *= noiseR;
        peak = std::max(peak, std::fabs(acc[static_cast<size_t>(i)]));
    }
    metal_.assign(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        // Six bits: 64 levels, -32 .. 31 steps of 1/32.
        const double q = std::round(acc[static_cast<size_t>(i)] / peak * 31.0);
        metal_[static_cast<size_t>(i)] = static_cast<float>(std::clamp(q, -32.0, 31.0) / 32.0);
    }
}

void PercKit::prepare(double sampleRate)
{
    sr_ = sampleRate;
    const size_t n = static_cast<size_t>(kMaxBlock * kPercLaneSlots);
    noise_.assign(n, 0.0f);
    reset_.assign(n, 0.0f);
    dNoise_.assign(n, 1.0f);
    outL_.assign(n, 0.0f);
    outR_.assign(n, 0.0f);
    chokeFactor_ = static_cast<float>(decayFactor(0.008, sr_));
    for (int l = 0; l < kPercLanes; ++l) { valid_[l] = false; shiftMul_[l] = 1.0; }
    if (metal_.empty()) buildMetalTable();
    reset();
}

void PercKit::reset()
{
    s_ = PercState{};
    for (int l = 0; l < kPercLaneSlots; ++l) {
        s_.cr[l] = 1.0f;
        s_.mr[l] = 1.0f;
        s_.choke[l] = 1.0f;
        c_.chokeD[l] = 1.0f;
        c_.dP[l] = 1.0f;
        c_.dA[l] = 1.0f;
        s_.pr[l] = 1.0f;
        s_.pi[l] = 0.0f;
        c_.panC[l] = 1.0f;
        c_.panS[l] = 0.0f;
    }
    for (int l = 0; l < kPercLanes; ++l) {
        noiseRng_[l].seed(0x5045524300ull + static_cast<uint64_t>(l));
        burstsLeft_[l] = 0;
        burstTimer_[l] = 0.0;
        metalPos_[l] = 1.0e30;   // past the end: silent until a hit
    }
    // The coefficients describe the lanes' settings, not their state: recompute them from the last values.
    for (int l = 0; l < kPercLanes; ++l) if (valid_[l]) computeCoefs(l);
}

void PercKit::update(int lane, const float* v, int keyRoot, int scale)
{
    bool same = valid_[lane] && keyRoot == keyRoot_[lane] && scale == scale_[lane];
    for (int i = 0; same && i < perc::Count; ++i) same = values_[lane][i] == v[i];
    if (same) return;
    std::copy(v, v + perc::Count, values_[lane]);
    keyRoot_[lane] = keyRoot;
    scale_[lane] = scale;
    valid_[lane] = true;
    computeCoefs(lane);
}

void PercKit::computeCoefs(int l)
{
    const float* v = values_[l];
    const bool active = v[perc::Active] >= 0.5f;
    role_[l] = static_cast<int>(std::lround(v[perc::Role]));
    engine_[l] = static_cast<int>(std::lround(v[perc::Engine]));
    choke_[l] = static_cast<int>(std::lround(v[perc::Choke]));
    metalNoise_[l] = std::lround(v[perc::NoiseType]) == static_cast<long>(NoiseType::Metal909);
    const PercEngine engine = static_cast<PercEngine>(engine_[l]);

    double hz = v[perc::Pitch];
    if (v[perc::Tune] >= 0.5f) hz = tuneToScale(hz, keyRoot_[l], scale_[l]);
    tunedHz_[l] = hz;
    const double f = hz * shiftMul_[l];

    c_.wTone[l] = (engine == PercEngine::Tone || engine == PercEngine::Fm) ? 1.0f : 0.0f;
    c_.wModal[l] = engine == PercEngine::Modal ? 1.0f : 0.0f;
    c_.wMetal[l] = engine == PercEngine::Metal ? 1.0f - kMetalNoiseTrade * v[perc::Noise] : 0.0f;
    c_.wNoise[l] = engine == PercEngine::Noise ? 1.0f : v[perc::Noise];

    const double w0 = std::min(2.0 * kPiD * f / sr_, 1.2);
    c_.w0[l] = static_cast<float>(w0);
    c_.pAmt[l] = v[perc::PitchAmount] - 1.0f;
    c_.dP[l] = static_cast<float>(std::exp(-1.0 / (std::max(0.5e-3, v[perc::PitchDecay] * 0.001) * sr_)));
    c_.fmIdx[l] = engine == PercEngine::Fm ? v[perc::FmIndex] : 0.0f;
    const double wm = std::min(w0 * v[perc::FmRatio], 0.95 * kPiD);
    c_.mc[l] = static_cast<float>(std::cos(wm));
    c_.ms[l] = static_cast<float>(std::sin(wm));
    const double decay = v[perc::Decay] * 0.001;
    c_.dA[l] = static_cast<float>(decayFactor(decay, sr_));

    const int set = std::clamp(static_cast<int>(std::lround(v[perc::ModeSet])), 0, 2);
    double ampSum = 0.0;
    for (int m = 0; m < kPercModes; ++m) ampSum += kModeAmps[set][m];
    for (int m = 0; m < kPercModes; ++m) {
        const double wk = 2.0 * kPiD * f * kModeRatios[set][m] / sr_;
        const bool audible = wk < 0.95 * kPiD;
        const double tau = decay * std::pow(kModeRatios[set][m], -1.5 * v[perc::ModeDamp]);
        const double r = audible ? decayFactor(tau, sr_) : 0.0;
        modeW_[m][l] = wk;
        modeR_[m][l] = r;
        modeAmp_[m][l] = audible ? static_cast<float>(kModeAmps[set][m] / ampSum) : 0.0f;
        c_.modeC[m][l] = static_cast<float>(r * std::cos(wk));
        c_.modeS[m][l] = static_cast<float>(r * std::sin(wk));
    }

    const double scale = v[perc::MetalScale];
    for (int j = 0; j < kPercMetalOsc; ++j) {
        const double dt = std::min(kMetalHz[j] * scale / sr_, 0.45);
        c_.dt[j][l] = static_cast<float>(dt);
        c_.inv[j][l] = static_cast<float>(1.0 / dt);
    }
    // The metal table's playback rate: Metal Scale times its own rate (the 909's tuning, 0.43 .. 1.7).
    metalStep_[l] = scale * kMetalRate / sr_;

    const double spacing = v[perc::BurstSpacing] * 0.001;
    burstSpacing_[l] = spacing * sr_;
    noiseTail_[l] = static_cast<float>(decayFactor(v[perc::NoiseDecay] * 0.001, sr_));
    noiseFast_[l] = static_cast<float>(std::exp(std::log(0.03) / std::max(1.0, spacing * sr_)));

    const int mode = std::clamp(static_cast<int>(std::lround(v[perc::Filter])), 0, 2);
    const double damping = 2.0 - 1.9 * std::clamp(static_cast<double>(v[perc::Resonance]), 0.0, 1.0);
    svfCoefs(v[perc::Cutoff], damping, sr_, c_.a1[l], c_.a2[l], c_.a3[l]);
    c_.k[l] = static_cast<float>(damping);
    c_.mLp[l] = mode == 0 ? 1.0f : 0.0f;
    c_.mBp[l] = mode == 1 ? 1.0f : 0.0f;
    c_.mHp[l] = mode == 2 ? 1.0f : 0.0f;
    // Low cut, never below 150 Hz: under it only kick, rumble and sub play (PLAN 5.4).
    const double cutTrack = std::clamp(static_cast<double>(v[perc::CutTrack]), 0.0, 2.0);
    const double lowCut = static_cast<double>(v[perc::LowCut]) * (cutTrack > 0.0 ? std::pow(shiftMul_[l], cutTrack) : 1.0);
    svfCoefs(std::max(150.0, lowCut), std::sqrt(2.0), sr_, c_.c1[l], c_.c2[l], c_.c3[l]);

    const double g = 0.1 + 6.0 * v[perc::Drive];
    c_.drvG[l] = static_cast<float>(g);
    c_.drvN[l] = static_cast<float>(1.0 / g);
    const double level = active ? std::pow(10.0, v[perc::Level] / 20.0) : 0.0;
    const double p0 = std::clamp(static_cast<double>(v[perc::Pan]), -1.0, 1.0);
    const double theta = (p0 + 1.0) * kPiD / 4.0;
    c_.gL[l] = static_cast<float>(level * std::cos(theta) * std::sqrt(2.0));
    c_.gR[l] = static_cast<float>(level * std::sin(theta) * std::sqrt(2.0));

    const double depth = std::clamp(static_cast<double>(v[perc::PanDepth]), 0.0, 1.0);
    double stand = std::sqrt(std::max(0.0, 1.0 - depth * depth)), swing = kPanRms * depth;
    const double reach = std::fabs(p0) * (stand + swing);
    if (reach > 1.0) { stand /= reach; swing /= reach; }
    double a = p0 * swing * kPiD / 4.0;
    double b = p0 * (stand - 1.0) * kPiD / 4.0;
    const double span = std::fabs(a) + std::fabs(b);
    if (span > kPanMaxAngle) { const double k = kPanMaxAngle / span; a *= k; b *= k; }
    c_.panA[l] = static_cast<float>(a);
    c_.panB[l] = static_cast<float>(b);
    updatePanRate(l);
    assignPanGroups();
}

void PercKit::updatePanRate(int l)
{
    const double bars = std::clamp(static_cast<double>(values_[l][perc::PanBars]), 0.0625, 64.0);
    const double w = 2.0 * kPiD / std::max(1.0, bars * 4.0 * 60.0 / bpm_ * sr_);
    c_.panC[l] = static_cast<float>(std::cos(w));
    c_.panS[l] = static_cast<float>(std::sin(w));
}

void PercKit::trigger(int l, float velocity, int shift, double late)
{
    if (l < 0 || l >= kPercLanes || !valid_[l]) return;
    const double mul = std::pow(2.0, shift / 12.0);
    if (mul != shiftMul_[l]) { shiftMul_[l] = mul; computeCoefs(l); }
    const double vel = clampv(static_cast<double>(velocity), 0.0, 1.0);

    s_.envA[l] = static_cast<float>(vel * std::pow(static_cast<double>(c_.dA[l]), late));
    s_.envP[l] = static_cast<float>(std::pow(static_cast<double>(c_.dP[l]), late));
    const int bursts = static_cast<int>(std::lround(values_[l][perc::Bursts]));
    burstsLeft_[l] = bursts - 1;
    burstTimer_[l] = burstSpacing_[l] - late;
    burstVel_[l] = static_cast<float>(vel);
    const double nf = bursts > 1 ? noiseFast_[l] : noiseTail_[l];
    s_.envN[l] = static_cast<float>(vel * std::pow(nf, late));
    metalPos_[l] = metalStep_[l] * late;

    const double wStart = std::min(static_cast<double>(c_.w0[l]) * (1.0 + c_.pAmt[l]), 1.2);
    s_.cr[l] = static_cast<float>(std::cos(wStart * late));
    s_.ci[l] = static_cast<float>(std::sin(wStart * late));
    s_.mr[l] = static_cast<float>(std::cos(std::atan2(c_.ms[l], c_.mc[l]) * late));
    s_.mi[l] = static_cast<float>(std::sin(std::atan2(c_.ms[l], c_.mc[l]) * late));
    for (int m = 0; m < kPercModes; ++m) {
        const double a = modeAmp_[m][l] * vel * std::pow(modeR_[m][l], late);
        s_.zr[m][l] += static_cast<float>(a * std::cos(modeW_[m][l] * late));
        s_.zi[m][l] += static_cast<float>(a * std::sin(modeW_[m][l] * late));
    }
    s_.choke[l] = 1.0f;
    c_.chokeD[l] = 1.0f;
    if (choke_[l] > 0) {
        for (int j = 0; j < kPercLanes; ++j) {
            if (j != l && choke_[j] == choke_[l]) c_.chokeD[j] = chokeFactor_;
        }
    }
}

template <class V>
void PercKit::processLanesWith(int n)
{
    n = std::min(n, kMaxBlock);
    const int width = laneWidth<V>();
    bool tone[2] = {}, modal[2] = {}, metal[2] = {}, noisy[2] = {};
    for (int l = 0; l < kPercLanes; ++l) {
        const int g = l / 8;
        tone[g] = tone[g] || c_.wTone[l] != 0.0f;
        modal[g] = modal[g] || c_.wModal[l] != 0.0f;
        metal[g] = metal[g] || c_.wMetal[l] != 0.0f;
        noisy[g] = noisy[g] || c_.wNoise[l] != 0.0f;
    }
    const size_t tableLen = metal_.size();
    for (int l = 0; l < kPercLaneSlots; ++l) {
        const bool live = l < kPercLanes;
        for (int i = 0; i < n; ++i) {
            const size_t row = static_cast<size_t>(i * kPercLaneSlots + l);
            reset_[row] = 0.0f;
            if (!live) { noise_[row] = 0.0f; dNoise_[row] = 1.0f; continue; }
            float nz = 0.0f;
            if (noisy[l / 8]) {
                if (metalNoise_[l]) {
                    const double pos = metalPos_[l];
                    if (pos < static_cast<double>(tableLen - 1)) {
                        const size_t k = static_cast<size_t>(pos);
                        const float fr = static_cast<float>(pos - static_cast<double>(k));
                        nz = metal_[k] + fr * (metal_[k + 1] - metal_[k]);
                        metalPos_[l] = pos + metalStep_[l];
                    }
                } else {
                    nz = noiseRng_[l].bipolar();
                }
            }
            noise_[row] = nz;
            if (burstsLeft_[l] > 0) {
                burstTimer_[l] -= 1.0;
                if (burstTimer_[l] <= 0.0) {
                    reset_[row] = burstVel_[l];
                    --burstsLeft_[l];
                    burstTimer_[l] += burstSpacing_[l];
                }
            }
            dNoise_[row] = burstsLeft_[l] > 0 ? noiseFast_[l] : noiseTail_[l];
        }
    }
    for (int l = 0; l < kPercLaneSlots; l += width) {
        const int g = std::min(l / 8, 1);
        percKernel<V>(s_, c_, l, n, noise_.data(), reset_.data(), dNoise_.data(), tone[g], modal[g], metal[g], panning_,
                      outL_.data(), outR_.data());
    }
}

template void PercKit::processLanesWith<float>(int);
#if PARH_VEC_PATH != 0
template void PercKit::processLanesWith<VecF>(int);
#endif

void PercKit::processLanes(int n)
{
    processLanesWith<VecF>(n);
}

void PercKit::process(float* L, float* R, int total)
{
    int done = 0;
    while (done < total) {
        const int n = std::min(kMaxBlock, total - done);
        processLanes(n);
        for (int i = 0; i < n; ++i) {
            float sl = 0.0f, sr = 0.0f;
            for (int l = 0; l < kPercLanes; ++l) {
                sl += outL_[static_cast<size_t>(i * kPercLaneSlots + l)];
                sr += outR_[static_cast<size_t>(i * kPercLaneSlots + l)];
            }
            L[done + i] = sl;
            R[done + i] = sr;
        }
        done += n;
    }
}

} // namespace parh
