/**
 * @file Piano.cpp
 * @brief The physical piano in real time (Piano.h).
 */
#include "parh/synth/Piano.h"
#include "parh/Params.h"
#include "parh/Vec.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace parh {

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr float kQuietForce = 2e-5f;   ///< N: a voice whose bridge force stays under this is freed
constexpr int kQuietBlocks = 24;
constexpr float kSymQuiet = 2e-7f;     ///< N: a sympathetic string under this stops when its damper is down
constexpr float kLongCoupling = 0.08f; ///< the longitudinal force's share in the bridge's vertical (the downbearing)
/**
 * The sympathetic strings' drive. The one-way coupling (Bank 2010) has no reaction of the ringing strings on the
 * bridge, so where a partial of theirs meets one of the played note's exactly (the octaves and fifths of a stretched
 * tuning) it takes energy the note never loses; a coupled solution would share the note's energy among them instead.
 * The drive is scaled down to where that bound puts them -- together about 12 dB under the note once it is released
 * with the pedal down (measured, testPiano) -- and piano.sympathetic scales it from there. 30.09.2026: 0.03 -> 0.017, since
 * the voicing against Pianoteq (PianoDesign.h) lets the board ring longer and knocks it a hundred times harder, and the
 * free strings listen to the board: at 0.03 they rang 4 dB under the note.
 */
constexpr float kSymDrive = 0.017f;

/** @brief The damper's contact for a released key at pedal @p p (0 up .. 1 down; the middle is the half pedal). */
float damperContact(float p)
{
    const float q = clampv(1.0f - p, 0.0f, 1.0f);
    return q * q;
}

/**
 * @brief A bank of complex one-poles, eight modes side by side: z = p z + g in, out += Re(c z). The outputs of the
 *        eight lanes go to eight accumulators per sample (acc[i * 8 + lane]), so every lane width sums alike.
 * @param in  the input per sample, or null (no input)
 */
template <class V>
void resonate(const float* pr, const float* pi, const float* gr, const float* gi, float* zr, float* zi, const float* cr, const float* ci,
              int padded, const float* in, float* acc, int n)
{
    constexpr int W = laneWidth<V>();
    for (int g = 0; g < padded; g += 8)
        for (int h = 0; h < 8; h += W) {
            const int m = g + h;
            const V Pr = loadLanes<V>(pr + m), Pi = loadLanes<V>(pi + m), Cr = loadLanes<V>(cr + m), Ci = loadLanes<V>(ci + m);
            V Zr = loadLanes<V>(zr + m), Zi = loadLanes<V>(zi + m);
            if (in != nullptr) {
                const V Gr = loadLanes<V>(gr + m), Gi = loadLanes<V>(gi + m);
                for (int i = 0; i < n; ++i) {
                    const V x = lanes<V>(in[i]);
                    const V nr = vfmadd(Gr, x, vfnmadd(Pi, Zi, Pr * Zr));
                    const V ni = vfmadd(Gi, x, vfmadd(Pi, Zr, Pr * Zi));
                    Zr = nr;
                    Zi = ni;
                    float* a = acc + i * 8 + h;
                    vstore(a, loadLanes<V>(a) + vfnmadd(Ci, Zi, Cr * Zr));
                }
            } else {
                for (int i = 0; i < n; ++i) {
                    const V nr = vfnmadd(Pi, Zi, Pr * Zr);
                    const V ni = vfmadd(Pi, Zr, Pr * Zi);
                    Zr = nr;
                    Zi = ni;
                    float* a = acc + i * 8 + h;
                    vstore(a, loadLanes<V>(a) + vfnmadd(Ci, Zi, Cr * Zr));
                }
            }
            vstore(zr + m, Zr);
            vstore(zi + m, Zi);
        }
}

/** @brief The eight accumulators of sample @p i, added in order. */
inline float sum8(const float* acc, int i)
{
    const float* a = acc + i * 8;
    return ((((((a[0] + a[1]) + a[2]) + a[3]) + a[4]) + a[5]) + a[6]) + a[7];
}

} // namespace

void Piano::prepare(double sampleRate)
{
    mod_.prepare(sampleRate, 0x5049414E4Full);   // "PIANO"
    sampleRate_ = sampleRate;
    for (Svf& f : lowCut_) f.setQ(lowCutHz_, 0.707f, static_cast<float>(sampleRate_));
    if (specSet_) setSpec(spec_);
}

