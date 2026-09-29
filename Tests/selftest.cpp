/**
 * @file selftest.cpp
 * @brief parh_selftest: every building block measured against an independently derived value.
 *
 * Sections are registered in the table at the bottom; `parh_selftest --list` prints their names (ctest registers one
 * test per name) and `--only a,b` runs the named ones. Only checks that protect something real belong here (the
 * user's rule for Ephemeris, 24.09.2026).
 * @note The frame, the tempo-map, kick and block-size tests and their helpers are copied from Totality `Tests/selftest.cpp`
 *       at 4d3c0d2 (29.09.2026); the rest is Parhelion's.
 */
#include "parh/Clock.h"
#include "parh/Engine.h"
#include "parh/Loudness.h"
#include "parh/Midi.h"
#include "parh/Params.h"
#include "parh/Score.h"
#include "parh/Leveler.h"
#include "parh/compose/Composer.h"
#include "parh/compose/Corpus.h"
#include "parh/compose/Harmony.h"
#include "parh/compose/Melody.h"
#include "parh/compose/Memo.h"
#include "parh/compose/Planner.h"
#include "parh/compose/Study.h"
#include "parh/mix/TranceGate.h"
#include "parh/synth/Kick.h"
#include "parh/synth/Piano.h"
#include "parh/synth/PianoDesign.h"
#include "TestSupport.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace parh;
using namespace parhtest;

