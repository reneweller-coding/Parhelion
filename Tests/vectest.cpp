/**
 * @file vectest.cpp
 * @brief Lane paths against the scalar reference, bit for bit.
 *
 * Built once per vector path (Tests/CMakeLists.txt): AVX2, NEON through the x86 shim, and scalar. Every lane of every
 * vector operation, of the half-band filters and of the percussion kit's kernel must equal the float instantiation
 * exactly -- not within a tolerance.
 * @note The operations and the half-band are copied from Ephemeris `Tests/vectest.cpp` at d047d79 (27.09.2026); the
 *       kit section follows Phosphene's; the polyphonic sections are Phosphene's `Tests/vectest.cpp` at 76f7100.
 * @note Copied from Totality `Tests/vectest.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#include "parh/Halfband.h"
#include "parh/Params.h"
#include "parh/Vec.h"
#include "parh/synth/Kit.h"
#include "parh/synth/Piano.h"
#include "parh/synth/Poly.h"
#include "TestSupport.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

using namespace parh;
using namespace parhtest;

namespace {

constexpr int W = kVecWidth;

float testValue(uint32_t i)
{
    uint32_t h = i * 2654435761u ^ 0x9E3779B9u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    const float u = static_cast<float>(h) / 4294967296.0f;
    switch (i % 5) {
    case 0:  return u * 2.0f - 1.0f;
    case 1:  return (u * 2.0f - 1.0f) * 1.0e-6f;
    case 2:  return (u * 2.0f - 1.0f) * 1000.0f;
    case 3:  return u + 0.5f;
    default: return -(u + 0.25f);
    }
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

void testOps()
{
    section("vector operations, lane by lane");
    int bad = 0, total = 0;
    float a[8], b[8], c[8];
    for (uint32_t round = 0; round < 2000; ++round) {
        for (int i = 0; i < 8; ++i) { a[i] = testValue(round * 8 + i); b[i] = testValue(round * 8 + i + 11); c[i] = testValue(round * 8 + i + 23); }
        const VecF va = loadLanes<VecF>(a), vb = loadLanes<VecF>(b), vc = loadLanes<VecF>(c);
        const VecF r[] = {
            va + vb, va - vb, va * vb, va / vb, vfmadd(va, vb, vc), vfnmadd(va, vb, vc),
            vmin(va, vb), vmax(va, vb), vsqrt(vabs(va)), vabs(va), vfloor(va), vselect(vlt(va, vb), vc, va),
        };
        for (int l = 0; l < W; ++l) {
            const float x = a[l], y = b[l], z = c[l];
            const float s[] = {
                x + y, x - y, x * y, x / y, vfmadd(x, y, z), vfnmadd(x, y, z),
                vmin(x, y), vmax(x, y), vsqrt(vabs(x)), vabs(x), vfloor(x), vselect(vlt(x, y), z, x),
            };
            for (size_t k = 0; k < sizeof(s) / sizeof(s[0]); ++k) {
                ++total;
                if (!sameBits(laneOf(r[k], l), s[k])) ++bad;
            }
        }
    }
    check(bad == 0, "12 operations identical to scalar", fmt("%d of %d lanes differ", bad, total));
}

void testHalfband()
{
    section("half-band lanes against scalar");
    const HalfbandDesign d = designHalfband(96.0, 0.1);
    HalfbandDown<VecF> vd;
    HalfbandUp<VecF> vu;
    HalfbandDown<float> sd[8];
    HalfbandUp<float> su[8];
    vd.setup(d); vu.setup(d);
    for (auto& s : sd) s.setup(d);
    for (auto& s : su) s.setup(d);
    int bad = 0;
    float a[8], b[8];
    for (uint32_t n = 0; n < 5000; ++n) {
        for (int l = 0; l < 8; ++l) { a[l] = testValue(n * 8 + l); b[l] = testValue(n * 8 + l + 5); }
        const VecF yd = vd.process(loadLanes<VecF>(a), loadLanes<VecF>(b));
        VecF u0, u1;
        vu.process(loadLanes<VecF>(a), u0, u1);
        for (int l = 0; l < W; ++l) {
            float s0, s1;
            su[l].process(a[l], s0, s1);
            if (!sameBits(laneOf(yd, l), sd[l].process(a[l], b[l]))) ++bad;
            if (!sameBits(laneOf(u0, l), s0) || !sameBits(laneOf(u1, l), s1)) ++bad;
        }
    }
    check(bad == 0, "decimator and interpolator identical to scalar", fmt("%d differing samples", bad));
}

/**
 * The kit: the default twelve lanes (every engine, the metal table, bursts, chokes, the auto-pan), a pattern of hits
 * with sub-sample onsets, rendered once through the lane path and once through the scalar reference.
 */