void Piano::setSpec(const PianoSpec& spec)
{
    // The radiation shelf: H(s) = (wp / wz) (s + wz) / (s + wp), unity at DC and wp / wz above wz (a first-order low pass
    // where radiationEnd is 0), by the bilinear transform. At radiation 0 it passes the acceleration as it is.
    if (spec.radiation > 0.0f) {
        const double K = 2.0 * sampleRate_, wp = 2.0 * kPiD * spec.radiation;
        const double wz = spec.radiationEnd > spec.radiation ? 2.0 * kPiD * spec.radiationEnd : 0.0;
        const double b0s = wz > 0.0 ? wp / wz : 0.0, b1s = wp, a1s = wp;
        const double den = K + a1s;
        radB0_ = static_cast<float>((b0s * K + b1s) / den);
        radB1_ = static_cast<float>((b1s - b0s * K) / den);
        radA1_ = static_cast<float>((a1s - K) / den);
    } else {
        radB0_ = 1.0f;
        radB1_ = radA1_ = 0.0f;
    }
    spec_ = spec;
    specSet_ = true;
    design_ = designPiano(spec, sampleRate_);
    const PianoDesign& d = *design_;
    int most = 8;
    for (const PianoKey& k : d.keys) most = std::max(most, k.modes.padded);
    for (Voice& v : voices_)
        for (std::vector<float>* a : { &v.zr, &v.zi, &v.pr, &v.pi }) a->assign(static_cast<size_t>(most), 0.0f);
    for (BoardState* b : { &board_, &symBoard_ }) {
        b->zr.assign(static_cast<size_t>(d.board.modes.padded), 0.0f);
        b->zi.assign(static_cast<size_t>(d.board.modes.padded), 0.0f);
        b->hzr.assign(static_cast<size_t>(d.board.high.padded), 0.0f);
        b->hzi.assign(static_cast<size_t>(d.board.high.padded), 0.0f);
    }
    const size_t symSize = static_cast<size_t>(kPianoKeys * kPianoSymPartials);
    for (std::vector<float>* a : { &symZr_, &symZi_, &symPr_, &symPi_ }) a->assign(symSize, 0.0f);
    micL_.assign(static_cast<size_t>(d.board.modes.padded), 0.0f);
    micR_.assign(static_cast<size_t>(d.board.modes.padded), 0.0f);
    // The listening weights for the current width.
    for (int n = 0; n < d.board.modes.padded; ++n) {
        const size_t q = static_cast<size_t>(n);
        micL_[q] = (1.0f - width_) * d.board.micC[q] + width_ * d.board.micL[q];
        micR_[q] = (1.0f - width_) * d.board.micC[q] + width_ * d.board.micR[q];
    }
    reset();
}

void Piano::update(const float* v)
{
    level_ = dbToGain(v[piano::Level]);
    pedal_ = clampv(v[piano::Pedal], 0.0f, 1.0f);
    sympathetic_ = v[piano::Sympathetic];
    phantom_ = v[piano::Phantom];
    damperNoise_ = v[piano::DamperNoise];
    mechanics_ = v[piano::Mechanics];
    if (v[piano::LowCut] != lowCutHz_) {
        lowCutHz_ = v[piano::LowCut];
        for (Svf& f : lowCut_) f.setQ(lowCutHz_, 0.707f, static_cast<float>(sampleRate_));
    }
    if (v[piano::Width] != width_ && design_) {
        width_ = clampv(v[piano::Width], 0.0f, 1.0f);
        const PianoBoard& b = design_->board;
        for (int n = 0; n < b.modes.padded; ++n) {
            const size_t q = static_cast<size_t>(n);
            micL_[q] = (1.0f - width_) * b.micC[q] + width_ * b.micL[q];
            micR_[q] = (1.0f - width_) * b.micC[q] + width_ * b.micR[q];
        }
    }
    mod_.set(readModCore(v + piano::ModFirst, kPianoModDests, static_cast<int>(std::size(kPianoModDests))));
}

void Piano::reset()
{
    for (Voice& v : voices_) {
        v.key = -1;
        v.contact = false;
        std::fill(v.zr.begin(), v.zr.end(), 0.0f);
        std::fill(v.zi.begin(), v.zi.end(), 0.0f);
        v.thump = v.damperNoise = -1;
    }
    for (BoardState* b : { &board_, &symBoard_ })
        for (std::vector<float>* a : { &b->zr, &b->zi, &b->hzr, &b->hzi }) std::fill(a->begin(), a->end(), 0.0f);
    std::fill(symZr_.begin(), symZr_.end(), 0.0f);
    std::fill(symZi_.begin(), symZi_.end(), 0.0f);
    for (int k = 0; k < kPianoKeys; ++k) { held_[k] = 0; symOn_[k] = false; symDamper_[k] = 1.0f; symPeak_[k] = 0.0f; }
    symBoardOn_ = false;
    for (Svf& f : lowCut_) f.ic1 = f.ic2 = 0.0f;
    radX_[0] = radX_[1] = radY_[0] = radY_[1] = 0.0f;
    pos_ = 0;
    mod_.reset();
}

