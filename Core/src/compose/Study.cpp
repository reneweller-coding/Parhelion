/**
 * @file Study.cpp
 * @brief The study of Phases 1 and 2 (Study.h): a fixed Uplifting form with its layer matrix, harmony, pump and automation.
 */
#include "parh/compose/Study.h"
#include "parh/Dsp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace parh {

namespace {

constexpr int kBars = 104;
constexpr int kBlocks = kBars / 8;
constexpr double beatOf(int bar) { return 4.0 * bar; }

using L = Layer;
using S = LayerState;

/** @brief The layer matrix of the study, block by block (Study.h). */
struct BlockPlan {
    int bar;
    std::array<S, kNumLayers> st;
    float energy;
};

std::array<S, kNumLayers> states(std::initializer_list<std::pair<L, S>> on)
{
    std::array<S, kNumLayers> a{};
    for (const auto& [l, s] : on) a[static_cast<size_t>(l)] = s;
    return a;
}

std::vector<BlockPlan> studyPlan()
{
    const S on = S::On, fi = S::Filtered;
    std::vector<BlockPlan> b;
    b.push_back({ 0, states({ { L::Kick, on } }), 2.0f });
    b.push_back({ 8, states({ { L::Kick, on }, { L::ClosedHat, on } }), 3.5f });
    b.push_back({ 16, states({ { L::Kick, on }, { L::Sub, on }, { L::Bass, on }, { L::ClosedHat, on }, { L::OpenHat, on } }), 5.0f });
    b.push_back({ 24, states({ { L::Kick, on }, { L::Sub, on }, { L::Bass, on }, { L::ClosedHat, on }, { L::OpenHat, on },
                               { L::Clap, on }, { L::Pluck, on }, { L::Pad, fi }, { L::Crash, on } }), 5.5f });
    b.push_back({ 32, states({ { L::Pad, on }, { L::Pluck, fi } }), 3.0f });
    b.push_back({ 40, states({ { L::Pad, on }, { L::Pluck, fi }, { L::Lead, fi } }), 4.5f });
    b.push_back({ 48, states({ { L::Pad, on }, { L::Pluck, on }, { L::Lead, fi }, { L::Perc, on } }), 7.0f });
    b.push_back({ 56, states({ { L::Kick, on }, { L::Sub, on }, { L::Bass, on }, { L::ClosedHat, on }, { L::OpenHat, on },
                               { L::Clap, on }, { L::Ride, on }, { L::Crash, on }, { L::Pluck, on }, { L::Pad, on }, { L::Lead, on } }), 9.0f });
    b.push_back({ 64, b.back().st, 9.5f });
    b.push_back({ 72, states({ { L::Kick, on }, { L::Sub, on }, { L::Bass, on }, { L::ClosedHat, on }, { L::OpenHat, on },
                               { L::Clap, on }, { L::Ride, on }, { L::Crash, on }, { L::Pluck, on }, { L::Pad, on }, { L::Lead, on },
                               { L::Arp, on } }), 10.0f });
    b.push_back({ 80, b.back().st, 10.0f });
    b.push_back({ 88, states({ { L::Kick, on }, { L::Sub, on }, { L::Bass, on }, { L::ClosedHat, on }, { L::OpenHat, on },
                               { L::Clap, on }, { L::Pluck, fi } }), 5.0f });
    b.push_back({ 96, states({ { L::Kick, on }, { L::Sub, on }, { L::Bass, on }, { L::ClosedHat, on } }), 3.0f });
    return b;
}

/** @brief A chord: its root in semitones above the key and its three tones (root, third, fifth) in semitones. */
struct Chord { int root; int tones[3]; };

Chord chordOn(const ScaleDef& sc, int degree)
{
    Chord c{};
    for (int k = 0; k < 3; ++k) {
        const int idx = degree + 2 * k;
        c.tones[k] = sc.steps[idx % 7] + 12 * (idx / 7);
    }
    c.root = c.tones[0];
    return c;
}

/** @brief The pitch of pitch class @p pc (semitones above C) at or above @p floor. */
int atOrAbove(int pc, int floor)
{
    int p = floor - ((floor - pc) % 12 + 12) % 12;
    if (p < floor) p += 12;
    return p;
}

/** @brief The pad's four voices over @p c: third or fifth at the bottom from about A3, the smallest movement from @p prev. */
std::array<int, 4> voicePad(const Chord& c, int key, const std::array<int, 4>& prev, bool first)
{
    std::array<int, 4> best{};
    int bestCost = 1 << 30;
    for (int bottom = 1; bottom <= 2; ++bottom) {   // third or fifth, never the root (Dok. 4)
        std::array<int, 4> v{};
        v[0] = atOrAbove((key + c.tones[bottom]) % 12, 57);
        int t = bottom;
        for (int k = 1; k < 4; ++k) {
            t = (t + 1) % 3;
            v[static_cast<size_t>(k)] = atOrAbove((key + c.tones[t]) % 12, v[static_cast<size_t>(k - 1)] + 1);
        }
        int cost = 0;
        for (int k = 0; k < 4; ++k) cost += std::abs(v[static_cast<size_t>(k)] - prev[static_cast<size_t>(k)]);
        if (first) cost = std::abs(v[0] - 60);
        if (cost < bestCost) { bestCost = cost; best = v; }
    }
    return best;
}

float humanised(Rng& r, float v, float amount)
{
    return clampv(v * (1.0f + amount * r.bipolar()), 0.05f, 1.0f);
}

} // namespace

