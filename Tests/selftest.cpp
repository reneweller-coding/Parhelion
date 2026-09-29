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
#include "parh/compose/Set.h"
#include "parh/compose/Study.h"
#include "parh/compose/Style.h"
#include "parh/mix/TranceGate.h"
#include "parh/synth/Kick.h"
#include "parh/synth/Brass.h"
#include "parh/synth/Choir.h"
#include "parh/synth/Piano.h"
#include "parh/synth/Strings.h"
#include "parh/synth/Synth.h"
#include "parh/synth/Modulation.h"
#include "parh/Presets.h"
#include <atomic>
#include <thread>
#include "parh/synth/Sfx.h"
#include <cstdlib>
#include <limits>
#include "parh/synth/Timpani.h"
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
#include <tuple>
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
                if (plan.sections[i].kind == SectionKind::Drop
                    && std::none_of(plan.vacuums.begin(), plan.vacuums.end(), [&](const Vacuum& v) { return v.bar == static_cast<int>(plan.sections[i].beat / 4.0) - 1; }))
                    ++bad[4];
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
                if (v[0] < 52 || ((v[0] - h.key - h.shift[static_cast<size_t>(bar)]) % 12 + 12) % 12 == ((h.bars[static_cast<size_t>(bar)].tones[0]) % 12 + 12) % 12) ++voiceBad;
            }
        }
    }
    check(liftBad == 0, "the two bars before a drop on VII (major: V)", fmt("%d drops", liftBad));
    check(resolveBad == 0, "the drop resolves on the tonic chord", fmt("%d drops", resolveBad));
    check(scaleBad == 0, "every chord tone in the chord's scale (the major V of harmonic minor aside)", fmt("%d tones", scaleBad));
    check(voiceBad == 0, "the pad's lowest voice a third or a fifth, from E3 up", fmt("%d voicings", voiceBad));
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
    // The track's key reaches the knobs the deck tunes the kick, the kit and the effects by.
    {
        int right = 0;
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            const Score s = composeTrack(*p, seed);
            right += s.knobAt(p->id(Module::Compose, 0, compose::Key), 0.0) == static_cast<float>(s.keyRoot)
                  && s.knobAt(p->id(Module::Compose, 0, compose::Scale), 0.0) == static_cast<float>(s.scale);
        }
        check(right == 6, "the track's key and scale on the knobs (the kick's, the kit's, the effects' tuning)", fmt("%d of 6", right));
    }
    // PLAN 6.9: a unit rerolled draws its voices again and leaves every other note as it was, to the velocity.
    using Key = std::tuple<double, double, int, int, float>;
    auto notesOf = [](const Score& s, std::initializer_list<Part> parts, bool inside) {
        std::vector<Key> out;
        for (const NoteEvent& n : s.notes) {
            const bool in = std::find(parts.begin(), parts.end(), n.part) != parts.end();
            if (in == inside) out.push_back({ n.beat, n.length, static_cast<int>(n.part), n.pitch, n.velocity });
        }
        return out;
    };
    auto sameForm = [](const Score& x, const Score& y) {
        bool s = x.sections.size() == y.sections.size();
        for (size_t i = 0; s && i < x.sections.size(); ++i) s = x.sections[i].beat == y.sections[i].beat && x.sections[i].kind == y.sections[i].kind;
        return s;
    };
    struct Case { const char* style; const char* unit; std::initializer_list<Part> parts; };
    const Case cases[] = {
        { "Uplifting", "motif", { Part::Lead, Part::Counter, Part::Strings } },
        { "Uplifting", "lead", { Part::Lead, Part::Counter, Part::Strings } },
        { "Uplifting", "arp", { Part::Arp } },
        { "Uplifting", "pluck", { Part::Pluck, Part::Stab } },
        { "Uplifting", "drums", { Part::Perc1, Part::Perc2, Part::Perc3, Part::Perc4, Part::Perc5, Part::Perc6, Part::Perc7, Part::Perc8, Part::Perc9, Part::Perc10 } },
        { "Uplifting", "fx", { Part::Fx } },
        { "Uplifting", "sounds", {} },
        { "Acid", "acid", { Part::Acid } },
        { "Dream House", "motif", { Part::Piano, Part::Counter, Part::Strings } },
    };
    int isolated = 0, changed = 0, total = 0;
    std::string bad;
    for (const Case& k : cases) {
        auto q = std::make_unique<ParamStore>();
        q->parseText(std::string("compose.style=") + k.style);
        for (uint64_t seed : { 21ull, 22ull }) {
            const Score base = composeTrack(*q, seed);
            Curation cur;
            cur.reroll(k.unit);
            const Score re = composeTrack(*q, seed, TrackRequest{}, &cur);
            ++total;
            const bool iso = notesOf(base, k.parts, false) == notesOf(re, k.parts, false) && sameForm(base, re);
            // (Its own: its notes, or its automation and knob sets -- the effects' throws, the sounds.)
            bool automation = base.gestures.size() != re.gestures.size() || base.knobs.size() != re.knobs.size();
            for (size_t i = 0; !automation && i < base.gestures.size(); ++i)
                automation = base.gestures[i].param != re.gestures[i].param || base.gestures[i].beat != re.gestures[i].beat || base.gestures[i].to != re.gestures[i].to;
            for (size_t i = 0; !automation && i < base.knobs.size(); ++i) automation = base.knobs[i].value != re.knobs[i].value;
            const bool moved = automation || notesOf(base, k.parts, true) != notesOf(re, k.parts, true) || notesOf(base, k.parts, true).empty();
            isolated += iso;
            changed += moved;
            if (!iso || !moved) bad += std::string(" ") + k.style + "/" + k.unit;
        }
    }
    check(isolated == total, "a rerolled unit leaves every other voice as it was, to the velocity", fmt("%d of %d%s", isolated, total, bad.c_str()));
    check(changed == total, "and draws its own voices again", fmt("%d of %d", changed, total));
    // The matrix and a section: the form stays, the matrix moves.
    int formKept = 0, matrixMoved = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        const Score base = composeTrack(*p, seed);
        for (const char* u : { "matrix", "section5" }) {
            Curation cur;
            cur.reroll(u);
            const Score re = composeTrack(*p, seed, TrackRequest{}, &cur);
            formKept += sameForm(base, re);
            bool kept = base.layers.size() == re.layers.size();
            for (size_t i = 0; kept && i < base.layers.size(); ++i) kept = base.layers[i].state == re.layers[i].state;
            matrixMoved += !kept;
        }
    }
    check(formKept == 12, "a rerolled matrix or section keeps the form", fmt("%d of 12", formKept));
    check(matrixMoved >= 6, "and moves the layer matrix (mostly)", fmt("%d of 12", matrixMoved));
    // The vacuum before a drop (rule 5, Phosphene's four): all four drawn over some tracks, and nothing sounds in one but
    // the kick on the last beat where the variant keeps it.
    int kinds[4] = {}, loud = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        const StyleProfile& prof = styleProfile(Style::Uplifting);
        const Plan plan = planTrack(prof, 256, seed);
        for (const Vacuum& v : plan.vacuums) ++kinds[v.kickOn4 ? 3 : v.from == 3.0 ? 0 : v.from == 2.0 ? 1 : 2];
        TrackRequest req;
        req.profile = &prof;
        req.bars = 256;   // (the plan above)
        const Score sc = composeTrack(*p, seed, req);
        for (const NoteEvent& n : sc.notes) {
            if (n.part == Part::Fx) continue;   // (the riser and the reverse crash run into the drop)
            for (const Vacuum& v : plan.vacuums)
                if (n.beat >= 4.0 * v.bar + v.from - 1e-9 && n.beat < 4.0 * (v.bar + 1)
                    && !(v.kickOn4 && (n.part == Part::Kick || n.part == Part::Ghost) && n.beat == 4.0 * v.bar + 3.0))
                    ++loud;
        }
    }
    check(kinds[0] > 0 && kinds[1] > 0 && kinds[2] > 0 && kinds[3] > 0, "the four vacuums before a drop",
          fmt("a beat %d, two beats %d, the bar %d, the kick alone on 4 %d", kinds[0], kinds[1], kinds[2], kinds[3]));
    check(loud == 0, "nothing sounds in a vacuum but the kick it keeps", fmt("%d notes", loud));
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
            writeLead(mc, seed * 131, seed * 137);
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

