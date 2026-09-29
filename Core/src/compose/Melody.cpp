/**
 * @file Melody.cpp
 * @brief The melodic voices (Melody.h).
 */
#include "parh/compose/Melody.h"
#include <algorithm>
#include <cmath>

namespace parh {

bool MelodyContext::silent(double beat) const
{
    for (int b : plan->vacuums)
        if (beat >= 4.0 * b + 3.0 - 1e-9 && beat < 4.0 * (b + 1)) return true;
    return false;
}

void MelodyContext::note(double beat, double len, Part part, int pitch, float velocity, int shift, bool accent, bool slide) const
{
    if (silent(beat)) return;
    // A note that would ring into a vacuum ends where it starts.
    for (int b : plan->vacuums) {
        const double v0 = 4.0 * b + 3.0;
        if (beat < v0 && beat + len > v0) len = std::max(0.05, v0 - beat);
    }
    const float v = vel != nullptr ? clampv(velocity * (1.0f + humanize * vel->bipolar()), 0.05f, 1.0f) : velocity;
    score->notes.push_back(NoteEvent{ beat, len, part, std::clamp(pitch, 0, 127), v, shift, accent, slide });
}

namespace {

/** @brief The next tone of the minor pentatonic (or the scale) under @p pitch, over the key @p key. */
int stepDown(int pitch, int key, const ScaleDef& sc)
{
    for (int d = 1; d <= 4; ++d) {
        const int pc = ((pitch - d - key) % 12 + 12) % 12;
        for (int i = 0; i < sc.size; ++i) if (sc.steps[i] == pc) return pitch - d;
    }
    return pitch - 2;
}
int stepUp(int pitch, int key, const ScaleDef& sc)
{
    for (int d = 1; d <= 4; ++d) {
        const int pc = ((pitch + d - key) % 12 + 12) % 12;
        for (int i = 0; i < sc.size; ++i) if (sc.steps[i] == pc) return pitch + d;
    }
    return pitch + 2;
}
/** @brief The nearest tone of the chord at @p bar to @p pitch. */
int nearestChordTone(const Harmony& h, int bar, int pitch)
{
    int best = pitch, bestD = 99;
    for (int i = 0; i < 3; ++i) {
        const int pc = h.pc(bar, i);
        for (int o = -1; o <= 1; ++o) {
            const int c = atOrAbove(pc, pitch - 6) + 12 * o;
            if (std::abs(c - pitch) < bestD) { bestD = std::abs(c - pitch); best = c; }
        }
    }
    return best;
}

} // namespace

void writeLead(const MelodyContext& c, uint64_t seed)
{
    const Plan& plan = *c.plan;
    const Harmony& h = *c.harmony;
    Rng r;
    r.seed(seed);
    // The lead's scale: the minor pentatonic or the key's own (Dok. 4: "pentatonisch-moll oder aeolisch").
    const bool pent = h.scale != static_cast<int>(Scale::Ionian) && r.uniform() < 0.55f;
    const ScaleDef& sc = scaleDef(pent ? static_cast<int>(Scale::MinorPentatonic) : h.scale);
    // The motif's rhythm: two bars of sixteenths, drawn from the trance idioms (onsets in sixteenths).
    static const std::vector<std::vector<int>> kRhythms = {
        { 0, 4, 6, 8, 12, 16, 20, 22, 24 },            // long-short-short, a held end
        { 0, 3, 6, 8, 10, 12, 16, 19, 22, 24, 28 },    // the 3-3-2 of the pluck, sung
        { 0, 2, 4, 8, 10, 12, 16, 18, 20, 24 },        // eighth pairs
        { 0, 6, 8, 12, 14, 16, 22, 24, 28 },           // dotted
        { 0, 4, 8, 10, 12, 14, 16, 20, 24, 26, 28 },   // walking eighths
    };
    const std::vector<int>& rhA = kRhythms[static_cast<size_t>(r.below(static_cast<int>(kRhythms.size())))];
    const std::vector<int>& rhB = kRhythms[static_cast<size_t>(r.below(static_cast<int>(kRhythms.size())))];
    // The register: the tonic between E5 and D#6 at the top, the motif under it.
    const int top = atOrAbove(h.key, 76);
    const int leapUp = r.uniform() < 0.6f ? 12 : 9;   // an octave or a sixth (Dok. 4)
    // The contour of A: the leap at the start, then a walk down with one turn.
    std::vector<int> contour;   // steps (+1 up, -1 down) after the leap
    for (size_t i = 1; i < rhA.size(); ++i) contour.push_back(i == rhA.size() / 2 && r.uniform() < 0.5f ? 1 : -1);
    for (int bar = 0; bar < plan.bars; bar += 2) {
        const LayerState st = plan.at(bar, Layer::Lead);
        if (st == LayerState::Off) continue;
        const int sec = plan.sectionAt(bar);
        const int secStart = sec >= 0 ? static_cast<int>(plan.sections[static_cast<size_t>(sec)].beat / 4.0) : 0;
        const int phrasePos = ((bar - secStart) / 2) % 4;   // A A' B A''
        const bool isB = phrasePos == 2;
        const std::vector<int>& rh = isB ? rhB : rhA;
        // The main drop's second half an octave up (Dok. 6: "Variation nach 16 Takten").
        int oct = 0;
        if (sec >= 0 && sec == plan.mainDrop) {
            const Section& s = plan.sections[static_cast<size_t>(sec)];
            if (bar * 4.0 >= s.beat + s.length / 2.0 && s.length >= 128.0) oct = 12;
        }
        const int shift = h.shift[static_cast<size_t>(bar)];
        std::vector<int> pitches;
        int pitch = top + shift + (isB ? 3 : 0);
        pitches.push_back(pitch - leapUp);   // the leap
        for (size_t k = 1; k < rh.size(); ++k) {
            pitches.push_back(pitch);
            pitch = (!isB && k - 1 < contour.size() && contour[k - 1] > 0) ? stepUp(pitch, h.key + shift, sc) : stepDown(pitch, h.key + shift, sc);
        }
        // The end on a chord tone (A' and A'' vary the last notes towards it).
        const int endBar = std::min(bar + 1, plan.bars - 1);
        pitches.back() = nearestChordTone(h, endBar, pitches.back());
        if (phrasePos == 1 || phrasePos == 3) {
            const size_t n = pitches.size();
            pitches[n - 2] = nearestChordTone(h, endBar, pitches[n - 2] + (phrasePos == 1 ? 2 : 3));
        }
        // Strong sixteenths (on the beat) on chord tones.
        for (size_t k = 0; k < rh.size(); ++k)
            if (rh[k] % 4 == 0) pitches[k] = nearestChordTone(h, bar + rh[k] / 16, pitches[k]);
        for (size_t k = 0; k < rh.size(); ++k) {
            const int s0 = rh[k], s1 = k + 1 < rh.size() ? rh[k + 1] : 32;
            const double len = 0.25 * (s1 - s0) - 0.02;
            const int b = bar + s0 / 16;
            if (b >= plan.bars || plan.at(b, Layer::Lead) == LayerState::Off) continue;
            c.note(4.0 * bar + 0.25 * s0, len, Part::Lead, pitches[k] + oct, k == 0 ? 0.95f : 0.8f);
        }
    }
}

void writeCounter(const MelodyContext& c, uint64_t seed)
{
    const Plan& plan = *c.plan;
    const Harmony& h = *c.harmony;
    Rng r;
    r.seed(seed);
    for (int bar = 0; bar < plan.bars; bar += 2) {
        if (plan.at(bar, Layer::Counter) == LayerState::Off) continue;
        // A held answer on beat 3 of the first bar, over the lead's phrase: the third or the fifth, high.
        const int tone = r.uniform() < 0.6f ? 1 : 2;
        const int pitch = atOrAbove(h.pc(bar, tone), 79);
        c.note(4.0 * bar + 2.0, 5.5, Part::Counter, pitch, 0.7f);
    }
}

void writeArp(const MelodyContext& c, uint64_t seed, bool slow)
{
    const Plan& plan = *c.plan;
    const Harmony& h = *c.harmony;
    Rng r;
    r.seed(seed);
    const int mode = r.below(3);   // up, up and down, random order (Dok. 4)
    const double step = slow ? 0.5 : 0.25;
    for (int bar = 0; bar < plan.bars; ++bar) {
        if (plan.at(bar, Layer::Arp) == LayerState::Off) continue;
        const int root = atOrAbove(h.pc(bar, 0), 64);
        const int third = atOrAbove(h.pc(bar, 1), root);
        const int fifth = atOrAbove(h.pc(bar, 2), third);
        std::vector<int> seq = { root, third, fifth, root + 12 };
        if (mode == 1) seq = { root, third, fifth, root + 12, fifth, third };
        if (mode == 2) { Rng br; br.seed(mixSeed(seed, static_cast<uint64_t>(bar % 4))); for (size_t i = seq.size(); i > 1; --i) std::swap(seq[i - 1], seq[static_cast<size_t>(br.below(static_cast<int>(i)))]); }
        const int n = static_cast<int>(4.0 / step);
        for (int s = 0; s < n; ++s)
            c.note(4.0 * bar + step * s, step * 0.5, Part::Arp, seq[static_cast<size_t>(s) % seq.size()], s % (slow ? 2 : 4) == 0 ? 0.85f : 0.6f);
    }
}

void writePluck(const MelodyContext& c, uint64_t seed)
{
    const Plan& plan = *c.plan;
    const Harmony& h = *c.harmony;
    Rng r;
    r.seed(seed);
    static const std::vector<std::vector<int>> kRhythms = {
        { 0, 3, 6, 9, 12, 14 },               // 3-3-3-3-2-2
        { 0, 2, 3, 6, 8, 10, 11, 14 },        // the rolling pluck
        { 0, 3, 6, 8, 11, 14 },               // 3-3-2-3-3-2
        { 2, 3, 6, 7, 10, 11, 14, 15 },       // the off-beat pairs
        { 0, 3, 6, 10, 12, 14 },
    };
    const std::vector<int>& rh = kRhythms[static_cast<size_t>(r.below(static_cast<int>(kRhythms.size())))];
    const int surpriseAt = static_cast<int>(rh.size()) - 1 - r.below(2);
    for (int bar = 0; bar < plan.bars; ++bar) {
        if (plan.at(bar, Layer::Pluck) == LayerState::Off) continue;
        const int root = atOrAbove(h.pc(bar, 0), 60);
        const int fifth = atOrAbove(h.pc(bar, 2), root);
        const int third = atOrAbove(h.pc(bar, 1), root + 12);
        const int seq[6] = { root, fifth, third, fifth, root + 12, third };
        for (size_t k = 0; k < rh.size(); ++k) {
            int p = seq[k % 6];
            // One surprise in four bars: the scale's neighbour above (Dok. 4: "ein bis zwei Ueberraschungsnoten").
            if (bar % 4 == 3 && static_cast<int>(k) == surpriseAt) p = atOrAbove(h.pc(bar, 1), p + 1) == p + 1 ? p + 2 : p + 2;
            c.note(4.0 * bar + 0.25 * rh[k], 0.22, Part::Pluck, p, k == 0 ? 0.9f : 0.72f);
        }
    }
}

void writeStab(const MelodyContext& c, uint64_t seed)
{
    const Plan& plan = *c.plan;
    const Harmony& h = *c.harmony;
    Rng r;
    r.seed(seed);
    static const std::vector<std::vector<int>> kRhythms = { { 3, 6, 10 }, { 2, 6, 10, 14 }, { 3, 7, 11, 14 } };
    const std::vector<int>& rh = kRhythms[static_cast<size_t>(r.below(3))];
    for (int bar = 0; bar < plan.bars; ++bar) {
        if (plan.at(bar, Layer::Stab) == LayerState::Off) continue;
        const int lo = atOrAbove(h.pc(bar, 0), 60);
        const int t = atOrAbove(h.pc(bar, 1), lo), f = atOrAbove(h.pc(bar, 2), t);
        for (int s : rh)
            for (int p : { lo, t, f }) c.note(4.0 * bar + 0.25 * s, 0.2, Part::Stab, p, 0.8f);
    }
}

} // namespace parh
