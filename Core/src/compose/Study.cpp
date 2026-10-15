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

constexpr int Bars = 104;   ///< the study's length, bars
constexpr int Blocks = Bars / 8;   ///< its blocks of eight bars
/** @brief The beat bar @p bar begins on. */
constexpr double beatOf(int bar) { return 4.0 * bar; }

using LT = LayerE;   ///< The layers, shortly.
using ST = LayerStateE;   ///< The layers' states, shortly.

/** @brief The layer matrix of the study, block by block (Study.h). */
struct BlockPlan {
    int m_bar;   ///< the bar the block begins on
    std::array<ST, NumLayers> m_st;   ///< every layer's state
    float m_energy;   ///< the block's energy
};

/** @brief Every layer off but those @p on names. */
std::array<ST, NumLayers> states(std::initializer_list<std::pair<LT, ST>> on)
{
    std::array<ST, NumLayers> a{};
    for (const auto& [l, s] : on) a[static_cast<size_t>(l)] = s;
    return a;
}

/** @brief The study's blocks: the layers in and out, the energy, as Study.h tells it. */
std::vector<BlockPlan> studyPlan()
{
    const ST on = ST::ON, fi = ST::FILTERED;
    std::vector<BlockPlan> b;
    b.push_back({ 0, states({ { LT::KICK, on } }), 2.0f });
    b.push_back({ 8, states({ { LT::KICK, on }, { LT::CLOSED_HAT, on } }), 3.5f });
    b.push_back({ 16, states({ { LT::KICK, on }, { LT::SUB, on }, { LT::BASS, on }, { LT::CLOSED_HAT, on }, { LT::OPEN_HAT, on } }), 5.0f });
    b.push_back({ 24, states({ { LT::KICK, on }, { LT::SUB, on }, { LT::BASS, on }, { LT::CLOSED_HAT, on }, { LT::OPEN_HAT, on },
                               { LT::CLAP, on }, { LT::PLUCK, on }, { LT::PAD, fi }, { LT::CRASH, on } }), 5.5f });
    b.push_back({ 32, states({ { LT::PAD, on }, { LT::PLUCK, fi } }), 3.0f });
    b.push_back({ 40, states({ { LT::PAD, on }, { LT::PLUCK, fi }, { LT::LEAD, fi } }), 4.5f });
    b.push_back({ 48, states({ { LT::PAD, on }, { LT::PLUCK, on }, { LT::LEAD, fi }, { LT::PERC, on } }), 7.0f });
    b.push_back({ 56, states({ { LT::KICK, on }, { LT::SUB, on }, { LT::BASS, on }, { LT::CLOSED_HAT, on }, { LT::OPEN_HAT, on },
                               { LT::CLAP, on }, { LT::RIDE, on }, { LT::CRASH, on }, { LT::PLUCK, on }, { LT::PAD, on }, { LT::LEAD, on } }), 9.0f });
    b.push_back({ 64, b.back().m_st, 9.5f });
    b.push_back({ 72, states({ { LT::KICK, on }, { LT::SUB, on }, { LT::BASS, on }, { LT::CLOSED_HAT, on }, { LT::OPEN_HAT, on },
                               { LT::CLAP, on }, { LT::RIDE, on }, { LT::CRASH, on }, { LT::PLUCK, on }, { LT::PAD, on }, { LT::LEAD, on },
                               { LT::ARP, on } }), 10.0f });
    b.push_back({ 80, b.back().m_st, 10.0f });
    b.push_back({ 88, states({ { LT::KICK, on }, { LT::SUB, on }, { LT::BASS, on }, { LT::CLOSED_HAT, on }, { LT::OPEN_HAT, on },
                               { LT::CLAP, on }, { LT::PLUCK, fi } }), 5.0f });
    b.push_back({ 96, states({ { LT::KICK, on }, { LT::SUB, on }, { LT::BASS, on }, { LT::CLOSED_HAT, on } }), 3.0f });
    return b;
}

/** @brief A chord: its root in semitones above the key and its three tones (root, third, fifth) in semitones. */
struct Chord {
    int m_root;       ///< semitones above the key
    int m_tones[3];   ///< root, third, fifth, in semitones
};