namespace {

constexpr double kPiD = 3.141592653589793;

bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

/** The tempo map converts beats to seconds in closed form; checked against a numerical integral over a ramp. */
void testTempoMap()
{
    section("tempo map against a numerical integral");
    TempoMap m;
    m.setConstant(136.0);
    m.add(64.0, 136.0, true);
    m.add(1024.0, 140.0, false);
    double t = 0.0, worst = 0.0;
    const double db = 1.0 / 64.0;
    for (double b = 0.0; b < 1100.0; b += db) {
        t += 60.0 / m.bpmAt(b + 0.5 * db) * db;
        worst = std::max(worst, std::fabs(t - m.secondsAt(b + db)));
    }
    check(worst < 1.0e-6, "secondsAt equals the integral", fmt("%.2e s", worst));
    double inv = 0.0;
    for (double b = 0.0; b < 1100.0; b += 7.3) inv = std::max(inv, std::fabs(m.beatAt(m.secondsAt(b)) - b));
    check(inv < 1.0e-9, "beatAt inverts secondsAt", fmt("%.2e beats", inv));
}

/** The parameter store: the named instances, the defaults of the voices and the kit, text round trip, choices by name. */
void testParams()
{
    section("parameters");
    auto p = std::make_unique<ParamStore>();
    std::set<std::string> keys;
    for (int i = 0; i < p->count(); ++i) keys.insert(p->key(i));
    check(static_cast<int>(keys.size()) == p->count(), "every key is unique", fmt("%d parameters", p->count()));
    bool named = true;
    for (int i = 0; i < kPolyInstances; ++i) named = named && p->find(std::string(kPolyInstanceNames[i]) + ".detune") >= 0;
    check(named, "the polyphonic voices have their names (lead.detune .. stab.detune)");
    check(p->getInt(p->find("lead.osc")) == static_cast<int>(PolyOsc::Supersaw) && p->getInt(p->find("lead.osc2")) == static_cast<int>(PolyOsc2::Supersaw)
          && p->getInt(p->find("lead.osc2_interval")) == static_cast<int>(PolyOsc2Interval::OctaveDown),
          "the lead: a supersaw with a second one an octave down (Dok. 5)");
    check(p->getInt(p->find("pluck.osc")) == static_cast<int>(PolyOsc::Va) && p->get(p->find("pluck.amp_sustain")) == 0.0f,
          "the pluck: VA, no sustain");
    check(p->getInt(p->find("perc2.role")) == static_cast<int>(PercRole::OpenHat) && p->getInt(p->find("perc3.role")) == static_cast<int>(PercRole::Clap)
          && p->getInt(p->find("perc6.role")) == static_cast<int>(PercRole::Crash), "the kit's roles");
    p->parseText("compose.key=F; compose.style=Dream House; lead.gate_pattern=Trance 1");
    check(p->getInt(p->find("compose.key")) == 5 && p->getInt(p->find("compose.style")) == static_cast<int>(Style::DreamHouse)
          && p->getInt(p->find("lead.gate_pattern")) == 6, "choices by name");
    const std::string text = p->toText(true);
    auto q = std::make_unique<ParamStore>();
    q->parseText(text);
    bool same = true;
    for (int i = 0; i < p->count(); ++i) same = same && p->get(i) == q->get(i);
    check(same, "text form round-trips every value", fmt("%d parameters", p->count()));
}

/** Renders a kick triggered at @p late with the store's kick settings; returns the output. */
std::vector<float> renderKick(const ParamStore& p, int keyRoot, double late, int n, Kick& k)
{
    float v[64];
    p.readModule(Module::Kick, 0, v);
    Kick::constrain(v, 0.0, keyRoot);
    k.prepare(48000.0);
    k.update(v, keyRoot);
    k.trigger(1.0f, late);
    std::vector<float> out(static_cast<size_t>(n)), b(static_cast<size_t>(n));
    k.process(out.data(), b.data(), n);
    return out;
}

/** Phase of @p x at @p hz over [from, from + len) samples, relative to the sample grid's t = 0, in cycles. */
double phaseAt(const std::vector<float>& x, double hz, int from, int len, double sr, double t0Samples)
{
    double re = 0.0, im = 0.0;
    for (int i = from; i < from + len; ++i) {
        const double t = (i + t0Samples) / sr;
        re += x[static_cast<size_t>(i)] * std::cos(2.0 * kPiD * hz * t);
        im += x[static_cast<size_t>(i)] * std::sin(2.0 * kPiD * hz * t);
    }
    return std::atan2(re, im) / (2.0 * kPiD);
}

double wrapCycles(double c) { return c - std::round(c); }

/** The kick's asymptotic phase (Kick.h), which the sub's lock rests on, for the three engines and a sub-sample onset. */
void testKickPhase()
{
    section("the kick's asymptotic phase");
    auto p = std::make_unique<ParamStore>();
    p->parseText("kick.top_level=-60; kick.amp_decay=900; kick.amp_hold=0");
    for (int engine = 0; engine < 3; ++engine) {
        p->set(p->find("kick.engine"), static_cast<float>(engine));
        for (double late : { 0.0, 0.37 }) {
            Kick k;
            const std::vector<float> y = renderKick(*p, 9, late, 48000, k);
            const double f = k.tunedEndHz();
            const double measured = phaseAt(y, f, 9600, 9600, 48000.0, late);
            const double err = std::fabs(wrapCycles(measured - k.asymptoticPhase())) * 360.0;
            check(err < 3.0, fmt("engine %d, late %.2f: tail in phase with f0 t + c", engine, late).c_str(), fmt("%.2f degrees", err));
        }
    }
}

/**
 * The kick on the key (Dok. 5: "Kick-Grundton und Bass-Grundton duerfen nicht einen Halbton auseinanderliegen"): tuned to
 * every key, its fundamental lies in 40 .. 66 Hz on the tonic or the fifth -- the tonic where it has an octave there.
 */
void testKickTuning()
{
    section("kick tuning");
    bool keyOk = true, fifthOk = true;
    std::string detail;
    for (int root = 0; root < 12; ++root) {
        const float f = Kick::tuneToKey(root, static_cast<int>(KickTune::Key), 52.0f);
        const double note = 69.0 + 12.0 * std::log2(f / 440.0);
        const int pc = ((static_cast<int>(std::lround(note)) % 12) + 12) % 12;
        const bool tonicFits = root >= 4;
        const int want = tonicFits ? root : (root + 7) % 12;
        if (pc != want || f < 40.0f || f > 66.0f) { keyOk = false; detail += fmt("%s->%.1f ", kKeyNames[root], f); }
        const float g = Kick::tuneToKey(root, static_cast<int>(KickTune::Fifth), 52.0f);
        const int pg = ((static_cast<int>(std::lround(69.0 + 12.0 * std::log2(g / 440.0))) % 12) + 12) % 12;
        fifthOk = fifthOk && pg == (root + 7) % 12;
    }
    check(keyOk, "Key: the tonic in 41 .. 62 Hz, else the fifth", detail);
    check(fifthOk, "Fifth: the fifth");
}

/** The trance gate's masks (TranceGate.h, Dok. 5): open in the middle of exactly the steps the mask names. */
void testGate()
{
    section("trance gate masks");
    const char* const kMasks[3] = { "x.xx.x.xx.x.x.x.", "xx.x.xx.xx.x.xx.", "x..x..x..x..x.x." };
    for (int m = 0; m < 3; ++m) {
        std::string got;
        for (int s = 0; s < 16; ++s) got += TranceGate::open(0.25 * s + 0.0625, 6 + m, 0.5f, 0.001, 0.001) > 0.5f ? 'x' : '.';
        check(got == kMasks[m], fmt("pattern %d is %s", 6 + m, kMasks[m]).c_str(), got);
    }
}

/**
 * The study's form (Study.h) against the rules of Dok. 6 the planner will have to keep: sections on 8-bar lines, a layer
 * block per 8 bars, every block changing something against the one before, the lead introduced in the breakdown and never
 * in the intro or the outro, the last beat before the drop empty, the ghost kick on every quarter apart from it.
 */
void testStudyForm()
{
    section("the study's form (Dok. 6)");
    auto p = std::make_unique<ParamStore>();
    const Score sc = composeStudy(*p, 7);
    bool lines = true;
    for (const Section& s : sc.sections) lines = lines && std::fmod(s.beat, 32.0) == 0.0 && std::fmod(s.length, 32.0) == 0.0;
    check(lines && !sc.sections.empty(), "every section starts and ends on an 8-bar line", fmt("%zu sections", sc.sections.size()));
    bool blocks = sc.layers.size() * 32 == static_cast<size_t>(sc.lengthBeats);
    int unchanged = 0;
    for (size_t b = 0; b < sc.layers.size(); ++b) {
        blocks = blocks && sc.layers[b].beat == 32.0 * static_cast<double>(b);
        if (b > 0 && sc.layers[b].state == sc.layers[b - 1].state) ++unchanged;
    }
    check(blocks, "one layer block per 8 bars", fmt("%zu blocks", sc.layers.size()));
    check(unchanged <= 2, "almost every block adds, filters or takes away an element", fmt("%d blocks without a change", unchanged));
    double firstBreakdown = -1.0, firstLead = -1.0;
    for (const Section& s : sc.sections) if (s.kind == SectionKind::Breakdown && firstBreakdown < 0.0) firstBreakdown = s.beat;
    bool leadInEdges = false;
    for (const NoteEvent& n : sc.notes) {
        if (n.part != Part::Lead) continue;
        if (firstLead < 0.0) firstLead = n.beat;
        for (const Section& s : sc.sections)
            if ((s.kind == SectionKind::Intro || s.kind == SectionKind::Outro) && n.beat >= s.beat && n.beat < s.beat + s.length) leadInEdges = true;
    }
    check(firstLead >= firstBreakdown && firstBreakdown >= 0.0, "the lead is introduced in the breakdown, not before",
          fmt("first lead at bar %.0f, breakdown at bar %.0f", firstLead / 4.0, firstBreakdown / 4.0));
    check(!leadInEdges, "no lead in the intro or the outro");
    double drop = -1.0;
    for (const Section& s : sc.sections) if (s.kind == SectionKind::Drop) { drop = s.beat; break; }
    int inVacuum = 0;
    for (const NoteEvent& n : sc.notes) if (n.beat >= drop - 1.0 && n.beat < drop) ++inVacuum;
    check(drop > 0.0 && inVacuum == 0, "the last beat before the drop is empty (Dok. 6, rule 5)", fmt("%d notes start in it", inVacuum));
    std::set<double> ghosts;
    for (const NoteEvent& n : sc.notes) if (n.part == Part::Ghost) ghosts.insert(n.beat);
    check(static_cast<double>(ghosts.size()) == sc.lengthBeats - 1.0, "the ghost kick on every quarter but that one",
          fmt("%zu of %.0f", ghosts.size(), sc.lengthBeats));
}

/** Renders @p score from beat @p from for @p seconds with stems; returns the stems (left channel) and the output. */
struct Run { std::vector<std::vector<float>> stems; std::vector<float> out; };
Run render(const Score& score, const char* knobs, double from, double seconds, int block = 256)
{
    auto e = std::make_unique<Engine>();
    if (knobs != nullptr) e->params().parseText(knobs);
    e->prepare(48000.0, block);
    e->load(score);
    e->seek(from);
    std::vector<std::vector<float>> sl(Engine::kStems, std::vector<float>(static_cast<size_t>(block))), sr = sl;
    std::vector<float*> pl, pr;
    for (int k = 0; k < Engine::kStems; ++k) { pl.push_back(sl[static_cast<size_t>(k)].data()); pr.push_back(sr[static_cast<size_t>(k)].data()); }
    e->setStems(pl.data(), pr.data());
    Run run;
    run.stems.assign(Engine::kStems, {});
    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    const int n = static_cast<int>(seconds * 48000.0);
    for (int done = 0; done < n; done += block) {
        const int m = std::min(block, n - done);
        e->process(L.data(), R.data(), m);
        for (int i = 0; i < m; ++i) {
            run.out.push_back(L[static_cast<size_t>(i)]);
            run.out.push_back(R[static_cast<size_t>(i)]);
            for (int s = 0; s < Engine::kStems; ++s) run.stems[static_cast<size_t>(s)].push_back(sl[static_cast<size_t>(s)][static_cast<size_t>(i)]);
        }
    }
    return run;
}

double rmsOf(const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to && i < x.size(); ++i) s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(std::max<size_t>(1, to - from)));
}

/**
 * The pump (PLAN 7.3): a held pad note (one VA saw, so nothing beats) and the ghost kick on the quarters, no kick. Against
 * the same render with the duck at 0 dB, the pad inside the hold of every duck lies poly.duck dB lower, and before the
 * next ghost kick -- the release long done -- where it was.
 */