void Piano::modVoice(int i, bool strike)
{
    Voice& v = voices_[i];
    const PianoKey& k = design_->keys[static_cast<size_t>(v.key)];
    mod_.evaluate(i, pos_);
    if (strike) v.hardMul = v.velHard * std::exp2(std::clamp(mod_.get(i, ModDest::Hardness), -2.0f, 2.0f));
    v.bend = std::clamp(static_cast<double>(mod_.get(i, ModDest::Pitch)), -2.0, 2.0);
    v.modGain = std::clamp(1.0f + mod_.get(i, ModDest::Level), 0.0f, 2.0f);
    if (mod_.targets(ModDest::Pan)) {
        // Where on the bridge: the key's place (between its two regions) moved by up to four regions.
        const float place = std::clamp(static_cast<float>(k.region) + k.regionW1 + 4.0f * mod_.get(i, ModDest::Pan),
                                       0.0f, static_cast<float>(kPianoRegions) - 1.001f);
        v.region = static_cast<int>(place);
        v.w1 = place - static_cast<float>(v.region);
        v.w0 = 1.0f - v.w1;
    }
}

int Piano::activeVoices() const
{
    int n = 0;
    for (const Voice& v : voices_) n += v.key >= 0;
    return n;
}

float Piano::maxTensionRise() const
{
    float m = 0.0f;
    for (const Voice& v : voices_) if (v.key >= 0) m = std::max(m, v.delta);
    return m;
}

int Piano::voiceFor(int key)
{
    for (int i = 0; i < voiceLimit_; ++i) if (voices_[i].key == key) return i;
    for (int i = 0; i < voiceLimit_; ++i) if (voices_[i].key < 0) return i;
    // Steal the quietest voice whose hammer is gone (the oldest among equals).
    int best = -1;
    for (int i = 0; i < voiceLimit_; ++i) {
        const Voice& v = voices_[i];
        if (v.contact) continue;
        if (best < 0 || v.lastPeak < voices_[best].lastPeak || (v.lastPeak == voices_[best].lastPeak && v.age < voices_[best].age)) best = i;
    }
    return best < 0 ? 0 : best;
}

void Piano::noteOn(int pitch, float velocity, double late)
{
    if (!design_) return;
    int key = pitch - kPianoLowKey;
    while (key < 0) key += 12;
    while (key >= kPianoKeys) key -= 12;
    ++held_[key];
    const int vi = voiceFor(key);
    Voice& v = voices_[vi];
    startVoice(v, key, clampv(velocity, 0.0f, 1.0f), late);
    mod_.noteOn(vi, pos_, pitch, clampv(velocity, 0.0f, 1.0f));
    if (mod_.on()) modVoice(vi, true);
}

void Piano::noteOff(int pitch)
{
    int key = pitch - kPianoLowKey;
    while (key < 0) key += 12;
    while (key >= kPianoKeys) key -= 12;
    held_[key] = std::max(0, held_[key] - 1);
    if (held_[key] == 0)
        for (int i = 0; i < kVoices; ++i) if (voices_[i].key == key) mod_.noteOff(i);
}