Score composeStudy(const ParamStore& p, uint64_t seed)
{
    Score sc;
    const double bpm = p.get(p.id(Module::Compose, 0, compose::Bpm));
    sc.clear(bpm);
    sc.seed = seed;
    sc.keyRoot = p.getInt(p.id(Module::Compose, 0, compose::Key));
    sc.scale = p.getInt(p.id(Module::Compose, 0, compose::Scale));
    sc.lengthBeats = beatOf(kBars);
    const float hum = p.get(p.id(Module::Compose, 0, compose::Humanize)) * 0.01f;
    const int key = sc.keyRoot;
    // Chords need seven degrees: the pentatonic lead scale harmonises in natural minor.
    const ScaleDef& sc7 = scaleDef(scaleDef(sc.scale).size == 7 ? sc.scale : static_cast<int>(Scale::Aeolian));
    Rng rng;
    rng.seed(seed);
    Rng velRng;
    velRng.seed(mixSeed(seed, 0x56454C));   // "VEL"

    // The plan.
    const std::vector<BlockPlan> plan = studyPlan();
    for (const BlockPlan& b : plan) {
        LayerBlock lb;
        lb.beat = beatOf(b.bar);
        lb.state = b.st;
        lb.energy = b.energy;
        sc.layers.push_back(lb);
    }
    const struct { int bar, bars; SectionKind kind; float from, to; } kSections[] = {
        { 0, 16, SectionKind::Intro, 2.0f, 4.0f },   { 16, 16, SectionKind::Groove, 5.0f, 5.5f },
        { 32, 16, SectionKind::Breakdown, 3.0f, 6.0f }, { 48, 8, SectionKind::Build, 6.0f, 8.0f },
        { 56, 32, SectionKind::Drop, 9.0f, 10.0f },  { 88, 16, SectionKind::Outro, 6.0f, 2.0f },
    };
    for (const auto& s : kSections) {
        sc.sections.push_back(Section{ beatOf(s.bar), beatOf(s.bars), s.kind, s.from, s.to });
        sc.markers.push_back(Marker{ beatOf(s.bar), kSectionNames[static_cast<int>(s.kind)] });
    }
    auto stateAt = [&](int bar, L l) {
        const BlockPlan& b = plan[static_cast<size_t>(std::min(bar / 8, kBlocks - 1))];
        return b.st[static_cast<size_t>(l)];
    };
    auto note = [&](double beat, double len, Part part, int pitch, float vel, int shift = 0) {
        sc.notes.push_back(NoteEvent{ beat, len, part, pitch, humanised(velRng, vel, hum), shift, false, false });
    };

    // The harmony: i-VI-III-VII, a chord a bar (two bars a chord in the breakdown), the build's last two bars on VII.
    static const int kProgression[4] = { 0, 5, 2, 6 };
    std::vector<Chord> chordOfBar(kBars);
    for (int bar = 0; bar < kBars; ++bar) {
        int step = bar % 4;
        if (bar >= 32 && bar < 48) step = ((bar - 32) / 2) % 4;
        if (bar >= 54 && bar < 56) step = 3;
        chordOfBar[static_cast<size_t>(bar)] = chordOn(sc7, kProgression[step]);
    }

    // The drums, the ghost kick, the bass.
    const Part kCh = Part::Perc1, kOh = Part::Perc2, kClap = Part::Perc3, kSnare = Part::Perc4, kRide = Part::Perc5,
               kCrash = Part::Perc6;
    const int subBase = 24 + key < 26 ? 36 + key : 24 + key;   // the sub's root between D1 and C#2 (Dok. 4: roots D .. A)
    for (int bar = 0; bar < kBars; ++bar) {
        const double b0 = beatOf(bar);
        const bool preDrop = bar == 55;
        for (int q = 0; q < 4; ++q) {
            // The ghost kick on every quarter, the empty last beat before the drop excepted (the pre-drop vacuum).
            if (!(preDrop && q == 3)) note(b0 + q, 0.25, Part::Ghost, 36, 1.0f);
            if (stateAt(bar, L::Kick) == S::On) note(b0 + q, 0.25, Part::Kick, 36, 1.0f);
            if (stateAt(bar, L::Clap) == S::On && (q == 1 || q == 3)) note(b0 + q, 0.25, kClap, 39, 0.9f);
            if (stateAt(bar, L::Ride) == S::On) {
                note(b0 + q, 0.25, kRide, 51, 0.7f);
                note(b0 + q + 0.5, 0.25, kRide, 51, 0.5f);
            }
            if (stateAt(bar, L::OpenHat) == S::On) note(b0 + q + 0.5, 0.25, kOh, 46, 0.85f);
            if (stateAt(bar, L::ClosedHat) == S::On) {
                for (int s = 0; s < 4; ++s) {
                    if (s == 2 && stateAt(bar, L::OpenHat) == S::On) continue;   // the open hat has the off-beat
                    static const float kAcc[4] = { 0.55f, 0.35f, 0.8f, 0.4f };
                    note(b0 + q + 0.25 * s, 0.1, kCh, 42, kAcc[s]);
                }
            }
            // Sub and mid-bass on the off-beat eighth, on the chord's root (Dok. 3: "dun-dun-dun zwischen den Kicks").
            const Chord& c = chordOfBar[static_cast<size_t>(bar)];
            const int rootPc = (key + c.root) % 12;
            const int sub = atOrAbove(rootPc, subBase - 5);
            if (stateAt(bar, L::Sub) == S::On) note(b0 + q + 0.5, 0.22, Part::Sub, sub, 1.0f);
            if (stateAt(bar, L::Bass) == S::On) note(b0 + q + 0.5, 0.22, Part::Bass, sub + 12, 0.9f);
        }
        if (stateAt(bar, L::Crash) == S::On && bar % 8 == 0) note(b0, 1.0, kCrash, 49, 0.9f);
    }
    // The snare roll of the build (Dok. 3): quarters, eighths, sixteenths, thirty-seconds, rising an octave, silent on beat 4.
    for (int bar = 48; bar < 56; ++bar) {
        const int rel = bar - 48;
        const double step = rel < 2 ? 1.0 : rel < 4 ? 0.5 : rel < 7 ? 0.25 : 0.125;
        const double end = bar == 55 ? 3.0 : 4.0;
        for (double t = 0.0; t < end - 1e-9; t += step) {
            const double pos = (rel * 4.0 + t) / 32.0;   // 0 .. 1 over the build
            note(beatOf(bar) + t, 0.1, kSnare, 38, static_cast<float>(0.35 + 0.65 * pos), static_cast<int>(std::lround(12.0 * pos)));
        }
    }
    note(beatOf(56), 1.0, kCrash, 49, 1.0f);

    // The pad: a chord a bar (held two in the breakdown), voiced by the smallest movement.
    std::array<int, 4> prev{ 60, 64, 67, 72 };
    bool first = true;
    for (int bar = 0; bar < kBars; ++bar) {
        const S st = stateAt(bar, L::Pad);
        if (st == S::Off) { first = true; continue; }
        const bool twoBars = bar >= 32 && bar < 48;
        if (twoBars && (bar - 32) % 2 == 1) continue;
        const Chord& c = chordOfBar[static_cast<size_t>(bar)];
        const std::array<int, 4> v = voicePad(c, key, prev, first);
        first = false;
        prev = v;
        const double len = twoBars ? 8.0 : (bar == 54 ? 8.0 : 4.0);
        if (bar == 55) continue;   // held from bar 54
        for (int k = 0; k < 4; ++k) note(beatOf(bar), len - 0.05, Part::Pad, v[static_cast<size_t>(k)], 0.75f);
    }

    // The pluck: a broken chord in a 3-3-3-3-2-2 rhythm (Dok. 4: root and fifth or broken triads, one or two surprises).
    static const int kPluckSteps[6] = { 0, 3, 6, 9, 12, 14 };
    for (int bar = 0; bar < kBars; ++bar) {
        if (stateAt(bar, L::Pluck) == S::Off) continue;
        if (bar == 55) continue;
        const Chord& c = chordOfBar[static_cast<size_t>(bar)];
        const int r = atOrAbove((key + c.tones[0]) % 12, 60);
        const int f = atOrAbove((key + c.tones[2]) % 12, r);
        const int t = atOrAbove((key + c.tones[1]) % 12, r + 12);
        const int seq[6] = { r, f, t, f, r + 12, (bar % 4 == 3 && rng.uniform() < 0.5f) ? t + 2 : t };
        for (int k = 0; k < 6; ++k) note(beatOf(bar) + 0.25 * kPluckSteps[k], 0.2, Part::Pluck, seq[k], k == 0 ? 0.9f : 0.75f);
    }

    // The arp: sixteenths up and down over the chord and its octave.
    for (int bar = 0; bar < kBars; ++bar) {
        if (stateAt(bar, L::Arp) != S::On) continue;
        const Chord& c = chordOfBar[static_cast<size_t>(bar)];
        const int r = atOrAbove((key + c.tones[0]) % 12, 64);
        const int t = atOrAbove((key + c.tones[1]) % 12, r);
        const int f = atOrAbove((key + c.tones[2]) % 12, t);
        const int seq[6] = { r, t, f, r + 12, f, t };
        for (int s = 0; s < 16; ++s) note(beatOf(bar) + 0.25 * s, 0.12, Part::Arp, seq[s % 6], s % 4 == 0 ? 0.85f : 0.6f);
    }

    // The lead (a stand-in until Phase 3): a two-bar motif that opens with a leap and walks down the minor pentatonic to a
    // chord tone; A A' B A'' over eight bars (Dok. 4).
    {
        static const int kPent[5] = { 0, 3, 5, 7, 10 };
        const int top = atOrAbove(key, 76);   // the key's root at the top of the lead's register (E5 .. D#6)
        auto pentDown = [&](int pitch) {      // the next pentatonic tone under pitch
            for (int d = 1; d <= 3; ++d) {
                const int pc = ((pitch - d - key) % 12 + 12) % 12;
                for (int k : kPent) if (k == pc) return pitch - d;
            }
            return pitch - 2;
        };
        // Rhythm of A (two bars of sixteenths) and of B.
        static const int kRhythmA[] = { 0, 4, 6, 8, 12, 16, 20, 22, 24 };
        static const int kRhythmB[] = { 0, 2, 4, 8, 10, 12, 16, 20, 24, 28 };
        for (int bar = 40; bar < 88; bar += 2) {
            if (stateAt(bar, L::Lead) == S::Off) continue;
            const int phrasePos = ((bar - 40) / 2) % 4;   // A A' B A''
            const bool isB = phrasePos == 2;
            const int* rh = isB ? kRhythmB : kRhythmA;
            const int n = isB ? 10 : 9;
            int pitch = isB ? top + 3 : top;
            std::vector<int> pitches;
            pitches.push_back(isB ? top - 5 : top - 12);   // the leap at the phrase's start
            for (int k = 1; k < n; ++k) { pitches.push_back(pitch); pitch = pentDown(pitch); }
            // The end on a tone of the chord under it (A' and A'' vary the last two notes towards it).
            const Chord& c = chordOfBar[static_cast<size_t>(std::min(bar + 1, kBars - 1))];
            const int endPc = (key + c.tones[phrasePos == 3 ? 0 : 2]) % 12;
            pitches.back() = atOrAbove(endPc, pitches.back() - 6);
            if (phrasePos == 1 || phrasePos == 3) pitches[static_cast<size_t>(n - 2)] = pitches.back() + (phrasePos == 1 ? 2 : 3);
            for (int k = 0; k < n; ++k) {
                const int s0 = rh[k], s1 = k + 1 < n ? rh[k + 1] : 32;
                const double len = 0.25 * (s1 - s0) - 0.02;
                note(beatOf(bar) + 0.25 * s0, len, Part::Lead, pitches[static_cast<size_t>(k)], k == 0 ? 0.95f : 0.8f);
            }
        }
    }

    // The automation (PLAN 6.4).
    auto off = [&](int id, float target) { return p.toNormalised(id, target) - p.toNormalised(id, p.get(id)); };
    auto step = [&](int id, int bar, float to) { sc.gestures.push_back(Gesture{ id, beatOf(bar), 0.0, 0.0f, to, GestureShape::Step, 0 }); };
    auto ramp = [&](int id, int bar, int bars, float from, float to, GestureShape shape = GestureShape::MinimumJerk) {
        sc.gestures.push_back(Gesture{ id, beatOf(bar), beatOf(bars), from, to, shape, 0 });
    };
    // The pump through the breakdown: the pad's, the lead's and the returns' ducks to nothing, back two bars before the drop.
    for (int id : { p.id(Module::Poly, static_cast<int>(PolyInstance::Pad), poly::Duck),
                    p.id(Module::Poly, static_cast<int>(PolyInstance::Lead), poly::Duck),
                    p.id(Module::Poly, static_cast<int>(PolyInstance::Pluck), poly::Duck),
                    p.id(Module::Pump, 0, pump::ReturnDuck) }) {
        const float zero = off(id, 0.0f);
        step(id, 32, zero);
        ramp(id, 54, 2, zero, 0.0f, GestureShape::Linear);
    }
    // The pad: filtered in the groove, opening through the breakdown and the build, through the gate in the drop.
    const int padCut = p.id(Module::Poly, static_cast<int>(PolyInstance::Pad), poly::Cutoff);
    step(padCut, 24, -0.25f);
    ramp(padCut, 32, 16, -0.25f, 0.1f);
    ramp(padCut, 48, 8, 0.1f, 0.2f, GestureShape::EaseIn);
    step(padCut, 56, 0.0f);
    const int padGate = p.id(Module::Poly, static_cast<int>(PolyInstance::Pad), poly::Gate);
    step(padGate, 56, 1.0f);
    step(padGate, 88, 0.0f);
    // The pluck: filtered where the matrix says so.
    const int pluckCut = p.id(Module::Poly, static_cast<int>(PolyInstance::Pluck), poly::Cutoff);
    for (const BlockPlan& b : plan) step(pluckCut, b.bar, b.st[static_cast<size_t>(L::Pluck)] == S::Filtered ? -0.3f : 0.0f);
    // The lead's tease: filtered, opening over the build.
    const int leadCut = p.id(Module::Poly, static_cast<int>(PolyInstance::Lead), poly::Cutoff);
    step(leadCut, 40, -0.35f);
    ramp(leadCut, 48, 8, -0.35f, -0.1f, GestureShape::EaseIn);
    step(leadCut, 56, 0.0f);
    // The hall: long in the breakdown, short in the drop (Dok. 7: 2 to 4 s against 0.8 to 1.5 s).
    const int hallDecay = p.id(Module::Sends, 0, sends::HallDecay);
    step(hallDecay, 0, -0.2f);
    ramp(hallDecay, 32, 4, -0.2f, 0.1f);
    step(hallDecay, 56, -0.35f);
    ramp(hallDecay, 88, 8, -0.35f, -0.2f);

    // The energy script on the synths' fader (Dok. 6, rule 3: the breakdown 4 to 8 LU under the drop): down through the
    // breakdown, back over the build.
    const int synthLevel = p.id(Module::Mix, 0, mix::SynthLevel);
    const float down = off(synthLevel, p.get(synthLevel) - 5.0f);
    step(synthLevel, 32, down);
    ramp(synthLevel, 48, 8, down, off(synthLevel, p.get(synthLevel) - 2.0f), GestureShape::EaseIn);
    step(synthLevel, 56, 0.0f);

    // The loudness mark: the drop (Leveler, Phase 2).
    LevelMark lm;
    lm.beat = 0.0;
    lm.peakBeat = beatOf(72);
    lm.targetLufs = -8.0f;
    sc.levels.push_back(lm);
    sc.sort();
    return sc;
}

} // namespace parh
