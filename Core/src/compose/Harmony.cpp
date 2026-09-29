/**
 * @file Harmony.cpp
 * @brief The harmony of a track (Harmony.h).
 */
#include "parh/compose/Harmony.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {

/** @brief The scale whose triads the chords are built from (Harmony.h). */
int chordScale(int scale)
{
    switch (static_cast<Scale>(scale)) {
    case Scale::Dorian: return static_cast<int>(Scale::Dorian);
    case Scale::Phrygian: return static_cast<int>(Scale::Phrygian);
    case Scale::Ionian: return static_cast<int>(Scale::Ionian);
    default: return static_cast<int>(Scale::Aeolian);
    }
}

/** @brief The degrees of a progression in a major key: the same functions from the tonic (vi-IV-I-V and relatives). */
const int kMajorDegrees[kProgressions][4] = {
    { 5, 3, 0, 4 },   // vi-IV-I-V: the Anthem's pitches from the relative major
    { 0, 4, 5, 3 },   // I-V-vi-IV
    { 3, 4, 0, 0 },   // IV-V-I
    { 0, 5, 3, 4 },   // I-vi-IV-V
    { 5, 3, 4, 2 },   // vi-IV-V-iii
};

} // namespace

int Harmony::pc(int bar, int i) const
{
    const size_t b = static_cast<size_t>(std::clamp(bar, 0, static_cast<int>(bars.size()) - 1));
    return ((key + shift[b] + bars[b].tones[i]) % 12 + 12) % 12;
}

void drawKey(const StyleProfile& prof, Rng& r, int& key, int& scale)
{
    // Tonics weighted towards D .. A (Dok. 4), A and F the workhorses.
    static const float kTonic[12] = { 0.6f, 0.6f, 1.0f, 0.9f, 1.0f, 1.4f, 1.0f, 1.0f, 0.9f, 1.4f, 0.8f, 0.6f };
    key = drawWeighted(kTonic, 12, r.uniform());
    if (r.uniform() >= prof.minor) { scale = static_cast<int>(Scale::Ionian); return; }
    std::array<float, static_cast<int>(Scale::Count)> w = prof.scales;
    w[static_cast<size_t>(Scale::Ionian)] = 0.0f;
    scale = drawWeighted(w.data(), static_cast<int>(w.size()), r.uniform());
}

Chord chordOn(int scale, int degree, bool majorFive)
{
    const ScaleDef& sc = scaleDef(chordScale(scale));
    Chord c;
    c.degree = ((degree % 7) + 7) % 7;
    for (int k = 0; k < 3; ++k) {
        const int idx = c.degree + 2 * k;
        c.tones[k] = sc.steps[idx % 7] + 12 * (idx / 7);
    }
    // Harmonic minor's major V (its raised seventh as the third of v).
    if (majorFive && c.degree == 4 && chordScale(scale) == static_cast<int>(Scale::Aeolian)) c.tones[1] += 1;
    return c;
}