void Piano::startVoice(Voice& v, int key, float velocity, double late)
{
    const PianoKey& k = design_->keys[static_cast<size_t>(key)];
    if (v.key != key) {
        std::fill(v.zr.begin(), v.zr.end(), 0.0f);
        std::fill(v.zi.begin(), v.zi.end(), 0.0f);
        std::copy(k.modes.pr.begin(), k.modes.pr.end(), v.pr.begin());
        std::copy(k.modes.pi.begin(), k.modes.pi.end(), v.pi.begin());
        for (int j = 0; j < kPianoLongModes; ++j) v.lzr[j] = v.lzi[j] = 0.0f;
        v.delta = 0.0f;
        v.damper = 0.0f;
        v.peak = v.lastPeak = 0.0f;
        v.quiet = 0;
    }
    v.key = key;
    v.bend = 0.0;
    // The felt stiffens with the blow (PianoSpec::hardVelocity): soft for a pianissimo, hard for a fortissimo.
    v.velHard = spec_.hardVelocity != 0.0f ? std::pow(std::max(velocity, 0.02f) / 0.75f, spec_.hardVelocity) : 1.0f;
    v.hardMul = v.velHard;
    v.modGain = 1.0f;
    v.region = k.region;
    v.w0 = k.regionW0;
    v.w1 = k.regionW1;
    // The strings of this key are the voice's now, not the sympathetic bank's.
    symOn_[key] = false;
    std::fill(symZr_.begin() + key * kPianoSymPartials, symZr_.begin() + (key + 1) * kPianoSymPartials, 0.0f);
    std::fill(symZi_.begin() + key * kPianoSymPartials, symZi_.begin() + (key + 1) * kPianoSymPartials, 0.0f);
    // The hammer meets the strings where they are, at its speed, the part of a sample it is late already flown.
    double ys = 0.0;
    for (int m = 0; m < k.modes.count; ++m)
        ys += static_cast<double>(k.hr[static_cast<size_t>(m)]) * v.zr[static_cast<size_t>(m)] - static_cast<double>(k.hi[static_cast<size_t>(m)]) * v.zi[static_cast<size_t>(m)];
    const double v0 = 0.4 * std::exp(2.6 * velocity);
    v.yh = ys + v0 * late / sampleRate_;
    v.vh = v0;
    v.hyst = 0.0;
    v.contact = true;
    v.contactSteps = 0;
    v.apart = 0;
    v.age = clock_++;
    // The key's thump on the keybed (Askenfelt and Jansson 1990): a short force pulse into the board.
    v.thump = 0;
    v.thumpGain = 3.0f * velocity * velocity * spec_.knock;
    v.damperNoise = -1;
}

void Piano::blockCoefficients(Voice& v, int n)
{
    const PianoKey& k = design_->keys[static_cast<size_t>(v.key)];
    const bool up = held_[v.key] > 0 || !k.damper;
    const float target = up ? 0.0f : damperContact(pedal_);
    if (target > 0.2f && v.damper < 0.05f && v.lastPeak > 1e-3f) {
        // The felt lands: its noise (a soft brush of the strings), by how much the strings still move.
        v.damperNoise = 0;
        v.damperNoiseGain = std::min(1.0f, v.lastPeak) * 0.3f;
        v.noise.seed(mixSeed(0x44414D50ull, v.age * 97u + static_cast<uint64_t>(v.key)));   // "DAMP"
    }
    const float alpha = 1.0f - std::exp(-static_cast<float>(n) / (0.012f * static_cast<float>(sampleRate_)));
    v.damper += (target - v.damper) * alpha;
    if (v.damper < 1e-5f) v.damper = 0.0f;
    // The tension: the time average of sum k^2 q_k^2 over the course.
    double e = 0.0;
    for (int m = 0; m < k.modes.count; ++m) {
        const size_t q = static_cast<size_t>(m);
        const double dd = static_cast<double>(k.dr[q]) * k.dr[q] + static_cast<double>(k.di[q]) * k.di[q];
        e += dd * (static_cast<double>(v.zr[q]) * v.zr[q] + static_cast<double>(v.zi[q]) * v.zi[q]);
    }
    v.delta = static_cast<float>(k.tensionGain * e);
    // The poles: turned by the tension (and the modulation's bend, exactly), shrunk by the damper.
    const double T = 1.0 / sampleRate_;
    const bool bent = v.bend != 0.0;
    const double rel = bent ? (1.0 + static_cast<double>(v.delta)) * std::pow(2.0, v.bend / 12.0) - 1.0 : 0.0;
    for (int m = 0; m < k.modes.count; ++m) {
        const size_t q = static_cast<size_t>(m);
        double c, s, g = v.damper > 0.0f ? std::exp(-static_cast<double>(k.damperSigma[q]) * v.damper * T) : 1.0;
        if (!bent) {
            const double a = static_cast<double>(k.omegaT[q]) * v.delta;
            c = 1.0 - 0.5 * a * a;
            s = a - a * a * a / 6.0;
        } else {
            const double a = static_cast<double>(k.omegaT[q]) * rel;
            c = std::cos(a);
            s = std::sin(a);
            if (k.omegaT[q] * (1.0 + rel) > 2.9) g = 0.0;   // (a mode bent towards Nyquist falls silent)
        }
        const double pr = k.modes.pr[q], pi = k.modes.pi[q];
        v.pr[q] = static_cast<float>((pr * c - pi * s) * g);
        v.pi[q] = static_cast<float>((pr * s + pi * c) * g);
    }
}