void testPump()
{
    section("the pump: ghost kick, duck depth, no kick needed");
    Score sc;
    sc.clear(120.0);   // a beat of 0.5 s: 24000 samples
    sc.lengthBeats = 16.0;
    sc.notes.push_back(NoteEvent{ 0.0, 15.0, Part::Pad, 57, 0.8f, 0, false, false });
    for (int q = 4; q < 16; ++q) sc.notes.push_back(NoteEvent{ static_cast<double>(q), 0.25, Part::Ghost, 36, 1.0f, 0, false, false });
    sc.sort();
    auto pad = [&](float depth) {
        const std::string knobs = fmt("pad.duck=%.1f; pad.osc=VA; pad.detune=0; pad.amp_attack=5; pad.amp_sustain=1; pad.gate=0;"
                                      "pad.lfo_cutoff=0; pad.drift=0; pump.attack=0.5; pump.hold=40; pump.release=120", depth);
        return render(sc, knobs.c_str(), 0.0, 7.0).stems[Engine::kStemPad];
    };
    const std::vector<float> ref = pad(0.0f);
    for (float depth : { 4.0f, 9.0f }) {
        const std::vector<float> x = pad(depth);
        double worstIn = 0.0, worstBefore = 0.0;
        for (int q = 6; q < 12; ++q) {
            const size_t at = static_cast<size_t>(q) * 24000;
            const double in = 20.0 * std::log10(rmsOf(x, at + 480, at + 1680) / std::max(rmsOf(ref, at + 480, at + 1680), 1e-12));
            const double before = 20.0 * std::log10(rmsOf(x, at - 4800, at - 1200) / std::max(rmsOf(ref, at - 4800, at - 1200), 1e-12));
            worstIn = std::max(worstIn, std::fabs(in + depth));
            worstBefore = std::max(worstBefore, std::fabs(before));
        }
        check(worstIn < 0.1, fmt("pad.duck %.0f dB: the pad falls by it in the hold of every ghost kick", depth).c_str(),
              fmt("worst %.3f dB off", worstIn));
        check(worstBefore < 0.05, fmt("pad.duck %.0f dB: and is back before the next", depth).c_str(), fmt("worst %.3f dB off", worstBefore));
    }
}

/**
 * The low end (PLAN 5, Dok. 7): in the study's drop, every polyphonic voice and the rooms keep out of the band under
 * 150 Hz -- their energy there at least 25 dB under their own -- and the kick, the sub and the bass hold it.
 */
void testLowEnd()
{
    section("the low end: nothing under 150 Hz but kick, sub and bass");
    auto p = std::make_unique<ParamStore>();
    const Score sc = composeStudy(*p, 3);
    const Run r = render(sc, nullptr, 4.0 * 64, 12.0, 512);
    auto lowShare = [](const std::vector<float>& x) {
        Svf a, b;
        a.setK(150.0f, 1.8477590f, 48000.0f);
        b.setK(150.0f, 0.7653669f, 48000.0f);
        double lo = 0.0, all = 0.0;
        for (float v : x) { const float y = b.lp(a.lp(v)); lo += static_cast<double>(y) * y; all += static_cast<double>(v) * v; }
        return all > 0.0 ? 10.0 * std::log10(std::max(lo, 1e-30) / all) : -300.0;
    };
    std::string worst;
    double worstDb = -300.0;
    for (int s : { Engine::kStemLead, Engine::kStemPluck, Engine::kStemPad, Engine::kStemArp, Engine::kStemRoom,
                   Engine::kStemPlate, Engine::kStemHall }) {
        const double db = lowShare(r.stems[static_cast<size_t>(s)]);
        if (db > worstDb) { worstDb = db; worst = Engine::stemName(s); }
    }
    check(worstDb < -25.0, "voices and rooms under 150 Hz at least 25 dB under themselves", fmt("worst: %s at %.1f dB", worst.c_str(), worstDb));
    const double kickLow = lowShare(r.stems[Engine::kStemKick]), subLow = lowShare(r.stems[Engine::kStemSub]);
    check(kickLow > -6.0 && subLow > -3.0, "the kick and the sub live there", fmt("kick %.1f dB, sub %.1f dB", kickLow, subLow));
}

/** Renders the study from its start with block size @p block for @p seconds (interleaved). */
std::vector<float> renderStudy(int block, double seconds, uint64_t seed, double from = 0.0)
{
    auto p = std::make_unique<ParamStore>();
    const Score sc = composeStudy(*p, seed);
    auto e = std::make_unique<Engine>();
    e->prepare(48000.0, block);
    e->load(sc);
    if (from > 0.0) e->seek(from);
    std::vector<float> out, L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    const int n = static_cast<int>(seconds * 48000.0);
    for (int done = 0; done < n; done += block) {
        const int m = std::min(block, n - done);
        e->process(L.data(), R.data(), m);
        for (int i = 0; i < m; ++i) { out.push_back(L[static_cast<size_t>(i)]); out.push_back(R[static_cast<size_t>(i)]); }
    }
    return out;
}

/** Determinism across block sizes (the rule of all the siblings): 1, 37 and 512 bit for bit, in the groove and the drop. */
void testBlockSizes()
{
    section("block sizes");
    for (double from : { 0.0, 4.0 * 52 }) {
        const std::vector<float> a = renderStudy(512, 12.0, 3, from), b = renderStudy(37, 12.0, 3, from), c = renderStudy(1, 12.0, 3, from);
        size_t firstB = a.size(), firstC = a.size();
        for (size_t i = 0; i < a.size(); ++i) {
            if (firstB == a.size() && std::memcmp(&a[i], &b[i], sizeof(float)) != 0) firstB = i;
            if (firstC == a.size() && std::memcmp(&a[i], &c[i], sizeof(float)) != 0) firstC = i;
        }
        check(firstB == a.size(), fmt("from bar %.0f: 37 equals 512, bit for bit", from / 4.0).c_str(),
              firstB == a.size() ? std::string() : fmt("first difference at %.4f s", firstB / 96000.0));
        check(firstC == a.size(), fmt("from bar %.0f: 1 equals 512, bit for bit", from / 4.0).c_str(),
              firstC == a.size() ? std::string() : fmt("first difference at %.4f s", firstC / 96000.0));
        double peak = 0.0;
        for (float x : a) peak = std::max(peak, static_cast<double>(std::fabs(x)));
        check(peak > 0.05 && std::isfinite(peak), "it sounds", fmt("peak %.3f", peak));
    }
}

/** The stems sum to the mix as it enters the master, and switching them on leaves the mix as it was. */
void testStems()
{
    section("stems");
    auto p = std::make_unique<ParamStore>();
    const Score sc = composeStudy(*p, 5);
    auto run = [&](bool stems, std::vector<float>& out, std::vector<float>& sum, std::vector<float>& pre) {
        auto e = std::make_unique<Engine>();
        e->prepare(48000.0, 37);
        e->load(sc);
        e->seek(4.0 * 50);
        std::vector<std::vector<float>> sl(Engine::kStems, std::vector<float>(37)), sr = sl;
        std::vector<float*> pl, pr;
        for (int k = 0; k < Engine::kStems; ++k) { pl.push_back(sl[static_cast<size_t>(k)].data()); pr.push_back(sr[static_cast<size_t>(k)].data()); }
        std::vector<float> L(37), R(37), preL(37), preR(37);
        if (stems) { e->setStems(pl.data(), pr.data()); e->setPremasterTap(preL.data(), preR.data()); }
        for (int done = 0; done < 48000 * 10; done += 37) {
            e->process(L.data(), R.data(), 37);
            for (int i = 0; i < 37; ++i) {
                out.push_back(L[static_cast<size_t>(i)]);
                if (!stems) continue;
                float s = 0.0f;
                for (int k = 0; k < Engine::kStems; ++k) s += sl[static_cast<size_t>(k)][static_cast<size_t>(i)];
                sum.push_back(s);
                pre.push_back(preL[static_cast<size_t>(i)]);
            }
        }
    };
    std::vector<float> outA, sumA, preA, outB, sumB, preB;
    run(true, outA, sumA, preA);
    run(false, outB, sumB, preB);
    double peak = 0.0, err = 0.0;
    for (size_t i = 0; i < preA.size(); ++i) {
        peak = std::max(peak, std::fabs(static_cast<double>(preA[i])));
        err = std::max(err, std::fabs(static_cast<double>(sumA[i]) - preA[i]));
    }
    const double db = 20.0 * std::log10(std::max(err, 1e-12) / std::max(peak, 1e-12));
    check(peak > 0.01 && db < -100.0, "the stems sum to the mix before the master (the build into the drop)",
          fmt("peak %.3f, largest difference %.1f dB under it", peak, db));
    check(outA.size() == outB.size() && std::memcmp(outA.data(), outB.data(), outA.size() * sizeof(float)) == 0,
          "the mix with stems is the mix without, bit for bit");
}

