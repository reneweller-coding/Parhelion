/**
 * @file Piano.cpp
 * @brief The physical piano in real time (Piano.h).
 * @author Rene Weller
 */
#include "parh/synth/Piano.h"
#include "parh/Params.h"
#include "parh/Vec.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace parh {

namespace {

constexpr double PiD = 3.14159265358979323846;   ///< pi
constexpr float QuietForce = 2e-5f;   ///< N: a voice whose bridge force stays under this is freed
constexpr int QuietBlocks = 24;   ///< blocks under QuietForce before a voice is freed
constexpr float SymQuiet = 2e-7f;     ///< N: a sympathetic string under this stops when its damper is down
constexpr float LongCoupling = 0.08f; ///< the longitudinal force's share in the bridge's vertical (the downbearing)
/**
 * The sympathetic strings' drive. The one-way coupling (Bank 2010) has no reaction of the ringing strings on the
 * bridge, so where a partial of theirs meets one of the played note's exactly (the octaves and fifths of a stretched
 * tuning) it takes energy the note never loses; a coupled solution would share the note's energy among them instead.
 * The drive is scaled down to where that bound puts them -- together about 12 dB under the note once it is released
 * with the pedal down (measured, testPiano) -- and piano.sympathetic scales it from there. 30.09.2026: 0.03 -> 0.017, since
 * the voicing against Pianoteq (PianoDesign.h) lets the board ring longer and knocks it a hundred times harder, and the
 * free strings listen to the board: at 0.03 they rang 4 dB under the note.
 */
constexpr float SymDrive = 0.017f;

/**
 * @brief The damper's contact for a released key at pedal @p p (0 up .. 1 down; the middle is the half pedal).
 * @param[in] p  the pedal, 0..1
 * @return the damper's contact, 0..1
 */
float damperContact(float p)
{
    const float q = clampv(1.0f - p, 0.0f, 1.0f);
    return q * q;
}

/**
 * @brief A bank of complex one-poles, eight modes side by side: z = p z + g in, out += Re(c z). The outputs of the
 *        eight lanes go to eight accumulators per sample (acc[i * 8 + lane]), so every lane width sums alike.
 * @param[in]     pr      the poles' real parts
 * @param[in]     pi      the poles' imaginary parts
 * @param[in]     gr      the input gains' real parts
 * @param[in]     gi      the input gains' imaginary parts
 * @param[in,out] zr      the states' real parts
 * @param[in,out] zi      the states' imaginary parts
 * @param[in]     cr      the output gains' real parts
 * @param[in]     ci      the output gains' imaginary parts
 * @param[in]     padded  the modes, a multiple of 8
 * @param[in]     in      the input per sample, or null (no input)
 * @param[in,out] acc     the accumulators, added to
 * @param[in]     n       the samples
 */
template <class VT>
void resonate(const float* pr, const float* pi, const float* gr, const float* gi, float* zr, float* zi, const float* cr, const float* ci,
              int padded, const float* in, float* acc, int n)
{
    constexpr int W = laneWidth<VT>();
    for (int g = 0; g < padded; g += 8)
        for (int h = 0; h < 8; h += W) {
            const int m = g + h;
            const VT Pr = loadLanes<VT>(pr + m), Pi = loadLanes<VT>(pi + m), Cr = loadLanes<VT>(cr + m), Ci = loadLanes<VT>(ci + m);
            VT Zr = loadLanes<VT>(zr + m), Zi = loadLanes<VT>(zi + m);
            if (in != nullptr) {
                const VT Gr = loadLanes<VT>(gr + m), Gi = loadLanes<VT>(gi + m);
                for (int i = 0; i < n; ++i) {
                    const VT x = lanes<VT>(in[i]);
                    const VT nr = vfmadd(Gr, x, vfnmadd(Pi, Zi, Pr * Zr));
                    const VT ni = vfmadd(Gi, x, vfmadd(Pi, Zr, Pr * Zi));
                    Zr = nr;
                    Zi = ni;
                    float* a = acc + i * 8 + h;
                    vstore(a, loadLanes<VT>(a) + vfnmadd(Ci, Zi, Cr * Zr));
                }
            } else {
                for (int i = 0; i < n; ++i) {
                    const VT nr = vfnmadd(Pi, Zi, Pr * Zr);
                    const VT ni = vfmadd(Pi, Zr, Pr * Zi);
                    Zr = nr;
                    Zi = ni;
                    float* a = acc + i * 8 + h;
                    vstore(a, loadLanes<VT>(a) + vfnmadd(Ci, Zi, Cr * Zr));
                }
            }
            vstore(zr + m, Zr);
            vstore(zi + m, Zi);
        }
}

/**
 * @brief The eight accumulators of sample @p i, added in order.
 * @param[in] acc  the accumulators
 * @param[in] i    the sample
 * @return the sum
 */
inline float sum8(const float* acc, int i)
{
    const float* a = acc + i * 8;
    return ((((((a[0] + a[1]) + a[2]) + a[3]) + a[4]) + a[5]) + a[6]) + a[7];
}

} // namespace