void Piano::contactBlock(Voice& v, float* force, int n)
{
    const PianoKey& k = design_->keys[static_cast<size_t>(v.key)];
    const int M = k.modes.count, os = k.oversample;
    const double Ts = 1.0 / (sampleRate_ * os);
    const double a = std::exp(-Ts / k.hammerTau);
    float* zr = v.zr.data();
    float* zi = v.zi.data();
    for (int i = 0; i < n; ++i) {
        for (int s = 0; s < os; ++s) {
            double ys = 0.0;
            for (int m = 0; m < M; ++m) ys += static_cast<double>(k.hr[static_cast<size_t>(m)]) * zr[m] - static_cast<double>(k.hi[static_cast<size_t>(m)]) * zi[m];
            const double u = v.yh - ys;
            const double up = u > 0.0 ? std::pow(u, k.hammerP) : 0.0;
            v.hyst = a * v.hyst + (1.0 - a) * up;
            double F = u > 0.0 ? k.q0 * static_cast<double>(v.hardMul) * (up - k.hammerEps * v.hyst) : 0.0;
            if (F < 0.0) F = 0.0;
            v.vh -= F / k.hammerMass * Ts;
            v.yh += v.vh * Ts;
            const float f = static_cast<float>(F);
            for (int m = 0; m < M; ++m) {
                const size_t q = static_cast<size_t>(m);
                const float r = k.spr[q] * zr[m] - k.spi[q] * zi[m] + k.sgr[q] * f;
                const float j = k.spr[q] * zi[m] + k.spi[q] * zr[m] + k.sgi[q] * f;
                zr[m] = r;
                zi[m] = j;
            }
            v.apart = u > 0.0 ? 0 : v.apart + 1;
            ++v.contactSteps;
        }
        float out = 0.0f;
        for (int m = 0; m < M; ++m) out += k.br[static_cast<size_t>(m)] * zr[m] - k.bi[static_cast<size_t>(m)] * zi[m];
        force[i] = out;
    }
}

template <class V>
void Piano::stringBlock(Voice& v, float* force, int n)
{
    const PianoKey& k = design_->keys[static_cast<size_t>(v.key)];
    std::fill(acc_, acc_ + n * 8, 0.0f);
    resonate<V>(v.pr.data(), v.pi.data(), nullptr, nullptr, v.zr.data(), v.zi.data(), k.br.data(), k.bi.data(), k.modes.padded, nullptr, acc_, n);
    for (int i = 0; i < n; ++i) force[i] = sum8(acc_, i);
}