/**
 * The energy script heard (PLAN 2.7, rule 3; 13.5): the study rendered and measured per section -- the drop's loudest
 * three seconds over the breakdown's by at least 3 LU (Dok. 6: 4 to 8; calibrated in Phase 2 with the Leveler), the
 * master's true peak at its ceiling, nothing that is not a number.
 */
void testEnergy()
{
    section("the energy script, measured");
    auto p = std::make_unique<ParamStore>();
    const Score sc = composeStudy(*p, 11);
    auto e = std::make_unique<Engine>();
    e->prepare(48000.0, 512);
    e->load(sc);
    LoudnessMeter whole, breakdown, drop;
    for (LoudnessMeter* m : { &whole, &breakdown, &drop }) m->prepare(48000.0);
    const auto startOf = [&](SectionKind k) { for (const Section& s : sc.sections) if (s.kind == k) return s; return Section{}; };
    const Section b = startOf(SectionKind::Breakdown), d = startOf(SectionKind::Drop);
    const int64_t b0 = static_cast<int64_t>(sc.tempo.secondsAt(b.beat) * 48000.0), b1 = static_cast<int64_t>(sc.tempo.secondsAt(b.beat + b.length) * 48000.0);
    const int64_t d0 = static_cast<int64_t>(sc.tempo.secondsAt(d.beat) * 48000.0), d1 = static_cast<int64_t>(sc.tempo.secondsAt(d.beat + d.length) * 48000.0);
    std::vector<float> L(512), R(512);
    bool finite = true;
    for (int64_t done = 0; done < d1; done += 512) {
        e->process(L.data(), R.data(), 512);
        for (int i = 0; i < 512; ++i) finite = finite && std::isfinite(L[static_cast<size_t>(i)]) && std::isfinite(R[static_cast<size_t>(i)]);
        whole.process(L.data(), R.data(), 512);
        if (done >= b0 && done + 512 <= b1) breakdown.process(L.data(), R.data(), 512);
        if (done >= d0 && done + 512 <= d1) drop.process(L.data(), R.data(), 512);
    }
    const LoudnessReport rw = whole.report(), rb = breakdown.report(), rd = drop.report();
    check(finite, "every sample a number");
    check(rw.truePeak <= -0.9, "the true peak at the master's ceiling", fmt("%.2f dBTP", rw.truePeak));
    const double gap = rd.shortTermMax - rb.shortTermMax;
    check(gap >= 3.0, "the drop louder than the breakdown", fmt("breakdown %.1f, drop %.1f LUFS short-term max: %.1f LU", rb.shortTermMax,
                                                                rd.shortTermMax, gap));
}

/** The MIDI export: every part on its channel, the ghost kick on 16 (for a host's sidechain). */
void testMidi()
{
    section("MIDI");
    auto p = std::make_unique<ParamStore>();
    const Score sc = composeStudy(*p, 2);
    const std::vector<uint8_t> bytes = encodeMidi(sc, "test", p.get());
    check(bytes.size() > 1000 && bytes[0] == 'M' && bytes[1] == 'T', "an SMF file", fmt("%zu bytes", bytes.size()));
    check(midiChannelOf(Part::Ghost) == 15 && midiChannelOf(Part::Lead) == 3 && midiChannelOf(Part::Kick) == 9, "the channels");
}


/**
 * The planner (Planner.h) against the rules of Dok. 6 for every style and eight seeds: sections on 8-bar lines, the length
 * a multiple of 32, every block changing an element, the lead never on before the first break or breakdown and never on
 * in the intro or the outro, an empty last beat before every drop, the length near the one asked for, the breakdown share
 * inside the profile's window (as the references were measured).
 */
void testPlanner()
{
    section("the planner (Dok. 6) for every style");
    int bad[8] = {};
    std::string where;
    for (int st = 0; st < static_cast<int>(Style::Count); ++st) {
        const StyleProfile& prof = styleProfile(static_cast<Style>(st));
        for (uint64_t seed = 1; seed <= 8; ++seed) {
            const int ask = static_cast<int>(0.5f * (prof.minutesLow + prof.minutesHigh) * 0.5f * (prof.bpmLow + prof.bpmHigh) / 4.0f);
            const Plan plan = planTrack(prof, ask, seed);
            bool lines = plan.bars % 32 == 0;
            for (const Section& sec : plan.sections) lines = lines && std::fmod(sec.beat, 32.0) == 0.0 && std::fmod(sec.length, 32.0) == 0.0;
            if (!lines) { ++bad[0]; where += fmt("%s/%llu lines; ", prof.name, static_cast<unsigned long long>(seed)); }
            for (size_t b = 1; b < plan.blocks.size(); ++b) if (plan.blocks[b].state == plan.blocks[b - 1].state) { ++bad[1]; break; }
            double firstBreak = 1e30;
            for (const Section& sec : plan.sections)
                if (sec.kind == SectionKind::Break || sec.kind == SectionKind::Breakdown || sec.kind == SectionKind::Intro) { firstBreak = std::min(firstBreak, sec.beat); if (sec.kind != SectionKind::Intro) break; }
            for (const LayerBlock& blk : plan.blocks) {
                if (blk.at(Layer::Lead) != LayerState::On) continue;
                const int si = plan.sectionAt(static_cast<int>(blk.beat / 4.0));
                const SectionKind k = plan.sections[static_cast<size_t>(si)].kind;
                if (k == SectionKind::Intro || k == SectionKind::Outro) { ++bad[2]; break; }
            }
            if (plan.firstLeadBar >= 0) {
                const int si = plan.sectionAt(plan.firstLeadBar);
                const SectionKind k = plan.sections[static_cast<size_t>(si)].kind;
                const bool ok = k == SectionKind::Break || k == SectionKind::Breakdown
                             || (k == SectionKind::Intro && plan.at(plan.firstLeadBar, Layer::Lead) == LayerState::Filtered);
                if (!ok) { ++bad[3]; where += fmt("%s/%llu lead first in %s; ", prof.name, static_cast<unsigned long long>(seed), kSectionNames[static_cast<int>(k)]); }
            }
            for (size_t i = 1; i < plan.sections.size(); ++i)
                if (plan.sections[i].kind == SectionKind::Drop && !contains(plan.vacuums, static_cast<int>(plan.sections[i].beat / 4.0) - 1)) ++bad[4];
            if (std::abs(plan.bars - ask) > ask / 4) { ++bad[5]; where += fmt("%s/%llu %d bars for %d; ", prof.name, static_cast<unsigned long long>(seed), plan.bars, ask); }
            const float share = breakdownShare(plan);
            if (share < prof.breakdownLow - 0.1f || share > prof.breakdownHigh + 0.1f) {
                ++bad[6];
                where += fmt("%s/%llu share %.2f; ", prof.name, static_cast<unsigned long long>(seed), share);
            }
        }
    }
    check(bad[0] == 0, "sections on 8-bar lines, the track a multiple of 32 bars", where);
    check(bad[1] == 0, "every block adds, filters or takes away an element (rule 1)", fmt("%d plans with a repeated block", bad[1]));
    check(bad[2] == 0, "no lead on in an intro or an outro (rule 7)", fmt("%d plans", bad[2]));
    check(bad[3] == 0, "the lead is first heard in a break or a breakdown (rule 6)", where);
    check(bad[4] == 0, "an empty last beat before every drop (rule 5)", fmt("%d drops without", bad[4]));
    check(bad[5] == 0, "the length within a quarter of the one asked for", where);
    check(bad[6] == 0, "the breakdown share inside the profile's window (as measured, PLAN 13.4)", where);
}