void Piano::prepare(double sampleRate)
{
    m_mod.prepare(sampleRate, 0x5049414E4Full);   // "PIANO"
    m_sampleRate = sampleRate;
    for (Svf& f : m_lowCut) f.setQ(m_lowCutHz, 0.707f, static_cast<float>(m_sampleRate));
    if (m_specSet) setSpec(m_spec);
}

void Piano::setSpec(const PianoSpec& spec)
{
    // The radiation shelf: H(s) = (wp / wz) (s + wz) / (s + wp), unity at DC and wp / wz above wz (a first-order low pass
    // where radiationEnd is 0), by the bilinear transform. At radiation 0 it passes the acceleration as it is.
    if (spec.m_radiation > 0.0f) {
        const double K = 2.0 * m_sampleRate, wp = 2.0 * PiD * spec.m_radiation;
        const double wz = spec.m_radiationEnd > spec.m_radiation ? 2.0 * PiD * spec.m_radiationEnd : 0.0;
        const double b0s = wz > 0.0 ? wp / wz : 0.0, b1s = wp, a1s = wp;
        const double den = K + a1s;
        m_radB0 = static_cast<float>((b0s * K + b1s) / den);
        m_radB1 = static_cast<float>((b1s - b0s * K) / den);
        m_radA1 = static_cast<float>((a1s - K) / den);
    } else {
        m_radB0 = 1.0f;
        m_radB1 = m_radA1 = 0.0f;
    }
    m_spec = spec;
    m_specSet = true;
    m_design = designPiano(spec, m_sampleRate);
    const PianoDesign& d = *m_design;
    int most = 8;
    for (const PianoKey& k : d.m_keys) most = std::max(most, k.m_modes.m_padded);
    for (Voice& v : m_voices)
        for (std::vector<float>* a : { &v.m_zr, &v.m_zi, &v.m_pr, &v.m_pi }) a->assign(static_cast<size_t>(most), 0.0f);
    for (BoardState* b : { &m_board, &m_symBoard }) {
        b->m_zr.assign(static_cast<size_t>(d.m_board.m_modes.m_padded), 0.0f);
        b->m_zi.assign(static_cast<size_t>(d.m_board.m_modes.m_padded), 0.0f);
        b->m_hzr.assign(static_cast<size_t>(d.m_board.m_high.m_padded), 0.0f);
        b->m_hzi.assign(static_cast<size_t>(d.m_board.m_high.m_padded), 0.0f);
    }
    const size_t symSize = static_cast<size_t>(PianoKeys * PianoSymPartials);
    for (std::vector<float>* a : { &m_symZr, &m_symZi, &m_symPr, &m_symPi }) a->assign(symSize, 0.0f);
    m_micL.assign(static_cast<size_t>(d.m_board.m_modes.m_padded), 0.0f);
    m_micR.assign(static_cast<size_t>(d.m_board.m_modes.m_padded), 0.0f);
    // The listening weights for the current width.
    for (int n = 0; n < d.m_board.m_modes.m_padded; ++n) {
        const size_t q = static_cast<size_t>(n);
        m_micL[q] = (1.0f - m_width) * d.m_board.m_micC[q] + m_width * d.m_board.m_micL[q];
        m_micR[q] = (1.0f - m_width) * d.m_board.m_micC[q] + m_width * d.m_board.m_micR[q];
    }
    reset();
}

void Piano::update(const float* v)
{
    m_level = dbToGain(v[piano::LEVEL]);
    m_pedal = clampv(v[piano::PEDAL], 0.0f, 1.0f);
    m_sympathetic = v[piano::SYMPATHETIC];
    m_phantom = v[piano::PHANTOM];
    m_damperNoise = v[piano::DAMPER_NOISE];
    m_mechanics = v[piano::MECHANICS];
    if (v[piano::LOW_CUT] != m_lowCutHz) {
        m_lowCutHz = v[piano::LOW_CUT];
        for (Svf& f : m_lowCut) f.setQ(m_lowCutHz, 0.707f, static_cast<float>(m_sampleRate));
    }
    if (v[piano::WIDTH] != m_width && m_design) {
        m_width = clampv(v[piano::WIDTH], 0.0f, 1.0f);
        const PianoBoard& b = m_design->m_board;
        for (int n = 0; n < b.m_modes.m_padded; ++n) {
            const size_t q = static_cast<size_t>(n);
            m_micL[q] = (1.0f - m_width) * b.m_micC[q] + m_width * b.m_micL[q];
            m_micR[q] = (1.0f - m_width) * b.m_micC[q] + m_width * b.m_micR[q];
        }
    }
    m_mod.set(readModCore(v + piano::MOD_FIRST, PianoModDests, static_cast<int>(std::size(PianoModDests))));
}

