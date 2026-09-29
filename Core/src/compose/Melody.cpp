/**
 * @file Melody.cpp
 * @brief The melodic voices (Melody.h).
 */
#include "parh/compose/Melody.h"
#include "parh/compose/Corpus.h"
#include "parh/compose/Memo.h"
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

/**
 * @brief The seven degrees the corpus lines are drawn on: the mode for Aeolian, Dorian and Phrygian; Aeolian for harmonic
 *        minor (its raised seventh comes with the major V, see scaleAt) and the pentatonic (five of its seven); in a major
 *        key Aeolian from the relative minor's tonic, where the corpus counts its major lines.
 */
struct StepFrame {
    int tonicPc = 9;     ///< the (relative) minor tonic's pitch class
    ScaleDef scale{};    ///< its degrees
    bool pent = false;   ///< the minor pentatonic: the second and the sixth degree left out
};

StepFrame frameOf(const Harmony& h)
{
    StepFrame f;
    const bool major = h.scale == static_cast<int>(Scale::Ionian);
    const bool own = h.scale == static_cast<int>(Scale::Aeolian) || h.scale == static_cast<int>(Scale::Dorian)
                     || h.scale == static_cast<int>(Scale::Phrygian);
    f.tonicPc = major ? (h.key + 9) % 12 : h.key;
    f.scale = scaleDef(own ? h.scale : static_cast<int>(Scale::Aeolian));
    f.pent = h.scale == static_cast<int>(Scale::MinorPentatonic);
    return f;
}

int clampBar(const Harmony& h, int bar) { return std::clamp(bar, 0, static_cast<int>(h.bars.size()) - 1); }

/** @brief The frame's degrees at @p bar: a chord tone the mode lacks (harmonic minor's major V) replaces the degree a
 *         semitone from it. */
ScaleDef scaleAt(const StepFrame& f, const Harmony& h, int bar)
{
    ScaleDef s = f.scale;
    const int shift = h.shift[static_cast<size_t>(clampBar(h, bar))];
    for (int i = 0; i < 3; ++i) {
        const int rel = ((h.pc(bar, i) - f.tonicPc - shift) % 12 + 12) % 12;
        if (std::find(s.steps, s.steps + 7, rel) != s.steps + 7) continue;
        for (int d = 0; d < 7; ++d)
            if (std::abs(s.steps[d] - rel) == 1) { s.steps[d] = rel; break; }
    }
    return s;
}

/** @brief The pitch of step index @p idx (index 7 is the tonic at @p base) in the degrees @p s. */
int stepPitch(int idx, int base, const ScaleDef& s)
{
    const int st = idx + kCorpusStepMin;
    const int o = st >= 0 ? st / 7 : -((-st + 6) / 7);
    return base + 12 * o + s.steps[st - 7 * o];
}

/** @brief The degree (0 .. 6) of step index @p idx. */
int degreeOf(int idx) { return ((idx + kCorpusStepMin) % 7 + 7) % 7; }

/** @brief Whether @p pitch is a tone of the chord at @p bar. */
bool chordHas(const Harmony& h, int bar, int pitch)
{
    const int pc = (pitch % 12 + 12) % 12;
    return pc == h.pc(bar, 0) || pc == h.pc(bar, 1) || pc == h.pc(bar, 2);
}

/** @brief One motif: onsets in sixteenths over two bars, lengths in sixteenths, step indices. */
struct Motif {
    std::vector<int> on, len, steps;
};