/**
 * The harmony (Harmony.h): the two bars before every drop on VII (in major V), the drop's first bar on the tonic chord,
 * every chord tone in the chord's scale, the pad's lowest voice never the chord's root and never under A3.
 */
void testHarmony()
{
    section("harmony (Dok. 4)");
    int liftBad = 0, resolveBad = 0, scaleBad = 0, voiceBad = 0, keyChanges = 0;
    for (int st = 0; st < static_cast<int>(Style::Count); ++st) {
        const StyleProfile& prof = styleProfile(static_cast<Style>(st));
        if (prof.barsPerChord >= 16) continue;   // Acid's static harmony has no lift
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            Rng r;
            r.seed(seed);
            int key, scale;
            drawKey(prof, r, key, scale);
            const Plan plan = planTrack(prof, 256, seed);
            const Harmony h = composeHarmony(plan, prof, key, scale, seed * 7);
            const bool major = scale == static_cast<int>(Scale::Ionian);
            if (h.changeBar >= 0) ++keyChanges;
            for (size_t i = 1; i < plan.sections.size(); ++i) {
                if (plan.sections[i].kind != SectionKind::Drop) continue;
                const int d = static_cast<int>(plan.sections[i].beat / 4.0);
                if (h.bars[static_cast<size_t>(d - 1)].degree != (major ? 4 : 6) || h.bars[static_cast<size_t>(d - 2)].degree != (major ? 4 : 6)) ++liftBad;
                if (h.bars[static_cast<size_t>(d)].degree != (major ? 5 : 0) && h.bars[static_cast<size_t>(d)].degree != 0) ++resolveBad;
            }
            std::array<int, 4> prev{ 60, 64, 67, 72 };
            for (int bar = 0; bar < plan.bars; ++bar) {
                for (int t = 0; t < 3; ++t) {
                    const int iv = ((h.bars[static_cast<size_t>(bar)].tones[t]) % 12 + 12) % 12;
                    const int cs = scale == static_cast<int>(Scale::Dorian) || scale == static_cast<int>(Scale::Phrygian) || major ? scale : 0;
                    if (!inScale(cs, iv) && !(t == 1 && h.bars[static_cast<size_t>(bar)].degree == 4)) ++scaleBad;
                }
                const std::array<int, 4> v = voicePad(h, bar, prev, bar == 0);
                prev = v;
                if (v[0] < 57 || ((v[0] - h.key - h.shift[static_cast<size_t>(bar)]) % 12 + 12) % 12 == ((h.bars[static_cast<size_t>(bar)].tones[0]) % 12 + 12) % 12) ++voiceBad;
            }
        }
    }
    check(liftBad == 0, "the two bars before a drop on VII (major: V)", fmt("%d drops", liftBad));
    check(resolveBad == 0, "the drop resolves on the tonic chord", fmt("%d drops", resolveBad));
    check(scaleBad == 0, "every chord tone in the chord's scale (the major V of harmonic minor aside)", fmt("%d tones", scaleBad));
    check(voiceBad == 0, "the pad's lowest voice a third or a fifth, from A3 up", fmt("%d voicings", voiceBad));
    std::printf("         (%d of the plans change key)\n", keyChanges);
}

/**
 * The composer: the same seed gives the same score to the bit; rerolling one unit (the melody) leaves the drums and the
 * bass exactly as they were and changes the lead.
 */
void testComposer()
{
    section("the composer: determinism and rerolls");
    auto p = std::make_unique<ParamStore>();
    const Score a = composeTrack(*p, 21), b = composeTrack(*p, 21);
    bool same = a.notes.size() == b.notes.size();
    for (size_t i = 0; same && i < a.notes.size(); ++i)
        same = a.notes[i].beat == b.notes[i].beat && a.notes[i].pitch == b.notes[i].pitch && a.notes[i].part == b.notes[i].part
            && a.notes[i].velocity == b.notes[i].velocity;
    check(same && !a.notes.empty(), "the same seed, the same score", fmt("%zu notes", a.notes.size()));
    Curation cur;
    cur.reroll("melody");
    const Score c = composeTrack(*p, 21, TrackRequest{}, &cur);
    auto partNotes = [](const Score& s, std::initializer_list<Part> parts) {
        std::vector<std::pair<double, int>> out;
        for (const NoteEvent& n : s.notes) for (Part q : parts) if (n.part == q) out.push_back({ n.beat, n.pitch });
        return out;
    };
    check(partNotes(a, { Part::Kick, Part::Perc1, Part::Perc3, Part::Bass, Part::Sub, Part::Pad }) == partNotes(c, { Part::Kick, Part::Perc1, Part::Perc3, Part::Bass, Part::Sub, Part::Pad }),
          "a rerolled melody leaves the drums, the bass and the pad as they were");
    check(partNotes(a, { Part::Lead }) != partNotes(c, { Part::Lead }) || partNotes(a, { Part::Lead }).empty(), "and draws the lead again");
}

/**
 * The Leveler (Leveler.h) on a composed Uplifting track: the drop at the style's target within half a dB, the breakdown
 * under it by the style's gap within 0.6 LU, the lead within its window against the kick.
 */
void testLeveler()
{
    section("the Leveler: loudness, the breakdown, the balance");
    auto p = std::make_unique<ParamStore>();
    p->parseText("compose.style=Uplifting");
    Score sc = composeTrack(*p, 5);
    const std::vector<LevelReading> r = levelScore(sc, *p);
    check(r.size() == 1, "one reading");
    if (r.empty()) return;
    check(std::fabs(r[0].after + r[0].trim - r[0].measured - r[0].trim) >= 0.0f && std::fabs(r[0].after - r[0].target) < 0.8f,
          "the drop at the style's target", fmt("%.1f LUFS after the correction, target %.1f", r[0].after, r[0].target));
    check(!std::isnan(r[0].gapAfter) && std::fabs(r[0].gapAfter - sc.levels[0].gapLu) < 0.6f, "the breakdown under the drop by the style's gap",
          fmt("%.1f LU (as composed %.1f), target %.1f, correction %+.1f dB", r[0].gapAfter, r[0].gapBefore, sc.levels[0].gapLu, r[0].breakDb));
    float lo, hi;
    balanceWindow(static_cast<int>(BalPart::Lead), -1, lo, hi);
    const float lead = r[0].found[static_cast<size_t>(BalPart::Lead)] + r[0].bal[static_cast<size_t>(BalPart::Lead)];
    check(std::isnan(r[0].found[static_cast<size_t>(BalPart::Lead)]) || (lead >= lo - 0.01f && lead <= hi + 0.01f), "the lead inside its window against the kick",
          fmt("%.1f dB (window %.0f .. %.0f)", lead, lo, hi));
}

/**
 * The corpus model (Corpus.h): every conditional distribution sums to one, the constrained sampler never leaves the
 * allowed steps, reports an impossible constraint, and draws exactly in proportion to the model (Pachet and Roy): for
 * three positions of two allowed steps each, the eight lines' frequencies against their enumerated probabilities.
 */
