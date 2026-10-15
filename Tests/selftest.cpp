/**
 * @file selftest.cpp
 * @brief parh_selftest: every building block measured against an independently derived value.
 *
 * Sections are registered in the table at the bottom; `parh_selftest --list` prints their names (ctest registers one
 * test per name) and `--only a,b` runs the named ones. Only checks that protect something real belong here (the
 * user's rule for Ephemeris, 24.09.2026).
 * @note The frame, the tempo-map, kick and block-size tests and their helpers are copied from Totality `Tests/selftest.cpp`
 *       at 4d3c0d2 (29.09.2026); the rest is Parhelion's.
 * @author Rene Weller
 */
#include "parh/Clock.h"
#include "parh/Engine.h"
#include "parh/Handover.h"
#include "parh/Loudness.h"
#include "parh/Midi.h"
#include "parh/NoteTap.h"
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

constexpr double PiD = 3.141592653589793;   ///< pi

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
    for (int i = 0; i < PolyInstances; ++i) named = named && p->find(std::string(PolyInstanceNames[i]) + ".detune") >= 0;
    check(named, "the polyphonic voices have their names (lead.detune .. stab.detune)");
    check(p->getInt(p->find("lead.osc")) == static_cast<int>(PolyOscE::SUPERSAW) && p->getInt(p->find("lead.osc2")) == static_cast<int>(PolyOsc2E::SUPERSAW)
          && p->getInt(p->find("lead.osc2_interval")) == static_cast<int>(PolyOsc2IntervalE::OCTAVE_DOWN),
          "the lead: a supersaw with a second one an octave down (Dok. 5)");
    check(p->getInt(p->find("pluck.osc")) == static_cast<int>(PolyOscE::VA) && p->get(p->find("pluck.amp_sustain")) == 0.0f,
          "the pluck: VA, no sustain");
    check(p->getInt(p->find("perc2.role")) == static_cast<int>(PercRoleE::OPEN_HAT) && p->getInt(p->find("perc3.role")) == static_cast<int>(PercRoleE::CLAP)
          && p->getInt(p->find("perc6.role")) == static_cast<int>(PercRoleE::CRASH), "the kit's roles");
    p->parseText("compose.key=F; compose.style=Dream House; lead.gate_pattern=Trance 1");
    check(p->getInt(p->find("compose.key")) == 5 && p->getInt(p->find("compose.style")) == static_cast<int>(StyleE::DREAM_HOUSE)
          && p->getInt(p->find("lead.gate_pattern")) == 6, "choices by name");
    const std::string text = p->toText(true);
    auto q = std::make_unique<ParamStore>();
    q->parseText(text);
    bool same = true;
    for (int i = 0; i < p->count(); ++i) same = same && p->get(i) == q->get(i);
    check(same, "text form round-trips every value", fmt("%d parameters", p->count()));
}

/**
 * Renders a kick triggered at @p late with the store's kick settings; returns the output.
 * @param[in]     p        the parameters
 * @param[in]     keyRoot  the key's root
 * @param[in]     late     samples after the ideal start
 * @param[in]     n        the samples rendered
 * @param[in,out] k        the kick
 * @return the output
 */
std::vector<float> renderKick(const ParamStore& p, int keyRoot, double late, int n, Kick& k)
{
    float v[64];
    p.readModule(ModuleE::KICK, 0, v);
    Kick::constrain(v, 0.0, keyRoot);
    k.prepare(48000.0);
    k.update(v, keyRoot);
    k.trigger(1.0f, late);
    std::vector<float> out(static_cast<size_t>(n)), b(static_cast<size_t>(n));
    k.process(out.data(), b.data(), n);
    return out;
}

/**
 * Phase of @p x at @p hz over [from, from + len) samples, relative to the sample grid's t = 0, in cycles.
 * @param[in] x          the signal
 * @param[in] hz         the frequency in Hz
 * @param[in] from       the first sample
 * @param[in] len        how many samples
 * @param[in] sr         the sample rate in Hz
 * @param[in] t0Samples  the time the phase is relative to, in samples
 * @return the phase in cycles
 */
double phaseAt(const std::vector<float>& x, double hz, int from, int len, double sr, double t0Samples)
{
    double re = 0.0, im = 0.0;
    for (int i = from; i < from + len; ++i) {
        const double t = (i + t0Samples) / sr;
        re += x[static_cast<size_t>(i)] * std::cos(2.0 * PiD * hz * t);
        im += x[static_cast<size_t>(i)] * std::sin(2.0 * PiD * hz * t);
    }
    return std::atan2(re, im) / (2.0 * PiD);
}

/**
 * @brief Phase @p c in cycles, wrapped into -0.5 .. 0.5.
 * @param[in] c  the phase in cycles
 * @return the phase wrapped into -0.5 .. 0.5
 */
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
        const float f = Kick::tuneToKey(root, static_cast<int>(KickTuneE::KEY), 52.0f);
        const double note = 69.0 + 12.0 * std::log2(f / 440.0);
        const int pc = ((static_cast<int>(std::lround(note)) % 12) + 12) % 12;
        const bool tonicFits = root >= 4;
        const int want = tonicFits ? root : (root + 7) % 12;
        if (pc != want || f < 40.0f || f > 66.0f) { keyOk = false; detail += fmt("%s->%.1f ", KeyNames[root], f); }
        const float g = Kick::tuneToKey(root, static_cast<int>(KickTuneE::FIFTH), 52.0f);
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
    const char* const Masks[3] = { "x.xx.x.xx.x.x.x.", "xx.x.xx.xx.x.xx.", "x..x..x..x..x.x." };
    for (int m = 0; m < 3; ++m) {
        std::string got;
        for (int s = 0; s < 16; ++s) got += TranceGate::open(0.25 * s + 0.0625, 6 + m, 0.5f, 0.001, 0.001) > 0.5f ? 'x' : '.';
        check(got == Masks[m], fmt("pattern %d is %s", 6 + m, Masks[m]).c_str(), got);
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
    for (const Section& s : sc.m_sections) lines = lines && std::fmod(s.m_beat, 32.0) == 0.0 && std::fmod(s.m_length, 32.0) == 0.0;
    check(lines && !sc.m_sections.empty(), "every section starts and ends on an 8-bar line", fmt("%zu sections", sc.m_sections.size()));
    bool blocks = sc.m_layers.size() * 32 == static_cast<size_t>(sc.m_lengthBeats);
    int unchanged = 0;
    for (size_t b = 0; b < sc.m_layers.size(); ++b) {
        blocks = blocks && sc.m_layers[b].m_beat == 32.0 * static_cast<double>(b);
        if (b > 0 && sc.m_layers[b].m_state == sc.m_layers[b - 1].m_state) ++unchanged;
    }
    check(blocks, "one layer block per 8 bars", fmt("%zu blocks", sc.m_layers.size()));
    check(unchanged <= 2, "almost every block adds, filters or takes away an element", fmt("%d blocks without a change", unchanged));
    double firstBreakdown = -1.0, firstLead = -1.0;
    for (const Section& s : sc.m_sections) if (s.m_kind == SectionKindE::BREAKDOWN && firstBreakdown < 0.0) firstBreakdown = s.m_beat;
    bool leadInEdges = false;
    for (const NoteEvent& n : sc.m_notes) {
        if (n.m_part != PartE::LEAD) continue;
        if (firstLead < 0.0) firstLead = n.m_beat;
        for (const Section& s : sc.m_sections)
            if ((s.m_kind == SectionKindE::INTRO || s.m_kind == SectionKindE::OUTRO) && n.m_beat >= s.m_beat && n.m_beat < s.m_beat + s.m_length) leadInEdges = true;
    }
    check(firstLead >= firstBreakdown && firstBreakdown >= 0.0, "the lead is introduced in the breakdown, not before",
          fmt("first lead at bar %.0f, breakdown at bar %.0f", firstLead / 4.0, firstBreakdown / 4.0));
    check(!leadInEdges, "no lead in the intro or the outro");
    double drop = -1.0;
    for (const Section& s : sc.m_sections) if (s.m_kind == SectionKindE::DROP) { drop = s.m_beat; break; }
    int inVacuum = 0;
    for (const NoteEvent& n : sc.m_notes) if (n.m_beat >= drop - 1.0 && n.m_beat < drop) ++inVacuum;
    check(drop > 0.0 && inVacuum == 0, "the last beat before the drop is empty (Dok. 6, rule 5)", fmt("%d notes start in it", inVacuum));
    std::set<double> ghosts;
    for (const NoteEvent& n : sc.m_notes) if (n.m_part == PartE::GHOST) ghosts.insert(n.m_beat);
    check(static_cast<double>(ghosts.size()) == sc.m_lengthBeats - 1.0, "the ghost kick on every quarter but that one",
          fmt("%zu of %.0f", ghosts.size(), sc.m_lengthBeats));
}

/** @brief What render() gives. */
struct Run {
    std::vector<std::vector<float>> m_stems;   ///< the stems, left channel
    std::vector<float> m_out;                  ///< the output
};
/**
 * Renders @p score from beat @p from for @p seconds with stems; returns the stems (left channel) and the output.
 * @param[in] score    the score
 * @param[in] knobs    key=value lines set before, or null
 * @param[in] from     the first beat
 * @param[in] seconds  the seconds rendered
 * @param[in] block    the block size
 * @return the stems and the output
 */
Run render(const Score& score, const char* knobs, double from, double seconds, int block = 256)
{
    auto e = std::make_unique<Engine>();
    if (knobs != nullptr) e->params().parseText(knobs);
    e->prepare(48000.0, block);
    e->load(score);
    e->seek(from);
    std::vector<std::vector<float>> sl(Engine::STEMS, std::vector<float>(static_cast<size_t>(block))), sr = sl;
    std::vector<float*> pl, pr;
    for (int k = 0; k < Engine::STEMS; ++k) { pl.push_back(sl[static_cast<size_t>(k)].data()); pr.push_back(sr[static_cast<size_t>(k)].data()); }
    e->setStems(pl.data(), pr.data());
    Run run;
    run.m_stems.assign(Engine::STEMS, {});
    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    const int n = static_cast<int>(seconds * 48000.0);
    for (int done = 0; done < n; done += block) {
        const int m = std::min(block, n - done);
        e->process(L.data(), R.data(), m);
        for (int i = 0; i < m; ++i) {
            run.m_out.push_back(L[static_cast<size_t>(i)]);
            run.m_out.push_back(R[static_cast<size_t>(i)]);
            for (int s = 0; s < Engine::STEMS; ++s) run.m_stems[static_cast<size_t>(s)].push_back(sl[static_cast<size_t>(s)][static_cast<size_t>(i)]);
        }
    }
    return run;
}

/**
 * @brief The RMS of @p x from index @p from up to @p to.
 * @param[in] x     the samples
 * @param[in] from  the first index
 * @param[in] to    the end index
 * @return the RMS
 */
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
    sc.m_lengthBeats = 16.0;
    sc.m_notes.push_back(NoteEvent{ 0.0, 15.0, PartE::PAD, 57, 0.8f, 0, false, false });
    for (int q = 4; q < 16; ++q) sc.m_notes.push_back(NoteEvent{ static_cast<double>(q), 0.25, PartE::GHOST, 36, 1.0f, 0, false, false });
    sc.sort();
    auto pad = [&](float depth) {
        const std::string knobs = fmt("pad.duck=%.1f; pad.osc=VA; pad.detune=0; pad.amp_attack=5; pad.amp_sustain=1; pad.gate=0;"
                                      "pad.lfo_cutoff=0; pad.drift=0; pump.attack=0.5; pump.hold=40; pump.release=120", depth);
        return render(sc, knobs.c_str(), 0.0, 7.0).m_stems[Engine::STEM_PAD];
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
    for (int s : { Engine::STEM_LEAD, Engine::STEM_PLUCK, Engine::STEM_PAD, Engine::STEM_ARP, Engine::STEM_ROOM,
                   Engine::STEM_PLATE, Engine::STEM_HALL }) {
        const double db = lowShare(r.m_stems[static_cast<size_t>(s)]);
        if (db > worstDb) { worstDb = db; worst = Engine::stemName(s); }
    }
    check(worstDb < -25.0, "voices and rooms under 150 Hz at least 25 dB under themselves", fmt("worst: %s at %.1f dB", worst.c_str(), worstDb));
    const double kickLow = lowShare(r.m_stems[Engine::STEM_KICK]), subLow = lowShare(r.m_stems[Engine::STEM_SUB]);
    check(kickLow > -6.0 && subLow > -3.0, "the kick and the sub live there", fmt("kick %.1f dB, sub %.1f dB", kickLow, subLow));
}