void Piano::reset()
{
    for (Voice& v : m_voices) {
        v.m_key = -1;
        v.m_contact = false;
        std::fill(v.m_zr.begin(), v.m_zr.end(), 0.0f);
        std::fill(v.m_zi.begin(), v.m_zi.end(), 0.0f);
        v.m_thump = v.m_damperNoise = -1;
    }
    for (BoardState* b : { &m_board, &m_symBoard })
        for (std::vector<float>* a : { &b->m_zr, &b->m_zi, &b->m_hzr, &b->m_hzi }) std::fill(a->begin(), a->end(), 0.0f);
    std::fill(m_symZr.begin(), m_symZr.end(), 0.0f);
    std::fill(m_symZi.begin(), m_symZi.end(), 0.0f);
    for (int k = 0; k < PianoKeys; ++k) { m_held[k] = 0; m_symOn[k] = false; m_symDamper[k] = 1.0f; m_symPeak[k] = 0.0f; }
    m_symBoardOn = false;
    for (Svf& f : m_lowCut) f.m_ic1 = f.m_ic2 = 0.0f;
    m_radX[0] = m_radX[1] = m_radY[0] = m_radY[1] = 0.0f;
    m_pos = 0;
    m_mod.reset();
}

void Piano::modVoice(int i, bool strike)
{
    Voice& v = m_voices[i];
    const PianoKey& k = m_design->m_keys[static_cast<size_t>(v.m_key)];
    m_mod.evaluate(i, m_pos);
    if (strike) v.m_hardMul = v.m_velHard * std::exp2(std::clamp(m_mod.get(i, ModDestE::HARDNESS), -2.0f, 2.0f));
    v.m_bend = std::clamp(static_cast<double>(m_mod.get(i, ModDestE::PITCH)), -2.0, 2.0);
    v.m_modGain = std::clamp(1.0f + m_mod.get(i, ModDestE::LEVEL), 0.0f, 2.0f);
    if (m_mod.targets(ModDestE::PAN)) {
        // Where on the bridge: the key's place (between its two regions) moved by up to four regions.
        const float place = std::clamp(static_cast<float>(k.m_region) + k.m_regionW1 + 4.0f * m_mod.get(i, ModDestE::PAN),
                                       0.0f, static_cast<float>(PianoRegions) - 1.001f);
        v.m_region = static_cast<int>(place);
        v.m_w1 = place - static_cast<float>(v.m_region);
        v.m_w0 = 1.0f - v.m_w1;
    }
}

int Piano::activeVoices() const
{
    int n = 0;
    for (const Voice& v : m_voices) n += v.m_key >= 0;
    return n;
}

float Piano::maxTensionRise() const
{
    float m = 0.0f;
    for (const Voice& v : m_voices) if (v.m_key >= 0) m = std::max(m, v.m_delta);
    return m;
}

int Piano::voiceFor(int key)
{
    for (int i = 0; i < m_voiceLimit; ++i) if (m_voices[i].m_key == key) return i;
    for (int i = 0; i < m_voiceLimit; ++i) if (m_voices[i].m_key < 0) return i;
    // Steal the quietest voice whose hammer is gone (the oldest among equals).
    int best = -1;
    for (int i = 0; i < m_voiceLimit; ++i) {
        const Voice& v = m_voices[i];
        if (v.m_contact) continue;
        if (best < 0 || v.m_lastPeak < m_voices[best].m_lastPeak || (v.m_lastPeak == m_voices[best].m_lastPeak && v.m_age < m_voices[best].m_age)) best = i;
    }
    return best < 0 ? 0 : best;
}

void Piano::noteOn(int pitch, float velocity, double late)
{
    if (!m_design) return;
    int key = pitch - PianoLowKey;
    while (key < 0) key += 12;
    while (key >= PianoKeys) key -= 12;
    ++m_held[key];
    const int vi = voiceFor(key);
    Voice& v = m_voices[vi];
    startVoice(v, key, clampv(velocity, 0.0f, 1.0f), late);
    m_mod.noteOn(vi, m_pos, pitch, clampv(velocity, 0.0f, 1.0f));
    if (m_mod.on()) modVoice(vi, true);
}

void Piano::noteOff(int pitch)
{
    int key = pitch - PianoLowKey;
    while (key < 0) key += 12;
    while (key >= PianoKeys) key -= 12;
    m_held[key] = std::max(0, m_held[key] - 1);
    if (m_held[key] == 0)
        for (int i = 0; i < Voices; ++i) if (m_voices[i].m_key == key) m_mod.noteOff(i);
}