template <class V>
void Piano::boardBlock(BoardState& s, const float (*region)[kMaxBlock], float* L, float* R, bool wantAccel, int n)
{
    constexpr int W = laneWidth<V>();
    const PianoBoard& b = design_->board;
    std::fill(accL_, accL_ + n * 8, 0.0f);
    std::fill(accR_, accR_ + n * 8, 0.0f);
    if (wantAccel) for (auto& a : accA_) std::fill(a, a + n * 8, 0.0f);
    const int P = b.modes.padded;
    for (int g = 0; g < P; g += 8)
        for (int h = 0; h < 8; h += W) {
            const int m = g + h;
            const V Pr = loadLanes<V>(&b.modes.pr[static_cast<size_t>(m)]), Pi = loadLanes<V>(&b.modes.pi[static_cast<size_t>(m)]);
            const V Gr = loadLanes<V>(&b.modes.gr[static_cast<size_t>(m)]), Gi = loadLanes<V>(&b.modes.gi[static_cast<size_t>(m)]);
            const V Ml = loadLanes<V>(&micL_[static_cast<size_t>(m)]), Mr = loadLanes<V>(&micR_[static_cast<size_t>(m)]);
            const V Ar = loadLanes<V>(&b.accR[static_cast<size_t>(m)]), Ai = loadLanes<V>(&b.accI[static_cast<size_t>(m)]);
            V S[kPianoRegions];
            for (int r = 0; r < kPianoRegions; ++r) S[r] = loadLanes<V>(&b.shape[static_cast<size_t>(r * P + m)]);
            V Zr = loadLanes<V>(&s.zr[static_cast<size_t>(m)]), Zi = loadLanes<V>(&s.zi[static_cast<size_t>(m)]);
            for (int i = 0; i < n; ++i) {
                V x = S[0] * lanes<V>(region[0][i]);
                for (int r = 1; r < kPianoRegions; ++r) x = vfmadd(S[r], lanes<V>(region[r][i]), x);
                const V nr = vfmadd(Gr, x, vfnmadd(Pi, Zi, Pr * Zr));
                const V ni = vfmadd(Gi, x, vfmadd(Pi, Zr, Pr * Zi));
                Zr = nr;
                Zi = ni;
                // The mode's acceleration, radiated to the listening points (and driving the sympathetic strings).
                const V acc = vfnmadd(Ai, Zi, Ar * Zr);
                float* al = accL_ + i * 8 + h;
                float* ar = accR_ + i * 8 + h;
                vstore(al, vfmadd(Ml, acc, loadLanes<V>(al)));
                vstore(ar, vfmadd(Mr, acc, loadLanes<V>(ar)));
                if (wantAccel) {
                    for (int r = 0; r < kPianoRegions; ++r) {
                        float* aa = accA_[r] + i * 8 + h;
                        vstore(aa, vfmadd(S[r], acc, loadLanes<V>(aa)));
                    }
                }
            }
            vstore(&s.zr[static_cast<size_t>(m)], Zr);
            vstore(&s.zi[static_cast<size_t>(m)], Zi);
        }
    // The high bank: the same regions, its own shapes, straight to the listening points.
    const int H = b.high.padded;
    for (int g = 0; g < H; g += 8)
        for (int h = 0; h < 8; h += W) {
            const int m = g + h;
            const V Pr = loadLanes<V>(&b.high.pr[static_cast<size_t>(m)]), Pi = loadLanes<V>(&b.high.pi[static_cast<size_t>(m)]);
            const V Gr = loadLanes<V>(&b.high.gr[static_cast<size_t>(m)]), Gi = loadLanes<V>(&b.high.gi[static_cast<size_t>(m)]);
            const V Hl = loadLanes<V>(&b.highL[static_cast<size_t>(m)]), Hr = loadLanes<V>(&b.highR[static_cast<size_t>(m)]);
            V S[kPianoRegions];
            for (int r = 0; r < kPianoRegions; ++r) S[r] = loadLanes<V>(&b.highShape[static_cast<size_t>(r * H + m)]);
            V Zr = loadLanes<V>(&s.hzr[static_cast<size_t>(m)]), Zi = loadLanes<V>(&s.hzi[static_cast<size_t>(m)]);
            for (int i = 0; i < n; ++i) {
                V x = S[0] * lanes<V>(region[0][i]);
                for (int r = 1; r < kPianoRegions; ++r) x = vfmadd(S[r], lanes<V>(region[r][i]), x);
                const V nr = vfmadd(Gr, x, vfnmadd(Pi, Zi, Pr * Zr));
                const V ni = vfmadd(Gi, x, vfmadd(Pi, Zr, Pr * Zi));
                Zr = nr;
                Zi = ni;
                float* al = accL_ + i * 8 + h;
                float* ar = accR_ + i * 8 + h;
                vstore(al, vfmadd(Hl, Zr, loadLanes<V>(al)));
                vstore(ar, vfmadd(Hr, Zr, loadLanes<V>(ar)));
            }
            vstore(&s.hzr[static_cast<size_t>(m)], Zr);
            vstore(&s.hzi[static_cast<size_t>(m)], Zi);
        }
    for (int i = 0; i < n; ++i) {
        L[i] = sum8(accL_, i);
        R[i] = sum8(accR_, i);
    }
    if (wantAccel)
        for (int r = 0; r < kPianoRegions; ++r)
            for (int i = 0; i < n; ++i) accel_[r][i] = sum8(accA_[r], i);
}