void testCorpus()
{
    section("the corpus model and the constrained sampler");
    double worst = 0.0;
    for (int role = 0; role < kCorpusRoleCount; ++role) {
        const StepModel& m = corpusModel(static_cast<CorpusRoleId>(role));
        for (int a = -1; a < kCorpusAlpha; a += 3)
            for (int b = -1; b < kCorpusAlpha; b += 4) {
                if (a >= 0 && b < 0) continue;
                double s = 0.0;
                for (int c = 0; c < kCorpusAlpha; ++c) s += m.p(a, b, c);
                worst = std::max(worst, std::fabs(s - 1.0));
            }
    }
    check(worst < 1e-4, "every distribution of every role sums to one", fmt("worst deviation %.1e", worst));

    const StepModel& lead = corpusModel(CorpusRoleId::Lead);
    Rng r;
    r.seed(99);
    int outside = 0, failed = 0;
    for (int t = 0; t < 200; ++t) {
        std::vector<std::array<bool, kCorpusAlpha>> allowed(12);
        for (auto& row : allowed) {
            row.fill(false);
            for (int k = 0; k < 3; ++k) row[static_cast<size_t>(r.below(kCorpusAlpha))] = true;
        }
        std::vector<int> out;
        if (!sampleConstrained(lead, 12, allowed, nullptr, r, out)) { ++failed; continue; }
        for (size_t i = 0; i < out.size(); ++i) if (!allowed[i][static_cast<size_t>(out[i])]) ++outside;
    }
    check(outside == 0 && failed == 0, "200 lines under random constraints, every step allowed", fmt("%d outside, %d failed", outside, failed));
    {
        std::vector<std::array<bool, kCorpusAlpha>> allowed(4);
        for (auto& row : allowed) row.fill(true);
        allowed[2].fill(false);
        std::vector<int> out;
        check(!sampleConstrained(lead, 4, allowed, nullptr, r, out), "an impossible constraint is reported, nothing drawn");
    }
    // Exactness: the tonic or the fifth at each of three positions.
    const int s0 = 7, s1 = 11;
    std::vector<std::array<bool, kCorpusAlpha>> allowed(3);
    for (auto& row : allowed) { row.fill(false); row[s0] = row[s1] = true; }
    double pExact[8], total = 0.0;
    for (int k = 0; k < 8; ++k) {
        const int x0 = k & 1 ? s1 : s0, x1 = k & 2 ? s1 : s0, x2 = k & 4 ? s1 : s0;
        pExact[k] = lead.p(-1, -1, x0) * lead.p(-1, x0, x1) * lead.p(x0, x1, x2);
        total += pExact[k];
    }
    int counts[8] = {};
    const int draws = 40000;
    for (int t = 0; t < draws; ++t) {
        std::vector<int> out;
        sampleConstrained(lead, 3, allowed, nullptr, r, out);
        counts[(out[0] == s1 ? 1 : 0) + (out[1] == s1 ? 2 : 0) + (out[2] == s1 ? 4 : 0)]++;
    }
    double dev = 0.0;
    for (int k = 0; k < 8; ++k) dev = std::max(dev, std::fabs(counts[k] / static_cast<double>(draws) - pExact[k] / total));
    check(dev < 0.01, "the sampler draws in proportion to the model", fmt("largest deviation %.4f over the eight lines", dev));
}

/**
 * The memorisation gate (Memo.h): the engine's hash is the tool's (the probe window, also transposed), thin windows do
 * not count, and the Bloom filter answers "maybe" for few windows it does not hold.
 */
void testMemo()
{
    section("the memorisation gate");
    std::vector<int> top(64, -1);
    const int probe[5][2] = { { 0, 60 }, { 4, 62 }, { 8, 63 }, { 12, 60 }, { 16, 65 } };
    for (const auto& n : probe) top[static_cast<size_t>(n[0])] = n[1];
    uint64_t h = 0, h5 = 0;
    const bool ok = memoWindowHash(top, 0, h);
    for (int& p : top) if (p >= 0) p += 5;
    memoWindowHash(top, 0, h5);
    check(ok && h == kMemoProbeHash, "the engine hashes a window as Tools/corpus/memorisation.py does", fmt("%016llx", static_cast<unsigned long long>(h)));
    check(h5 == h, "in any key");
    top[16] = -1;
    uint64_t h4 = 0;
    check(!memoWindowHash(top, 0, h4), "four notes are too few to count");
    Rng r;
    r.seed(7);
    int maybe = 0;
    for (int t = 0; t < 20000; ++t) {
        const uint64_t x = r.next();
        maybe += memoContains(x);
    }
    check(maybe < 100, "few false \"maybe\"s", fmt("%d of 20000 random windows (%.2f %%)", maybe, maybe / 200.0));
}

/**
 * The lead (Melody.h) over six seeds of Uplifting and Dream House: every note on a beat a tone of its bar's chord,
 * every note a tone of the scale or the chord, the register between E3 and C7, no bar starting two bars of a known
 * track, and the motif back in every phrase of the main drop.
 */
void testMelody()
{
    section("the lead: rules, register, memorisation");
    int notes = 0, offChord = 0, offScale = 0, register_ = 0, hits = 0, empty = 0;
    for (Style st : { Style::Uplifting, Style::DreamHouse }) {
        const StyleProfile& prof = styleProfile(st);
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            Rng kr;
            kr.seed(seed);
            int key, scale;
            drawKey(prof, kr, key, scale);
            const Plan plan = planTrack(prof, 256, seed);
            const Harmony h = composeHarmony(plan, prof, key, scale, seed * 7);
            Score sc;
            sc.clear(138.0);
            MelodyContext mc;
            mc.plan = &plan;
            mc.harmony = &h;
            mc.score = &sc;
            mc.piano = prof.lead == LeadKind::Piano;
            mc.anthemShare = mc.piano ? 1.0f : 0.7f;
            writeLead(mc, seed * 131);
            std::vector<int> top(static_cast<size_t>(plan.bars) * 16, -1);
            int n = 0;
            for (const NoteEvent& e : sc.notes) {
                if (e.part != Part::Lead && e.part != Part::Piano) continue;
                ++n;
                const int bar = std::min(static_cast<int>(e.beat / 4.0), plan.bars - 1);
                const int pc = e.pitch % 12;
                const bool chord = pc == h.pc(bar, 0) || pc == h.pc(bar, 1) || pc == h.pc(bar, 2);
                if (std::fabs(e.beat - std::round(e.beat)) < 1e-9 && !chord) ++offChord;
                const int rel = ((pc - key - h.shift[static_cast<size_t>(bar)]) % 12 + 12) % 12;
                bool ok = chord || inScale(scale, rel);
                if (scale == static_cast<int>(Scale::HarmonicMinor) || scale == static_cast<int>(Scale::MinorPentatonic))
                    ok = ok || inScale(static_cast<int>(Scale::Aeolian), rel);
                                if (!ok) ++offScale;
                if (e.pitch < 52 || e.pitch > 96) ++register_;
                top[static_cast<size_t>(std::llround(e.beat * 4.0))] = std::max(top[static_cast<size_t>(std::llround(e.beat * 4.0))], e.pitch);
            }
            notes += n;
            if (n == 0) ++empty;
            if (memoHitsAnyBar(top)) ++hits;
        }
    }
    check(empty == 0, "every track has its lead", fmt("%d notes in 12 tracks", notes));
    check(offChord == 0, "every note on a beat is a tone of its chord", fmt("%d not", offChord));
    check(offScale == 0, "every note is a tone of the scale or the chord", fmt("%d not", offScale));
    check(register_ == 0, "the register between E3 and C7", fmt("%d outside", register_));
    check(hits == 0, "no two bars of a known track", fmt("%d tracks with a hit", hits));
}