void Piano::startVoice(Voice& v, int key, float velocity, double late)
{
    const PianoKey& k = m_design->m_keys[static_cast<size_t>(key)];
    if (v.m_key != key) {
        std::fill(v.m_zr.begin(), v.m_zr.end(), 0.0f);
        std::fill(v.m_zi.begin(), v.m_zi.end(), 0.0f);
        std::copy(k.m_modes.m_pr.begin(), k.m_modes.m_pr.end(), v.m_pr.begin());
        std::copy(k.m_modes.m_pi.begin(), k.m_modes.m_pi.end(), v.m_pi.begin());
        for (int j = 0; j < PianoLongModes; ++j) v.m_lzr[j] = v.m_lzi[j] = 0.0f;
        v.m_delta = 0.0f;
        v.m_damper = 0.0f;
        v.m_peak = v.m_lastPeak = 0.0f;
        v.m_quiet = 0;
    }
    v.m_key = key;
    v.m_bend = 0.0;
    // The felt stiffens with the blow (PianoSpec::hardVelocity): soft for a pianissimo, hard for a fortissimo.
    v.m_velHard = m_spec.m_hardVelocity != 0.0f ? std::pow(std::max(velocity, 0.02f) / 0.75f, m_spec.m_hardVelocity) : 1.0f;
    v.m_hardMul = v.m_velHard;
    v.m_modGain = 1.0f;
    v.m_region = k.m_region;
    v.m_w0 = k.m_regionW0;
    v.m_w1 = k.m_regionW1;
    // The strings of this key are the voice's now, not the sympathetic bank's.
    m_symOn[key] = false;
    std::fill(m_symZr.begin() + key * PianoSymPartials, m_symZr.begin() + (key + 1) * PianoSymPartials, 0.0f);
    std::fill(m_symZi.begin() + key * PianoSymPartials, m_symZi.begin() + (key + 1) * PianoSymPartials, 0.0f);
    // The hammer meets the strings where they are, at its speed, the part of a sample it is late already flown.
    double ys = 0.0;
    for (int m = 0; m < k.m_modes.m_count; ++m)
        ys += static_cast<double>(k.m_hr[static_cast<size_t>(m)]) * v.m_zr[static_cast<size_t>(m)] - static_cast<double>(k.m_hi[static_cast<size_t>(m)]) * v.m_zi[static_cast<size_t>(m)];
    const double v0 = 0.4 * std::exp(2.6 * velocity);
    v.m_yh = ys + v0 * late / m_sampleRate;
    v.m_vh = v0;
    v.m_hyst = 0.0;
    v.m_contact = true;
    v.m_contactSteps = 0;
    v.m_apart = 0;
    v.m_age = m_clock++;
    // The key's thump on the keybed (Askenfelt and Jansson 1990): a short force pulse into the board.
    v.m_thump = 0;
    v.m_thumpGain = 3.0f * velocity * velocity * m_spec.m_knock;
    v.m_damperNoise = -1;
}

void Piano::blockCoefficients(Voice& v, int n)
{
    const PianoKey& k = m_design->m_keys[static_cast<size_t>(v.m_key)];
    const bool up = m_held[v.m_key] > 0 || !k.m_damper;
    const float target = up ? 0.0f : damperContact(m_pedal);
    if (target > 0.2f && v.m_damper < 0.05f && v.m_lastPeak > 1e-3f) {
        // The felt lands: its noise (a soft brush of the strings), by how much the strings still move.
        v.m_damperNoise = 0;
        v.m_damperNoiseGain = std::min(1.0f, v.m_lastPeak) * 0.3f;
        v.m_noise.seed(mixSeed(0x44414D50ull, v.m_age * 97u + static_cast<uint64_t>(v.m_key)));   // "DAMP"
    }
    const float alpha = 1.0f - std::exp(-static_cast<float>(n) / (0.012f * static_cast<float>(m_sampleRate)));
    v.m_damper += (target - v.m_damper) * alpha;
    if (v.m_damper < 1e-5f) v.m_damper = 0.0f;
    // The tension: the time average of sum k^2 q_k^2 over the course.
    double e = 0.0;
    for (int m = 0; m < k.m_modes.m_count; ++m) {
        const size_t q = static_cast<size_t>(m);
        const double dd = static_cast<double>(k.m_dr[q]) * k.m_dr[q] + static_cast<double>(k.m_di[q]) * k.m_di[q];
        e += dd * (static_cast<double>(v.m_zr[q]) * v.m_zr[q] + static_cast<double>(v.m_zi[q]) * v.m_zi[q]);
    }
    v.m_delta = static_cast<float>(k.m_tensionGain * e);
    // The poles: turned by the tension (and the modulation's bend, exactly), shrunk by the damper.
    const double T = 1.0 / m_sampleRate;
    const bool bent = v.m_bend != 0.0;
    const double rel = bent ? (1.0 + static_cast<double>(v.m_delta)) * std::pow(2.0, v.m_bend / 12.0) - 1.0 : 0.0;
    for (int m = 0; m < k.m_modes.m_count; ++m) {
        const size_t q = static_cast<size_t>(m);
        double c, s, g = v.m_damper > 0.0f ? std::exp(-static_cast<double>(k.m_damperSigma[q]) * v.m_damper * T) : 1.0;
        if (!bent) {
            const double a = static_cast<double>(k.m_omegaT[q]) * v.m_delta;
            c = 1.0 - 0.5 * a * a;
            s = a - a * a * a / 6.0;
        } else {
            const double a = static_cast<double>(k.m_omegaT[q]) * rel;
            c = std::cos(a);
            s = std::sin(a);
            if (k.m_omegaT[q] * (1.0 + rel) > 2.9) g = 0.0;   // (a mode bent towards Nyquist falls silent)
        }
        const double pr = k.m_modes.m_pr[q], pi = k.m_modes.m_pi[q];
        v.m_pr[q] = static_cast<float>((pr * c - pi * s) * g);
        v.m_pi[q] = static_cast<float>((pr * s + pi * c) * g);
    }
}