template <class V>
void Piano::symBlock(int n, bool sounding, bool cellStart, bool cellEnd)
{
    const PianoDesign& d = *design_;
    const double T = 1.0 / sampleRate_;
    const float alpha = 1.0f - std::exp(-static_cast<float>(kMaxBlock) / (0.012f * static_cast<float>(sampleRate_)));
    bool voiced[kPianoKeys] = {};
    for (const Voice& v : voices_) if (v.key >= 0) voiced[v.key] = true;
    float in[kMaxBlock], out[kMaxBlock];
    for (int key = 0; key < kPianoKeys; ++key) {
        if (voiced[key]) continue;
        const PianoKey& k = d.keys[static_cast<size_t>(key)];
        const bool up = held_[key] > 0 || !k.damper || pedal_ > 0.02f;
        float* zr = &symZr_[static_cast<size_t>(key * kPianoSymPartials)];
        float* zi = &symZi_[static_cast<size_t>(key * kPianoSymPartials)];
        float* pr = &symPr_[static_cast<size_t>(key * kPianoSymPartials)];
        float* pi = &symPi_[static_cast<size_t>(key * kPianoSymPartials)];
        if (cellStart) {
            if (!symOn_[key]) {
                if (!up || !sounding) continue;   // nothing to ring along with
                symOn_[key] = true;
                symDamper_[key] = damperContact(pedal_) * (held_[key] > 0 ? 0.0f : 1.0f);
                symPeak_[key] = 0.0f;
            }
            const float target = held_[key] > 0 || !k.damper ? 0.0f : damperContact(pedal_);
            symDamper_[key] += (target - symDamper_[key]) * alpha;
            for (int m = 0; m < k.sym.padded; ++m) {
                const size_t q = static_cast<size_t>(m);
                const double g = symDamper_[key] > 1e-5f ? std::exp(-static_cast<double>(k.symDamper[q]) * symDamper_[key] * T) : 1.0;
                pr[m] = static_cast<float>(k.sym.pr[q] * g);
                pi[m] = static_cast<float>(k.sym.pi[q] * g);
            }
        }
        if (!symOn_[key]) continue;
        for (int i = 0; i < n; ++i) in[i] = kSymDrive * (k.regionW0 * accel_[k.region][i] + k.regionW1 * accel_[k.region + 1][i]);
        std::fill(acc_, acc_ + n * 8, 0.0f);
        resonate<V>(pr, pi, k.sym.gr.data(), k.sym.gi.data(), zr, zi, k.symOutR.data(), k.symOutI.data(), k.sym.padded, in, acc_, n);
        float peak = cellStart ? 0.0f : symPeak_[key];
        for (int i = 0; i < n; ++i) {
            out[i] = sum8(acc_, i) * sympathetic_;
            peak = std::max(peak, std::fabs(out[i]));
            symRegionF_[k.region][i] += k.regionW0 * out[i];
            symRegionF_[k.region + 1][i] += k.regionW1 * out[i];
        }
        symPeak_[key] = peak;
        if (cellEnd && !up && symDamper_[key] > 0.5f && peak < kSymQuiet) {
            symOn_[key] = false;
            std::fill(zr, zr + kPianoSymPartials, 0.0f);
            std::fill(zi, zi + kPianoSymPartials, 0.0f);
        }
    }
}

template <class V>
void Piano::processWith(float* L, float* R, int n)
{
    if (!design_ || n <= 0) {
        std::fill(L, L + std::max(n, 0), 0.0f);
        std::fill(R, R + std::max(n, 0), 0.0f);
        pos_ += std::max(n, 0);
        return;
    }
    // Cut at the grid: every decision of a cell is made at its first or its last sample, whoever calls how.
    int done = 0;
    while (done < n) {
        const int into = static_cast<int>(pos_ % kMaxBlock);
        const int seg = std::min(n - done, kMaxBlock - into);
        renderSegment<V>(L + done, R + done, seg, into == 0, into + seg == kMaxBlock);
        pos_ += seg;
        done += seg;
    }
}