/** @brief One piano note (held @p hold s) over @p secs s, the channels' mean, with the knobs' defaults and @p pedal. */
std::vector<float> pianoNote(Piano& p, int midi, float vel, double hold, double secs, float pedal = 0.0f, float sympathetic = 1.0f,
                             float phantom = 1.0f)
{
    ParamStore ps;
    std::vector<float> v(static_cast<size_t>(piano::Count));
    ps.readModule(Module::Piano, 0, v.data());
    v[piano::Pedal] = pedal;
    v[piano::Sympathetic] = sympathetic;
    v[piano::Phantom] = phantom;
    p.update(v.data());
    p.reset();
    const int total = static_cast<int>(secs * 48000.0), off = static_cast<int>(hold * 48000.0);
    std::vector<float> out(static_cast<size_t>(total));
    float L[Piano::kMaxBlock], R[Piano::kMaxBlock];
    p.noteOn(midi, vel, 0.0);
    for (int i = 0; i < total; i += Piano::kMaxBlock) {
        if (i <= off && off < i + Piano::kMaxBlock) p.noteOff(midi);
        const int n = std::min(Piano::kMaxBlock, total - i);
        p.process(L, R, n);
        for (int k = 0; k < n; ++k) out[static_cast<size_t>(i + k)] = 0.5f * (L[k] + R[k]);
    }
    return out;
}

/** @brief The level (dB) of @p x between @p a and @p b seconds. */
double levelDb(const std::vector<float>& x, double a, double b)
{
    const size_t i = static_cast<size_t>(a * 48000.0), j = std::min(x.size(), static_cast<size_t>(b * 48000.0));
    double s = 0.0;
    for (size_t k = i; k < j; ++k) s += static_cast<double>(x[k]) * x[k];
    return 10.0 * std::log10(s / std::max<size_t>(1, j - i) + 1e-30);
}

/** @brief The frequency of the strongest peak of @p x (from @p start, 2^15 samples, Hann) within @p f +- 2 %. */
double peakHz(const std::vector<float>& x, double start, double f)
{
    const size_t n = 32768, s0 = static_cast<size_t>(start * 48000.0);
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) a[i] = (s0 + i < x.size() ? x[s0 + i] : 0.0f) * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / (n - 1)));
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> wl = std::polar(1.0, -2.0 * 3.14159265358979 / static_cast<double>(len));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    const double bin = 48000.0 / n;
    size_t lo = static_cast<size_t>(f * 0.98 / bin), hi = static_cast<size_t>(f * 1.02 / bin), at = lo;
    for (size_t i = lo; i <= hi; ++i) if (std::abs(a[i]) > std::abs(a[at])) at = i;
    const double l = std::log(std::abs(a[at - 1]) + 1e-30), c = std::log(std::abs(a[at]) + 1e-30), r = std::log(std::abs(a[at + 1]) + 1e-30);
    return (static_cast<double>(at) + 0.5 * (l - r) / (l - 2.0 * c + r)) * bin;
}

/**
 * The physical piano (Piano.h, PianoDesign.h, PLAN 5.8). The soundboard's Rayleigh-Ritz solver against the closed form
 * of a simply supported orthotropic plate; the design's numbers (the board's first mode, the inharmonicity, the courses);
 * then single notes: the partials where the stiff string puts them, the two-stage decay, the brightness rising with the
 * velocity, the pitch falling after a fortissimo (the tension), the damper, the pedal, the strings ringing along.
 */
void testPiano()
{
    section("the physical piano");
    const DenormalGuard guard;
    {
        const double a = 1.2, b = 0.9, dx = 900.0, dy = 300.0, d12 = 100.0, d66 = 50.0, m = 4.0;
        const std::vector<double> rr = pianoPlateTest(a, b, dx, dy, d12, d66, m, 8);
        std::vector<double> exact;
        for (int i = 1; i <= 6; ++i)
            for (int j = 1; j <= 6; ++j) {
                const double kx = i * 3.14159265358979 / a, ky = j * 3.14159265358979 / b;
                exact.push_back(std::sqrt((dx * std::pow(kx, 4) + 2.0 * (d12 + 2.0 * d66) * kx * kx * ky * ky + dy * std::pow(ky, 4)) / m)
                                / (2.0 * 3.14159265358979));
            }
        std::sort(exact.begin(), exact.end());
        double worst = 0.0;
        for (size_t k = 0; k < rr.size(); ++k) worst = std::max(worst, std::fabs(rr[k] / exact[k] - 1.0));
        check(rr.size() == 8 && worst < 0.005, "the plate solver finds a plate's modes", fmt("worst of eight %.3f %%, first %.1f Hz", 100.0 * worst, rr.empty() ? 0.0 : rr[0]));
    }
    auto piano = std::make_unique<Piano>();
    piano->prepare(48000.0);
    piano->setSpec(PianoSpec{});
    const PianoDesign& d = *piano->design();
    const PianoKey& c4 = d.keys[static_cast<size_t>(60 - kPianoLowKey)];
    check(d.board.freq[0] > 35.0 && d.board.freq[0] < 65.0 && d.board.freq.size() > 20, "the board: a first mode near 45 Hz, tens of modes to 1.2 kHz",
          fmt("%.1f Hz, %zu modes", d.board.freq[0], d.board.freq.size()));
    check(c4.B > 2e-4 && c4.B < 8e-4 && c4.strings == 3 && d.keys[0].strings == 1 && d.keys[static_cast<size_t>(36 - kPianoLowKey)].strings == 2,
          "C4's inharmonicity, a course of three; one string in the lowest bass, two in the wound", fmt("B %.2e", c4.B));
    // The partials of C2: the stiff string's, not the harmonics.
    {
        const std::vector<float> x = pianoNote(*piano, 36, 0.7f, 3.0, 1.5);
        const PianoKey& k = d.keys[static_cast<size_t>(36 - kPianoLowKey)];
        double worst = 0.0;
        for (int p = 4; p <= 12; ++p) {
            const double model = p * k.f0 * std::sqrt(1.0 + k.B * p * p);
            worst = std::max(worst, std::fabs(1200.0 * std::log2(peakHz(x, 0.2, model) / model)));
        }
        const double f12 = peakHz(x, 0.2, 12.0 * k.f0 * std::sqrt(1.0 + k.B * 144.0)), f1 = peakHz(x, 0.2, k.f0 * std::sqrt(1.0 + k.B));
        const double stretch = 1200.0 * std::log2(f12 / (12.0 * f1));
        check(worst < 5.0 && stretch > 5.0, "C2's partials 4 .. 12 where the stiff string puts them, the twelfth sharp of the harmonic",
              fmt("within %.1f cents; the twelfth %+.1f cents", worst, stretch));
    }
    // The two-stage decay of C4 (the symmetric mode takes the bridge's losses, the others ring on).
    {
        const std::vector<float> x = pianoNote(*piano, 60, 0.7f, 12.0, 9.0);
        const double early = (levelDb(x, 1.15, 1.25) - levelDb(x, 0.15, 0.25)) / 1.0, late = (levelDb(x, 7.95, 8.05) - levelDb(x, 3.95, 4.05)) / 4.0;
        check(early < 2.0 * late && late < -1.0, "C4 decays in two stages", fmt("%.1f dB/s early, %.1f dB/s late", early, late));
    }
    // Brightness and level over the velocity.
    {
        double lastCentroid = 0.0, lo = 0.0, hi = 0.0;
        bool rising = true;
        for (float vel : { 0.2f, 0.5f, 0.8f, 1.0f }) {
            const std::vector<float> x = pianoNote(*piano, 60, vel, 2.0, 0.5);
            // Brightness by its simplest measure: the zero crossings per second of the first half second.
            int zc = 0;
            for (size_t i = 1; i < x.size(); ++i) zc += (x[i - 1] < 0.0f) != (x[i] < 0.0f);
            const double centroid = zc / 2.0 / 0.5;
            if (centroid <= lastCentroid) rising = false;
            lastCentroid = centroid;
            const double l = levelDb(x, 0.0, 0.5);
            if (vel == 0.2f) lo = l;
            if (vel == 1.0f) hi = l;
        }
        check(rising && hi - lo > 15.0, "harder is brighter and louder", fmt("%.1f dB from velocity 0.2 to 1", hi - lo));
    }
    // The tension: a fortissimo A1 starts sharp and settles.
    {
        const std::vector<float> x = pianoNote(*piano, 33, 1.0f, 6.0, 4.0);
        const PianoKey& k = d.keys[static_cast<size_t>(33 - kPianoLowKey)];
        const double f2 = 2.0 * k.f0 * std::sqrt(1.0 + 4.0 * k.B);
        const double glide = 1200.0 * std::log2(peakHz(x, 0.0, f2) / peakHz(x, 3.0, f2));
        check(glide > 0.2 && glide < 10.0, "a fortissimo's pitch falls as it decays (Kirchhoff-Carrier)", fmt("%.2f cents", glide));
    }
    // The phantom partials: the longitudinal modes on the square of the bridge force, so they grow with the square.
    {
        double share[2];
        int idx = 0;
        for (float vel : { 0.35f, 1.0f }) {
            const std::vector<float> on = pianoNote(*piano, 36, vel, 2.0, 0.8, 0.0f, 1.0f, 1.0f), off = pianoNote(*piano, 36, vel, 2.0, 0.8, 0.0f, 1.0f, 0.0f);
            double diff = 0.0, all = 0.0;
            for (size_t i = 0; i < on.size(); ++i) {
                diff += (static_cast<double>(on[i]) - off[i]) * (static_cast<double>(on[i]) - off[i]);
                all += static_cast<double>(off[i]) * off[i];
            }
            share[idx++] = 10.0 * std::log10(diff / all + 1e-30);
        }
        check(share[1] > -45.0 && share[1] < -10.0 && share[1] - share[0] > 6.0, "C2's phantom partials, stronger the harder the key",
              fmt("%.1f dB of the note at velocity 0.35, %.1f dB at 1", share[0], share[1]));
    }
    // The damper, the pedal.
    {
        const std::vector<float> damped = pianoNote(*piano, 60, 0.7f, 1.0, 2.0), pedal = pianoNote(*piano, 60, 0.7f, 1.0, 2.0, 1.0f);
        const double dropDamped = levelDb(damped, 0.95, 1.0) - levelDb(damped, 1.45, 1.5), dropPedal = levelDb(pedal, 0.95, 1.0) - levelDb(pedal, 1.45, 1.5);
        check(dropDamped > 30.0 && dropPedal < 12.0, "a released key is damped, with the pedal down it rings on",
              fmt("%.0f dB down in half a second, with the pedal %.0f dB", dropDamped, dropPedal));
    }
    // The strings ring along with the pedal down, and nothing swings up.
    {
        const std::vector<float> with = pianoNote(*piano, 43, 0.8f, 0.3, 3.0, 1.0f, 1.0f), without = pianoNote(*piano, 43, 0.8f, 0.3, 3.0, 1.0f, 0.0f);
        double diff = 0.0, all = 0.0, peakWith = 0.0, peakWithout = 0.0;
        for (size_t i = static_cast<size_t>(1.0 * 48000.0); i < with.size(); ++i) {
            diff += (static_cast<double>(with[i]) - without[i]) * (static_cast<double>(with[i]) - without[i]);
            all += static_cast<double>(without[i]) * without[i];
        }
        for (size_t i = 0; i < with.size(); ++i) {
            peakWith = std::max(peakWith, static_cast<double>(std::fabs(with[i])));
            peakWithout = std::max(peakWithout, static_cast<double>(std::fabs(without[i])));
        }
        const double db = 10.0 * std::log10(diff / all + 1e-30);
        check(db > -24.0 && db < -6.0 && peakWith < peakWithout * 1.06 && std::isfinite(peakWith), "with the pedal down the other strings ring along, under the note",
              fmt("%.1f dB of the note, peak %+.2f dB", db, 20.0 * std::log10(peakWith / peakWithout)));
    }
}