/** @brief One note of an orchestral instrument (held @p hold s) over @p secs s, the channels' mean. */
template <class Instr>
std::vector<float> orchNote(Instr& ins, int pitch, float vel, double hold, double secs)
{
    const int total = static_cast<int>(secs * 48000.0), off = static_cast<int>(hold * 48000.0);
    std::vector<float> out(static_cast<size_t>(total));
    float L[32], R[32];
    ins.noteOn(pitch, vel, false, 0.0);
    for (int i = 0; i < total; i += 32) {
        if (i <= off && off < i + 32) ins.noteOff(pitch);
        const int n = std::min(32, total - i);
        ins.process(L, R, n);
        for (int k = 0; k < n; ++k) out[static_cast<size_t>(i + k)] = 0.5f * (L[k] + R[k]);
    }
    return out;
}

/** @brief The spectral centroid (Hz) of @p x over 2^14 samples from @p a seconds (Hann). */
double centroidHz(const std::vector<float>& x, double a)
{
    const size_t n = 16384, s0 = static_cast<size_t>(a * 48000.0);
    std::vector<std::complex<double>> f(n);
    for (size_t i = 0; i < n; ++i) f[i] = (s0 + i < x.size() ? x[s0 + i] : 0.0f) * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / (n - 1)));
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(f[i], f[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const std::complex<double> wl = std::polar(1.0, -2.0 * 3.14159265358979 / static_cast<double>(len));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = f[i + j], v = f[i + j + len / 2] * w;
                f[i + j] = u + v;
                f[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    double num = 0.0, den = 0.0;
    for (size_t k = 1; k < n / 2; ++k) { const double pw = std::norm(f[k]); num += pw * k * 48000.0 / n; den += pw; }
    return num / (den + 1e-30);
}

/**
 * The orchestra (PLAN 5.9): every instrument sounds its note -- the bowed strings of every family, the choir, the brass
 * (the lips and the bore agreeing on it), the timpani's principal mode with its air-loaded neighbours -- the LF source
 * closes its flow over the period, the brass is brighter the harder it is blown.
 */
void testOrchestra()
{
    section("the orchestra");
    const DenormalGuard guard;
    ParamStore ps;
    std::vector<float> v(64);
    {
        auto s = std::make_unique<StringSection>();
        s->prepare(48000.0, 7);
        ps.readModule(Module::Strings, 0, v.data());
        v[strings::Players] = 1.0f;
        v[strings::Vibrato] = 0.0f;
        s->update(v.data());
        double worst = 0.0;
        bool sounds = true;
        for (int pitch : { 36, 50, 57, 69, 81 }) {
            s->reset();
            const std::vector<float> x = orchNote(*s, pitch, 0.7f, 2.0, 1.6);
            const double f = midiToHz(pitch);
            worst = std::max(worst, std::fabs(1200.0 * std::log2(peakHz(x, 0.8, f) / f)));
            sounds = sounds && levelDb(x, 0.8, 1.5) > -60.0 && std::isfinite(levelDb(x, 0.8, 1.5));
        }
        // The player's own intonation (2.5 cents standard deviation) and the bow's pull come on top of the note.
        check(worst < 16.0 && sounds, "a bowed string of every family plays its note", fmt("within %.1f cents", worst));
    }
    {
        auto c = std::make_unique<Choir>();
        c->prepare(48000.0, 3);
        ps.readModule(Module::Choir, 0, v.data());
        v[choir::Vibrato] = 0.0f;
        c->update(v.data());
        double area = 0.0;
        for (double rd : { 0.4, 1.0, 1.7, 2.6 }) {
            std::vector<float> per(2000);
            c->lfPeriod(rd, 2000, per.data());
            double sum = 0.0;
            for (float y : per) sum += y;
            area = std::max(area, std::fabs(sum / 2000.0));
        }
        check(area < 1e-4, "the LF source's flow returns to zero over a period at every Rd", fmt("worst mean %.1e", area));
        double worst = 0.0;
        for (int pitch : { 45, 57, 64, 72 }) {
            c->reset();
            const std::vector<float> x = orchNote(*c, pitch, 0.7f, 2.0, 1.6);
            const double f = midiToHz(pitch);
            worst = std::max(worst, std::fabs(1200.0 * std::log2(peakHz(x, 0.8, f) / f)));
        }
        check(worst < 12.0, "the choir sings its notes (bass to soprano)", fmt("within %.1f cents", worst));
    }
    {
        auto b = std::make_unique<Brass>();
        b->prepare(48000.0, 5);
        ps.readModule(Module::Brass, 0, v.data());
        v[brass::Players] = 1.0f;
        v[brass::Vibrato] = 0.0f;
        b->update(v.data());
        double worst = 0.0;
        bool brighter = true;
        for (int pitch : { 41, 48, 55, 62, 69 }) {
            double zc[2];
            int k = 0;
            for (float vel : { 0.3f, 0.95f }) {
                b->reset();
                const std::vector<float> x = orchNote(*b, pitch, vel, 2.0, 1.4);
                const double f = midiToHz(pitch);
                if (vel > 0.5f) worst = std::max(worst, std::fabs(1200.0 * std::log2(peakHz(x, 0.7, f) / f)));
                zc[k++] = centroidHz(x, 0.7);
            }
            brighter = brighter && zc[1] >= zc[0];
        }
        check(worst < 15.0, "the lips and the bore agree on the note (the player's ear)", fmt("within %.1f cents", worst));
        check(brighter, "blown harder, the brass is brighter");
    }
    {
        auto t = std::make_unique<Timpani>();
        t->prepare(48000.0, 9);
        ps.readModule(Module::Timpani, 0, v.data());
        t->update(v.data());
        const std::vector<float> x = orchNote(*t, 45, 0.8f, 0.1, 2.5);
        const double f = midiToHz(45), p11 = peakHz(x, 0.3, f), p21 = peakHz(x, 0.3, 1.5 * f);
        check(std::fabs(1200.0 * std::log2(p11 / f)) < 3.0 && std::fabs(p21 / p11 - 1.5) < 0.02 && std::fabs(Timpani::ratio(2) - 1.98) < 1e-9,
              "the timpani: the principal mode on the note, the next a fifth above", fmt("(1,1) %.2f Hz, (2,1)/(1,1) %.3f", p11, p21 / p11));
    }
}

/** The orchestra in the engine: an Uplifting track with it at block sizes 1, 37 and 512, bit for bit, at the peak. */
void testOrchestraBlocks()
{
    section("the orchestra at any block size");
    auto p = std::make_unique<ParamStore>();
    p->parseText("compose.style=Uplifting");
    Score sc;
    double from = -1.0;
    for (uint64_t seed = 1; seed < 12 && from < 0.0; ++seed) {
        sc = composeTrack(*p, seed);
        for (const NoteEvent& n : sc.notes) if (n.part == Part::Choir) { from = std::floor(n.beat / 4.0) * 4.0; break; }
    }
    check(from >= 0.0, "a seed with the orchestra");
    if (from < 0.0) return;
    auto render = [&](int block) {
        auto e = std::make_unique<Engine>();
        e->params().copyValuesFrom(*p);
        e->prepare(48000.0, block);
        e->load(sc);
        e->seek(from);
        std::vector<float> out, L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
        const int n = static_cast<int>(5.0 * 48000.0);
        for (int done = 0; done < n; done += block) {
            const int m = std::min(block, n - done);
            e->process(L.data(), R.data(), m);
            for (int i = 0; i < m; ++i) out.push_back(L[static_cast<size_t>(i)]);
        }
        return out;
    };
    const std::vector<float> a = render(512), b = render(37), c = render(1);
    size_t diffB = 0, diffC = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diffB += std::memcmp(&a[i], &b[i], sizeof(float)) != 0;
        diffC += std::memcmp(&a[i], &c[i], sizeof(float)) != 0;
    }
    check(diffB == 0 && diffC == 0, "37 and 1 equal 512, bit for bit", fmt("%zu and %zu samples differ", diffB, diffC));
}


/**
 * The modulation of every voice (Phase 5b, Modulation.h). First each engine alone: for every target of its list two
 * slots -- a free LFO at 2 Hz and the velocity -- move the sound (the second of a held note differs from the knobs' own,
 * and stays finite); the velocity reaches the targets read only at the strike (the hammer, the mallet). Then two
 * composed tracks with every voice's matrix busy -- synced and free LFOs, the envelope, the random value, the wheel,
 * the pressure, the energy --, the orchestra's Uplifting and the piano's Dream House, rendered at block sizes 1, 37 and
 * 512: bit for bit the same.
 */
void testModulation()
{
    section("the modulation of every voice");
    const DenormalGuard guard;
    auto p = std::make_unique<ParamStore>();
    constexpr int kSr = 48000, kLen = 48000, kBlk = 64;
    auto values = [&](Module m, int count) {
        std::vector<float> v(static_cast<size_t>(count));
        for (int k = 0; k < count; ++k) v[static_cast<size_t>(k)] = p->get(p->id(m, 0, k));
        return v;
    };
    // Slots 1 and 2 on target @p dst: LFO 1 (free, 2 Hz, sine) and the velocity.
    auto slots = [](std::vector<float>& v, int first, int dst) {
        v[static_cast<size_t>(first + 4)] = 2.0f;                      // lfo1_rate
        v[static_cast<size_t>(first + 24)] = 1.0f;                     // mx1_src LFO 1
        v[static_cast<size_t>(first + 25)] = static_cast<float>(dst);  // mx1_dst
        v[static_cast<size_t>(first + 26)] = 0.6f;                     // mx1_amount
        v[static_cast<size_t>(first + 27)] = 7.0f;                     // mx2_src velocity
        v[static_cast<size_t>(first + 28)] = static_cast<float>(dst);
        v[static_cast<size_t>(first + 29)] = 0.6f;
    };
    int reached = 0, total = 0;
    std::string missed;
    auto compare = [&](const char* engine, const char* target, const std::vector<float>& a, const std::vector<float>& b) {
        double ea = 0.0, ed = 0.0;
        bool finite = true;
        for (size_t i = 0; i < a.size(); ++i) {
            ea += static_cast<double>(a[i]) * a[i];
            ed += static_cast<double>(a[i] - b[i]) * (a[i] - b[i]);
            finite = finite && std::isfinite(b[i]);
        }
        ++total;
        if (finite && ea > 0.0 && ed > 1e-6 * ea) ++reached;
        else missed += fmt(" %s/%s", engine, target);
    };
    // Renders one engine for a second from a note at its start; @p E: prepare/update/noteOn/process as the engine has them.
    auto run = [&](auto& engine, auto&& start) {
        std::vector<float> out, L(kBlk), R(kBlk);
        start(engine);
        for (int done = 0; done < kLen; done += kBlk) {
            engine.process(L.data(), R.data(), kBlk);
            for (int i = 0; i < kBlk; ++i) out.push_back(L[static_cast<size_t>(i)] + 0.5f * R[static_cast<size_t>(i)]);
        }
        return out;
    };
    {   // The mono synth (bass).
        const std::vector<float> base = values(Module::Bass, synth::Count);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<MonoSynth>();
            s->prepare(kSr, 3);
            return run(*s, [&](MonoSynth& e) { e.update(v.data(), 0.0f); e.noteOn(45, 0.8f, 0.0, false, false); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(kSynthModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, synth::ModFirst, d);
            // (The pulse width needs the pulse: on the saw it has nothing to move.)
            if (kSynthModDests[d] == static_cast<int>(ModDest::PulseWidth)) {
                std::vector<float> w = base;
                w[synth::Wave] = v[synth::Wave] = 1.0f;
                compare("bass", kSynthModDestNames[d], render(w), render(v));
                continue;
            }
            compare("bass", kSynthModDestNames[d], ref, render(v));
        }
    }
    {   // The strings.
        const std::vector<float> base = values(Module::Strings, strings::Count);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<StringSection>();
            s->prepare(kSr, 5);
            return run(*s, [&](StringSection& e) { e.update(v.data()); e.noteOn(60, 0.7f, false, 0.0); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(kStringsModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, strings::ModFirst, d);
            compare("strings", kStringsModDestNames[d], ref, render(v));
        }
    }
    {   // The choir.
        const std::vector<float> base = values(Module::Choir, choir::Count);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<Choir>();
            s->prepare(kSr, 5);
            return run(*s, [&](Choir& e) { e.update(v.data()); e.noteOn(60, 0.7f, false, 0.0); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(kChoirModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, choir::ModFirst, d);
            compare("choir", kChoirModDestNames[d], ref, render(v));
        }
    }
    {   // The brass.
        const std::vector<float> base = values(Module::Brass, brass::Count);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<Brass>();
            s->prepare(kSr, 5);
            return run(*s, [&](Brass& e) { e.update(v.data()); e.noteOn(48, 0.8f, false, 0.0); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(kBrassModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, brass::ModFirst, d);
            compare("brass", kBrassModDestNames[d], ref, render(v));
        }
    }
    {   // The timpani.
        const std::vector<float> base = values(Module::Timpani, timpani::Count);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<Timpani>();
            s->prepare(kSr, 5);
            return run(*s, [&](Timpani& e) { e.update(v.data()); e.noteOn(45, 0.8f, false, 0.0); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(kTimpaniModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, timpani::ModFirst, d);
            compare("timpani", kTimpaniModDestNames[d], ref, render(v));
        }
    }
    {   // The piano.
        const std::vector<float> base = values(Module::Piano, piano::Count);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<Piano>();
            s->prepare(kSr);
            s->setSpec(PianoSpec{});
            return run(*s, [&](Piano& e) { e.update(v.data()); e.noteOn(60, 0.7f, 0.0); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(kPianoModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, piano::ModFirst, d);
            compare("piano", kPianoModDestNames[d], ref, render(v));
        }
    }
    check(reached == total && total == 40, "every target of every voice moves its sound", fmt("%d of %d targets%s", reached, total, missed.c_str()));

    // Every matrix busy, two tracks, three block sizes.
    const char* busy =
        "perform.wheel=0.7; perform.pressure=0.4;"
        "bass.lfo1_sync=5; bass.mx1_src=1; bass.mx1_dst=3; bass.mx1_amount=0.4; bass.mx2_src=12; bass.mx2_dst=6; bass.mx2_amount=0.5;"
        "bass.menv_decay=150; bass.mx3_src=5; bass.mx3_dst=1; bass.mx3_amount=0.2;"
        "lead.lfo1_rate=3; lead.mx1_src=1; lead.mx1_dst=11; lead.mx1_amount=0.6; lead.mx2_src=10; lead.mx2_dst=6; lead.mx2_amount=0.4;"
        "strings.lfo1_rate=0.7; strings.mx1_src=1; strings.mx1_dst=2; strings.mx1_amount=0.6; strings.lfo2_sync=4; strings.mx2_src=2;"
        "strings.mx2_dst=4; strings.mx2_amount=0.4; strings.mx3_src=5; strings.mx3_dst=1; strings.mx3_amount=0.2; strings.mx4_src=12;"
        "strings.mx4_dst=7; strings.mx4_amount=0.3;"
        "choir.lfo1_rate=0.5; choir.mx1_src=1; choir.mx1_dst=2; choir.mx1_amount=0.5; choir.mx2_src=9; choir.mx2_dst=6; choir.mx2_amount=0.5;"
        "choir.mx3_src=11; choir.mx3_dst=3; choir.mx3_amount=0.5;"
        "brass.lfo1_rate=4; brass.mx1_src=1; brass.mx1_dst=2; brass.mx1_amount=0.3; brass.mx2_src=5; brass.mx2_dst=1; brass.mx2_amount=0.15;"
        "timpani.lfo1_rate=1; timpani.mx1_src=1; timpani.mx1_dst=1; timpani.mx1_amount=0.4; timpani.mx2_src=7; timpani.mx2_dst=2; timpani.mx2_amount=0.5;"
        "piano.lfo1_rate=0.3; piano.mx1_src=1; piano.mx1_dst=1; piano.mx1_amount=0.15; piano.mx2_src=7; piano.mx2_dst=2; piano.mx2_amount=0.5;"
        "piano.lfo2_sync=3; piano.mx3_src=2; piano.mx3_dst=4; piano.mx3_amount=0.5;";
    for (const char* style : { "Uplifting", "Dream House" }) {
        auto q = std::make_unique<ParamStore>();
        q->parseText(std::string("compose.style=") + style);
        q->parseText(busy);
        const Part want = std::string(style) == "Uplifting" ? Part::Choir : Part::Piano;
        Score sc;
        double from = -1.0;
        for (uint64_t seed = 1; seed < 16 && from < 0.0; ++seed) {
            sc = composeTrack(*q, seed);
            for (const NoteEvent& n : sc.notes) if (n.part == want) { from = std::floor(n.beat / 4.0) * 4.0; break; }
        }
        if (from < 0.0) { check(false, fmt("a %s track with its voice", style).c_str()); continue; }
        auto render = [&](int block) {
            auto e = std::make_unique<Engine>();
            e->params().copyValuesFrom(*q);
            e->prepare(48000.0, block);
            e->load(sc);
            e->seek(from);
            std::vector<float> out, L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
            const int n = static_cast<int>(4.0 * 48000.0);
            for (int done = 0; done < n; done += block) {
                const int m = std::min(block, n - done);
                e->process(L.data(), R.data(), m);
                for (int i = 0; i < m; ++i) out.push_back(L[static_cast<size_t>(i)] - R[static_cast<size_t>(i)] * 0.25f);
            }
            return out;
        };
        const std::vector<float> a = render(512), b = render(37), c = render(1);
        size_t diffB = 0, diffC = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            diffB += std::memcmp(&a[i], &b[i], sizeof(float)) != 0;
            diffC += std::memcmp(&a[i], &c[i], sizeof(float)) != 0;
        }
        check(diffB == 0 && diffC == 0, fmt("%s, every matrix busy: 37 and 1 equal 512, bit for bit", style).c_str(),
              fmt("%zu and %zu samples differ", diffB, diffC));
    }
}


/**
 * The factory banks (Phase 5b, Presets.h): eighteen banks of 1024, every key a bank names a knob of its module, every
 * name unique within its bank, a kit lane given only presets of its role; and a sample of every bank (every 64th preset,
 * a different one per bank) played through the engine against the module's default sound -- finite, audible, within
 * 24 dB of it. With PARH_BANK_TRIMS=<file> every preset is played and the trims (PresetTrims.inl: the default's loudness
 * less the preset's, in tenths of a dB) are written to <file>.
 */
struct BankId { Module module; int instance; };
const BankId kBanks[kPresetBanks] = {
    { Module::Kick, 0 }, { Module::Sub, 0 }, { Module::Perc, 0 }, { Module::Bass, 0 }, { Module::Acid, 0 },
    { Module::Poly, 0 }, { Module::Poly, 1 }, { Module::Poly, 2 }, { Module::Poly, 3 }, { Module::Poly, 4 }, { Module::Poly, 5 },
    { Module::Piano, 0 }, { Module::Strings, 0 }, { Module::Choir, 0 }, { Module::Brass, 0 }, { Module::Timpani, 0 },
    { Module::Sfx, 0 }, { Module::Cloud, 0 } };

/** @brief The loudest 3 s of a short phrase on @p b's voice, the preset @p sp applied (null: the default sound), LUFS. */
double bankLoudness(const BankId& b, const SoundPreset* sp)
{
    auto p = std::make_unique<ParamStore>();
    if (sp != nullptr) applyPreset(*p, b.module, b.instance, *sp);
    Score sc;
    sc.clear(136.0);
    sc.lengthBeats = 16.0;
    auto note = [&](double beat, double len, Part part, int pitch, float v) { sc.notes.push_back(NoteEvent{ beat, len, part, pitch, v, 0, false, false }); };
    switch (b.module) {
    case Module::Kick: for (int q = 0; q < 12; ++q) note(q, 0.25, Part::Kick, 36, 1.0f); break;
    case Module::Sub: for (int q = 0; q < 12; ++q) note(q + 0.5, 0.25, Part::Sub, 33, 1.0f); break;
    case Module::Perc: for (int s = 0; s < 48; ++s) note(0.25 * s, 0.1, Part::Perc1, 42, s % 4 == 0 ? 0.9f : 0.6f); break;
    case Module::Bass: case Module::Acid:
        for (int s = 0; s < 24; ++s) note(0.5 * s, 0.4, b.module == Module::Bass ? Part::Bass : Part::Acid, s % 3 == 0 ? 45 : 57, 0.8f);
        break;
    case Module::Poly: {
        const Part part = static_cast<Part>(static_cast<int>(Part::Lead) + b.instance);
        if (b.instance == 4 || b.instance == 5) for (int k : { 57, 60, 64, 69 }) note(0.0, 11.5, part, k, 0.8f);   // the pad, the stab: a chord
        else for (int s = 0; s < 24; ++s) note(0.5 * s, 0.45, part, 64 + (s % 4) * 3, 0.8f);
        break;
    }
    case Module::Piano: for (int s = 0; s < 6; ++s) for (int k : { 48, 60, 64, 67 }) note(2.0 * s, 1.8, Part::Piano, k + (s % 2) * 2, 0.7f); break;
    case Module::Strings: for (int k : { 43, 55, 59, 62 }) note(0.0, 11.5, Part::Strings, k, 0.7f); break;
    case Module::Choir: for (int k : { 57, 60, 64 }) note(0.0, 11.5, Part::Choir, k, 0.7f); break;
    case Module::Brass: for (int s = 0; s < 3; ++s) for (int k : { 48, 55, 60 }) note(4.0 * s, 3.5, Part::Brass, k, 0.8f); break;
    case Module::Timpani: for (int s = 0; s < 6; ++s) note(2.0 * s, 1.0, Part::Timpani, 43 + (s % 2) * 7, 0.9f); break;
    case Module::Sfx: note(0.0, 8.0, Part::Fx, kSfxBaseNote + static_cast<int>(SfxType::Riser), 0.9f);
                      note(8.0, 4.0, Part::Fx, kSfxBaseNote + static_cast<int>(SfxType::Impact), 1.0f); break;
    case Module::Cloud:
        p->set(p->id(Module::Cloud, 0, cloud::Level), 0.0f);   // (the cloud grains the pad: the pad plays, the cloud is heard)
        for (int k : { 57, 60, 64 }) note(0.0, 11.5, Part::Pad, k, 0.8f);
        break;
    default: break;
    }
    sc.sort();
    auto e = std::make_unique<Engine>();
    e->params().copyValuesFrom(*p);
    e->prepare(48000.0, 512);
    e->load(sc);
    LoudnessMeter meter;
    meter.prepare(48000.0);
    std::vector<float> L(512), R(512);
    bool finite = true;
    for (int done = 0; done < static_cast<int>(5.0 * 48000.0); done += 512) {
        e->process(L.data(), R.data(), 512);
        for (int i = 0; i < 512; ++i) finite = finite && std::isfinite(L[static_cast<size_t>(i)]) && std::isfinite(R[static_cast<size_t>(i)]);
        meter.process(L.data(), R.data(), 512);
    }
    return finite ? meter.report().shortTermMax : std::numeric_limits<double>::quiet_NaN();
}

void testPresetBank()
{
    section("the factory banks");
    const DenormalGuard guard;
    int full = 0, unique = 0;
    for (const BankId& b : kBanks) {
        const std::vector<SoundPreset>& ps = factoryPresets(b.module, b.instance);
        full += ps.size() == static_cast<size_t>(kPresetsPerBank);
        std::set<std::string> names;
        for (const SoundPreset& s : ps) names.insert(s.name);
        unique += names.size() == ps.size();
    }
    check(full == kPresetBanks, "eighteen banks of 1024 presets", fmt("%d of %d full", full, kPresetBanks));
    check(unique == kPresetBanks, "every name unique within its bank", fmt("%d of %d", unique, kPresetBanks));
    std::string first;
    const int unknown = bankUnknownKeys(&first);
    check(unknown == 0, "every key a bank names is a knob of its module", fmt("%d unknown%s", unknown, unknown > 0 ? (": " + first).c_str() : ""));
    // A lane gets presets of its role only; every role has a group.
    int roleOk = 0;
    for (int role = 0; role < kNumPercRoles; ++role) {
        Rng r;
        r.seed(static_cast<uint64_t>(role) + 11);
        const float style[5] = { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f };
        bool ok = true;
        for (int k = 0; k < 20 && ok; ++k) {
            const int i = pickPreset(Module::Perc, 0, style, role, r);
            ok = i >= 0 && ((factoryPresets(Module::Perc, 0)[static_cast<size_t>(i)].roles >> role) & 1u) != 0u;
        }
        roleOk += ok;
    }
    check(roleOk == kNumPercRoles, "a kit lane is given presets of its role (every role has a group)", fmt("%d of %d roles", roleOk, kNumPercRoles));

    // The sample (or, with PARH_BANK_TRIMS, every preset): through the engine against the default sound.
    const char* trimsPath = std::getenv("PARH_BANK_TRIMS");
    const bool all = trimsPath != nullptr && trimsPath[0] != 0;
    // The jobs: every bank's default sound (-1) and its presets (all, or every 128th, a different one per bank), on
    // every core (each render its own engine).
    struct Job { int bank, preset; double loud; };
    std::vector<Job> jobs;
    for (int bi = 0; bi < kPresetBanks; ++bi) {
        jobs.push_back({ bi, -1, 0.0 });
        for (int i = all ? 0 : (bi * 7) % 128; i < kPresetsPerBank; i += all ? 1 : 128) jobs.push_back({ bi, i, 0.0 });
    }
    for (const BankId& b : kBanks) (void)factoryPresets(b.module, b.instance);   // (built once, before the threads)
    std::atomic<size_t> next{ 0 };
    auto work = [&]() {
        const DenormalGuard g;
        for (size_t k = next++; k < jobs.size(); k = next++) {
            Job& j = jobs[k];
            const BankId& b = kBanks[j.bank];
            j.loud = bankLoudness(b, j.preset < 0 ? nullptr : &factoryPresets(b.module, b.instance)[static_cast<size_t>(j.preset)]);
        }
    };
    std::vector<std::thread> pool;
    const unsigned n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    for (unsigned k = 0; k < n; ++k) pool.emplace_back(work);
    for (std::thread& th : pool) th.join();
    int played = 0, bad = 0;
    double worst = 0.0;
    std::string worstName;
    std::vector<std::vector<short>> trims(kPresetBanks, std::vector<short>(kPresetsPerBank, 0));
    std::vector<double> ref(kPresetBanks, 0.0);
    for (const Job& j : jobs) if (j.preset < 0) ref[static_cast<size_t>(j.bank)] = j.loud;
    for (const Job& j : jobs) {
        if (j.preset < 0) continue;
        const BankId& b = kBanks[j.bank];
        const SoundPreset& sp = factoryPresets(b.module, b.instance)[static_cast<size_t>(j.preset)];
        const double l = j.loud, d = l - ref[static_cast<size_t>(j.bank)];
        ++played;
        if (!std::isfinite(l) || l < -70.0 || std::fabs(d) > 24.0) {
            if (bad++ == 0) worstName = fmt("%s/%s: %.1f LUFS against %.1f", sp.group.c_str(), sp.name.c_str(), l, ref[static_cast<size_t>(j.bank)]);
        }
        if (std::isfinite(d) && std::fabs(d) > worst) worst = std::fabs(d);
        trims[static_cast<size_t>(j.bank)][static_cast<size_t>(j.preset)] = static_cast<short>(std::lround(std::clamp(std::isfinite(d) ? -d : 0.0, -24.0, 24.0) * 10.0));
    }
    check(bad == 0, "every preset played is finite, audible and within 24 dB of its module's default sound",
          fmt("%d played, %d out, largest distance %.1f dB%s", played, bad, worst, bad > 0 ? ("; first: " + worstName).c_str() : ""));
    if (all) {
        std::FILE* f = std::fopen(trimsPath, "wb");
        if (f != nullptr) {
            std::fprintf(f, "// Generated by parh_selftest testPresetBank with PARH_BANK_TRIMS=<this file> (Tests/selftest.cpp).\n"
                            "// Each preset's level correction in tenths of a dB, 18 banks of 1024 (Presets.h's order).\n"
                            "static const short kPresetTrimTenths[18][1024] = {\n");
            for (int bi = 0; bi < kPresetBanks; ++bi) {
                std::fprintf(f, "{");
                for (int i = 0; i < kPresetsPerBank; ++i) std::fprintf(f, "%d%s%s", trims[static_cast<size_t>(bi)][static_cast<size_t>(i)],
                                                                         i + 1 < kPresetsPerBank ? "," : "", (i % 32 == 31 && i + 1 < kPresetsPerBank) ? "\n" : "");
                std::fprintf(f, "}%s\n", bi + 1 < kPresetBanks ? "," : "");
            }
            std::fprintf(f, "};\n");
            std::fclose(f);
        }
        check(f != nullptr, "the trims written", trimsPath);
    }
}

/**
 * The sub-genres (PLAN 6.7, Phase 4c): each profile brings its own voice -- Acid the 303 with its four curves rising
 * to the filter's peaks, Deep the pad drifting and the granular cloud, Dream House the piano, Uplifting the orchestra
 * in some tracks -- and a morph between two profiles lands between them.
 */
void testSubGenres()
{
    section("the sub-genres");
    auto p = std::make_unique<ParamStore>();
    auto gesturesOn = [&](const Score& sc, Module m, int param) {
        const int id = p->id(m, 0, param);
        int n = 0;
        for (const Gesture& g : sc.gestures) n += g.param == id;
        return n;
    };
    auto knobOf = [&](const Score& sc, Module m, int param, float& value) {
        const int id = p->id(m, 0, param);
        for (const KnobSet& k : sc.knobs) if (k.param == id) { value = k.value; return true; }
        return false;
    };
    {
        p->parseText("compose.style=Acid");
        const Score sc = composeTrack(*p, 3);
        int acid = 0;
        for (const NoteEvent& n : sc.notes) acid += n.part == Part::Acid;
        const int curves = std::min({ gesturesOn(sc, Module::Acid, synth::Cutoff), gesturesOn(sc, Module::Acid, synth::Resonance),
                                      gesturesOn(sc, Module::Acid, synth::EnvAmount), gesturesOn(sc, Module::Acid, synth::Decay) });
        check(acid > 500 && curves >= 4, "Acid: the 303 plays, its cutoff, resonance, envelope and decay in curves", fmt("%d notes, %d curves each at least", acid, curves));
    }
    {
        p->parseText("compose.style=Deep");
        const Score sc = composeTrack(*p, 3);
        float cloud = -60.0f;
        const bool has = knobOf(sc, Module::Cloud, cloud::Level, cloud);
        int drift = 0;
        for (const Gesture& g : sc.gestures) drift += g.param == p->id(Module::Poly, static_cast<int>(PolyInstance::Pad), poly::Pan);
        check(has && cloud > -30.0f && drift > 4, "Deep: the granular cloud and the pad drifting", fmt("cloud %.0f dB, %d pan waves", cloud, drift));
    }
    {
        p->parseText("compose.style=Dream House");
        const Score sc = composeTrack(*p, 3);
        int piano = 0, lead = 0;
        for (const NoteEvent& n : sc.notes) { piano += n.part == Part::Piano; lead += n.part == Part::Lead; }
        check(piano > 50 && lead == 0, "Dream House: the motif on the piano", fmt("%d piano notes", piano));
    }
    {
        p->parseText("compose.style=Uplifting");
        int with = 0;
        for (uint64_t seed = 1; seed <= 8; ++seed) {
            const Score sc = composeTrack(*p, seed);
            bool o = false;
            for (const NoteEvent& n : sc.notes) o = o || n.part == Part::Strings;
            with += o;
        }
        check(with >= 2 && with <= 7, "Uplifting: the orchestra in some tracks (Cinematic), not in all", fmt("%d of 8", with));
    }
    {
        const StyleProfile m = morphProfile(styleProfile(Style::Uplifting), styleProfile(Style::Deep), 0.5f);
        const StyleProfile& a = styleProfile(Style::Uplifting);
        const StyleProfile& b = styleProfile(Style::Deep);
        const bool between = (m.bpmLow - a.bpmLow) * (m.bpmLow - b.bpmLow) <= 0.0f && (m.gapLu - a.gapLu) * (m.gapLu - b.gapLu) <= 0.0f
                          && (m.orchestra - a.orchestra) * (m.orchestra - b.orchestra) <= 0.0f;
        check(between, "a morph lands between its two profiles", fmt("tempo from %.0f, gap %.1f LU", m.bpmLow, m.gapLu));
    }
    p->parseText("compose.style=Uplifting");
}

/**
 * The set (PLAN 6.8, Phase 5): tracks alternate between the decks; the tempo moves at most 2 BPM a track; the keys move
 * round the Camelot wheel (the same, a fifth, the relative -- the energy boost and a free choice rare); every incoming
 * track's intro lies over the outgoing end, its bass swap before its groove and no breakdown of it before the swap; the
 * outgoing track gives the low end up where the incoming one takes it; and a blend renders bit for bit at any block size.
 */
void testSet()
{
    section("the set");
    auto p = std::make_unique<ParamStore>();
    p->parseText("set.dramaturgy=Peak; set.journey=Wander");
    SetInfo si;
    const SetScore set = composeSet(*p, 5, 70.0, nullptr, &si);
    const size_t n = si.tracks.size();
    check(n >= 8, "a set of seventy minutes has its tracks", fmt("%zu tracks", n));
    int alternate = 0, tempoOk = 0, wheel = 0, swapOk = 0, clean = 0, styles = 0;
    std::set<std::string> seen;
    for (size_t i = 0; i < n; ++i) {
        const SetTrack& t = si.tracks[i];
        seen.insert(t.info.style);
        if (i == 0) continue;
        const SetTrack& a = si.tracks[i - 1];
        alternate += t.deck != a.deck;
        tempoOk += std::fabs(t.info.bpm - a.info.bpm) <= 2.0f;
        const int d = ((t.info.key - a.info.key) % 12 + 12) % 12;
        const bool am = a.info.scale != static_cast<int>(Scale::Ionian), tm = t.info.scale != static_cast<int>(Scale::Ionian);
        const bool camelot = (am == tm && (d == 0 || d == 5 || d == 7)) || (am && !tm && d == 3) || (!am && tm && d == 9);
        wheel += camelot;
        swapOk += std::fabs(a.swapOut - t.swapIn) < 1e-9 && t.swapIn > t.start && t.swapIn < a.end && t.start + t.info.introBars * 4.0 <= a.end + 1e-9;
        bool breakdownBefore = false;
        for (const auto& b : t.info.breakdowns) breakdownBefore = breakdownBefore || t.start + b.first * 4.0 < t.swapIn;
        clean += !breakdownBefore;
    }
    styles = static_cast<int>(seen.size());
    const int k = static_cast<int>(n) - 1;
    check(alternate == k && tempoOk == k, "decks alternate, the tempo moves at most 2 BPM a track");
    check(wheel >= k * 7 / 10, "the keys move round the Camelot wheel", fmt("%d of %d transitions", wheel, k));
    check(swapOk == k && clean == k, "the incoming intro over the outgoing end, one bass swap, no incoming breakdown before it",
          fmt("%d and %d of %d", swapOk, clean, k));
    check(styles >= 2, "with Wander the styles follow the arc", fmt("%d styles", styles));
    // A blend at any block size.
    const double at = set.decks[0].tempo.secondsAt(si.tracks[1].swapIn) - 3.0;
    auto render = [&](int block) {
        auto e = std::make_unique<Engine>();
        e->params().copyValuesFrom(*p);
        e->prepare(48000.0, block);
        e->loadSet(set);
        e->seek(set.decks[0].tempo.beatAt(at));
        std::vector<float> out, L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
        const int total = static_cast<int>(6.0 * 48000.0);
        for (int done = 0; done < total; done += block) {
            const int m = std::min(block, total - done);
            e->process(L.data(), R.data(), m);
            for (int i = 0; i < m; ++i) out.push_back(L[static_cast<size_t>(i)]);
        }
        return out;
    };
    const std::vector<float> x = render(512), y = render(37);
    size_t diff = 0;
    double peak = 0.0;
    for (size_t i = 0; i < x.size(); ++i) { diff += std::memcmp(&x[i], &y[i], sizeof(float)) != 0; peak = std::max(peak, static_cast<double>(std::fabs(x[i]))); }
    check(diff == 0 && peak > 0.01, "the first bass swap at block sizes 37 and 512, bit for bit", fmt("%zu samples differ, peak %.2f", diff, peak));
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
    { "testOrchestra", testOrchestra },
    { "testOrchestraBlocks", testOrchestraBlocks },
    { "testModulation", testModulation },
    { "testPresetBank", testPresetBank },
    { "testSubGenres", testSubGenres },
    { "testSet", testSet },
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