/**
 * Renders the study from its start with block size @p block for @p seconds (interleaved).
 * @param[in] block    the block size
 * @param[in] seconds  the seconds rendered
 * @param[in] seed     the seed
 * @param[in] from     the first beat
 * @return the output, interleaved
 */
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
        std::vector<std::vector<float>> sl(Engine::STEMS, std::vector<float>(37)), sr = sl;
        std::vector<float*> pl, pr;
        for (int k = 0; k < Engine::STEMS; ++k) { pl.push_back(sl[static_cast<size_t>(k)].data()); pr.push_back(sr[static_cast<size_t>(k)].data()); }
        std::vector<float> L(37), R(37), preL(37), preR(37);
        if (stems) { e->setStems(pl.data(), pr.data()); e->setPremasterTap(preL.data(), preR.data()); }
        for (int done = 0; done < 48000 * 10; done += 37) {
            e->process(L.data(), R.data(), 37);
            for (int i = 0; i < 37; ++i) {
                out.push_back(L[static_cast<size_t>(i)]);
                if (!stems) continue;
                float s = 0.0f;
                for (int k = 0; k < Engine::STEMS; ++k) s += sl[static_cast<size_t>(k)][static_cast<size_t>(i)];
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
    const auto startOf = [&](SectionKindE k) { for (const Section& s : sc.m_sections) if (s.m_kind == k) return s; return Section{}; };
    const Section b = startOf(SectionKindE::BREAKDOWN), d = startOf(SectionKindE::DROP);
    const int64_t b0 = static_cast<int64_t>(sc.m_tempo.secondsAt(b.m_beat) * 48000.0), b1 = static_cast<int64_t>(sc.m_tempo.secondsAt(b.m_beat + b.m_length) * 48000.0);
    const int64_t d0 = static_cast<int64_t>(sc.m_tempo.secondsAt(d.m_beat) * 48000.0), d1 = static_cast<int64_t>(sc.m_tempo.secondsAt(d.m_beat + d.m_length) * 48000.0);
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
    check(rw.m_truePeak <= -0.9, "the true peak at the master's ceiling", fmt("%.2f dBTP", rw.m_truePeak));
    const double gap = rd.m_shortTermMax - rb.m_shortTermMax;
    check(gap >= 3.0, "the drop louder than the breakdown", fmt("breakdown %.1f, drop %.1f LUFS short-term max: %.1f LU", rb.m_shortTermMax,
                                                                rd.m_shortTermMax, gap));
}

/** The MIDI export: every part on its channel, the ghost kick on 16 (for a host's sidechain). */
void testMidi()
{
    section("MIDI");
    auto p = std::make_unique<ParamStore>();
    const Score sc = composeStudy(*p, 2);
    const std::vector<uint8_t> bytes = encodeMidi(sc, "test", p.get());
    check(bytes.size() > 1000 && bytes[0] == 'M' && bytes[1] == 'T', "an SMF file", fmt("%zu bytes", bytes.size()));
    check(midiChannelOf(PartE::GHOST) == 15 && midiChannelOf(PartE::LEAD) == 3 && midiChannelOf(PartE::KICK) == 9, "the channels");
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
    for (int st = 0; st < static_cast<int>(StyleE::COUNT); ++st) {
        const StyleProfile& prof = styleProfile(static_cast<StyleE>(st));
        for (uint64_t seed = 1; seed <= 8; ++seed) {
            const int ask = static_cast<int>(0.5f * (prof.m_minutesLow + prof.m_minutesHigh) * 0.5f * (prof.m_bpmLow + prof.m_bpmHigh) / 4.0f);
            const Plan plan = planTrack(prof, ask, seed);
            bool lines = plan.m_bars % 32 == 0;
            for (const Section& sec : plan.m_sections) lines = lines && std::fmod(sec.m_beat, 32.0) == 0.0 && std::fmod(sec.m_length, 32.0) == 0.0;
            if (!lines) { ++bad[0]; where += fmt("%s/%llu lines; ", prof.m_name, static_cast<unsigned long long>(seed)); }
            for (size_t b = 1; b < plan.m_blocks.size(); ++b) if (plan.m_blocks[b].m_state == plan.m_blocks[b - 1].m_state) { ++bad[1]; break; }
            double firstBreak = 1e30;
            for (const Section& sec : plan.m_sections)
                if (sec.m_kind == SectionKindE::BREAK || sec.m_kind == SectionKindE::BREAKDOWN || sec.m_kind == SectionKindE::INTRO) { firstBreak = std::min(firstBreak, sec.m_beat); if (sec.m_kind != SectionKindE::INTRO) break; }
            for (const LayerBlock& blk : plan.m_blocks) {
                if (blk.at(LayerE::LEAD) != LayerStateE::ON) continue;
                const int si = plan.sectionAt(static_cast<int>(blk.m_beat / 4.0));
                const SectionKindE k = plan.m_sections[static_cast<size_t>(si)].m_kind;
                if (k == SectionKindE::INTRO || k == SectionKindE::OUTRO) { ++bad[2]; break; }
            }
            if (plan.m_firstLeadBar >= 0) {
                const int si = plan.sectionAt(plan.m_firstLeadBar);
                const SectionKindE k = plan.m_sections[static_cast<size_t>(si)].m_kind;
                const bool ok = k == SectionKindE::BREAK || k == SectionKindE::BREAKDOWN
                             || (k == SectionKindE::INTRO && plan.at(plan.m_firstLeadBar, LayerE::LEAD) == LayerStateE::FILTERED);
                if (!ok) { ++bad[3]; where += fmt("%s/%llu lead first in %s; ", prof.m_name, static_cast<unsigned long long>(seed), SectionNames[static_cast<int>(k)]); }
            }
            for (size_t i = 1; i < plan.m_sections.size(); ++i)
                if (plan.m_sections[i].m_kind == SectionKindE::DROP
                    && std::none_of(plan.m_vacuums.begin(), plan.m_vacuums.end(), [&](const Vacuum& v) { return v.m_bar == static_cast<int>(plan.m_sections[i].m_beat / 4.0) - 1; }))
                    ++bad[4];
            if (std::abs(plan.m_bars - ask) > ask / 4) { ++bad[5]; where += fmt("%s/%llu %d bars for %d; ", prof.m_name, static_cast<unsigned long long>(seed), plan.m_bars, ask); }
            const float share = breakdownShare(plan);
            if (share < prof.m_breakdownLow - 0.1f || share > prof.m_breakdownHigh + 0.1f) {
                ++bad[6];
                where += fmt("%s/%llu share %.2f; ", prof.m_name, static_cast<unsigned long long>(seed), share);
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
    for (int st = 0; st < static_cast<int>(StyleE::COUNT); ++st) {
        const StyleProfile& prof = styleProfile(static_cast<StyleE>(st));
        if (prof.m_barsPerChord >= 16) continue;   // Acid's static harmony has no lift
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            Rng r;
            r.seed(seed);
            int key, scale;
            drawKey(prof, r, key, scale);
            const Plan plan = planTrack(prof, 256, seed);
            const Harmony h = composeHarmony(plan, prof, key, scale, seed * 7);
            const bool major = scale == static_cast<int>(ScaleE::IONIAN);
            if (h.m_changeBar >= 0) ++keyChanges;
            for (size_t i = 1; i < plan.m_sections.size(); ++i) {
                if (plan.m_sections[i].m_kind != SectionKindE::DROP) continue;
                const int d = static_cast<int>(plan.m_sections[i].m_beat / 4.0);
                if (h.m_bars[static_cast<size_t>(d - 1)].m_degree != (major ? 4 : 6) || h.m_bars[static_cast<size_t>(d - 2)].m_degree != (major ? 4 : 6)) ++liftBad;
                if (h.m_bars[static_cast<size_t>(d)].m_degree != (major ? 5 : 0) && h.m_bars[static_cast<size_t>(d)].m_degree != 0) ++resolveBad;
            }
            std::array<int, 4> prev{ 60, 64, 67, 72 };
            for (int bar = 0; bar < plan.m_bars; ++bar) {
                for (int t = 0; t < 3; ++t) {
                    const int iv = ((h.m_bars[static_cast<size_t>(bar)].m_tones[t]) % 12 + 12) % 12;
                    const int cs = scale == static_cast<int>(ScaleE::DORIAN) || scale == static_cast<int>(ScaleE::PHRYGIAN) || major ? scale : 0;
                    if (!inScale(cs, iv) && !(t == 1 && h.m_bars[static_cast<size_t>(bar)].m_degree == 4)) ++scaleBad;
                }
                const std::array<int, 4> v = voicePad(h, bar, prev, bar == 0);
                prev = v;
                if (v[0] < 52 || ((v[0] - h.m_key - h.m_shift[static_cast<size_t>(bar)]) % 12 + 12) % 12 == ((h.m_bars[static_cast<size_t>(bar)].m_tones[0]) % 12 + 12) % 12) ++voiceBad;
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
    bool same = a.m_notes.size() == b.m_notes.size();
    for (size_t i = 0; same && i < a.m_notes.size(); ++i)
        same = a.m_notes[i].m_beat == b.m_notes[i].m_beat && a.m_notes[i].m_pitch == b.m_notes[i].m_pitch && a.m_notes[i].m_part == b.m_notes[i].m_part
            && a.m_notes[i].m_velocity == b.m_notes[i].m_velocity;
    check(same && !a.m_notes.empty(), "the same seed, the same score", fmt("%zu notes", a.m_notes.size()));
    // The track's key reaches the knobs the deck tunes the kick, the kit and the effects by.
    {
        int right = 0;
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            const Score s = composeTrack(*p, seed);
            right += s.knobAt(p->id(ModuleE::COMPOSE, 0, compose::KEY), 0.0) == static_cast<float>(s.m_keyRoot)
                  && s.knobAt(p->id(ModuleE::COMPOSE, 0, compose::SCALE), 0.0) == static_cast<float>(s.m_scale);
        }
        check(right == 6, "the track's key and scale on the knobs (the kick's, the kit's, the effects' tuning)", fmt("%d of 6", right));
    }
    // PLAN 6.9: a unit rerolled draws its voices again and leaves every other note as it was, to the velocity.
    using KeyT = std::tuple<double, double, int, int, float>;
    auto notesOf = [](const Score& s, std::initializer_list<PartE> parts, bool inside) {
        std::vector<KeyT> out;
        for (const NoteEvent& n : s.m_notes) {
            const bool in = std::find(parts.begin(), parts.end(), n.m_part) != parts.end();
            if (in == inside) out.push_back({ n.m_beat, n.m_length, static_cast<int>(n.m_part), n.m_pitch, n.m_velocity });
        }
        return out;
    };
    auto sameForm = [](const Score& x, const Score& y) {
        bool s = x.m_sections.size() == y.m_sections.size();
        for (size_t i = 0; s && i < x.m_sections.size(); ++i) s = x.m_sections[i].m_beat == y.m_sections[i].m_beat && x.m_sections[i].m_kind == y.m_sections[i].m_kind;
        return s;
    };
    struct Case { const char* m_style; const char* m_unit; std::initializer_list<PartE> m_parts; };
    const Case cases[] = {
        { "Uplifting", "motif", { PartE::LEAD, PartE::COUNTER, PartE::STRINGS } },
        { "Uplifting", "lead", { PartE::LEAD, PartE::COUNTER, PartE::STRINGS } },
        { "Uplifting", "arp", { PartE::ARP } },
        { "Uplifting", "pluck", { PartE::PLUCK, PartE::STAB } },
        { "Uplifting", "drums", { PartE::PERC1, PartE::PERC2, PartE::PERC3, PartE::PERC4, PartE::PERC5, PartE::PERC6, PartE::PERC7, PartE::PERC8, PartE::PERC9, PartE::PERC10 } },
        { "Uplifting", "fx", { PartE::FX } },
        { "Uplifting", "sounds", {} },
        { "Acid", "acid", { PartE::ACID } },
        { "Dream House", "motif", { PartE::PIANO, PartE::COUNTER, PartE::STRINGS } },
    };
    int isolated = 0, changed = 0, total = 0;
    std::string bad;
    for (const Case& k : cases) {
        auto q = std::make_unique<ParamStore>();
        q->parseText(std::string("compose.style=") + k.m_style);
        for (uint64_t seed : { 21ull, 22ull }) {
            const Score base = composeTrack(*q, seed);
            Curation cur;
            cur.reroll(k.m_unit);
            const Score re = composeTrack(*q, seed, TrackRequest{}, &cur);
            ++total;
            const bool iso = notesOf(base, k.m_parts, false) == notesOf(re, k.m_parts, false) && sameForm(base, re);
            // (Its own: its notes, or its automation and knob sets -- the effects' throws, the sounds.)
            bool automation = base.m_gestures.size() != re.m_gestures.size() || base.m_knobs.size() != re.m_knobs.size();
            for (size_t i = 0; !automation && i < base.m_gestures.size(); ++i)
                automation = base.m_gestures[i].m_param != re.m_gestures[i].m_param || base.m_gestures[i].m_beat != re.m_gestures[i].m_beat || base.m_gestures[i].m_to != re.m_gestures[i].m_to;
            for (size_t i = 0; !automation && i < base.m_knobs.size(); ++i) automation = base.m_knobs[i].m_value != re.m_knobs[i].m_value;
            const bool moved = automation || notesOf(base, k.m_parts, true) != notesOf(re, k.m_parts, true) || notesOf(base, k.m_parts, true).empty();
            isolated += iso;
            changed += moved;
            if (!iso || !moved) bad += std::string(" ") + k.m_style + "/" + k.m_unit;
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
            bool kept = base.m_layers.size() == re.m_layers.size();
            for (size_t i = 0; kept && i < base.m_layers.size(); ++i) kept = base.m_layers[i].m_state == re.m_layers[i].m_state;
            matrixMoved += !kept;
        }
    }
    check(formKept == 12, "a rerolled matrix or section keeps the form", fmt("%d of 12", formKept));
    check(matrixMoved >= 6, "and moves the layer matrix (mostly)", fmt("%d of 12", matrixMoved));
    // The vacuum before a drop (rule 5, Phosphene's four): all four drawn over some tracks, and nothing sounds in one but
    // the kick on the last beat where the variant keeps it.
    int kinds[4] = {}, loud = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        const StyleProfile& prof = styleProfile(StyleE::UPLIFTING);
        const Plan plan = planTrack(prof, 256, seed);
        for (const Vacuum& v : plan.m_vacuums) ++kinds[v.m_kickOn4 ? 3 : v.m_from == 3.0 ? 0 : v.m_from == 2.0 ? 1 : 2];
        TrackRequest req;
        req.m_profile = &prof;
        req.m_bars = 256;   // (the plan above)
        const Score sc = composeTrack(*p, seed, req);
        for (const NoteEvent& n : sc.m_notes) {
            if (n.m_part == PartE::FX) continue;   // (the riser and the reverse crash run into the drop)
            for (const Vacuum& v : plan.m_vacuums)
                if (n.m_beat >= 4.0 * v.m_bar + v.m_from - 1e-9 && n.m_beat < 4.0 * (v.m_bar + 1)
                    && !(v.m_kickOn4 && (n.m_part == PartE::KICK || n.m_part == PartE::GHOST) && n.m_beat == 4.0 * v.m_bar + 3.0))
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
    check(std::fabs(r[0].m_after + r[0].m_trim - r[0].m_measured - r[0].m_trim) >= 0.0f && std::fabs(r[0].m_after - r[0].m_target) < 0.8f,
          "the drop at the style's target", fmt("%.1f LUFS after the correction, target %.1f", r[0].m_after, r[0].m_target));
    check(!std::isnan(r[0].m_gapAfter) && std::fabs(r[0].m_gapAfter - sc.m_levels[0].m_gapLu) < 0.6f, "the breakdown under the drop by the style's gap",
          fmt("%.1f LU (as composed %.1f), target %.1f, correction %+.1f dB", r[0].m_gapAfter, r[0].m_gapBefore, sc.m_levels[0].m_gapLu, r[0].m_breakDb));
    float lo, hi;
    balanceWindow(static_cast<int>(BalPartE::LEAD), -1, lo, hi);
    const float lead = r[0].m_found[static_cast<size_t>(BalPartE::LEAD)] + r[0].m_bal[static_cast<size_t>(BalPartE::LEAD)];
    check(std::isnan(r[0].m_found[static_cast<size_t>(BalPartE::LEAD)]) || (lead >= lo - 0.01f && lead <= hi + 0.01f), "the lead inside its window against the kick",
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
    for (int role = 0; role < CorpusRoleCount; ++role) {
        const StepModel& m = corpusModel(static_cast<CorpusRoleIdE>(role));
        for (int a = -1; a < CorpusAlpha; a += 3)
            for (int b = -1; b < CorpusAlpha; b += 4) {
                if (a >= 0 && b < 0) continue;
                double s = 0.0;
                for (int c = 0; c < CorpusAlpha; ++c) s += m.p(a, b, c);
                worst = std::max(worst, std::fabs(s - 1.0));
            }
    }
    check(worst < 1e-4, "every distribution of every role sums to one", fmt("worst deviation %.1e", worst));

    const StepModel& lead = corpusModel(CorpusRoleIdE::LEAD);
    Rng r;
    r.seed(99);
    int outside = 0, failed = 0;
    for (int t = 0; t < 200; ++t) {
        std::vector<std::array<bool, CorpusAlpha>> allowed(12);
        for (auto& row : allowed) {
            row.fill(false);
            for (int k = 0; k < 3; ++k) row[static_cast<size_t>(r.below(CorpusAlpha))] = true;
        }
        std::vector<int> out;
        if (!sampleConstrained(lead, 12, allowed, nullptr, r, out)) { ++failed; continue; }
        for (size_t i = 0; i < out.size(); ++i) if (!allowed[i][static_cast<size_t>(out[i])]) ++outside;
    }
    check(outside == 0 && failed == 0, "200 lines under random constraints, every step allowed", fmt("%d outside, %d failed", outside, failed));
    {
        std::vector<std::array<bool, CorpusAlpha>> allowed(4);
        for (auto& row : allowed) row.fill(true);
        allowed[2].fill(false);
        std::vector<int> out;
        check(!sampleConstrained(lead, 4, allowed, nullptr, r, out), "an impossible constraint is reported, nothing drawn");
    }
    // Exactness: the tonic or the fifth at each of three positions.
    const int s0 = 7, s1 = 11;
    std::vector<std::array<bool, CorpusAlpha>> allowed(3);
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
    check(ok && h == MemoProbeHash, "the engine hashes a window as Tools/corpus/memorisation.py does", fmt("%016llx", static_cast<unsigned long long>(h)));
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
    for (StyleE st : { StyleE::UPLIFTING, StyleE::DREAM_HOUSE }) {
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
            mc.m_plan = &plan;
            mc.m_harmony = &h;
            mc.m_score = &sc;
            mc.m_piano = prof.m_lead == LeadKindE::PIANO;
            mc.m_anthemShare = mc.m_piano ? 1.0f : 0.7f;
            writeLead(mc, seed * 131, seed * 137);
            std::vector<int> top(static_cast<size_t>(plan.m_bars) * 16, -1);
            int n = 0;
            for (const NoteEvent& e : sc.m_notes) {
                if (e.m_part != PartE::LEAD && e.m_part != PartE::PIANO) continue;
                ++n;
                const int bar = std::min(static_cast<int>(e.m_beat / 4.0), plan.m_bars - 1);
                const int pc = e.m_pitch % 12;
                const bool chord = pc == h.pc(bar, 0) || pc == h.pc(bar, 1) || pc == h.pc(bar, 2);
                if (std::fabs(e.m_beat - std::round(e.m_beat)) < 1e-9 && !chord) ++offChord;
                const int rel = ((pc - key - h.m_shift[static_cast<size_t>(bar)]) % 12 + 12) % 12;
                bool ok = chord || inScale(scale, rel);
                if (scale == static_cast<int>(ScaleE::HARMONIC_MINOR) || scale == static_cast<int>(ScaleE::MINOR_PENTATONIC))
                    ok = ok || inScale(static_cast<int>(ScaleE::AEOLIAN), rel);
                                if (!ok) ++offScale;
                if (e.m_pitch < 52 || e.m_pitch > 96) ++register_;
                top[static_cast<size_t>(std::llround(e.m_beat * 4.0))] = std::max(top[static_cast<size_t>(std::llround(e.m_beat * 4.0))], e.m_pitch);
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

/**
 * @brief One piano note (held @p hold s) over @p secs s, the channels' mean, with the knobs' defaults and @p pedal.
 * @param[in,out] p            the piano
 * @param[in]     midi         the MIDI note
 * @param[in]     vel          the velocity, 0..1
 * @param[in]     hold         the seconds held
 * @param[in]     secs         the seconds rendered
 * @param[in]     pedal        the pedal, 0..1
 * @param[in]     sympathetic  the sympathetic strings' level
 * @param[in]     phantom      the phantom partials' level
 * @return the channels' mean
 */
std::vector<float> pianoNote(Piano& p, int midi, float vel, double hold, double secs, float pedal = 0.0f, float sympathetic = 1.0f,
                             float phantom = 1.0f)
{
    ParamStore ps;
    std::vector<float> v(static_cast<size_t>(piano::COUNT));
    ps.readModule(ModuleE::PIANO, 0, v.data());
    v[piano::PEDAL] = pedal;
    v[piano::SYMPATHETIC] = sympathetic;
    v[piano::PHANTOM] = phantom;
    p.update(v.data());
    p.reset();
    const int total = static_cast<int>(secs * 48000.0), off = static_cast<int>(hold * 48000.0);
    std::vector<float> out(static_cast<size_t>(total));
    float L[Piano::MaxBlock], R[Piano::MaxBlock];
    p.noteOn(midi, vel, 0.0);
    for (int i = 0; i < total; i += Piano::MaxBlock) {
        if (i <= off && off < i + Piano::MaxBlock) p.noteOff(midi);
        const int n = std::min(Piano::MaxBlock, total - i);
        p.process(L, R, n);
        for (int k = 0; k < n; ++k) out[static_cast<size_t>(i + k)] = 0.5f * (L[k] + R[k]);
    }
    return out;
}

/**
 * @brief The level (dB) of @p x between @p a and @p b seconds.
 * @param[in] x  the samples
 * @param[in] a  the first second
 * @param[in] b  the last second
 * @return the level in dB
 */
double levelDb(const std::vector<float>& x, double a, double b)
{
    const size_t i = static_cast<size_t>(a * 48000.0), j = std::min(x.size(), static_cast<size_t>(b * 48000.0));
    double s = 0.0;
    for (size_t k = i; k < j; ++k) s += static_cast<double>(x[k]) * x[k];
    return 10.0 * std::log10(s / std::max<size_t>(1, j - i) + 1e-30);
}

/**
 * @brief The frequency of the strongest peak of @p x (from @p start, 2^15 samples, Hann) within @p f +- 2 %.
 * @param[in] x      the samples
 * @param[in] start  the first second
 * @param[in] f      the frequency expected
 * @return the peak's frequency in Hz
 */
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
    const PianoKey& c4 = d.m_keys[static_cast<size_t>(60 - PianoLowKey)];
    check(d.m_board.m_freq[0] > 35.0 && d.m_board.m_freq[0] < 65.0 && d.m_board.m_freq.size() > 20, "the board: a first mode near 45 Hz, tens of modes to 1.2 kHz",
          fmt("%.1f Hz, %zu modes", d.m_board.m_freq[0], d.m_board.m_freq.size()));
    check(c4.m_B > 2e-4 && c4.m_B < 8e-4 && c4.m_strings == 3 && d.m_keys[0].m_strings == 1 && d.m_keys[static_cast<size_t>(36 - PianoLowKey)].m_strings == 2,
          "C4's inharmonicity, a course of three; one string in the lowest bass, two in the wound", fmt("B %.2e", c4.m_B));
    // The partials of C2: the stiff string's, not the harmonics.
    {
        const std::vector<float> x = pianoNote(*piano, 36, 0.7f, 3.0, 1.5);
        const PianoKey& k = d.m_keys[static_cast<size_t>(36 - PianoLowKey)];
        double worst = 0.0;
        for (int p = 4; p <= 12; ++p) {
            const double model = p * k.m_f0 * std::sqrt(1.0 + k.m_B * p * p);
            worst = std::max(worst, std::fabs(1200.0 * std::log2(peakHz(x, 0.2, model) / model)));
        }
        const double f12 = peakHz(x, 0.2, 12.0 * k.m_f0 * std::sqrt(1.0 + k.m_B * 144.0)), f1 = peakHz(x, 0.2, k.m_f0 * std::sqrt(1.0 + k.m_B));
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
        const PianoKey& k = d.m_keys[static_cast<size_t>(33 - PianoLowKey)];
        const double f2 = 2.0 * k.m_f0 * std::sqrt(1.0 + 4.0 * k.m_B);
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
    for (const NoteEvent& n : sc.m_notes) if (n.m_part == PartE::PIANO) { if (notes == 0) from = std::floor(n.m_beat / 4.0) * 4.0; ++notes; }
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

/**
 * @brief One note of an orchestral instrument (held @p hold s) over @p secs s, the channels' mean.
 * @param[in,out] ins    the instrument
 * @param[in]     pitch  the MIDI note
 * @param[in]     vel    the velocity, 0..1
 * @param[in]     hold   the seconds held
 * @param[in]     secs   the seconds rendered
 * @return the channels' mean
 */
template <class InstrT>
std::vector<float> orchNote(InstrT& ins, int pitch, float vel, double hold, double secs)
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

/**
 * @brief The spectral centroid (Hz) of @p x over 2^14 samples from @p a seconds (Hann).
 * @param[in] x  the samples
 * @param[in] a  the first second
 * @return the centroid in Hz
 */
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
        // The Quest's share (Deck::setQuest) is a limit on the knobs: the knob's players come through update() and
        // stop at it. (Until 29.09.2026 the knob overwrote the Quest's three at the next cell.)
        auto s = std::make_unique<StringSection>();
        auto c = std::make_unique<Choir>();
        auto b = std::make_unique<Brass>();
        s->prepare(48000.0, 7);
        c->prepare(48000.0, 7);
        b->prepare(48000.0, 7);
        s->setPlayerLimit(3);
        c->setSingerLimit(3);
        b->setPlayerLimit(2);
        ps.readModule(ModuleE::STRINGS, 0, v.data());
        v[strings::PLAYERS] = 6.0f;
        s->update(v.data());
        ps.readModule(ModuleE::CHOIR, 0, v.data());
        v[choir::SINGERS] = 6.0f;
        c->update(v.data());
        ps.readModule(ModuleE::BRASS, 0, v.data());
        v[brass::PLAYERS] = 4.0f;
        b->update(v.data());
        s->noteOn(60, 0.7f, false, 0.0);
        c->noteOn(60, 0.7f, false, 0.0);
        b->noteOn(60, 0.7f, false, 0.0);
        check(s->activeLanes() == 3 && c->activeSingers() == 3 && b->activePlayers() == 2,
              "the Quest's limit holds against the knobs (players, singers)",
              fmt("strings %d, choir %d, brass %d", s->activeLanes(), c->activeSingers(), b->activePlayers()));
    }
    {
        auto s = std::make_unique<StringSection>();
        s->prepare(48000.0, 7);
        ps.readModule(ModuleE::STRINGS, 0, v.data());
        v[strings::PLAYERS] = 1.0f;
        v[strings::VIBRATO] = 0.0f;
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
        ps.readModule(ModuleE::CHOIR, 0, v.data());
        v[choir::VIBRATO] = 0.0f;
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
        ps.readModule(ModuleE::BRASS, 0, v.data());
        v[brass::PLAYERS] = 1.0f;
        v[brass::VIBRATO] = 0.0f;
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
        ps.readModule(ModuleE::TIMPANI, 0, v.data());
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
        for (const NoteEvent& n : sc.m_notes) if (n.m_part == PartE::CHOIR) { from = std::floor(n.m_beat / 4.0) * 4.0; break; }
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
    constexpr int Sr = 48000, Len = 48000, Blk = 64;
    auto values = [&](ModuleE m, int count) {
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
        std::vector<float> out, L(Blk), R(Blk);
        start(engine);
        for (int done = 0; done < Len; done += Blk) {
            engine.process(L.data(), R.data(), Blk);
            for (int i = 0; i < Blk; ++i) out.push_back(L[static_cast<size_t>(i)] + 0.5f * R[static_cast<size_t>(i)]);
        }
        return out;
    };
    {   // The mono synth (bass).
        const std::vector<float> base = values(ModuleE::BASS, synth::COUNT);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<MonoSynth>();
            s->prepare(Sr, 3);
            return run(*s, [&](MonoSynth& e) { e.update(v.data(), 0.0f); e.noteOn(45, 0.8f, 0.0, false, false); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(SynthModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, synth::MOD_FIRST, d);
            // (The pulse width needs the pulse: on the saw it has nothing to move.)
            if (SynthModDests[d] == static_cast<int>(ModDestE::PULSE_WIDTH)) {
                std::vector<float> w = base;
                w[synth::WAVE] = v[synth::WAVE] = 1.0f;
                compare("bass", SynthModDestNames[d], render(w), render(v));
                continue;
            }
            compare("bass", SynthModDestNames[d], ref, render(v));
        }
    }
    {   // The strings.
        const std::vector<float> base = values(ModuleE::STRINGS, strings::COUNT);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<StringSection>();
            s->prepare(Sr, 5);
            return run(*s, [&](StringSection& e) { e.update(v.data()); e.noteOn(60, 0.7f, false, 0.0); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(StringsModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, strings::MOD_FIRST, d);
            compare("strings", StringsModDestNames[d], ref, render(v));
        }
    }
    {   // The choir.
        const std::vector<float> base = values(ModuleE::CHOIR, choir::COUNT);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<Choir>();
            s->prepare(Sr, 5);
            return run(*s, [&](Choir& e) { e.update(v.data()); e.noteOn(60, 0.7f, false, 0.0); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(ChoirModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, choir::MOD_FIRST, d);
            compare("choir", ChoirModDestNames[d], ref, render(v));
        }
    }
    {   // The brass.
        const std::vector<float> base = values(ModuleE::BRASS, brass::COUNT);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<Brass>();
            s->prepare(Sr, 5);
            return run(*s, [&](Brass& e) { e.update(v.data()); e.noteOn(48, 0.8f, false, 0.0); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(BrassModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, brass::MOD_FIRST, d);
            compare("brass", BrassModDestNames[d], ref, render(v));
        }
    }
    {   // The timpani.
        const std::vector<float> base = values(ModuleE::TIMPANI, timpani::COUNT);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<Timpani>();
            s->prepare(Sr, 5);
            return run(*s, [&](Timpani& e) { e.update(v.data()); e.noteOn(45, 0.8f, false, 0.0); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(TimpaniModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, timpani::MOD_FIRST, d);
            compare("timpani", TimpaniModDestNames[d], ref, render(v));
        }
    }
    {   // The piano.
        const std::vector<float> base = values(ModuleE::PIANO, piano::COUNT);
        auto render = [&](const std::vector<float>& v) {
            auto s = std::make_unique<Piano>();
            s->prepare(Sr);
            s->setSpec(PianoSpec{});
            return run(*s, [&](Piano& e) { e.update(v.data()); e.noteOn(60, 0.7f, 0.0); });
        };
        const std::vector<float> ref = render(base);
        for (int d = 1; d < static_cast<int>(std::size(PianoModDests)); ++d) {
            std::vector<float> v = base;
            slots(v, piano::MOD_FIRST, d);
            compare("piano", PianoModDestNames[d], ref, render(v));
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
        const PartE want = std::string(style) == "Uplifting" ? PartE::CHOIR : PartE::PIANO;
        Score sc;
        double from = -1.0;
        for (uint64_t seed = 1; seed < 16 && from < 0.0; ++seed) {
            sc = composeTrack(*q, seed);
            for (const NoteEvent& n : sc.m_notes) if (n.m_part == want) { from = std::floor(n.m_beat / 4.0) * 4.0; break; }
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
 * 24 dB of it. With PARH_BANK_TRIMS=\<file\> every preset is played and the trims (PresetTrims.inl: the default's loudness
 * less the preset's, in tenths of a dB) are written to \<file\>.
 */
struct BankId {
    ModuleE m_module;   ///< the module
    int m_instance;    ///< its instance
};
/** @brief Every bank, in the order of Presets.h. */
const BankId Banks[PresetBanks] = {
    { ModuleE::KICK, 0 }, { ModuleE::SUB, 0 }, { ModuleE::PERC, 0 }, { ModuleE::BASS, 0 }, { ModuleE::ACID, 0 },
    { ModuleE::POLY, 0 }, { ModuleE::POLY, 1 }, { ModuleE::POLY, 2 }, { ModuleE::POLY, 3 }, { ModuleE::POLY, 4 }, { ModuleE::POLY, 5 },
    { ModuleE::PIANO, 0 }, { ModuleE::STRINGS, 0 }, { ModuleE::CHOIR, 0 }, { ModuleE::BRASS, 0 }, { ModuleE::TIMPANI, 0 },
    { ModuleE::SFX, 0 }, { ModuleE::CLOUD, 0 } };

/**
 * @brief The loudest 3 s of a short phrase on @p b's voice, the preset @p sp applied (null: the default sound), LUFS.
 * @param[in] b      the bank
 * @param[in] sp     the preset, or null
 * @param[in] quest  true for the Quest's limits
 * @return the loudness in LUFS
 */
double bankLoudness(const BankId& b, const SoundPreset* sp, bool quest = false)
{
    auto p = std::make_unique<ParamStore>();
    if (sp != nullptr) applyPreset(*p, b.m_module, b.m_instance, *sp);
    Score sc;
    sc.clear(136.0);
    sc.m_lengthBeats = 16.0;
    auto note = [&](double beat, double len, PartE part, int pitch, float v) { sc.m_notes.push_back(NoteEvent{ beat, len, part, pitch, v, 0, false, false }); };
    switch (b.m_module) {
    case ModuleE::KICK: for (int q = 0; q < 12; ++q) note(q, 0.25, PartE::KICK, 36, 1.0f); break;
    case ModuleE::SUB: for (int q = 0; q < 12; ++q) note(q + 0.5, 0.25, PartE::SUB, 33, 1.0f); break;
    case ModuleE::PERC: for (int s = 0; s < 48; ++s) note(0.25 * s, 0.1, PartE::PERC1, 42, s % 4 == 0 ? 0.9f : 0.6f); break;
    case ModuleE::BASS: case ModuleE::ACID:
        for (int s = 0; s < 24; ++s) note(0.5 * s, 0.4, b.m_module == ModuleE::BASS ? PartE::BASS : PartE::ACID, s % 3 == 0 ? 45 : 57, 0.8f);
        break;
    case ModuleE::POLY: {
        const PartE part = static_cast<PartE>(static_cast<int>(PartE::LEAD) + b.m_instance);
        if (b.m_instance == 4 || b.m_instance == 5) for (int k : { 57, 60, 64, 69 }) note(0.0, 11.5, part, k, 0.8f);   // the pad, the stab: a chord
        else for (int s = 0; s < 24; ++s) note(0.5 * s, 0.45, part, 64 + (s % 4) * 3, 0.8f);
        break;
    }
    case ModuleE::PIANO: for (int s = 0; s < 6; ++s) for (int k : { 48, 60, 64, 67 }) note(2.0 * s, 1.8, PartE::PIANO, k + (s % 2) * 2, 0.7f); break;
    case ModuleE::STRINGS: for (int k : { 43, 55, 59, 62 }) note(0.0, 11.5, PartE::STRINGS, k, 0.7f); break;
    case ModuleE::CHOIR: for (int k : { 57, 60, 64 }) note(0.0, 11.5, PartE::CHOIR, k, 0.7f); break;
    case ModuleE::BRASS: for (int s = 0; s < 3; ++s) for (int k : { 48, 55, 60 }) note(4.0 * s, 3.5, PartE::BRASS, k, 0.8f); break;
    case ModuleE::TIMPANI: for (int s = 0; s < 6; ++s) note(2.0 * s, 1.0, PartE::TIMPANI, 43 + (s % 2) * 7, 0.9f); break;
    case ModuleE::SFX: note(0.0, 8.0, PartE::FX, SfxBaseNote + static_cast<int>(SfxTypeE::RISER), 0.9f);
                      note(8.0, 4.0, PartE::FX, SfxBaseNote + static_cast<int>(SfxTypeE::IMPACT), 1.0f); break;
    case ModuleE::CLOUD:
        p->set(p->id(ModuleE::CLOUD, 0, cloud::LEVEL), 0.0f);   // (the cloud grains the pad: the pad plays, the cloud is heard)
        for (int k : { 57, 60, 64 }) note(0.0, 11.5, PartE::PAD, k, 0.8f);
        break;
    default: break;
    }
    sc.sort();
    auto e = std::make_unique<Engine>();
    e->params().copyValuesFrom(*p);
    if (quest) e->setQuality(Engine::QualityE::QUEST);
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
    return finite ? meter.report().m_shortTermMax : std::numeric_limits<double>::quiet_NaN();
}

/** @brief Every bank's presets through the engine against the default sound (see BankId). */
void testPresetBank()
{
    section("the factory banks");
    const DenormalGuard guard;
    int full = 0, unique = 0;
    for (const BankId& b : Banks) {
        const std::vector<SoundPreset>& ps = factoryPresets(b.m_module, b.m_instance);
        full += ps.size() == static_cast<size_t>(PresetsPerBank);
        std::set<std::string> names;
        for (const SoundPreset& s : ps) names.insert(s.m_name);
        unique += names.size() == ps.size();
    }
    check(full == PresetBanks, "eighteen banks of 1024 presets", fmt("%d of %d full", full, PresetBanks));
    check(unique == PresetBanks, "every name unique within its bank", fmt("%d of %d", unique, PresetBanks));
    std::string first;
    const int unknown = bankUnknownKeys(&first);
    check(unknown == 0, "every key a bank names is a knob of its module", fmt("%d unknown%s", unknown, unknown > 0 ? (": " + first).c_str() : ""));
    // A lane gets presets of its role only; every role has a group.
    int roleOk = 0;
    for (int role = 0; role < NumPercRoles; ++role) {
        Rng r;
        r.seed(static_cast<uint64_t>(role) + 11);
        const float style[5] = { 0.2f, 0.2f, 0.2f, 0.2f, 0.2f };
        bool ok = true;
        for (int k = 0; k < 20 && ok; ++k) {
            const int i = pickPreset(ModuleE::PERC, 0, style, role, r);
            ok = i >= 0 && ((factoryPresets(ModuleE::PERC, 0)[static_cast<size_t>(i)].m_roles >> role) & 1u) != 0u;
        }
        roleOk += ok;
    }
    check(roleOk == NumPercRoles, "a kit lane is given presets of its role (every role has a group)", fmt("%d of %d roles", roleOk, NumPercRoles));

    // The sample (or, with PARH_BANK_TRIMS, every preset): through the engine against the default sound.
    const char* trimsPath = std::getenv("PARH_BANK_TRIMS");
    const bool all = trimsPath != nullptr && trimsPath[0] != 0;
    // The jobs: every bank's default sound (-1) and its presets (all, or every 128th, a different one per bank), on
    // every core (each render its own engine).
    struct Job { int m_bank, m_preset; double m_loud; };
    std::vector<Job> jobs;
    for (int bi = 0; bi < PresetBanks; ++bi) {
        jobs.push_back({ bi, -1, 0.0 });
        for (int i = all ? 0 : (bi * 7) % 128; i < PresetsPerBank; i += all ? 1 : 128) jobs.push_back({ bi, i, 0.0 });
    }
    for (const BankId& b : Banks) (void)factoryPresets(b.m_module, b.m_instance);   // (built once, before the threads)
    std::atomic<size_t> next{ 0 };
    auto work = [&]() {
        const DenormalGuard g;
        for (size_t k = next++; k < jobs.size(); k = next++) {
            Job& j = jobs[k];
            const BankId& b = Banks[j.m_bank];
            j.m_loud = bankLoudness(b, j.m_preset < 0 ? nullptr : &factoryPresets(b.m_module, b.m_instance)[static_cast<size_t>(j.m_preset)]);
        }
    };
    std::vector<std::thread> pool;
    const unsigned n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    for (unsigned k = 0; k < n; ++k) pool.emplace_back(work);
    for (std::thread& th : pool) th.join();
    int played = 0, bad = 0;
    double worst = 0.0;
    std::string worstName;
    std::vector<std::vector<short>> trims(PresetBanks, std::vector<short>(PresetsPerBank, 0));
    std::vector<double> ref(PresetBanks, 0.0);
    for (const Job& j : jobs) if (j.m_preset < 0) ref[static_cast<size_t>(j.m_bank)] = j.m_loud;
    for (const Job& j : jobs) {
        if (j.m_preset < 0) continue;
        const BankId& b = Banks[j.m_bank];
        const SoundPreset& sp = factoryPresets(b.m_module, b.m_instance)[static_cast<size_t>(j.m_preset)];
        const double l = j.m_loud, d = l - ref[static_cast<size_t>(j.m_bank)];
        ++played;
        if (!std::isfinite(l) || l < -70.0 || std::fabs(d) > 24.0) {
            if (bad++ == 0) worstName = fmt("%s/%s: %.1f LUFS against %.1f", sp.m_group.c_str(), sp.m_name.c_str(), l, ref[static_cast<size_t>(j.m_bank)]);
        }
        if (std::isfinite(d) && std::fabs(d) > worst) worst = std::fabs(d);
        trims[static_cast<size_t>(j.m_bank)][static_cast<size_t>(j.m_preset)] = static_cast<short>(std::lround(std::clamp(std::isfinite(d) ? -d : 0.0, -24.0, 24.0) * 10.0));
    }
    check(bad == 0, "every preset played is finite, audible and within 24 dB of its module's default sound",
          fmt("%d played, %d out, largest distance %.1f dB%s", played, bad, worst, bad > 0 ? ("; first: " + worstName).c_str() : ""));
    if (all) {
        std::FILE* f = std::fopen(trimsPath, "wb");
        if (f != nullptr) {
            std::fprintf(f, "// Generated by parh_selftest testPresetBank with PARH_BANK_TRIMS=<this file> (Tests/selftest.cpp).\n"
                            "/// Each preset's level correction in tenths of a dB, 18 banks of 1024 (Presets.h's order).\n"
                            "static const short PresetTrimTenths[18][1024] = {\n");
            for (int bi = 0; bi < PresetBanks; ++bi) {
                std::fprintf(f, "{");
                for (int i = 0; i < PresetsPerBank; ++i) std::fprintf(f, "%d%s%s", trims[static_cast<size_t>(bi)][static_cast<size_t>(i)],
                                                                         i + 1 < PresetsPerBank ? "," : "", (i % 32 == 31 && i + 1 < PresetsPerBank) ? "\n" : "");
                std::fprintf(f, "}%s\n", bi + 1 < PresetBanks ? "," : "");
            }
            std::fprintf(f, "};\n");
            std::fclose(f);
        }
        check(f != nullptr, "the trims written", trimsPath);
    }
}



/**
 * User presets (Phase 6, userPresetText/userPresetKnobs): a sound saved from one kit lane loads into another exactly --
 * every knob a preset sets as it was, a knob the text does not name back at its default, the lane's own knobs (its
 * level, its pattern: presetLeaves) untouched -- and a voice's text into another voice of the same module.
 */
void testUserPreset()
{
    section("user presets");
    auto a = std::make_unique<ParamStore>();
    auto b = std::make_unique<ParamStore>();
    int exact = 0, total = 0, leftAlone = 0, leaves = 0;
    std::string bad;
    const struct { ModuleE m_m; int m_from, m_to, m_preset; } cases[] = { { ModuleE::PERC, 2, 7, 300 }, { ModuleE::POLY, 0, 3, 77 }, { ModuleE::BASS, 0, 0, 512 } };
    for (const auto& c : cases) {
        applyPreset(*a, c.m_m, c.m_from, factoryPresets(c.m_m, c.m_from)[static_cast<size_t>(c.m_preset)]);
        const std::string text = userPresetText(*a, c.m_m, c.m_from);
        // The target full of other values first: a knob the text leaves out must go back to its default.
        applyPreset(*b, c.m_m, c.m_to, factoryPresets(c.m_m, c.m_to)[static_cast<size_t>((c.m_preset + 101) % PresetsPerBank)]);
        std::vector<float> before(static_cast<size_t>(ParamStore::moduleCount(c.m_m)));
        for (int k = 0; k < ParamStore::moduleCount(c.m_m); ++k) {
            // (A preset leaves these alone; set one to a value of its own, to see it kept.)
            if (presetLeaves(c.m_m, k)) b->set(b->id(c.m_m, c.m_to, k), b->desc(b->id(c.m_m, c.m_to, k)).m_maxValue);
            before[static_cast<size_t>(k)] = b->get(b->id(c.m_m, c.m_to, k));
        }
        for (const auto& [k, v] : userPresetKnobs(*b, c.m_m, c.m_to, text)) b->set(b->id(c.m_m, c.m_to, k), v);
        for (int k = 0; k < ParamStore::moduleCount(c.m_m); ++k) {
            const float got = b->get(b->id(c.m_m, c.m_to, k));
            if (presetLeaves(c.m_m, k)) {
                ++leaves;
                leftAlone += got == before[static_cast<size_t>(k)];
                continue;
            }
            ++total;
            const float want = a->get(a->id(c.m_m, c.m_from, k));
            if (got == want) ++exact;
            else if (bad.empty()) bad = fmt("; first: %s %g, not %g", b->key(b->id(c.m_m, c.m_to, k)).c_str(), got, want);
        }
    }
    check(exact == total, "a user preset saved from one lane or voice loads into another, knob for knob", fmt("%d of %d%s", exact, total, bad.c_str()));
    check(leftAlone == leaves, "and leaves the knobs a preset leaves alone", fmt("%d of %d", leftAlone, leaves));
}

/**
 * The Quest's quality on the synths' presets (Phase 7): fewer unison oscillators and voices, and the filter models with
 * fewer Newton steps (Deck::setQuest). Every 32nd preset of the six polyphonic voices, the mid-bass and the 303 played at
 * both qualities: finite, and as loud at the Quest's as at the desktop's.
 */
void testQuestSounds()
{
    section("the synths' presets at the Quest's quality");
    const DenormalGuard guard;
    struct Job { int m_bank, m_preset; double m_desk, m_quest; };
    std::vector<Job> jobs;
    for (int bi = 0; bi < PresetBanks; ++bi) {
        const ModuleE m = Banks[bi].m_module;
        if (m != ModuleE::POLY && m != ModuleE::BASS && m != ModuleE::ACID) continue;
        for (int i = bi % 32; i < PresetsPerBank; i += 32) jobs.push_back({ bi, i, 0.0, 0.0 });
    }
    for (const BankId& b : Banks) (void)factoryPresets(b.m_module, b.m_instance);
    std::atomic<size_t> next{ 0 };
    auto work = [&]() {
        const DenormalGuard g;
        for (size_t k = next++; k < jobs.size(); k = next++) {
            Job& j = jobs[k];
            const BankId& b = Banks[j.m_bank];
            const SoundPreset* sp = &factoryPresets(b.m_module, b.m_instance)[static_cast<size_t>(j.m_preset)];
            j.m_desk = bankLoudness(b, sp, false);
            j.m_quest = bankLoudness(b, sp, true);
        }
    };
    std::vector<std::thread> pool;
    const unsigned n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    for (unsigned k = 0; k < n; ++k) pool.emplace_back(work);
    for (std::thread& th : pool) th.join();
    int finite = 0;
    double worst = 0.0, sum = 0.0;
    std::string worstName;
    for (const Job& j : jobs) {
        if (!std::isfinite(j.m_quest) || j.m_quest < -70.0) continue;
        ++finite;
        const double d = j.m_quest - j.m_desk;
        sum += std::fabs(d);
        if (std::fabs(d) > std::fabs(worst)) {
            worst = d;
            const SoundPreset& sp = factoryPresets(Banks[j.m_bank].m_module, Banks[j.m_bank].m_instance)[static_cast<size_t>(j.m_preset)];
            worstName = fmt("%s/%s", sp.m_group.c_str(), sp.m_name.c_str());
        }
    }
    check(finite == static_cast<int>(jobs.size()), "every one finite and audible at the Quest's quality", fmt("%d of %zu", finite, jobs.size()));
    check(std::fabs(worst) <= 3.0, "as loud as at the desktop's", fmt("mean |difference| %.2f dB, largest %+.2f dB (%s)",
                                                                         sum / std::max(1, finite), worst, worstName.c_str()));
}

/**
 * The opening (01.10.2026; the user: his mix "begann ... wieder mit einer Solo-Kick"): the kick never alone -- where it
 * enters, the closed hat plays with it (the references: where the kick plays in the first eight bars, the hats play with
 * it; the kick alone in 1 of 30) --, in tracks of every style and in mixes; a mix's first track may open without the
 * kick (on its atmosphere, nothing is mixed into it), every later one opens with it (its DJ intro).
 */
void testOpening()
{
    section("the opening: never the kick alone");
    auto p = std::make_unique<ParamStore>();
    // The first block of a deck's score (from @p from) with the kick on: does the closed hat play there too?
    auto kickWithHats = [](const Score& s, double from, double to) {
        for (const LayerBlock& b : s.m_layers) {
            if (b.m_beat < from - 1e-6 || b.m_beat >= to) continue;
            if (b.at(LayerE::KICK) == LayerStateE::ON) return b.at(LayerE::CLOSED_HAT) != LayerStateE::OFF;
        }
        return true;
    };
    int tracks = 0, withHats = 0;
    for (const char* style : { "Uplifting", "Progressive", "Dream House", "Acid", "Deep" }) {
        p->parseText(std::string("compose.style=") + style);
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            const Score s = composeTrack(*p, seed);
            ++tracks;
            withHats += kickWithHats(s, 0.0, s.m_lengthBeats);
        }
    }
    check(withHats == tracks, "a track's kick enters with the closed hat", fmt("%d of %d tracks", withHats, tracks));
    p->resetDefaults();
    int mixes = 0, mixHats = 0, openers = 0, kickless = 0, later = 0, laterKick = 0;
    for (uint64_t seed = 101; seed <= 112; ++seed) {
        SetInfo si;
        const SetScore set = composeSet(*p, seed, 12.0, nullptr, &si);
        ++mixes;
        bool ok = true;
        for (size_t i = 0; i < si.m_tracks.size(); ++i) {
            const SetTrack& tr = si.m_tracks[i];
            const Score& deck = set.m_decks[static_cast<size_t>(tr.m_deck)];
            ok = ok && kickWithHats(deck, tr.m_start, tr.m_end);
            // Its first block: the kick there or not.
            bool kickFirst = false;
            for (const LayerBlock& b : deck.m_layers)
                if (std::fabs(b.m_beat - tr.m_start) < 1e-6) kickFirst = b.at(LayerE::KICK) == LayerStateE::ON;
            if (i == 0) { ++openers; kickless += !kickFirst; }
            else { ++later; laterKick += kickFirst; }
        }
        mixHats += ok;
    }
    check(mixHats == mixes, "a mix's tracks: the kick enters with the closed hat", fmt("%d of %d mixes", mixHats, mixes));
    check(kickless >= 1 && laterKick == later, "a mix's first track may open on its atmosphere, every later one opens with its kick",
          fmt("first tracks without the kick %d of %d, later tracks with it %d of %d", kickless, openers, laterKick, later));
}

/**
 * The performer's "Breakdown now" and "Drop now" (Phase 6, planTrackRewritten): from an 8-bar line the plan turns to a
 * breakdown (its build, a drop) or to a drop at once -- the vacuum in the bar before it --, the whole a multiple of 32
 * bars; and every note before the line is what it was (the drums, the bass, the pad, the pluck, the arp: the voices
 * whose notes do not reach ahead -- the lead's second motif and the riser do), before a drop at once up to its eight
 * bars of approach (the harmony's turnaround VI-VII-i before every drop, PLAN 6.5: the drop arrives prepared).
 */
void testRewrite()
{
    section("breakdown now, drop now");
    auto p = std::make_unique<ParamStore>();
    p->parseText("compose.style=Uplifting");
    int shaped = 0, kept = 0, total = 0;
    std::string bad;
    for (uint64_t seed : { 3ull, 8ull }) {
        const Score base = composeTrack(*p, seed);
        for (SectionKindE kind : { SectionKindE::BREAKDOWN, SectionKindE::DROP }) {
            const int bar = 64;
            TrackRequest req;
            req.m_rewriteBar = bar;
            req.m_rewriteKind = kind;
            const Score re = composeTrack(*p, seed, req);
            ++total;
            // The form from the line on.
            int at = -1;
            for (size_t i = 0; i < re.m_sections.size(); ++i) if (re.m_sections[i].m_beat == 4.0 * bar) at = static_cast<int>(i);
            bool ok = at >= 0 && re.m_sections[static_cast<size_t>(at)].m_kind == kind && static_cast<int>(re.m_lengthBeats / 4.0) % 32 == 0;
            if (ok && kind == SectionKindE::BREAKDOWN)
                ok = static_cast<size_t>(at) + 2 < re.m_sections.size() && re.m_sections[static_cast<size_t>(at) + 1].m_kind == SectionKindE::BUILD
                  && re.m_sections[static_cast<size_t>(at) + 2].m_kind == SectionKindE::DROP;
            if (ok && kind == SectionKindE::DROP) {
                bool quiet = true;   // the vacuum: no kick on the bar's last beat before the drop
                for (const NoteEvent& n : re.m_notes) if (n.m_part == PartE::KICK && n.m_beat >= 4.0 * bar - 1.0 && n.m_beat < 4.0 * bar) quiet = false;
                ok = quiet;
            }
            shaped += ok;
            // The notes before the line (and its last beat, the vacuum's).
            const double upTo = kind == SectionKindE::DROP ? 4.0 * (bar - 8) : 4.0 * bar - 4.0;
            auto before = [&](const Score& s) {
                std::vector<std::tuple<double, int, int, float>> out;
                for (const NoteEvent& n : s.m_notes) {
                    if (n.m_beat >= upTo) continue;
                    if (n.m_part == PartE::LEAD || n.m_part == PartE::COUNTER || n.m_part == PartE::PIANO || n.m_part == PartE::STRINGS
                        || n.m_part == PartE::CHOIR || n.m_part == PartE::BRASS || n.m_part == PartE::TIMPANI || n.m_part == PartE::FX) continue;
                    out.push_back({ n.m_beat, static_cast<int>(n.m_part), n.m_pitch, n.m_velocity });
                }
                return out;
            };
            const bool same = before(base) == before(re);
            kept += same;
            if (!ok || !same) bad += fmt(" seed %llu %s:%s%s", static_cast<unsigned long long>(seed), kind == SectionKindE::DROP ? "drop" : "breakdown",
                                         ok ? "" : " form", same ? "" : " notes");
        }
    }
    check(shaped == total, "from the line: the breakdown with its build and drop, or the drop at once after its vacuum",
          fmt("%d of %d%s", shaped, total, bad.c_str()));
    check(kept == total, "every note before the line as it was (before a drop at once: before its approach)", fmt("%d of %d", kept, total));
    // A second "now" keeps the first (Phase 7, TrackRequest::earlier): a breakdown from bar 48, then from bar 96, in the
    // drop it led to, another -- the first breakdown stays, and every note before the second line is the first
    // rewrite's.
    int chained = 0, chainKept = 0;
    std::string chainBad;
    for (uint64_t seed : { 3ull, 8ull }) {
        TrackRequest one;
        one.m_rewriteBar = 48;
        one.m_rewriteKind = SectionKindE::BREAKDOWN;
        const Score first = composeTrack(*p, seed, one);
        TrackRequest two;
        two.m_rewriteBar = 96;
        two.m_rewriteKind = SectionKindE::BREAKDOWN;
        two.m_earlier = { { 48, SectionKindE::BREAKDOWN } };
        const Score second = composeTrack(*p, seed, two);
        auto kindAt = [](const Score& s, int bar) {
            for (const Section& x : s.m_sections) if (x.m_beat == 4.0 * bar) return static_cast<int>(x.m_kind);
            return -1;
        };
        const int k48 = kindAt(second, 48), k96 = kindAt(second, 96);
        const bool shape = k48 == static_cast<int>(SectionKindE::BREAKDOWN)
                        && (k96 == static_cast<int>(SectionKindE::BREAKDOWN) || k96 == static_cast<int>(SectionKindE::BREAK))
                        && kindAt(first, 88) == static_cast<int>(SectionKindE::DROP);
        chained += shape;
        auto upTo = [](const Score& s) {
            std::vector<std::tuple<double, int, int, float>> out;
            for (const NoteEvent& n : s.m_notes) {
                if (n.m_beat >= 4.0 * 96 - 4.0) continue;
                if (n.m_part == PartE::LEAD || n.m_part == PartE::COUNTER || n.m_part == PartE::PIANO || n.m_part == PartE::STRINGS
                    || n.m_part == PartE::CHOIR || n.m_part == PartE::BRASS || n.m_part == PartE::TIMPANI || n.m_part == PartE::FX) continue;
                out.push_back({ n.m_beat, static_cast<int>(n.m_part), n.m_pitch, n.m_velocity });
            }
            return out;
        };
        const bool same = upTo(first) == upTo(second);
        chainKept += same;
        if (!shape || !same) chainBad += fmt(" seed %llu:%s%s", static_cast<unsigned long long>(seed), shape ? "" : " form", same ? "" : " notes");
    }
    check(chained == 2 && chainKept == 2, "a second now keeps the first: its breakdown, and every note before the second line",
          fmt("%d and %d of 2%s", chained, chainKept, chainBad.c_str()));
}

/**
 * The handover (Phase 7, Handover.h): the performer's "now" on the Quest without a gap -- the rewritten track in a second
 * engine, sought four bars back and rendered up to where the first one plays, then faded over. A worker thread does the
 * pre-roll while this thread plays the audio thread's part at about five times real time. After the fade the output is
 * the second engine's, bit for bit as if it had played from its seek alone (the handover does not touch its timeline);
 * before it the first engine's, untouched; the fade begins on a beat and before the first note the two scores play
 * differently, nothing clicks, the level does not dip (the two engines' free-running phases are not the same, Handover.h),
 * and after it the music is as loud as the first engine's would have been.
 */
void testHandover()
{
    section("the handover to a rewritten track");
    auto p = std::make_unique<ParamStore>();
    p->parseText("compose.style=Uplifting");
    const uint64_t seed = 3;
    const int bar = 72;
    const Score base = composeTrack(*p, seed);
    TrackRequest req;
    req.m_rewriteBar = bar;
    req.m_rewriteKind = SectionKindE::BREAKDOWN;
    const Score re = composeTrack(*p, seed, req);
    const double fs = 48000.0;
    const int block = 256;
    auto make = [&](const Score& s) {
        auto e = std::make_unique<Engine>();
        e->params().copyValuesFrom(*p);
        e->setLive(true);
        e->prepare(fs, block);
        e->load(s);
        return e;
    };
    auto a = make(base), b = make(re), alone = make(base), ref = make(re);
    Handover h;
    h.prepare(block);
    std::vector<float> L(block), R(block);
    // The first engine plays to bar 60; the second is sought four bars back from there.
    const int64_t bar60 = static_cast<int64_t>(std::llround(base.m_tempo.secondsAt(4.0 * 60) * fs));
    while (a->samplePosition() < bar60) {
        h.process(*a, *b, L.data(), R.data(), block);
        alone->process(L.data(), R.data(), block);
    }
    const int64_t start = a->samplePosition();
    const double seekBeat = 4.0 * 56;
    b->seek(seekBeat);
    ref->seek(seekBeat);
    const double diff = firstDifference(base, re, seekBeat);
    const int64_t deadline = static_cast<int64_t>(std::llround(base.m_tempo.secondsAt(std::min(diff, 4.0 * bar)) * fs)) - Handover::Fade;
    std::atomic<bool> done{ false }, ok{ false };
    const auto nextBeat = [&](int64_t s) {
        const double beat = std::ceil(base.m_tempo.beatAt(static_cast<double>(s) / fs) - 1e-9);
        return static_cast<int64_t>(std::llround(base.m_tempo.secondsAt(beat) * fs));
    };
    std::thread worker([&] { ok = h.handOver(*b, deadline, {}, nextBeat); done = true; });
    // The audio thread's part, paced: its blocks and those of the first engine alone, which it should match until the fade.
    std::vector<float> out, lone;
    bool switched = false;
    Engine* front = a.get();
    Engine* back = b.get();
    const int64_t end = start + static_cast<int64_t>(30.0 * fs);
    while (front->samplePosition() < end) {
        if (h.process(*front, *back, L.data(), R.data(), block)) {
            std::swap(front, back);
            switched = true;
        }
        out.insert(out.end(), L.begin(), L.end());
        alone->process(L.data(), R.data(), block);
        lone.insert(lone.end(), L.begin(), L.end());
        if (!done) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    worker.join();
    const int64_t switchedAt = switched ? h.lastTaken() : -1;
    check(ok && switchedAt >= 0 && switchedAt <= deadline
              && std::fabs(base.m_tempo.beatAt(switchedAt / fs) - std::round(base.m_tempo.beatAt(switchedAt / fs))) < 1e-4,
          "the second engine took over on a beat, before the scores differ",
          fmt("at %.3f s of the track (beat %.3f), the first difference at bar %.2f, deadline %.2f s", switchedAt / fs,
              base.m_tempo.beatAt(switchedAt / fs), diff / 4.0, deadline / fs));
    if (switchedAt < 0) return;
    // After the fade: the second engine's own render from its seek, bit for bit.
    std::vector<float> want;
    while (ref->samplePosition() < start) ref->process(L.data(), R.data(), std::min<int>(block, static_cast<int>(start - ref->samplePosition())));
    const size_t fadeEnd = static_cast<size_t>(switchedAt - start) + Handover::Fade;
    int64_t pos = start;
    while (pos < end) {
        ref->process(L.data(), R.data(), block);
        want.insert(want.end(), L.begin(), L.end());
        pos += block;
    }
    size_t same = 0, compared = 0;
    for (size_t i = fadeEnd; i < out.size() && i < want.size(); ++i, ++compared) same += out[i] == want[i];
    check(compared > 0 && same == compared, "after the fade: the second engine as it plays alone, bit for bit",
          fmt("%zu of %zu samples", same, compared));
    // During the fade and before it: the first engine's music. The largest step from one sample to the next round the
    // fade against the largest the first engine alone makes there, and the difference to it.
    const size_t f0 = static_cast<size_t>(switchedAt - start);
    const size_t w0 = f0 > 4800 ? f0 - 4800 : 0, w1 = std::min(out.size(), f0 + 4800 + Handover::Fade);
    float stepOut = 0.0f, stepLone = 0.0f;
    for (size_t i = w0 + 1; i < w1; ++i) {
        stepOut = std::max(stepOut, std::fabs(out[i] - out[i - 1]));
        stepLone = std::max(stepLone, std::fabs(lone[i] - lone[i - 1]));
    }
    check(stepOut <= 1.25f * stepLone + 1e-3f, "no click: the largest step round the fade as the first engine's own",
          fmt("%.4f against %.4f", stepOut, stepLone));
    // The level through the fade, 2.7 ms windows, against the quieter of the two engines alone there.
    const auto rms = [](const std::vector<float>& x, size_t from, size_t n) {
        double s = 0.0;
        for (size_t i = from; i < from + n && i < x.size(); ++i) s += static_cast<double>(x[i]) * x[i];
        return std::sqrt(s / static_cast<double>(n)) + 1e-12;
    };
    double dip = 0.0;
    for (size_t i = f0; i + 128 <= f0 + Handover::Fade && i + 128 <= out.size(); i += 128)
        dip = std::min(dip, 20.0 * std::log10(rms(out, i, 128) / std::min(rms(lone, i, 128), rms(want, i, 128))));
    check(dip > -3.0, "no hole: through the fade the level stays with the two engines'", fmt("at most %.2f dB under the quieter", dip));
    // After it, until the rewrite: the same music, as loud (what the second lacks: notes held from before its seek).
    const size_t after = f0 + Handover::Fade;
    const size_t span = std::min<size_t>(static_cast<size_t>(2.0 * fs), out.size() - after);
    const double lvl = 20.0 * std::log10(rms(out, after, span) / rms(lone, after, span));
    check(std::fabs(lvl) < 0.5, "after it as loud as the first engine would have been", fmt("%+.2f dB over 2 s", lvl));
    // Before the handover began: the first engine, untouched.
    size_t before = 0;
    for (size_t i = 0; i < f0; ++i) before += out[i] == lone[i];
    check(before == f0, "until it the first engine as it plays alone", fmt("%zu of %zu samples", before, f0));
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
    auto gesturesOn = [&](const Score& sc, ModuleE m, int param) {
        const int id = p->id(m, 0, param);
        int n = 0;
        for (const Gesture& g : sc.m_gestures) n += g.m_param == id;
        return n;
    };
    auto knobOf = [&](const Score& sc, ModuleE m, int param, float& value) {
        const int id = p->id(m, 0, param);
        for (const KnobSet& k : sc.m_knobs) if (k.m_param == id) { value = k.m_value; return true; }
        return false;
    };
    {
        p->parseText("compose.style=Acid");
        const Score sc = composeTrack(*p, 3);
        int acid = 0;
        for (const NoteEvent& n : sc.m_notes) acid += n.m_part == PartE::ACID;
        const int curves = std::min({ gesturesOn(sc, ModuleE::ACID, synth::CUTOFF), gesturesOn(sc, ModuleE::ACID, synth::RESONANCE),
                                      gesturesOn(sc, ModuleE::ACID, synth::ENV_AMOUNT), gesturesOn(sc, ModuleE::ACID, synth::DECAY) });
        check(acid > 500 && curves >= 4, "Acid: the 303 plays, its cutoff, resonance, envelope and decay in curves", fmt("%d notes, %d curves each at least", acid, curves));
    }
    {
        p->parseText("compose.style=Deep");
        const Score sc = composeTrack(*p, 3);
        float cloud = -60.0f;
        const bool has = knobOf(sc, ModuleE::CLOUD, cloud::LEVEL, cloud);
        int drift = 0;
        for (const Gesture& g : sc.m_gestures) drift += g.m_param == p->id(ModuleE::POLY, static_cast<int>(PolyInstanceE::PAD), poly::PAN);
        check(has && cloud > -30.0f && drift > 4, "Deep: the granular cloud and the pad drifting", fmt("cloud %.0f dB, %d pan waves", cloud, drift));
    }
    {
        p->parseText("compose.style=Dream House");
        const Score sc = composeTrack(*p, 3);
        int piano = 0, lead = 0;
        for (const NoteEvent& n : sc.m_notes) { piano += n.m_part == PartE::PIANO; lead += n.m_part == PartE::LEAD; }
        check(piano > 50 && lead == 0, "Dream House: the motif on the piano", fmt("%d piano notes", piano));
    }
    {
        p->parseText("compose.style=Uplifting");
        int with = 0;
        for (uint64_t seed = 1; seed <= 8; ++seed) {
            const Score sc = composeTrack(*p, seed);
            bool o = false;
            for (const NoteEvent& n : sc.m_notes) o = o || n.m_part == PartE::STRINGS;
            with += o;
        }
        check(with >= 2 && with <= 7, "Uplifting: the orchestra in some tracks (Cinematic), not in all", fmt("%d of 8", with));
    }
    {
        const StyleProfile m = morphProfile(styleProfile(StyleE::UPLIFTING), styleProfile(StyleE::DEEP), 0.5f);
        const StyleProfile& a = styleProfile(StyleE::UPLIFTING);
        const StyleProfile& b = styleProfile(StyleE::DEEP);
        const bool between = (m.m_bpmLow - a.m_bpmLow) * (m.m_bpmLow - b.m_bpmLow) <= 0.0f && (m.m_gapLu - a.m_gapLu) * (m.m_gapLu - b.m_gapLu) <= 0.0f
                          && (m.m_orchestra - a.m_orchestra) * (m.m_orchestra - b.m_orchestra) <= 0.0f;
        check(between, "a morph lands between its two profiles", fmt("tempo from %.0f, gap %.1f LU", m.m_bpmLow, m.m_gapLu));
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
    const size_t n = si.m_tracks.size();
    check(n >= 8, "a set of seventy minutes has its tracks", fmt("%zu tracks", n));
    int alternate = 0, tempoOk = 0, wheel = 0, swapOk = 0, clean = 0, styles = 0;
    std::set<std::string> seen;
    for (size_t i = 0; i < n; ++i) {
        const SetTrack& t = si.m_tracks[i];
        seen.insert(t.m_info.m_style);
        if (i == 0) continue;
        const SetTrack& a = si.m_tracks[i - 1];
        alternate += t.m_deck != a.m_deck;
        tempoOk += std::fabs(t.m_info.m_bpm - a.m_info.m_bpm) <= 2.0f;
        const int d = ((t.m_info.m_key - a.m_info.m_key) % 12 + 12) % 12;
        const bool am = a.m_info.m_scale != static_cast<int>(ScaleE::IONIAN), tm = t.m_info.m_scale != static_cast<int>(ScaleE::IONIAN);
        const bool camelot = (am == tm && (d == 0 || d == 5 || d == 7)) || (am && !tm && d == 3) || (!am && tm && d == 9);
        wheel += camelot;
        swapOk += std::fabs(a.m_swapOut - t.m_swapIn) < 1e-9 && t.m_swapIn > t.m_start && t.m_swapIn < a.m_end && t.m_start + t.m_info.m_introBars * 4.0 <= a.m_end + 1e-9;
        bool breakdownBefore = false;
        for (const auto& b : t.m_info.m_breakdowns) breakdownBefore = breakdownBefore || t.m_start + b.first * 4.0 < t.m_swapIn;
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
    const double at = set.m_decks[0].m_tempo.secondsAt(si.m_tracks[1].m_swapIn) - 3.0;
    auto render = [&](int block) {
        auto e = std::make_unique<Engine>();
        e->params().copyValuesFrom(*p);
        e->prepare(48000.0, block);
        e->loadSet(set);
        e->seek(set.m_decks[0].m_tempo.beatAt(at));
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

/** @brief A section of the tests, as main() can run it alone. */
struct TestSection {
    const char* m_name;   ///< its name, as the command line names it
    std::function<void()> m_fn;   ///< the section
};

/** The keyboard (01.10.2026, Engine::queueLive): with the composer off nothing generated sounds; a played key sounds on
 *  the voice the keyboard plays, from its sample; without live play the keys do nothing. */
void testKeyboard()
{
    section("keyboard (live)");
    auto p = std::make_unique<ParamStore>();
    const Score score = composeTrack(*p, 21);
    auto run = [&](bool live, const char* knobs, bool key) {
        auto e = std::make_unique<Engine>();
        e->params().parseText(knobs);
        e->setLive(live);
        e->prepare(48000.0, 256);
        e->load(score);
        e->seek(128.0);
        std::vector<float> L(256), R(256);
        double sum = 0.0;
        for (int b = 0; b < 48000 * 4 / 256; ++b) {
            if (key && b == 100) e->queueLive(17, 60, 110, 0, true);   // middle C on the lead, 17 samples into the block
            if (key && b == 300) e->queueLive(3, 60, 0, 0, false);
            e->process(L.data(), R.data(), 256);
            if (b >= 100)
                for (int i = 0; i < 256; ++i) sum += static_cast<double>(L[static_cast<size_t>(i)]) * L[static_cast<size_t>(i)]
                                                   + static_cast<double>(R[static_cast<size_t>(i)]) * R[static_cast<size_t>(i)];
        }
        return sum;
    };
    const double composed = run(true, "", false);
    const double silent = run(true, "perform.composer=0", false);
    const double played = run(true, "perform.composer=0; perform.keyboard_part=4", true);
    const double offline = run(false, "perform.composer=0; perform.keyboard_part=4", true);
    const double plainOffline = run(false, "", false);
    check(silent < 1e-4 * composed, "the composer off: nothing generated sounds", fmt("%.3g of %.3g", silent, composed));
    check(played > 1e3 * std::max(silent, 1e-9), "a played key sounds on the lead", fmt("energy %.3g, silence %.3g", played, silent));
    check(offline == plainOffline, "without live play the keyboard and the composer switch do nothing (renders, exports)",
          fmt("%.6g against %.6g", offline, plainOffline));
}

/** MIDI out (02.10.2026, NoteTap.h): in live play every deck writes the composer notes it plays into the tap, each on its
 *  sample inside the block, a poly voice with the sample its gate ends; a muted group's notes are not written, and with
 *  the composer off nothing is. */
void testNoteTap()
{
    section("MIDI out (note tap)");
    auto p = std::make_unique<ParamStore>();
    const Score score = composeTrack(*p, 21);
    struct Count { int m_ons = 0, m_kicks = 0, m_polyOffs = 0, m_polyOns = 0; bool m_inBlock = true, m_offsAfter = true; };
    auto run = [&](const char* knobs) {
        auto e = std::make_unique<Engine>();
        e->params().parseText(knobs);
        e->setLive(true);
        e->prepare(48000.0, 256);
        e->load(score);
        e->seek(128.0);
        auto tap = std::make_unique<NoteTap>();
        e->setNoteTap(tap.get());
        std::vector<float> L(256), R(256);
        Count c;
        for (int b = 0; b < 48000 * 8 / 256; ++b) {
            tap->clear();
            const int64_t s0 = e->samplePosition();
            e->process(L.data(), R.data(), 256);
            for (int i = 0; i < tap->m_count; ++i) {
                const NoteTap::Note& nt = tap->m_notes[i];
                if (nt.m_sample < s0 || nt.m_sample >= s0 + 256) c.m_inBlock = false;
                if (nt.m_velocity == 0) continue;
                ++c.m_ons;
                if (nt.m_part == static_cast<uint8_t>(PartE::KICK)) ++c.m_kicks;
                if (polyOf(static_cast<PartE>(nt.m_part)) >= 0) {
                    ++c.m_polyOns;
                    if (nt.m_offSample > nt.m_sample) ++c.m_polyOffs;
                }
                if (nt.m_offSample >= 0 && nt.m_offSample <= nt.m_sample) c.m_offsAfter = false;
            }
        }
        return c;
    };
    const Count all = run("");
    const Count noKick = run("perform.mute_kick=1");
    const Count off = run("perform.composer=0");
    check(all.m_kicks >= 8 && all.m_ons > all.m_kicks, "the composer's notes are tapped, the kick among them",
          fmt("%d notes, %d kicks, %d of the poly voices", all.m_ons, all.m_kicks, all.m_polyOns));
    check(all.m_inBlock && noKick.m_inBlock, "every tapped note lies in the block it was played in");
    check(all.m_offsAfter && all.m_polyOffs == all.m_polyOns, "a note without an off of its own carries one after it (a poly voice: its gate)",
          fmt("%d of %d", all.m_polyOffs, all.m_polyOns));
    check(noKick.m_kicks == 0 && noKick.m_ons > 0, "a muted group's notes are not tapped", fmt("%d kicks of %d", noKick.m_kicks, noKick.m_ons));
    check(off.m_ons == 0, "the composer off: nothing tapped", fmt("%d notes", off.m_ons));
}

/** The family jam (02.10.2026, the plugin's Jam.h): a follower's engine plays its pitched parts the jam's transposition
 *  away, note for note, and the kit as written; in the leader's break the rhythm's foundation is out. */
void testJam()
{
    section("family jam (a follower's engine)");
    auto p = std::make_unique<ParamStore>();
    const Score score = composeTrack(*p, 21);
    struct Got {
        std::vector<std::pair<int, int>> m_pitched;   ///< (part, pitch) of every pitched note-on, in order
        std::vector<int> m_kit;                       ///< the kit's note-on pitches, in order
        int m_out = 0;                                ///< note-ons of the parts a break takes out
    };
    auto isPitched = [](PartE part) { return part == PartE::SUB || part == PartE::BASS || part == PartE::ACID || part == PartE::LEAD || part == PartE::COUNTER || part == PartE::PLUCK || part == PartE::ARP || part == PartE::PAD || part == PartE::STAB || part == PartE::PIANO || part == PartE::STRINGS || part == PartE::CHOIR || part == PartE::BRASS || part == PartE::TIMPANI; };
    auto isOut = [](PartE part) { return part == PartE::KICK || part == PartE::SUB || part == PartE::BASS || part == PartE::ACID; };
    auto run = [&](int transpose, bool rhythmOut) {
        auto e = std::make_unique<Engine>();
        e->setLive(true);
        e->prepare(48000.0, 256);
        e->load(score);
        e->seek(128.0);
        e->setJam(transpose, 1.0f, rhythmOut);
        auto tap = std::make_unique<NoteTap>();
        e->setNoteTap(tap.get());
        std::vector<float> L(256), R(256);
        Got g;
        for (int b = 0; b < 48000 * 8 / 256; ++b) {
            tap->clear();
            e->process(L.data(), R.data(), 256);
            for (int i = 0; i < tap->m_count; ++i) {
                const NoteTap::Note& nt = tap->m_notes[i];
                if (nt.m_velocity == 0) continue;
                const PartE part = static_cast<PartE>(nt.m_part);
                if (isOut(part)) ++g.m_out;
                if (isPitched(part)) g.m_pitched.emplace_back(nt.m_part, nt.m_pitch);
                else g.m_kit.push_back(nt.m_pitch);
            }
        }
        return g;
    };
    const Got plain = run(0, false), up = run(3, false), down = run(-5, false), brk = run(0, true);
    bool shifted = !plain.m_pitched.empty() && up.m_pitched.size() == plain.m_pitched.size() && down.m_pitched.size() == plain.m_pitched.size();
    for (size_t i = 0; shifted && i < plain.m_pitched.size(); ++i)
        shifted = up.m_pitched[i].first == plain.m_pitched[i].first && up.m_pitched[i].second == std::min(127, plain.m_pitched[i].second + 3)
               && down.m_pitched[i].second == std::max(0, plain.m_pitched[i].second - 5);
    check(shifted, "the pitched parts play the jam's transposition, note for note (+3 and -5)",
          fmt("%d pitched notes", static_cast<int>(plain.m_pitched.size())));
    check(up.m_kit == plain.m_kit && !plain.m_kit.empty(), "the kit plays as written", fmt("%d hits", static_cast<int>(plain.m_kit.size())));
    check(plain.m_out > 0 && brk.m_out == 0, "in the leader's break the rhythm's foundation is out",
          fmt("%d notes as written, %d in the break", plain.m_out, brk.m_out));
}

/** The keyboard's split (02.10.2026): a key that names its target plays that voice, whatever Keyboard Plays says --
 *  with Keyboard Plays off a plain key is silent, a key naming Lead sounds; its release finds it. */
void testKeySplit()
{
    section("keyboard split (a key names its voice)");
    auto p = std::make_unique<ParamStore>();
    const Score score = composeTrack(*p, 21);
    auto run = [&](int target) {
        auto e = std::make_unique<Engine>();
        e->params().parseText("perform.composer=0");
        e->setLive(true);
        e->prepare(48000.0, 256);
        e->load(score);
        e->seek(128.0);
        std::vector<float> L(256), R(256);
        double held = 0.0, after = 0.0;
        for (int b = 0; b < 48000 * 4 / 256; ++b) {
            if (b == 50) e->queueLive(5, 72, 110, 0, true, target);
            if (b == 200) e->queueLive(5, 72, 0, 0, false, target);
            e->process(L.data(), R.data(), 256);
            double sum = 0.0;
            for (int i = 0; i < 256; ++i) sum += static_cast<double>(L[static_cast<size_t>(i)]) * L[static_cast<size_t>(i)];
            if (b >= 60 && b < 200) held += sum;
            if (b >= 650) after += sum;
        }
        return std::pair<double, double>(held, after);
    };
    const auto plain = run(-1);
    const auto named = run(perform::keys::LEAD);
    check(plain.first < 1e-9, "Keyboard Plays off: a plain key is silent", fmt("%.3g", plain.first));
    check(named.first > 1e-3, "a key naming its voice plays it", fmt("%.3g", named.first));
    check(named.second < 0.05 * named.first, "and its release ends it", fmt("%.3g after, %.3g held", named.second, named.first));
}

/** @brief Every section, in the order they run. */
const TestSection Sections[] = {
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
    { "testQuestSounds", testQuestSounds },
    { "testUserPreset", testUserPreset },
    { "testRewrite", testRewrite },
    { "testHandover", testHandover },
    { "testSubGenres", testSubGenres },
    { "testSet", testSet },
    { "testOpening", testOpening },
    { "testKeyboard", testKeyboard },
    { "testNoteTap", testNoteTap },
    { "testKeySplit", testKeySplit },
    { "testJam", testJam },
};

} // namespace

/**
 * @brief Runs every section, or those the command line names; the exit code is the number of failures.
 * @param[in]     argc  the number of arguments
 * @param[in,out] argv  the arguments: sections to run
 * @return the number of failures
 */
int main(int argc, char** argv)
{
    std::string only;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            for (const TestSection& s : Sections) std::printf("%s\n", s.m_name);
            return 0;
        }
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = std::string(",") + argv[++i] + ",";
    }
    for (const TestSection& s : Sections)
        if (only.empty() || only.find(std::string(",") + s.m_name + ",") != std::string::npos) s.m_fn();
    return finish();
}