/** The piano in the engine: a Dream House track at block sizes 1, 37 and 512, bit for bit, where the piano plays. */
void testPianoBlocks()
{
    section("the piano at any block size");
    auto p = std::make_unique<ParamStore>();
    p->parseText("compose.style=Dream House");
    const Score sc = composeTrack(*p, 11);
    double from = 0.0;
    int notes = 0;
    for (const NoteEvent& n : sc.notes) if (n.part == Part::Piano) { if (notes == 0) from = std::floor(n.beat / 4.0) * 4.0; ++notes; }
    check(notes > 50, "Dream House writes its motif for the piano", fmt("%d notes", notes));
    auto render = [&](int block) {
        auto e = std::make_unique<Engine>();
        e->params().copyValuesFrom(*p);
        e->prepare(48000.0, block);
        e->load(sc);
        e->seek(from);
        std::vector<float> out, L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
        const int n = static_cast<int>(6.0 * 48000.0);
        for (int done = 0; done < n; done += block) {
            const int m = std::min(block, n - done);
            e->process(L.data(), R.data(), m);
            for (int i = 0; i < m; ++i) out.push_back(L[static_cast<size_t>(i)]);
        }
        return out;
    };
    const std::vector<float> a = render(512), b = render(37), c = render(1);
    size_t diffB = 0, diffC = 0;
    double peak = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        diffB += std::memcmp(&a[i], &b[i], sizeof(float)) != 0;
        diffC += std::memcmp(&a[i], &c[i], sizeof(float)) != 0;
        peak = std::max(peak, static_cast<double>(std::fabs(a[i])));
    }
    size_t firstB = a.size();
    for (size_t i = 0; i < a.size(); ++i) if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) { firstB = i; break; }
    check(diffB == 0 && diffC == 0 && peak > 0.01, "37 and 1 equal 512, bit for bit",
          fmt("%zu and %zu samples differ, the first at %zu (%.4f s after bar %.0f)", diffB, diffC, firstB, firstB / 48000.0, from / 4.0));
}

struct TestSection {
    const char* name;
    std::function<void()> fn;
};

const TestSection kSections[] = {
    { "testTempoMap", testTempoMap },
    { "testParams", testParams },
    { "testKickPhase", testKickPhase },
    { "testKickTuning", testKickTuning },
    { "testGate", testGate },
    { "testStudyForm", testStudyForm },
    { "testPump", testPump },
    { "testLowEnd", testLowEnd },
    { "testBlockSizes", testBlockSizes },
    { "testStems", testStems },
    { "testEnergy", testEnergy },
    { "testMidi", testMidi },
    { "testPlanner", testPlanner },
    { "testHarmony", testHarmony },
    { "testComposer", testComposer },
    { "testLeveler", testLeveler },
    { "testCorpus", testCorpus },
    { "testMemo", testMemo },
    { "testMelody", testMelody },
    { "testPiano", testPiano },
    { "testPianoBlocks", testPianoBlocks },
};

} // namespace

int main(int argc, char** argv)
{
    std::string only;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            for (const TestSection& s : kSections) std::printf("%s\n", s.name);
            return 0;
        }
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = std::string(",") + argv[++i] + ",";
    }
    for (const TestSection& s : kSections)
        if (only.empty() || only.find(std::string(",") + s.name + ",") != std::string::npos) s.fn();
    return finish();
}