void Piano::contactBlock(Voice& v, float* force, int n)
{
    const PianoKey& k = m_design->m_keys[static_cast<size_t>(v.m_key)];
    const int M = k.m_modes.m_count, os = k.m_oversample;
    const double Ts = 1.0 / (m_sampleRate * os);
    const double a = std::exp(-Ts / k.m_hammerTau);
    float* zr = v.m_zr.data();
    float* zi = v.m_zi.data();
    for (int i = 0; i < n; ++i) {
        for (int s = 0; s < os; ++s) {
            double ys = 0.0;
            for (int m = 0; m < M; ++m) ys += static_cast<double>(k.m_hr[static_cast<size_t>(m)]) * zr[m] - static_cast<double>(k.m_hi[static_cast<size_t>(m)]) * zi[m];
            const double u = v.m_yh - ys;
            const double up = u > 0.0 ? std::pow(u, k.m_hammerP) : 0.0;
            v.m_hyst = a * v.m_hyst + (1.0 - a) * up;
            double F = u > 0.0 ? k.m_q0 * static_cast<double>(v.m_hardMul) * (up - k.m_hammerEps * v.m_hyst) : 0.0;
            if (F < 0.0) F = 0.0;
            v.m_vh -= F / k.m_hammerMass * Ts;
            v.m_yh += v.m_vh * Ts;
            const float f = static_cast<float>(F);
            for (int m = 0; m < M; ++m) {
                const size_t q = static_cast<size_t>(m);
                const float r = k.m_spr[q] * zr[m] - k.m_spi[q] * zi[m] + k.m_sgr[q] * f;
                const float j = k.m_spr[q] * zi[m] + k.m_spi[q] * zr[m] + k.m_sgi[q] * f;
                zr[m] = r;
                zi[m] = j;
            }
            v.m_apart = u > 0.0 ? 0 : v.m_apart + 1;
            ++v.m_contactSteps;
        }
        float out = 0.0f;
        for (int m = 0; m < M; ++m) out += k.m_br[static_cast<size_t>(m)] * zr[m] - k.m_bi[static_cast<size_t>(m)] * zi[m];
        force[i] = out;
    }
}

template <class VT>
void Piano::stringBlock(Voice& v, float* force, int n)
{
    const PianoKey& k = m_design->m_keys[static_cast<size_t>(v.m_key)];
    std::fill(m_acc, m_acc + n * 8, 0.0f);
    resonate<VT>(v.m_pr.data(), v.m_pi.data(), nullptr, nullptr, v.m_zr.data(), v.m_zi.data(), k.m_br.data(), k.m_bi.data(), k.m_modes.m_padded, nullptr, m_acc, n);
    for (int i = 0; i < n; ++i) force[i] = sum8(m_acc, i);
}