template <class V>
void Piano::renderSegment(float* L, float* R, int n, bool cellStart, bool cellEnd)
{
    const PianoDesign& d = *design_;
    for (int r = 0; r < kPianoRegions; ++r) {
        std::fill(regionF_[r], regionF_[r] + n, 0.0f);
        std::fill(symRegionF_[r], symRegionF_[r] + n, 0.0f);
    }
    bool sounding = false;
    const int thumpLen = static_cast<int>(0.0015 * sampleRate_), noiseLen = static_cast<int>(0.04 * sampleRate_);
    const bool modded = mod_.on();
    for (int vi = 0; vi < kVoices; ++vi) {
        Voice& v = voices_[vi];
        if (v.key < 0) continue;
        sounding = true;
        const PianoKey& k = d.keys[static_cast<size_t>(v.key)];
        if (cellStart && modded) modVoice(vi, false);
        if (cellStart) blockCoefficients(v, kMaxBlock);
        if (v.contact) contactBlock(v, force_, n);
        else stringBlock<V>(v, force_, n);
        float peak = cellStart ? 0.0f : v.peak;
        for (int i = 0; i < n; ++i) {
            float F = force_[i];
            peak = std::max(peak, std::fabs(F));
            // The longitudinal modes on the square of the bridge force.
            if (k.longCount > 0 && phantom_ > 0.0f) {
                const float u = static_cast<float>(k.longDrive) * F * F;
                float fl = 0.0f;
                for (int j = 0; j < k.longCount; ++j) {
                    const float r = k.lpr[j] * v.lzr[j] - k.lpi[j] * v.lzi[j] + k.lgr[j] * u;
                    const float q = k.lpr[j] * v.lzi[j] + k.lpi[j] * v.lzr[j] + k.lgi[j] * u;
                    v.lzr[j] = r;
                    v.lzi[j] = q;
                    fl += k.lcr[j] * r - k.lci[j] * q;
                }
                F += phantom_ * kLongCoupling * fl;
            }
            // The mechanics: the thump and the damper's noise, into the board where the key meets it.
            if (v.thump >= 0) {
                const int t = v.thump + i;
                if (t < thumpLen) F += mechanics_ * v.thumpGain * 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(t) / static_cast<float>(thumpLen)));
            }
            if (v.damperNoise >= 0) {
                const int t = v.damperNoise + i;
                if (t < noiseLen) F += damperNoise_ * v.damperNoiseGain * v.noise.bipolar() * std::exp(-6.0f * static_cast<float>(t) / static_cast<float>(noiseLen));
            }
            const float Fg = F * v.modGain;
            regionF_[v.region][i] += v.w0 * Fg;
            regionF_[v.region + 1][i] += v.w1 * Fg;
        }
        if (v.thump >= 0) { v.thump += n; if (v.thump >= thumpLen) v.thump = -1; }
        if (v.damperNoise >= 0) { v.damperNoise += n; if (v.damperNoise >= noiseLen) v.damperNoise = -1; }
        v.peak = peak;
        if (cellEnd) {
            // The hammer has left: away from the strings for a while and flying back (or held too long: the check).
            const int os = k.oversample;
            if (v.contact && ((v.apart > 8 * os && v.vh < 0.0) || v.contactSteps > static_cast<int>(0.08 * sampleRate_) * os)) v.contact = false;
            v.lastPeak = peak;
            v.quiet = !v.contact && peak < kQuietForce ? v.quiet + 1 : 0;
            if (v.quiet > kQuietBlocks && v.thump < 0 && v.damperNoise < 0) v.key = -1;
        }
    }
    // The sympathetic strings listen to the board when some damper is up and something sounds.
    bool anySym = false;
    for (int k = 0; k < kPianoKeys && !anySym; ++k) anySym = symOn_[k] || held_[k] > 0;
    anySym = anySym || pedal_ > 0.02f;
    const bool wantAccel = anySym && sympathetic_ > 0.0f;
    float bl[kMaxBlock], br[kMaxBlock];
    boardBlock<V>(board_, regionF_, bl, br, wantAccel, n);
    bool symSounding = false;
    if (wantAccel) symBlock<V>(n, sounding, cellStart, cellEnd);
    for (int k = 0; k < kPianoKeys && !symSounding; ++k) symSounding = symOn_[k];
    if (symSounding) {
        float sl[kMaxBlock], sr[kMaxBlock];
        boardBlock<V>(symBoard_, symRegionF_, sl, sr, false, n);
        for (int i = 0; i < n; ++i) { bl[i] += sl[i]; br[i] += sr[i]; }
        symBoardOn_ = true;
    } else if (symBoardOn_) {
        // The last ringing string has stopped (under its threshold): its board's rest is silence, not kept for later.
        for (std::vector<float>* a : { &symBoard_.zr, &symBoard_.zi, &symBoard_.hzr, &symBoard_.hzi }) std::fill(a->begin(), a->end(), 0.0f);
        symBoardOn_ = false;
    }
    const float gain = d.outGain * level_;
    for (int i = 0; i < n; ++i) {
        float lp, bp, hp;
        float x[2] = { bl[i] * gain, br[i] * gain };
        // The radiation (PianoSpec::radiation): a shelf from the board's acceleration to its velocity.
        for (int c = 0; c < 2; ++c) {
            const float y = radB0_ * x[c] + radB1_ * radX_[c] - radA1_ * radY_[c];
            radX_[c] = x[c];
            radY_[c] = y;
            x[c] = y;
        }
        lowCut_[0].tick(x[0], lp, bp, hp);
        L[i] = hp;
        lowCut_[1].tick(x[1], lp, bp, hp);
        R[i] = hp;
    }
}

void Piano::process(float* L, float* R, int n)
{
    processWith<VecF>(L, R, n);
}

template void Piano::processWith<float>(float*, float*, int);
#if PARH_VEC_PATH != 0
template void Piano::processWith<VecF>(float*, float*, int);
#endif

} // namespace parh