/** @brief How good a motif is by the rules of Dok. 4 (higher is better). */
double motifScore(const Motif& m, bool anthem)
{
    if (m.steps.size() < 3) return -1e9;
    int lo = 99, hi = -99, distinct = 0, stepwise = 0, droning = 0, run = 0;
    bool seen[kCorpusAlpha] = {};
    for (size_t k = 0; k < m.steps.size(); ++k) {
        lo = std::min(lo, m.steps[k]);
        hi = std::max(hi, m.steps[k]);
        if (!seen[m.steps[k]]) { seen[m.steps[k]] = true; ++distinct; }
        if (k > 0) {
            const int d = std::abs(m.steps[k] - m.steps[k - 1]);
            if (d == 1 || d == 2) ++stepwise;
            run = d == 0 ? run + 1 : 0;
            if (run >= 2) ++droning;
        }
    }
    double s = -std::fabs((hi - lo) - (anthem ? 7.0 : 6.0)) * 0.6;   // about an octave (a riff a little less)
    s += std::min(distinct, 6) * 0.8;                                  // enough different tones to be a melody
    s -= droning * (anthem ? 4.0 : 1.5);                               // no droning on one tone
    const double sw = static_cast<double>(stepwise) / static_cast<double>(m.steps.size() - 1);
    s -= std::fabs(sw - (anthem ? 0.6 : 0.4)) * 4.0;                  // mostly stepwise (Dok. 4)
    if (anthem && m.steps[1] - m.steps[0] >= 4) s += 2.0;              // the leap at the start
    if (anthem && m.steps.back() < m.steps[m.steps.size() / 2]) s += 1.0;   // the walk down to the end
    return s;
}

/** @brief The rhythms of the anthem motif (sixteenths over two bars, Dok. 4). */
const std::vector<std::vector<int>> kAnthemRhythms = {
    { 0, 4, 6, 8, 12, 16, 20, 22, 24 },            // long-short-short, a held end
    { 0, 3, 6, 8, 10, 12, 16, 19, 22, 24, 28 },    // the 3-3-2 of the pluck, sung
    { 0, 2, 4, 8, 10, 12, 16, 18, 20, 24 },        // eighth pairs
    { 0, 6, 8, 12, 14, 16, 22, 24, 28 },           // dotted
    { 0, 4, 8, 10, 12, 14, 16, 20, 24, 26, 28 },   // walking eighths
    { 0, 3, 6, 10, 12, 16, 19, 22, 26 },           // the syncopated call
};