template <class VT>
void Piano::boardBlock(BoardState& s, const float (*region)[MaxBlock], float* L, float* R, bool wantAccel, int n)
{
    constexpr int W = laneWidth<VT>();
    const PianoBoard& b = m_design->m_board;
    std::fill(m_accL, m_accL + n * 8, 0.0f);
    std::fill(m_accR, m_accR + n * 8, 0.0f);
    if (wantAccel) for (auto& a : m_accA) std::fill(a, a + n * 8, 0.0f);
    const int P = b.m_modes.m_padded;
    for (int g = 0; g < P; g += 8)
        for (int h = 0; h < 8; h += W) {
            const int m = g + h;
            const VT Pr = loadLanes<VT>(&b.m_modes.m_pr[static_cast<size_t>(m)]), Pi = loadLanes<VT>(&b.m_modes.m_pi[static_cast<size_t>(m)]);
            const VT Gr = loadLanes<VT>(&b.m_modes.m_gr[static_cast<size_t>(m)]), Gi = loadLanes<VT>(&b.m_modes.m_gi[static_cast<size_t>(m)]);
            const VT Ml = loadLanes<VT>(&m_micL[static_cast<size_t>(m)]), Mr = loadLanes<VT>(&m_micR[static_cast<size_t>(m)]);
            const VT Ar = loadLanes<VT>(&b.m_accR[static_cast<size_t>(m)]), Ai = loadLanes<VT>(&b.m_accI[static_cast<size_t>(m)]);
            VT S[PianoRegions];
            for (int r = 0; r < PianoRegions; ++r) S[r] = loadLanes<VT>(&b.m_shape[static_cast<size_t>(r * P + m)]);
            VT Zr = loadLanes<VT>(&s.m_zr[static_cast<size_t>(m)]), Zi = loadLanes<VT>(&s.m_zi[static_cast<size_t>(m)]);
            for (int i = 0; i < n; ++i) {
                VT x = S[0] * lanes<VT>(region[0][i]);
                for (int r = 1; r < PianoRegions; ++r) x = vfmadd(S[r], lanes<VT>(region[r][i]), x);
                const VT nr = vfmadd(Gr, x, vfnmadd(Pi, Zi, Pr * Zr));
                const VT ni = vfmadd(Gi, x, vfmadd(Pi, Zr, Pr * Zi));
                Zr = nr;
                Zi = ni;
                // The mode's acceleration, radiated to the listening points (and driving the sympathetic strings).
                const VT acc = vfnmadd(Ai, Zi, Ar * Zr);
                float* al = m_accL + i * 8 + h;
                float* ar = m_accR + i * 8 + h;
                vstore(al, vfmadd(Ml, acc, loadLanes<VT>(al)));
                vstore(ar, vfmadd(Mr, acc, loadLanes<VT>(ar)));
                if (wantAccel) {
                    for (int r = 0; r < PianoRegions; ++r) {
                        float* aa = m_accA[r] + i * 8 + h;
                        vstore(aa, vfmadd(S[r], acc, loadLanes<VT>(aa)));
                    }
                }
            }
            vstore(&s.m_zr[static_cast<size_t>(m)], Zr);
            vstore(&s.m_zi[static_cast<size_t>(m)], Zi);
        }
    // The high bank: the same regions, its own shapes, straight to the listening points.
    const int H = b.m_high.m_padded;
    for (int g = 0; g < H; g += 8)
        for (int h = 0; h < 8; h += W) {
            const int m = g + h;
            const VT Pr = loadLanes<VT>(&b.m_high.m_pr[static_cast<size_t>(m)]), Pi = loadLanes<VT>(&b.m_high.m_pi[static_cast<size_t>(m)]);
            const VT Gr = loadLanes<VT>(&b.m_high.m_gr[static_cast<size_t>(m)]), Gi = loadLanes<VT>(&b.m_high.m_gi[static_cast<size_t>(m)]);
            const VT Hl = loadLanes<VT>(&b.m_highL[static_cast<size_t>(m)]), Hr = loadLanes<VT>(&b.m_highR[static_cast<size_t>(m)]);
            VT S[PianoRegions];
            for (int r = 0; r < PianoRegions; ++r) S[r] = loadLanes<VT>(&b.m_highShape[static_cast<size_t>(r * H + m)]);
            VT Zr = loadLanes<VT>(&s.m_hzr[static_cast<size_t>(m)]), Zi = loadLanes<VT>(&s.m_hzi[static_cast<size_t>(m)]);
            for (int i = 0; i < n; ++i) {
                VT x = S[0] * lanes<VT>(region[0][i]);
                for (int r = 1; r < PianoRegions; ++r) x = vfmadd(S[r], lanes<VT>(region[r][i]), x);
                const VT nr = vfmadd(Gr, x, vfnmadd(Pi, Zi, Pr * Zr));
                const VT ni = vfmadd(Gi, x, vfmadd(Pi, Zr, Pr * Zi));
                Zr = nr;
                Zi = ni;
                float* al = m_accL + i * 8 + h;
                float* ar = m_accR + i * 8 + h;
                vstore(al, vfmadd(Hl, Zr, loadLanes<VT>(al)));
                vstore(ar, vfmadd(Hr, Zr, loadLanes<VT>(ar)));
            }
            vstore(&s.m_hzr[static_cast<size_t>(m)], Zr);
            vstore(&s.m_hzi[static_cast<size_t>(m)], Zi);
        }
    for (int i = 0; i < n; ++i) {
        L[i] = sum8(m_accL, i);
        R[i] = sum8(m_accR, i);
    }
    if (wantAccel)
        for (int r = 0; r < PianoRegions; ++r)
            for (int i = 0; i < n; ++i) m_accel[r][i] = sum8(m_accA[r], i);
}