Harmony composeHarmony(const Plan& plan, const StyleProfile& prof, int key, int scale, uint64_t seed)
{
    Rng r;
    r.seed(seed);
    Harmony h;
    h.key = key;
    h.scale = scale;
    h.progression = drawWeighted(prof.progressions.data(), kProgressions, r.uniform());
    if (h.progression == static_cast<int>(Progression::Lift)) h.progression = static_cast<int>(Progression::Anthem);
    // The breakdown's progression: the same, or the melancholic one (the piano's), now and then.
    const int breakProg = r.uniform() < 0.3f ? static_cast<int>(Progression::Melancholy) : h.progression;
    const bool major = scale == static_cast<int>(Scale::Ionian);
    const bool acid = prof.barsPerChord >= 16;
    auto degreeOf = [&](int prog, int step) {
        return major ? kMajorDegrees[prog][step % 4] : kProgressionDegrees[prog][step % 4];
    };
    h.bars.assign(static_cast<size_t>(plan.bars), Chord{});
    h.shift.assign(static_cast<size_t>(plan.bars), 0);
    // Acid's static harmony: i, then a fourth or a fifth away, every 16 bars (Dok. 4).
    static const int kAcidMoves[2][4] = { { 0, 0, 3, 0 }, { 0, 4, 0, 3 } };
    const int acidMoves = r.below(2);
    for (size_t si = 0; si < plan.sections.size(); ++si) {
        const Section& s = plan.sections[si];
        const int start = static_cast<int>(s.beat / 4.0), n = static_cast<int>(s.length / 4.0);
        const bool breakdown = s.kind == SectionKind::Breakdown || s.kind == SectionKind::Break;
        const int per = std::max(1, breakdown ? prof.barsPerChordBreak : prof.barsPerChord);
        const int prog = s.kind == SectionKind::Breakdown ? breakProg : h.progression;
        for (int b = 0; b < n; ++b) {
            Chord c;
            if (acid) c = chordOn(scale, kAcidMoves[acidMoves][((start + b) / 16) % 4]);
            else c = chordOn(scale, degreeOf(prog, b / per), breakdown && prog == static_cast<int>(Progression::Tension)
                                                            && scale == static_cast<int>(Scale::HarmonicMinor));
            h.bars[static_cast<size_t>(start + b)] = c;
        }
    }
    // The lift: the last two bars before every drop hold VII (in major: V), the drop resolves to i.
    for (size_t si = 0; si < plan.sections.size(); ++si) {
        if (plan.sections[si].kind != SectionKind::Drop || si == 0 || acid) continue;
        const int drop = static_cast<int>(plan.sections[si].beat / 4.0);
        for (int b = std::max(0, drop - 2); b < drop; ++b) h.bars[static_cast<size_t>(b)] = chordOn(scale, major ? 4 : 6);
        h.bars[static_cast<size_t>(drop)] = chordOn(scale, degreeOf(h.progression, 0));
    }
    // The Cinematic key change: a minor third up from the section after the second breakdown or break.
    if (!acid && r.uniform() < prof.keyChange) {
        int breaks = 0;
        for (size_t si = 0; si < plan.sections.size(); ++si) {
            const SectionKind k = plan.sections[si].kind;
            if (k == SectionKind::Break || k == SectionKind::Breakdown) ++breaks;
            if (breaks >= 2 && k == SectionKind::Drop) {
                h.changeBar = static_cast<int>(plan.sections[si].beat / 4.0);
                break;
            }
        }
        if (h.changeBar >= 0)
            for (int b = h.changeBar; b < plan.bars; ++b) h.shift[static_cast<size_t>(b)] = 3;
    }
    return h;
}

int atOrAbove(int pc, int floor)
{
    int p = floor - ((floor - pc) % 12 + 12) % 12;
    if (p < floor) p += 12;
    return p;
}

std::array<int, 4> voicePad(const Harmony& h, int bar, const std::array<int, 4>& prev, bool first, int floor)
{
    std::array<int, 4> best{};
    int bestCost = 1 << 30;
    for (int bottom = 1; bottom <= 2; ++bottom) {   // third or fifth, never the root
        std::array<int, 4> v{};
        v[0] = atOrAbove(h.pc(bar, bottom), floor);
        int t = bottom;
        for (int k = 1; k < 4; ++k) {
            t = (t + 1) % 3;
            v[static_cast<size_t>(k)] = atOrAbove(h.pc(bar, t), v[static_cast<size_t>(k - 1)] + 1);
        }
        int cost = 0;
        for (int k = 0; k < 4; ++k) cost += std::abs(v[static_cast<size_t>(k)] - prev[static_cast<size_t>(k)]);
        if (first) cost = std::abs(v[0] - (floor + 3));
        if (cost < bestCost) { bestCost = cost; best = v; }
    }
    return best;
}

} // namespace parh