/** @brief The triad on scale degree @p degree of @p sc. */
Chord chordOn(const ScaleDef& sc, int degree)
{
    Chord c{};
    for (int k = 0; k < 3; ++k) {
        const int idx = degree + 2 * k;
        c.m_tones[k] = sc.m_steps[idx % 7] + 12 * (idx / 7);
    }
    c.m_root = c.m_tones[0];
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
        v[0] = atOrAbove((key + c.m_tones[bottom]) % 12, 57);
        int t = bottom;
        for (int k = 1; k < 4; ++k) {
            t = (t + 1) % 3;
            v[static_cast<size_t>(k)] = atOrAbove((key + c.m_tones[t]) % 12, v[static_cast<size_t>(k - 1)] + 1);
        }
        int cost = 0;
        for (int k = 0; k < 4; ++k) cost += std::abs(v[static_cast<size_t>(k)] - prev[static_cast<size_t>(k)]);
        if (first) cost = std::abs(v[0] - 60);
        if (cost < bestCost) { bestCost = cost; best = v; }
    }
    return best;
}

/** @brief @p v moved by up to @p amount of itself either way, kept in 0.05 .. 1. */
float humanised(Rng& r, float v, float amount)
{
    return clampv(v * (1.0f + amount * r.bipolar()), 0.05f, 1.0f);
}

} // namespace