template <class VT>
void Piano::symBlock(int n, bool sounding, bool cellStart, bool cellEnd)
{
    const PianoDesign& d = *m_design;
    const double T = 1.0 / m_sampleRate;
    const float alpha = 1.0f - std::exp(-static_cast<float>(MaxBlock) / (0.012f * static_cast<float>(m_sampleRate)));
    bool voiced[PianoKeys] = {};
    for (const Voice& v : m_voices) if (v.m_key >= 0) voiced[v.m_key] = true;
    float in[MaxBlock], out[MaxBlock];
    for (int key = 0; key < PianoKeys; ++key) {
        if (voiced[key]) continue;
        const PianoKey& k = d.m_keys[static_cast<size_t>(key)];
        const bool up = m_held[key] > 0 || !k.m_damper || m_pedal > 0.02f;
        float* zr = &m_symZr[static_cast<size_t>(key * PianoSymPartials)];
        float* zi = &m_symZi[static_cast<size_t>(key * PianoSymPartials)];
        float* pr = &m_symPr[static_cast<size_t>(key * PianoSymPartials)];
        float* pi = &m_symPi[static_cast<size_t>(key * PianoSymPartials)];
        if (cellStart) {
            if (!m_symOn[key]) {
                if (!up || !sounding) continue;   // nothing to ring along with
                m_symOn[key] = true;
                m_symDamper[key] = damperContact(m_pedal) * (m_held[key] > 0 ? 0.0f : 1.0f);
                m_symPeak[key] = 0.0f;
            }
            const float target = m_held[key] > 0 || !k.m_damper ? 0.0f : damperContact(m_pedal);
            m_symDamper[key] += (target - m_symDamper[key]) * alpha;
            for (int m = 0; m < k.m_sym.m_padded; ++m) {
                const size_t q = static_cast<size_t>(m);
                const double g = m_symDamper[key] > 1e-5f ? std::exp(-static_cast<double>(k.m_symDamper[q]) * m_symDamper[key] * T) : 1.0;
                pr[m] = static_cast<float>(k.m_sym.m_pr[q] * g);
                pi[m] = static_cast<float>(k.m_sym.m_pi[q] * g);
            }
        }
        if (!m_symOn[key]) continue;
        for (int i = 0; i < n; ++i) in[i] = SymDrive * (k.m_regionW0 * m_accel[k.m_region][i] + k.m_regionW1 * m_accel[k.m_region + 1][i]);
        std::fill(m_acc, m_acc + n * 8, 0.0f);
        resonate<VT>(pr, pi, k.m_sym.m_gr.data(), k.m_sym.m_gi.data(), zr, zi, k.m_symOutR.data(), k.m_symOutI.data(), k.m_sym.m_padded, in, m_acc, n);
        float peak = cellStart ? 0.0f : m_symPeak[key];
        for (int i = 0; i < n; ++i) {
            out[i] = sum8(m_acc, i) * m_sympathetic;
            peak = std::max(peak, std::fabs(out[i]));
            m_symRegionF[k.m_region][i] += k.m_regionW0 * out[i];
            m_symRegionF[k.m_region + 1][i] += k.m_regionW1 * out[i];
        }
        m_symPeak[key] = peak;
        if (cellEnd && !up && m_symDamper[key] > 0.5f && peak < SymQuiet) {
            m_symOn[key] = false;
            std::fill(zr, zr + PianoSymPartials, 0.0f);
            std::fill(zi, zi + PianoSymPartials, 0.0f);
        }
    }
}

template <class VT>
void Piano::processWith(float* L, float* R, int n)
{
    if (!m_design || n <= 0) {
        std::fill(L, L + std::max(n, 0), 0.0f);
        std::fill(R, R + std::max(n, 0), 0.0f);
        m_pos += std::max(n, 0);
        return;
    }
    // Cut at the grid: every decision of a cell is made at its first or its last sample, whoever calls how.
    int done = 0;
    while (done < n) {
        const int into = static_cast<int>(m_pos % MaxBlock);
        const int seg = std::min(n - done, MaxBlock - into);
        renderSegment<VT>(L + done, R + done, seg, into == 0, into + seg == MaxBlock);
        m_pos += seg;
        done += seg;
    }
}