void testKit()
{
    section("percussion kit lanes against the scalar kit");
    auto params = std::make_unique<ParamStore>();
    PercKit vk, sk;
    vk.prepare(48000.0);
    sk.prepare(48000.0);
    for (PercKit* k : { &vk, &sk }) {
        k->setTempo(130.0);
        for (int l = 0; l < kPercLanes; ++l) {
            float v[64];
            params->readModule(Module::Perc, l, v);
            k->update(l, v, 9, 0);
        }
    }
    int bad = 0, samples = 0;
    float peak = 0.0f;
    for (int block = 0; block < 1500; ++block) {
        if (block % 3 == 0) {
            const int lane = (block / 3) % kPercLanes;
            const float vel = 0.4f + 0.05f * static_cast<float>(block % 12);
            const double late = static_cast<double>(block % 7) / 7.0;
            vk.trigger(lane, vel, block % 5 == 0 ? 2 : 0, late);
            sk.trigger(lane, vel, block % 5 == 0 ? 2 : 0, late);
        }
        vk.processLanes(32);
        sk.processLanesWith<float>(32);
        for (int i = 0; i < 32 * PercKit::kStride; ++i) {
            if (!sameBits(vk.laneL()[i], sk.laneL()[i]) || !sameBits(vk.laneR()[i], sk.laneR()[i])) ++bad;
            peak = std::max(peak, std::fabs(sk.laneL()[i]));
            ++samples;
        }
    }
    check(bad == 0, "every lane identical to scalar", fmt("%d of %d samples differ", bad, samples));
    check(peak > 0.01f && std::isfinite(peak), "the kit sounds", fmt("peak %.3f", static_cast<double>(peak)));
}

/** @brief Vector test: polyphonic engine lanes against the scalar engine. */
void testPoly()
{
    section("polyphonic engine lanes against the scalar engine");
    const DenormalGuard guard;
    ParamStore p;
    auto a = std::make_unique<Poly>(), b = std::make_unique<Poly>();
    a->prepare(48000.0);
    b->prepare(48000.0);
    int bad = 0;
    double energy = 0.0;
    std::vector<float> aL(64), aR(64), bL(64), bR(64);
    for (int block = 0; block < 3000; ++block) {
        if (block % 250 == 0) {
            // A different oscillator and filter every so often, the same on both. Thermal drift and
            // the disperser are switched on here on purpose (16.09.2026): the drift moves the phase
            // step of every one of the 56 slots and the disperser sits on the summed output, and both
            // are computed on the scalar side precisely so that the lane paths cannot diverge -- a run
            // with them off would never notice if one day they did.
            p.parseText(fmt("lead.osc=%d lead.table=%d lead.pos_env=0.4 lead.pos_lfo_beats=0.5 lead.resonance=%.2f lead.fm_index=%d "
                            "lead.wave=0.5 lead.drift=4 lead.disperse=%d lead.disperse_freq=900",
                            (block / 250) % 4, (block / 250) % 6, 0.1 + 0.07 * (block / 250 % 10), block / 250 % 7,
                            1 + (block / 250) % 8).c_str());
            std::vector<float> v(static_cast<size_t>(poly::Count));
            p.readModule(Module::Poly, 0, v.data());
            a->update(v.data(), 145.0);
            b->update(v.data(), 145.0);
        }
        if (block % 9 == 0) {
            const int pitch = 55 + (block * 7) % 30;
            const double late = static_cast<double>(block % 10) / 10.0;
            a->noteOn(pitch, 0.8f, 0.25 * (1 + block % 8), 2000 + block % 5000, late);
            b->noteOn(pitch, 0.8f, 0.25 * (1 + block % 8), 2000 + block % 5000, late);
        }
        const int n = 5 + block % 60;
        a->processWith<float>(aL.data(), aR.data(), n);
        b->processWith<VecF>(bL.data(), bR.data(), n);
        for (int i = 0; i < n; ++i) {
            if (!sameBits(aL[static_cast<size_t>(i)], bL[static_cast<size_t>(i)]) || !sameBits(aR[static_cast<size_t>(i)], bR[static_cast<size_t>(i)])) ++bad;
            energy += static_cast<double>(aL[static_cast<size_t>(i)]) * aL[static_cast<size_t>(i)];
        }
    }
    check(bad == 0 && energy > 1.0, "56 oscillator slots and 16 voice channels identical to scalar (supersaw, VA, FM, wavetable, with drift and disperser)", fmt("%d differing samples, energy %.1f", bad, energy));

}