/** @brief The lead once, from @p seed (writeLead draws again on a memorisation hit). */
void writeLeadOnce(const MelodyContext& c, uint64_t seed)
{
    const Plan& plan = *c.plan;
    const Harmony& h = *c.harmony;
    Rng r;
    r.seed(seed);
    const StepModel& model = corpusModel(c.piano ? CorpusRoleId::Piano : CorpusRoleId::Lead);
    const StepFrame f = frameOf(h);
    // The kind: the anthem motif (long notes) or the sixteenth riff of the EMP melodies; the piano plays its corpus's
    // eighths, legato.
    const bool anthem = !c.piano && r.uniform() < c.anthemShare;
    const bool pent = f.pent || (!c.piano && h.scale != static_cast<int>(Scale::Ionian) && r.uniform() < 0.4f);
    // The register: the tonic from E4 (the piano's from C4) is index 7; the line from a third under it to a tenth over.
    const int base = atOrAbove(f.tonicPc, c.piano ? 60 : 64);
    const int lo = 5, hi = anthem || c.piano ? 17 : 15;

    auto drawRhythm = [&](Motif& m) {
        m.on.clear();
        if (anthem) {
            m.on = kAnthemRhythms[static_cast<size_t>(r.below(static_cast<int>(kAnthemRhythms.size())))];
        } else if (c.piano) {
            // The piano corpus's chances per sixteenth over both bars.
            bool prev = true;
            for (int s = 0; s < 32; ++s) {
                const bool on = s == 0 || r.uniform() < model.onset(s, prev);
                if (on) m.on.push_back(s);
                prev = on;
            }
        } else {
            // The riff: a cell of half a bar from the lead corpus's chances (four onsets at least), four times.
            std::vector<int> cell;
            while (cell.size() < 4) {
                cell.clear();
                bool prev = true;
                for (int s = 0; s < 8; ++s) {
                    const bool on = s == 0 || r.uniform() < model.onset(s, prev) * 0.9;
                    if (on) cell.push_back(s);
                    prev = on;
                }
            }
            for (int q = 0; q < 4; ++q) for (int s : cell) m.on.push_back(8 * q + s);
        }
        m.len.clear();
        for (size_t k = 0; k < m.on.size(); ++k) m.len.push_back((k + 1 < m.on.size() ? m.on[k + 1] : 32) - m.on[k]);
    };
    // The steps under the constraints; @p fixed: a step per note that stays (-1: drawn), @p lift: the range moved up,
    // @p cadence: the last note on the tonic where the chord has it.
    auto drawSteps = [&](Motif& m, int bar, const std::vector<int>& fixed, int lift, bool cadence, int ceiling = 127) {
        const int n = static_cast<int>(m.on.size());
        std::vector<std::array<bool, kCorpusAlpha>> allowed(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k) {
            const int b = clampBar(h, bar + m.on[static_cast<size_t>(k)] / 16);
            const ScaleDef sc = scaleAt(f, h, b);
            const int shift = h.shift[static_cast<size_t>(b)];
            const bool strong = m.on[static_cast<size_t>(k)] % 4 == 0 || k == n - 1;
            const bool tonicInChord = chordHas(h, b, f.tonicPc + shift);
            for (int s = 0; s < kCorpusAlpha; ++s) {
                bool ok = s >= lo + lift && s <= hi + lift && stepPitch(s, base, sc) + shift <= ceiling;
                if (ok && pent) ok = degreeOf(s) != 1 && degreeOf(s) != 5;
                if (ok && strong) ok = chordHas(h, b, stepPitch(s, base, sc) + shift);
                if (ok && cadence && k == n - 1 && tonicInChord) ok = degreeOf(s) == 0;
                if (k < static_cast<int>(fixed.size()) && fixed[static_cast<size_t>(k)] >= 0) ok = s == fixed[static_cast<size_t>(k)];
                allowed[static_cast<size_t>(k)][static_cast<size_t>(s)] = ok;
            }
        }
        // The contour (Dok. 4): a leap up after the first note (a fourth to an octave), then down, stepwise; the riff
        // keeps the corpus's figures, only its widest leaps damped.
        auto shape = [&](int i, int before, int prevStep, int s) {
            if (prevStep < 0) return 1.0;
            const int d = s - prevStep;
            if (!anthem) return std::abs(d) > 7 ? 0.3 : 1.0;
            if (i == 1) return d >= 3 && d <= 7 ? 4.0 : d > 0 ? 1.0 : 0.3;
            if (d == 0 && before == prevStep) return 0.05;   // a third time the same tone: a drone, not a melody
            if (std::abs(d) <= 2) return d < 0 ? 1.6 : d == 0 ? 0.25 : 1.0;
            return std::abs(d) <= 4 ? 0.5 : 0.1;
        };
        return sampleConstrained(model, n, allowed, shape, r, m.steps);
    };
    // A motif's notes at @p bar that fit its chords (-1 where a tone on a beat has lost its chord).
    auto fitting = [&](const Motif& m, int bar) {
        std::vector<int> fixed(m.steps.size(), -1);
        for (size_t k = 0; k < m.steps.size(); ++k) {
            const int b = clampBar(h, bar + m.on[k] / 16);
            const bool strong = m.on[k] % 4 == 0 || k + 1 == m.steps.size();
            if (!strong || chordHas(h, b, stepPitch(m.steps[k], base, scaleAt(f, h, b)) + h.shift[static_cast<size_t>(b)]))
                fixed[k] = m.steps[k];
        }
        return fixed;
    };
    // The riff's repetition: every cell sounds the first one's steps, the last two notes of the second and the fourth
    // drawn again (and a tone on a beat whose chord has moved).
    const bool riff = !anthem && !c.piano;
    auto repeatCells = [&](const Motif& m, int bar) {
        std::vector<int> fixed(m.steps.size(), -1);
        std::vector<int> first(8, -1);
        size_t perCell = 0;
        for (size_t k = 0; k < m.on.size(); ++k)
            if (m.on[k] < 8) { first[static_cast<size_t>(m.on[k])] = m.steps[k]; ++perCell; }
        for (size_t k = 0; k < m.on.size(); ++k) {
            const int q = m.on[k] / 8;
            const size_t inCell = k % std::max<size_t>(1, perCell);
            if ((q == 1 || q == 3) && inCell + 2 >= perCell) continue;
            const int st = first[static_cast<size_t>(m.on[k] % 8)];
            const int b = clampBar(h, bar + m.on[k] / 16);
            const bool strong = m.on[k] % 4 == 0 || k + 1 == m.on.size();
            if (st >= 0 && (!strong || chordHas(h, b, stepPitch(st, base, scaleAt(f, h, b)) + h.shift[static_cast<size_t>(b)])))
                fixed[k] = st;
        }
        return fixed;
    };
    auto best = [&](int bar, int lift, int tries) {
        Motif out;
        double bestScore = -1e18;
        for (int k = 0; k < tries; ++k) {
            Motif m;
            drawRhythm(m);
            if (!drawSteps(m, bar, {}, lift, false)) continue;
            if (riff && !drawSteps(m, bar, repeatCells(m, bar), lift, false)) continue;
            const double s = motifScore(m, anthem);
            if (s > bestScore) { bestScore = s; out = m; }
        }
        return out;
    };
    auto phrasePos = [&](int bar) {
        const int sec = plan.sectionAt(bar);
        const int secStart = sec >= 0 ? static_cast<int>(plan.sections[static_cast<size_t>(sec)].beat / 4.0) : 0;
        return ((bar - secStart) / 2) % 4;
    };
    // A over the chords where it is first heard, B where the first whole phrase reaches it.
    int barA = -1, barB = -1;
    for (int bar = 0; bar < plan.bars; bar += 2) {
        const LayerState st = plan.at(bar, Layer::Lead);
        if (st == LayerState::Off) continue;
        if (barA < 0 && (phrasePos(bar) == 0 || st == LayerState::Filtered)) barA = bar;
        if (barB < 0 && st == LayerState::On && phrasePos(bar) == 2) barB = bar;
    }
    if (barA < 0) return;
    const Motif a = best(barA, 0, 8);
    if (a.steps.empty()) return;
    Motif b = barB >= 0 ? best(barB, 2, 4) : a;
    if (b.steps.empty()) b = a;
    // The main drop's octave up only where the motifs stay under A6 up there (decided once, for the whole drop).
    int top = 0;
    for (int s : a.steps) top = std::max(top, stepPitch(s, base, f.scale));
    for (int s : b.steps) top = std::max(top, stepPitch(s, base, f.scale));
    const bool mainUp = top + *std::max_element(h.shift.begin(), h.shift.end()) + 12 <= 93;

    for (int bar = 0; bar < plan.bars; bar += 2) {
        const LayerState st = plan.at(bar, Layer::Lead);
        if (st == LayerState::Off) continue;
        const int pos = phrasePos(bar);
        // Filtered: the motif alone, every four bars (the tease, the fragment in a break or a build).
        if (st == LayerState::Filtered && pos % 2 == 1) continue;
        const bool isB = st == LayerState::On && pos == 2;
        Motif m = isB ? b : a;
        // The main drop's second half an octave up (Dok. 6: "Variation nach 16 Takten"); not the piano (PLAN 6.6). A
        // variant drawn there stays under C7.
        int oct = 0;
        const int sec = plan.sectionAt(bar);
        if (mainUp && !c.piano && sec >= 0 && sec == plan.mainDrop) {
            const Section& s = plan.sections[static_cast<size_t>(sec)];
            if (bar * 4.0 >= s.beat + s.length / 2.0 && s.length >= 128.0) oct = 12;
        }
        const int ceiling = 96 - oct;
        if (st == LayerState::On && pos == 3) {
            // A'': A's head (its tones that fit here), the rest walking to the cadence.
            std::vector<int> fixed = fitting(a, bar);
            for (size_t k = a.steps.size() / 2; k < fixed.size(); ++k) fixed[k] = -1;
            Motif v = a;
            if (drawSteps(v, bar, fixed, 0, true, ceiling)) m = v;
        } else {
            // A, A' and B: the tones that fit this bar's chords stay, the others are drawn again.
            const std::vector<int> fixed = fitting(m, bar);
            if (std::find(fixed.begin(), fixed.end(), -1) != fixed.end()) {
                Motif v = m;
                if (drawSteps(v, bar, fixed, isB ? 2 : 0, false, ceiling)) m = v;
            }
        }
        auto pitchOf = [&](size_t k) {
            const int nb = std::min(bar + m.on[k] / 16, plan.bars - 1);
            return stepPitch(m.steps[k], base, scaleAt(f, h, nb)) + h.shift[static_cast<size_t>(nb)];
        };
        for (size_t k = 0; k < m.on.size() && k < m.steps.size(); ++k) {
            const int nb = bar + m.on[k] / 16;
            if (nb >= plan.bars || plan.at(nb, Layer::Lead) == LayerState::Off) continue;
            const int pitch = pitchOf(k) + oct;
            const double len = c.piano ? 0.25 * m.len[k] + 0.25 : anthem ? 0.25 * m.len[k] - 0.02 : 0.22;
            // The piano plays mezzo-forte (its velocity is its hammer's speed, and it has the whole range of a piano).
            const float vs = c.piano ? 0.72f : 1.0f;
            c.note(4.0 * bar + 0.25 * m.on[k], len, c.piano ? Part::Piano : Part::Lead, pitch,
                   vs * (k == 0 ? 0.95f : (m.on[k] % 4 == 0 ? 0.85f : 0.72f)));
        }
    }
}