template <class VT>
void Piano::renderSegment(float* L, float* R, int n, bool cellStart, bool cellEnd)
{
    const PianoDesign& d = *m_design;
    for (int r = 0; r < PianoRegions; ++r) {
        std::fill(m_regionF[r], m_regionF[r] + n, 0.0f);
        std::fill(m_symRegionF[r], m_symRegionF[r] + n, 0.0f);
    }
    bool sounding = false;
    const int thumpLen = static_cast<int>(0.0015 * m_sampleRate), noiseLen = static_cast<int>(0.04 * m_sampleRate);
    const bool modded = m_mod.on();
    for (int vi = 0; vi < Voices; ++vi) {
        Voice& v = m_voices[vi];
        if (v.m_key < 0) continue;
        sounding = true;
        const PianoKey& k = d.m_keys[static_cast<size_t>(v.m_key)];
        if (cellStart && modded) modVoice(vi, false);
        if (cellStart) blockCoefficients(v, MaxBlock);
        if (v.m_contact) contactBlock(v, m_force, n);
        else stringBlock<VT>(v, m_force, n);
        float peak = cellStart ? 0.0f : v.m_peak;
        for (int i = 0; i < n; ++i) {
            float F = m_force[i];
            peak = std::max(peak, std::fabs(F));
            // The longitudinal modes on the square of the bridge force.
            if (k.m_longCount > 0 && m_phantom > 0.0f) {
                const float u = static_cast<float>(k.m_longDrive) * F * F;
                float fl = 0.0f;
                for (int j = 0; j < k.m_longCount; ++j) {
                    const float r = k.m_lpr[j] * v.m_lzr[j] - k.m_lpi[j] * v.m_lzi[j] + k.m_lgr[j] * u;
                    const float q = k.m_lpr[j] * v.m_lzi[j] + k.m_lpi[j] * v.m_lzr[j] + k.m_lgi[j] * u;
                    v.m_lzr[j] = r;
                    v.m_lzi[j] = q;
                    fl += k.m_lcr[j] * r - k.m_lci[j] * q;
                }
                F += m_phantom * LongCoupling * fl;
            }
            // The mechanics: the thump and the damper's noise, into the board where the key meets it.
            if (v.m_thump >= 0) {
                const int t = v.m_thump + i;
                if (t < thumpLen) F += m_mechanics * v.m_thumpGain * 0.5f * (1.0f - std::cos(TwoPi * static_cast<float>(t) / static_cast<float>(thumpLen)));
            }
            if (v.m_damperNoise >= 0) {
                const int t = v.m_damperNoise + i;
                if (t < noiseLen) F += m_damperNoise * v.m_damperNoiseGain * v.m_noise.bipolar() * std::exp(-6.0f * static_cast<float>(t) / static_cast<float>(noiseLen));
            }
            const float Fg = F * v.m_modGain;
            m_regionF[v.m_region][i] += v.m_w0 * Fg;
            m_regionF[v.m_region + 1][i] += v.m_w1 * Fg;
        }
        if (v.m_thump >= 0) { v.m_thump += n; if (v.m_thump >= thumpLen) v.m_thump = -1; }
        if (v.m_damperNoise >= 0) { v.m_damperNoise += n; if (v.m_damperNoise >= noiseLen) v.m_damperNoise = -1; }
        v.m_peak = peak;
        if (cellEnd) {
            // The hammer has left: away from the strings for a while and flying back (or held too long: the check).
            const int os = k.m_oversample;
            if (v.m_contact && ((v.m_apart > 8 * os && v.m_vh < 0.0) || v.m_contactSteps > static_cast<int>(0.08 * m_sampleRate) * os)) v.m_contact = false;
            v.m_lastPeak = peak;
            v.m_quiet = !v.m_contact && peak < QuietForce ? v.m_quiet + 1 : 0;
            if (v.m_quiet > QuietBlocks && v.m_thump < 0 && v.m_damperNoise < 0) v.m_key = -1;
        }
    }
    // The sympathetic strings listen to the board when some damper is up and something sounds.
    bool anySym = false;
    for (int k = 0; k < PianoKeys && !anySym; ++k) anySym = m_symOn[k] || m_held[k] > 0;
    anySym = anySym || m_pedal > 0.02f;
    const bool wantAccel = anySym && m_sympathetic > 0.0f;
    float bl[MaxBlock], br[MaxBlock];
    boardBlock<VT>(m_board, m_regionF, bl, br, wantAccel, n);
    bool symSounding = false;
    if (wantAccel) symBlock<VT>(n, sounding, cellStart, cellEnd);
    for (int k = 0; k < PianoKeys && !symSounding; ++k) symSounding = m_symOn[k];
    if (symSounding) {
        float sl[MaxBlock], sr[MaxBlock];
        boardBlock<VT>(m_symBoard, m_symRegionF, sl, sr, false, n);
        for (int i = 0; i < n; ++i) { bl[i] += sl[i]; br[i] += sr[i]; }
        m_symBoardOn = true;
    } else if (m_symBoardOn) {
        // The last ringing string has stopped (under its threshold): its board's rest is silence, not kept for later.
        for (std::vector<float>* a : { &m_symBoard.m_zr, &m_symBoard.m_zi, &m_symBoard.m_hzr, &m_symBoard.m_hzi }) std::fill(a->begin(), a->end(), 0.0f);
        m_symBoardOn = false;
    }
    const float gain = d.m_outGain * m_level;
    for (int i = 0; i < n; ++i) {
        float lp, bp, hp;
        float x[2] = { bl[i] * gain, br[i] * gain };
        // The radiation (PianoSpec::radiation): a shelf from the board's acceleration to its velocity.
        for (int c = 0; c < 2; ++c) {
            const float y = m_radB0 * x[c] + m_radB1 * m_radX[c] - m_radA1 * m_radY[c];
            m_radX[c] = x[c];
            m_radY[c] = y;
            x[c] = y;
        }
        m_lowCut[0].tick(x[0], lp, bp, hp);
        L[i] = hp;
        m_lowCut[1].tick(x[1], lp, bp, hp);
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