/**
 * @brief Vector test (26.09.2026): the filter models (Filters.h) and the voice's own modulation (Modulation.h) on the
 *        polyphonic engine's lanes -- every model, with a matrix that moves cutoff, resonance, mode, table, pitch,
 *        pulse width, pan and level from all four LFOs, the modulation envelope and the note's random value.
 */
void testPolyModels()
{
    section("filter models and modulation on the polyphonic lanes against the scalar engine");
    const DenormalGuard guard;
    ParamStore p;
    auto a = std::make_unique<Poly>(), b = std::make_unique<Poly>();
    a->prepare(48000.0);
    b->prepare(48000.0);
    a->seedPhases(7);
    b->seedPhases(7);
    int bad = 0;
    double energy = 0.0;
    float maxAbs = 0.0f;
    std::vector<float> aL(64), aR(64), bL(64), bR(64);
    double beat = 0.0;
    for (int block = 0; block < 4500; ++block) {
        if (block % 500 == 0) {
            const int m = 1 + (block / 500) % 9;
            p.parseText(fmt("lead.filter_model=%d lead.filter_mode=%.2f lead.resonance=%.2f lead.osc=%d lead.table=3 lead.drift=3 "
                            "lead.filt_attack=40 lead.filt_sustain=0.3 lead.lfo1_sync=6 lead.lfo2_rate=3.1 lead.lfo2_shape=6 "
                            "lead.lfo3_shape=5 lead.lfo3_sync=7 lead.lfo4_retrig=1 lead.lfo4_fade=0.2 lead.menv_decay=300 "
                            "lead.mx1_src=1 lead.mx1_dst=6 lead.mx1_amount=0.4 lead.mx2_src=2 lead.mx2_dst=7 lead.mx2_amount=0.3 "
                            "lead.mx3_src=5 lead.mx3_dst=4 lead.mx3_amount=0.7 lead.mx4_src=3 lead.mx4_dst=8 lead.mx4_amount=0.8 "
                            "lead.mx5_src=4 lead.mx5_dst=1 lead.mx5_amount=0.1 lead.mx6_src=9 lead.mx6_dst=10 lead.mx6_amount=0.6 "
                            "lead.mx7_src=7 lead.mx7_dst=9 lead.mx7_amount=-0.4 lead.mx8_src=6 lead.mx8_dst=3 lead.mx8_amount=0.5",
                            m, 0.11 * (block / 500 % 9), 0.2 + 0.09 * (block / 500 % 9), (block / 500) % 4).c_str());
            std::vector<float> v(static_cast<size_t>(poly::Count));
            p.readModule(Module::Poly, 0, v.data());
            a->update(v.data(), 145.0);
            b->update(v.data(), 145.0);
        }
        if (block % 9 == 0) {
            const int pitch = 50 + (block * 7) % 34;
            const double late = static_cast<double>(block % 10) / 10.0;
            a->noteOn(pitch, 0.6f + 0.04f * (block % 10), 0.25 * (1 + block % 8), 1500 + block % 4000, late);
            b->noteOn(pitch, 0.6f + 0.04f * (block % 10), 0.25 * (1 + block % 8), 1500 + block % 4000, late);
        }
        const int n = 5 + block % 60;
        a->setClock(beat, 145.0 / 60.0 / 48000.0);
        b->setClock(beat, 145.0 / 60.0 / 48000.0);
        beat += n * 145.0 / 60.0 / 48000.0;
        a->processWith<float>(aL.data(), aR.data(), n);
        b->processWith<VecF>(bL.data(), bR.data(), n);
        for (int i = 0; i < n; ++i) {
            if (!sameBits(aL[static_cast<size_t>(i)], bL[static_cast<size_t>(i)]) || !sameBits(aR[static_cast<size_t>(i)], bR[static_cast<size_t>(i)])) ++bad;
            energy += static_cast<double>(aL[static_cast<size_t>(i)]) * aL[static_cast<size_t>(i)];
            maxAbs = std::max(maxAbs, std::fabs(aL[static_cast<size_t>(i)]));
        }
    }
    check(bad == 0 && energy > 1.0, "nine filter models with a full modulation matrix identical to scalar",
          fmt("%d differing samples, energy %.1f", bad, energy));
    check(std::isfinite(maxAbs) && maxAbs < 20.0f, "the models stay bounded under modulated resonance and mode", fmt("max |y| = %.3f", static_cast<double>(maxAbs)));
}