/** @brief The top voice of @p part from note @p from on: a pitch per sixteenth (-1: no onset). */
std::vector<int> topVoice(const Score& sc, size_t from, Part part, int bars)
{
    std::vector<int> top(static_cast<size_t>(bars) * 16, -1);
    for (size_t i = from; i < sc.notes.size(); ++i) {
        const NoteEvent& n = sc.notes[i];
        if (n.part != part) continue;
        const long long k = std::llround(n.beat * 4.0);
        if (k >= 0 && k < static_cast<long long>(top.size())) top[static_cast<size_t>(k)] = std::max(top[static_cast<size_t>(k)], n.pitch);
    }
    return top;
}

} // namespace

void writeLead(const MelodyContext& c, uint64_t seed)
{
    const size_t n0 = c.score->notes.size();
    for (int attempt = 0; attempt < 16; ++attempt) {
        writeLeadOnce(c, attempt == 0 ? seed : mixSeed(seed, static_cast<uint64_t>(attempt)));
        if (!memoHitsAnyBar(topVoice(*c.score, n0, c.piano ? Part::Piano : Part::Lead, c.plan->bars))) return;
        c.score->notes.resize(n0);   // two bars of a known track: drawn again
    }
}

void writeCounter(const MelodyContext& c, uint64_t seed)
{
    const Plan& plan = *c.plan;
    const Harmony& h = *c.harmony;
    Rng r;
    r.seed(seed);
    std::vector<NoteEvent> held;
    for (const NoteEvent& n : c.score->notes)
        if ((n.part == Part::Lead || n.part == Part::Piano) && n.length >= 0.95) held.push_back(n);
    // The chord tone at or under @p pitch at @p bar.
    auto toneUnder = [&](int bar, int pitch) {
        for (int p = pitch; p > pitch - 12; --p) if (chordHas(h, bar, p)) return p;
        return pitch;
    };
    for (int bar = 0; bar < plan.bars; bar += 2) {
        if (plan.at(bar, Layer::Counter) == LayerState::Off) continue;
        int answers = 0;
        for (const NoteEvent& n : held) {
            if (n.beat < 4.0 * bar || n.beat >= 4.0 * bar + 8.0 || answers >= 2) continue;
            // In the lead's held tone, an octave over it: chord tones walking down in eighths, the last held.
            const int nb = std::min(static_cast<int>(n.beat / 4.0), plan.bars - 1);
            int top = n.pitch + 12;
            while (top > 88) top -= 12;
            const double room = n.length - 0.5;
            if (room < 0.9) continue;
            const int count = room >= 1.4 ? 3 : 2;
            int p = toneUnder(nb, top);
            for (int k = 0; k < count; ++k) {
                const double at = n.beat + 0.5 + 0.5 * k;
                const double len = k + 1 == count ? std::max(0.3, n.beat + n.length - at - 0.05) : 0.45;
                c.note(at, len, Part::Counter, p, k == 0 ? 0.75f : 0.65f);
                p = toneUnder(nb, p - 1);
            }
            ++answers;
        }
        if (answers == 0) {
            // A held answer on beat 3 over the riff: the third or the fifth, high.
            const int tone = r.uniform() < 0.6f ? 1 : 2;
            c.note(4.0 * bar + 2.0, 5.5, Part::Counter, atOrAbove(h.pc(bar, tone), 79), 0.7f);
        }
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

void writeAcid(const MelodyContext& c, uint64_t seed)
{
    const Plan& plan = *c.plan;
    const Harmony& h = *c.harmony;
    Rng r;
    r.seed(seed);
    const StepModel& model = corpusModel(CorpusRoleId::Acid);
    const StepFrame f = frameOf(h);
    // The bar: onsets from the corpus (the first step always), accents and slides by the rules (Dok. 5; the EMP acid
    // loops carry no velocity accents and few slides, so their counts only shape where the slides fall).
    struct Step { bool on; int idx; bool accent; bool slide; };
    Step seq[16];
    double slideMean = 0.0;
    for (int s = 0; s < 16; ++s) slideMean += model.slide(s) / 16.0;
    bool prev = true;
    std::vector<int> ons;
    for (int s = 0; s < 16; ++s) {
        seq[s].on = s == 0 || r.uniform() < model.onset(s, prev);
        seq[s].accent = r.uniform() < 0.3f;
        seq[s].slide = r.uniform() < 0.22 * model.slide(s) / std::max(1e-6, slideMean);
        seq[s].idx = 7;
        prev = seq[s].on;
        if (seq[s].on) ons.push_back(s);
    }
    // The pitches: an octave either side of the root, the first note on it.
    auto drawPitches = [&](const std::vector<int>& fixed) {
        const int n = static_cast<int>(ons.size());
        std::vector<std::array<bool, kCorpusAlpha>> allowed(static_cast<size_t>(n));
        for (int k = 0; k < n; ++k)
            for (int s = 0; s < kCorpusAlpha; ++s) {
                bool ok = s <= 14 && (!f.pent || (degreeOf(s) != 1 && degreeOf(s) != 5));
                if (k == 0) ok = s == 7;
                if (k < static_cast<int>(fixed.size()) && fixed[static_cast<size_t>(k)] >= 0) ok = s == fixed[static_cast<size_t>(k)];
                allowed[static_cast<size_t>(k)][static_cast<size_t>(s)] = ok;
            }
        std::vector<int> out;
        if (!sampleConstrained(model, n, allowed, nullptr, r, out)) return;
        for (int k = 0; k < n; ++k) seq[ons[static_cast<size_t>(k)]].idx = out[static_cast<size_t>(k)];
    };
    drawPitches({});
    for (int bar = 0; bar < plan.bars; ++bar) {
        if (plan.at(bar, Layer::Acid) == LayerState::Off) continue;
        if (bar % 4 == 0 && bar > 0 && ons.size() > 2) {
            // The variation: two notes drawn again, one accent moved.
            std::vector<int> fixed;
            for (int s : ons) fixed.push_back(seq[s].idx);
            for (int k = 0; k < 2; ++k) fixed[static_cast<size_t>(1 + r.below(static_cast<int>(ons.size()) - 1))] = -1;
            drawPitches(fixed);
            Step& s = seq[1 + r.below(15)];
            s.accent = !s.accent;
        }
        // The line moves with the chord's root (Dok. 5: a fourth or a fifth every sixteen bars in Acid).
        const int root = atOrAbove(h.pc(bar, 0), 28) + 24;
        const ScaleDef sc = scaleAt(f, h, bar);
        for (int s = 0; s < 16; ++s) {
            const Step& st = seq[s];
            if (!st.on) continue;
            const int pitch = root + stepPitch(st.idx, 0, sc);
            c.note(4.0 * bar + 0.25 * s, st.slide ? 0.3 : 0.18, Part::Acid, pitch, st.accent ? 1.0f : 0.75f, 0, st.accent, st.slide);
        }
    }
}

} // namespace parh