Score composeStudy(const ParamStore& p, uint64_t seed)
{
    Score sc;
    const double bpm = p.get(p.id(ModuleE::COMPOSE, 0, compose::BPM));
    sc.clear(bpm);
    sc.m_seed = seed;
    sc.m_keyRoot = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::KEY));
    sc.m_scale = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::SCALE));
    sc.m_lengthBeats = beatOf(Bars);
    const float hum = p.get(p.id(ModuleE::COMPOSE, 0, compose::HUMANIZE)) * 0.01f;
    const int key = sc.m_keyRoot;
    // Chords need seven degrees: the pentatonic lead scale harmonises in natural minor.
    const ScaleDef& sc7 = scaleDef(scaleDef(sc.m_scale).m_size == 7 ? sc.m_scale : static_cast<int>(ScaleE::AEOLIAN));
    Rng rng;
    rng.seed(seed);
    Rng velRng;
    velRng.seed(mixSeed(seed, 0x56454C));   // "VEL"

    // The plan.
    const std::vector<BlockPlan> plan = studyPlan();
    for (const BlockPlan& b : plan) {
        LayerBlock lb;
        lb.m_beat = beatOf(b.m_bar);
        lb.m_state = b.m_st;
        lb.m_energy = b.m_energy;
        sc.m_layers.push_back(lb);
    }
    const struct { int m_bar, m_bars; SectionKindE m_kind; float m_from, m_to; } Sections[] = {
        { 0, 16, SectionKindE::INTRO, 2.0f, 4.0f },   { 16, 16, SectionKindE::GROOVE, 5.0f, 5.5f },
        { 32, 16, SectionKindE::BREAKDOWN, 3.0f, 6.0f }, { 48, 8, SectionKindE::BUILD, 6.0f, 8.0f },
        { 56, 32, SectionKindE::DROP, 9.0f, 10.0f },  { 88, 16, SectionKindE::OUTRO, 6.0f, 2.0f },
    };
    for (const auto& s : Sections) {
        sc.m_sections.push_back(Section{ beatOf(s.m_bar), beatOf(s.m_bars), s.m_kind, s.m_from, s.m_to });
        sc.m_markers.push_back(Marker{ beatOf(s.m_bar), SectionNames[static_cast<int>(s.m_kind)] });
    }
    auto stateAt = [&](int bar, LT l) {
        const BlockPlan& b = plan[static_cast<size_t>(std::min(bar / 8, Blocks - 1))];
        return b.m_st[static_cast<size_t>(l)];
    };
    auto note = [&](double beat, double len, PartE part, int pitch, float vel, int shift = 0) {
        sc.m_notes.push_back(NoteEvent{ beat, len, part, pitch, humanised(velRng, vel, hum), shift, false, false });
    };

    // The harmony: i-VI-III-VII, a chord a bar (two bars a chord in the breakdown), the build's last two bars on VII.
    static const int Progression[4] = { 0, 5, 2, 6 };
    std::vector<Chord> chordOfBar(Bars);
    for (int bar = 0; bar < Bars; ++bar) {
        int step = bar % 4;
        if (bar >= 32 && bar < 48) step = ((bar - 32) / 2) % 4;
        if (bar >= 54 && bar < 56) step = 3;
        chordOfBar[static_cast<size_t>(bar)] = chordOn(sc7, Progression[step]);
    }

    // The drums, the ghost kick, the bass.
    const PartE Ch = PartE::PERC1, Oh = PartE::PERC2, Clap = PartE::PERC3, Snare = PartE::PERC4, Ride = PartE::PERC5,
               Crash = PartE::PERC6;
    const int subBase = 24 + key < 26 ? 36 + key : 24 + key;   // the sub's root between D1 and C#2 (Dok. 4: roots D .. A)
    for (int bar = 0; bar < Bars; ++bar) {
        const double b0 = beatOf(bar);
        const bool preDrop = bar == 55;
        for (int q = 0; q < 4; ++q) {
            // The ghost kick on every quarter, the empty last beat before the drop excepted (the pre-drop vacuum).
            if (!(preDrop && q == 3)) note(b0 + q, 0.25, PartE::GHOST, 36, 1.0f);
            if (stateAt(bar, LT::KICK) == ST::ON) note(b0 + q, 0.25, PartE::KICK, 36, 1.0f);
            if (stateAt(bar, LT::CLAP) == ST::ON && (q == 1 || q == 3)) note(b0 + q, 0.25, Clap, 39, 0.9f);
            if (stateAt(bar, LT::RIDE) == ST::ON) {
                note(b0 + q, 0.25, Ride, 51, 0.7f);
                note(b0 + q + 0.5, 0.25, Ride, 51, 0.5f);
            }
            if (stateAt(bar, LT::OPEN_HAT) == ST::ON) note(b0 + q + 0.5, 0.25, Oh, 46, 0.85f);
            if (stateAt(bar, LT::CLOSED_HAT) == ST::ON) {
                for (int s = 0; s < 4; ++s) {
                    if (s == 2 && stateAt(bar, LT::OPEN_HAT) == ST::ON) continue;   // the open hat has the off-beat
                    static const float Acc[4] = { 0.55f, 0.35f, 0.8f, 0.4f };
                    note(b0 + q + 0.25 * s, 0.1, Ch, 42, Acc[s]);
                }
            }
            // Sub and mid-bass on the off-beat eighth, on the chord's root (Dok. 3: "dun-dun-dun zwischen den Kicks").
            const Chord& c = chordOfBar[static_cast<size_t>(bar)];
            const int rootPc = (key + c.m_root) % 12;
            const int sub = atOrAbove(rootPc, subBase - 5);
            if (stateAt(bar, LT::SUB) == ST::ON) note(b0 + q + 0.5, 0.22, PartE::SUB, sub, 1.0f);
            if (stateAt(bar, LT::BASS) == ST::ON) note(b0 + q + 0.5, 0.22, PartE::BASS, sub + 12, 0.9f);
        }
        if (stateAt(bar, LT::CRASH) == ST::ON && bar % 8 == 0) note(b0, 1.0, Crash, 49, 0.9f);
    }
    // The snare roll of the build (Dok. 3): quarters, eighths, sixteenths, thirty-seconds, rising an octave, silent on beat 4.
    for (int bar = 48; bar < 56; ++bar) {
        const int rel = bar - 48;
        const double step = rel < 2 ? 1.0 : rel < 4 ? 0.5 : rel < 7 ? 0.25 : 0.125;
        const double end = bar == 55 ? 3.0 : 4.0;
        for (double t = 0.0; t < end - 1e-9; t += step) {
            const double pos = (rel * 4.0 + t) / 32.0;   // 0 .. 1 over the build
            note(beatOf(bar) + t, 0.1, Snare, 38, static_cast<float>(0.35 + 0.65 * pos), static_cast<int>(std::lround(12.0 * pos)));
        }
    }
    note(beatOf(56), 1.0, Crash, 49, 1.0f);

    // The pad: a chord a bar (held two in the breakdown), voiced by the smallest movement.
    std::array<int, 4> prev{ 60, 64, 67, 72 };
    bool first = true;
    for (int bar = 0; bar < Bars; ++bar) {
        const ST st = stateAt(bar, LT::PAD);
        if (st == ST::OFF) { first = true; continue; }
        const bool twoBars = bar >= 32 && bar < 48;
        if (twoBars && (bar - 32) % 2 == 1) continue;
        const Chord& c = chordOfBar[static_cast<size_t>(bar)];
        const std::array<int, 4> v = voicePad(c, key, prev, first);
        first = false;
        prev = v;
        const double len = twoBars ? 8.0 : (bar == 54 ? 8.0 : 4.0);
        if (bar == 55) continue;   // held from bar 54
        for (int k = 0; k < 4; ++k) note(beatOf(bar), len - 0.05, PartE::PAD, v[static_cast<size_t>(k)], 0.75f);
    }

    // The pluck: a broken chord in a 3-3-3-3-2-2 rhythm (Dok. 4: root and fifth or broken triads, one or two surprises).
    static const int PluckSteps[6] = { 0, 3, 6, 9, 12, 14 };
    for (int bar = 0; bar < Bars; ++bar) {
        if (stateAt(bar, LT::PLUCK) == ST::OFF) continue;
        if (bar == 55) continue;
        const Chord& c = chordOfBar[static_cast<size_t>(bar)];
        const int r = atOrAbove((key + c.m_tones[0]) % 12, 60);
        const int f = atOrAbove((key + c.m_tones[2]) % 12, r);
        const int t = atOrAbove((key + c.m_tones[1]) % 12, r + 12);
        const int seq[6] = { r, f, t, f, r + 12, (bar % 4 == 3 && rng.uniform() < 0.5f) ? t + 2 : t };
        for (int k = 0; k < 6; ++k) note(beatOf(bar) + 0.25 * PluckSteps[k], 0.2, PartE::PLUCK, seq[k], k == 0 ? 0.9f : 0.75f);
    }

    // The arp: sixteenths up and down over the chord and its octave.
    for (int bar = 0; bar < Bars; ++bar) {
        if (stateAt(bar, LT::ARP) != ST::ON) continue;
        const Chord& c = chordOfBar[static_cast<size_t>(bar)];
        const int r = atOrAbove((key + c.m_tones[0]) % 12, 64);
        const int t = atOrAbove((key + c.m_tones[1]) % 12, r);
        const int f = atOrAbove((key + c.m_tones[2]) % 12, t);
        const int seq[6] = { r, t, f, r + 12, f, t };
        for (int s = 0; s < 16; ++s) note(beatOf(bar) + 0.25 * s, 0.12, PartE::ARP, seq[s % 6], s % 4 == 0 ? 0.85f : 0.6f);
    }

    // The lead (a stand-in until Phase 3): a two-bar motif that opens with a leap and walks down the minor pentatonic to a
    // chord tone; A A' B A'' over eight bars (Dok. 4).
    {
        static const int Pent[5] = { 0, 3, 5, 7, 10 };
        const int top = atOrAbove(key, 76);   // the key's root at the top of the lead's register (E5 .. D#6)
        auto pentDown = [&](int pitch) {      // the next pentatonic tone under pitch
            for (int d = 1; d <= 3; ++d) {
                const int pc = ((pitch - d - key) % 12 + 12) % 12;
                for (int k : Pent) if (k == pc) return pitch - d;
            }
            return pitch - 2;
        };
        // Rhythm of A (two bars of sixteenths) and of B.
        static const int RhythmA[] = { 0, 4, 6, 8, 12, 16, 20, 22, 24 };
        static const int RhythmB[] = { 0, 2, 4, 8, 10, 12, 16, 20, 24, 28 };
        for (int bar = 40; bar < 88; bar += 2) {
            if (stateAt(bar, LT::LEAD) == ST::OFF) continue;
            const int phrasePos = ((bar - 40) / 2) % 4;   // A A' B A''
            const bool isB = phrasePos == 2;
            const int* rh = isB ? RhythmB : RhythmA;
            const int n = isB ? 10 : 9;
            int pitch = isB ? top + 3 : top;
            std::vector<int> pitches;
            pitches.push_back(isB ? top - 5 : top - 12);   // the leap at the phrase's start
            for (int k = 1; k < n; ++k) { pitches.push_back(pitch); pitch = pentDown(pitch); }
            // The end on a tone of the chord under it (A' and A'' vary the last two notes towards it).
            const Chord& c = chordOfBar[static_cast<size_t>(std::min(bar + 1, Bars - 1))];
            const int endPc = (key + c.m_tones[phrasePos == 3 ? 0 : 2]) % 12;
            pitches.back() = atOrAbove(endPc, pitches.back() - 6);
            if (phrasePos == 1 || phrasePos == 3) pitches[static_cast<size_t>(n - 2)] = pitches.back() + (phrasePos == 1 ? 2 : 3);
            for (int k = 0; k < n; ++k) {
                const int s0 = rh[k], s1 = k + 1 < n ? rh[k + 1] : 32;
                const double len = 0.25 * (s1 - s0) - 0.02;
                note(beatOf(bar) + 0.25 * s0, len, PartE::LEAD, pitches[static_cast<size_t>(k)], k == 0 ? 0.95f : 0.8f);
            }
        }
    }

    // The automation (PLAN 6.4).
    auto off = [&](int id, float target) { return p.toNormalised(id, target) - p.toNormalised(id, p.get(id)); };
    auto step = [&](int id, int bar, float to) { sc.m_gestures.push_back(Gesture{ id, beatOf(bar), 0.0, 0.0f, to, GestureShapeE::STEP, 0 }); };
    auto ramp = [&](int id, int bar, int bars, float from, float to, GestureShapeE shape = GestureShapeE::MINIMUM_JERK) {
        sc.m_gestures.push_back(Gesture{ id, beatOf(bar), beatOf(bars), from, to, shape, 0 });
    };
    // The pump through the breakdown: the pad's, the lead's and the returns' ducks to nothing, back two bars before the drop.
    for (int id : { p.id(ModuleE::POLY, static_cast<int>(PolyInstanceE::PAD), poly::DUCK),
                    p.id(ModuleE::POLY, static_cast<int>(PolyInstanceE::LEAD), poly::DUCK),
                    p.id(ModuleE::POLY, static_cast<int>(PolyInstanceE::PLUCK), poly::DUCK),
                    p.id(ModuleE::PUMP, 0, pump::RETURN_DUCK) }) {
        const float zero = off(id, 0.0f);
        step(id, 32, zero);
        ramp(id, 54, 2, zero, 0.0f, GestureShapeE::LINEAR);
    }
    // The pad: filtered in the groove, opening through the breakdown and the build, through the gate in the drop.
    const int padCut = p.id(ModuleE::POLY, static_cast<int>(PolyInstanceE::PAD), poly::CUTOFF);
    step(padCut, 24, -0.25f);
    ramp(padCut, 32, 16, -0.25f, 0.1f);
    ramp(padCut, 48, 8, 0.1f, 0.2f, GestureShapeE::EASE_IN);
    step(padCut, 56, 0.0f);
    const int padGate = p.id(ModuleE::POLY, static_cast<int>(PolyInstanceE::PAD), poly::GATE);
    step(padGate, 56, 1.0f);
    step(padGate, 88, 0.0f);
    // The pluck: filtered where the matrix says so.
    const int pluckCut = p.id(ModuleE::POLY, static_cast<int>(PolyInstanceE::PLUCK), poly::CUTOFF);
    for (const BlockPlan& b : plan) step(pluckCut, b.m_bar, b.m_st[static_cast<size_t>(LT::PLUCK)] == ST::FILTERED ? -0.3f : 0.0f);
    // The lead's tease: filtered, opening over the build.
    const int leadCut = p.id(ModuleE::POLY, static_cast<int>(PolyInstanceE::LEAD), poly::CUTOFF);
    step(leadCut, 40, -0.35f);
    ramp(leadCut, 48, 8, -0.35f, -0.1f, GestureShapeE::EASE_IN);
    step(leadCut, 56, 0.0f);
    // The hall: long in the breakdown, short in the drop (Dok. 7: 2 to 4 s against 0.8 to 1.5 s).
    const int hallDecay = p.id(ModuleE::SENDS, 0, sends::HALL_DECAY);
    step(hallDecay, 0, -0.2f);
    ramp(hallDecay, 32, 4, -0.2f, 0.1f);
    step(hallDecay, 56, -0.35f);
    ramp(hallDecay, 88, 8, -0.35f, -0.2f);

    // The energy script on the synths' fader (Dok. 6, rule 3: the breakdown 4 to 8 LU under the drop): down through the
    // breakdown, back over the build.
    const int synthLevel = p.id(ModuleE::MIX, 0, mix::SYNTH_LEVEL);
    const float down = off(synthLevel, p.get(synthLevel) - 5.0f);
    step(synthLevel, 32, down);
    ramp(synthLevel, 48, 8, down, off(synthLevel, p.get(synthLevel) - 2.0f), GestureShapeE::EASE_IN);
    step(synthLevel, 56, 0.0f);

    // The loudness mark: the drop (Leveler, Phase 2).
    LevelMark lm;
    lm.m_beat = 0.0;
    lm.m_peakBeat = beatOf(72);
    lm.m_targetLufs = -8.0f;
    sc.m_levels.push_back(lm);
    sc.sort();
    return sc;
}

} // namespace parh