/**
 * @brief Vector test (Phase 4a): the physical piano's resonator banks -- the strings, the board and its high bank, the
 *        sympathetic strings -- on the lanes against the scalar reference: chords struck, released and struck again,
 *        the pedal moving (half pedal, dampers landing, the strings under raised dampers ringing along), calls of odd
 *        sizes.
 */
void testPiano()
{
    section("the piano's resonator banks against the scalar engine");
    const DenormalGuard guard;
    auto a = std::make_unique<Piano>(), b = std::make_unique<Piano>();
    ParamStore ps;
    std::vector<float> v(static_cast<size_t>(piano::Count));
    ps.readModule(Module::Piano, 0, v.data());
    for (Piano* p : { a.get(), b.get() }) {
        p->prepare(48000.0);
        p->setSpec(PianoSpec{});
        p->update(v.data());
    }
    int bad = 0;
    double energy = 0.0;
    std::vector<float> aL(64), aR(64), bL(64), bR(64);
    static const int kChords[4][3] = { { 45, 57, 64 }, { 41, 53, 60 }, { 36, 55, 64 }, { 43, 59, 74 } };
    for (int block = 0; block < 4000; ++block) {
        if (block % 400 == 0) {
            v[piano::Pedal] = static_cast<float>((block / 400) % 3) * 0.5f;
            a->update(v.data());
            b->update(v.data());
        }
        if (block % 250 == 0) {
            const int* c = kChords[(block / 250) % 4];
            for (int k = 0; k < 3; ++k) { a->noteOn(c[k], 0.5f + 0.15f * k, 0.3); b->noteOn(c[k], 0.5f + 0.15f * k, 0.3); }
        }
        if (block % 250 == 120) {
            const int* c = kChords[(block / 250) % 4];
            for (int k = 0; k < 3; ++k) { a->noteOff(c[k]); b->noteOff(c[k]); }
        }
        const int n = 5 + block % 40;
        a->processWith<float>(aL.data(), aR.data(), n);
        b->processWith<VecF>(bL.data(), bR.data(), n);
        for (int i = 0; i < n; ++i) {
            if (!sameBits(aL[static_cast<size_t>(i)], bL[static_cast<size_t>(i)]) || !sameBits(aR[static_cast<size_t>(i)], bR[static_cast<size_t>(i)])) ++bad;
            energy += static_cast<double>(aL[static_cast<size_t>(i)]) * aL[static_cast<size_t>(i)];
        }
    }
    check(bad == 0 && energy > 0.1, "strings, board, high bank and sympathetic strings identical to scalar", fmt("%d differing samples, energy %.2f", bad, energy));
}
} // namespace

int main()
{
    std::printf("parh_vectest: path %s, %d lanes\n", kVecPathName, W);
#if defined(PARH_EXPECT_PATH)
    check(std::strcmp(kVecPathName, PARH_EXPECT_PATH) == 0, "built for the expected path", fmt("expected %s, got %s", PARH_EXPECT_PATH, kVecPathName));
#endif
    testOps();
    testHalfband();
    testKit();
    testPoly();
    testPolyModels();
    testPiano();
    return finish();
}
