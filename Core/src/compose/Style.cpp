/**
 * @file Style.cpp
 * @brief The five style profiles (Style.h).
 */
#include "parh/compose/Style.h"
#include <algorithm>
#include <cmath>

namespace parh {

const int kProgressionDegrees[kProgressions][4] = {
    { 0, 5, 2, 6 },   // i-VI-III-VII: the Uplifting standard (Dok. 4)
    { 0, 6, 5, 6 },   // i-VII-VI-VII: circling, hypnotic
    { 5, 6, 0, 0 },   // VI-VII-i: the lift, the last bars before a drop
    { 0, 3, 5, 6 },   // i-iv-VI-VII: melancholic, the piano's
    { 0, 5, 6, 4 },   // i-VI-VII-v: the minor dominant, Cinematic
};
const char* const kProgressionNames[kProgressions] = { "i-VI-III-VII", "i-VII-VI-VII", "VI-VII-i", "i-iv-VI-VII", "i-VI-VII-v" };
const char* const kBassPatternNames[kBassPatterns] = { "Off-beat", "Rolling", "Gallop", "Walking", "303", "Drone" };

namespace {

using Sc = std::array<float, static_cast<int>(Scale::Count)>;   // Aeolian, Dorian, HarmonicMinor, MinorPentatonic, Phrygian, Ionian
using Pr = std::array<float, kProgressions>;                    // Anthem, Circling, Lift, Melancholy, Tension
using Ba = std::array<float, kBassPatterns>;                    // Offbeat, Rolling, Gallop, Walking, Acid, Drone
using Fo = std::array<float, kFormTemplates>;                   // Anthem, Dream, Acid, Plateau, Drift

/**
 * The profiles. Tempo and length after the references (Tools/ref_stats.json, 29.09.2026); the rest after Dok. 2 to 10
 * [Q, I]: Uplifting with Cinematic's orchestral breakdown and key change; Progressive's plateaus, Dorian and walking bass;
 * Dream House's piano, one chord a bar, a dry short kick; Acid's static harmony, the 303 as bass and lead; Deep's drones,
 * long rooms, a soft or absent kick.
 */
const StyleProfile kProfiles[static_cast<int>(Style::Count)] = {
    { .name = "Uplifting", .bpmLow = 136.0f, .bpmHigh = 140.0f, .minutesLow = 7.5f, .minutesHigh = 9.5f,
      .breakdownLow = 0.1f, .breakdownHigh = 0.26f, .forms = Fo{ 1, 0, 0, 0, 0 }, .introBars = 32, .outroBars = 32, .introLong = 0.5f,
      .ambientIntro = 0.1f, .ambientIntroBars = 16, .ambientOutro = 0.1f, .gapLu = 2.0f,
      .minor = 0.85f, .scales = Sc{ 0.75f, 0.05f, 0.15f, 0.0f, 0.05f, 0.0f }, .progressions = Pr{ 0.5f, 0.15f, 0.0f, 0.15f, 0.2f },
      .barsPerChord = 1, .barsPerChordBreak = 2, .keyChange = 0.25f,
      .bass = Ba{ 0.6f, 0.35f, 0.05f, 0.0f, 0.0f, 0.0f }, .hats16 = 0.7f, .ride = 0.8f, .perc = 0.5f, .shaker = 0.4f,
      .lead = LeadKind::Supersaw, .pluck = 0.8f, .arp = 0.5f, .stab = 0.3f, .counter = 0.5f, .gate = 0.6f, .orchestra = 0.5f,
      .hallBreakS = 6.0f, .hallDropS = 1.5f, .bassDuckDb = 10.0f, .padDuckDb = 4.0f, .targetLufs = -8.0f },
    { .name = "Progressive", .bpmLow = 128.0f, .bpmHigh = 138.0f, .minutesLow = 7.0f, .minutesHigh = 9.5f,
      .breakdownLow = 0.07f, .breakdownHigh = 0.24f, .forms = Fo{ 0.1f, 0, 0, 0.9f, 0 }, .introBars = 32, .outroBars = 32, .introLong = 0.5f,
      .ambientIntro = 0.6f, .ambientIntroBars = 24, .ambientOutro = 0.6f, .gapLu = 3.0f,
      .minor = 0.85f, .scales = Sc{ 0.55f, 0.3f, 0.05f, 0.0f, 0.1f, 0.0f }, .progressions = Pr{ 0.2f, 0.4f, 0.0f, 0.2f, 0.2f },
      .barsPerChord = 2, .barsPerChordBreak = 4, .keyChange = 0.0f,
      .bass = Ba{ 0.35f, 0.2f, 0.0f, 0.45f, 0.0f, 0.0f }, .hats16 = 0.5f, .ride = 0.5f, .perc = 0.8f, .shaker = 0.6f,
      .lead = LeadKind::PluckArp, .pluck = 0.9f, .arp = 0.8f, .stab = 0.3f, .counter = 0.2f, .gate = 0.5f, .orchestra = 0.0f,
      .hallBreakS = 3.0f, .hallDropS = 1.2f, .bassDuckDb = 8.0f, .padDuckDb = 4.0f, .targetLufs = -8.0f },
    { .name = "Dream House", .bpmLow = 134.0f, .bpmHigh = 140.0f, .minutesLow = 6.0f, .minutesHigh = 8.0f,
      .breakdownLow = 0.0f, .breakdownHigh = 0.12f, .forms = Fo{ 0, 1, 0, 0, 0 }, .introBars = 16, .outroBars = 16, .introLong = 0.3f,
      .ambientIntro = 0.85f, .ambientIntroBars = 48, .ambientOutro = 0.1f, .gapLu = 3.0f,
      .minor = 0.8f, .scales = Sc{ 0.8f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f }, .progressions = Pr{ 0.3f, 0.2f, 0.1f, 0.4f, 0.0f },
      .barsPerChord = 1, .barsPerChordBreak = 2, .keyChange = 0.0f,
      .bass = Ba{ 0.9f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f }, .hats16 = 0.3f, .ride = 0.3f, .perc = 0.3f, .shaker = 0.3f, .kickSoft = 0.3f,
      .lead = LeadKind::Piano, .pluck = 0.3f, .arp = 0.3f, .stab = 0.2f, .counter = 0.2f, .gate = 0.3f, .orchestra = 0.25f,
      .hallBreakS = 3.0f, .hallDropS = 1.2f, .bassDuckDb = 8.0f, .padDuckDb = 3.0f, .targetLufs = -10.0f },
    { .name = "Acid", .bpmLow = 130.0f, .bpmHigh = 140.0f, .minutesLow = 6.0f, .minutesHigh = 9.0f,
      .breakdownLow = 0.04f, .breakdownHigh = 0.16f, .forms = Fo{ 0, 0, 1, 0, 0 }, .introBars = 32, .outroBars = 32, .introLong = 0.5f,
      .ambientIntro = 0.1f, .ambientIntroBars = 16, .ambientOutro = 0.2f, .gapLu = 2.5f,
      .minor = 0.7f, .scales = Sc{ 0.4f, 0.2f, 0.0f, 0.1f, 0.3f, 0.0f }, .progressions = Pr{ 0.2f, 0.6f, 0.0f, 0.1f, 0.1f },
      .barsPerChord = 16, .barsPerChordBreak = 16, .keyChange = 0.0f,
      .bass = Ba{ 0.3f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f }, .hats16 = 0.7f, .ride = 0.5f, .perc = 0.6f, .shaker = 0.3f,
      .lead = LeadKind::Acid, .pluck = 0.1f, .arp = 0.1f, .stab = 0.3f, .counter = 0.0f, .gate = 0.2f, .orchestra = 0.0f,
      .hallBreakS = 1.5f, .hallDropS = 0.8f, .bassDuckDb = 6.0f, .padDuckDb = 3.0f, .targetLufs = -8.5f },
    { .name = "Deep", .bpmLow = 126.0f, .bpmHigh = 134.0f, .minutesLow = 6.5f, .minutesHigh = 10.0f,
      .breakdownLow = 0.1f, .breakdownHigh = 0.32f, .forms = Fo{ 0, 0, 0, 0, 1 }, .introBars = 16, .outroBars = 16, .introLong = 0.5f,
      .ambientIntro = 0.5f, .ambientIntroBars = 16, .ambientOutro = 0.8f, .gapLu = 3.0f,
      .minor = 0.75f, .scales = Sc{ 0.5f, 0.35f, 0.0f, 0.15f, 0.0f, 0.0f }, .progressions = Pr{ 0.3f, 0.3f, 0.0f, 0.4f, 0.0f },
      .barsPerChord = 2, .barsPerChordBreak = 4, .keyChange = 0.0f,
      .bass = Ba{ 0.2f, 0.0f, 0.0f, 0.4f, 0.0f, 0.4f }, .hats16 = 0.2f, .ride = 0.1f, .perc = 0.6f, .shaker = 0.8f, .kickSoft = 0.8f,
      .beatless = 0.1f, .lead = LeadKind::Pad, .pluck = 0.5f, .arp = 0.8f, .stab = 0.0f, .counter = 0.2f, .gate = 0.4f, .orchestra = 0.15f,
      .hallBreakS = 9.0f, .hallDropS = 3.0f, .bassDuckDb = 3.0f, .padDuckDb = 2.0f, .targetLufs = -9.0f },
};

float lerp(float a, float b, float t) { return a + (b - a) * t; }

template <size_t N>
std::array<float, N> mixArrays(const std::array<float, N>& a, const std::array<float, N>& b, float t)
{
    std::array<float, N> o{};
    for (size_t i = 0; i < N; ++i) o[i] = lerp(a[i], b[i], t);
    return o;
}

} // namespace

const StyleProfile& styleProfile(Style s)
{
    const int i = std::clamp(static_cast<int>(s), 0, static_cast<int>(Style::Count) - 1);
    return kProfiles[i];
}

StyleProfile morphProfile(const StyleProfile& a, const StyleProfile& b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const StyleProfile& near = t < 0.5f ? a : b;
    StyleProfile o = near;
    o.bpmLow = lerp(a.bpmLow, b.bpmLow, t);
    o.bpmHigh = lerp(a.bpmHigh, b.bpmHigh, t);
    o.minutesLow = lerp(a.minutesLow, b.minutesLow, t);
    o.minutesHigh = lerp(a.minutesHigh, b.minutesHigh, t);
    o.breakdownLow = lerp(a.breakdownLow, b.breakdownLow, t);
    o.breakdownHigh = lerp(a.breakdownHigh, b.breakdownHigh, t);
    o.forms = mixArrays(a.forms, b.forms, t);
    o.introLong = lerp(a.introLong, b.introLong, t);
    o.ambientIntro = lerp(a.ambientIntro, b.ambientIntro, t);
    o.ambientIntroBars = static_cast<int>(std::lround(lerp(static_cast<float>(a.ambientIntroBars), static_cast<float>(b.ambientIntroBars), t) / 8.0f)) * 8;
    o.ambientOutro = lerp(a.ambientOutro, b.ambientOutro, t);
    o.gapLu = lerp(a.gapLu, b.gapLu, t);
    o.minor = lerp(a.minor, b.minor, t);
    o.scales = mixArrays(a.scales, b.scales, t);
    o.progressions = mixArrays(a.progressions, b.progressions, t);
    o.keyChange = lerp(a.keyChange, b.keyChange, t);
    o.bass = mixArrays(a.bass, b.bass, t);
    o.hats16 = lerp(a.hats16, b.hats16, t);
    o.ride = lerp(a.ride, b.ride, t);
    o.perc = lerp(a.perc, b.perc, t);
    o.shaker = lerp(a.shaker, b.shaker, t);
    o.kickSoft = lerp(a.kickSoft, b.kickSoft, t);
    o.beatless = lerp(a.beatless, b.beatless, t);
    o.pluck = lerp(a.pluck, b.pluck, t);
    o.arp = lerp(a.arp, b.arp, t);
    o.stab = lerp(a.stab, b.stab, t);
    o.counter = lerp(a.counter, b.counter, t);
    o.gate = lerp(a.gate, b.gate, t);
    o.orchestra = lerp(a.orchestra, b.orchestra, t);
    o.hallBreakS = std::exp(lerp(std::log(a.hallBreakS), std::log(b.hallBreakS), t));
    o.hallDropS = std::exp(lerp(std::log(a.hallDropS), std::log(b.hallDropS), t));
    o.bassDuckDb = lerp(a.bassDuckDb, b.bassDuckDb, t);
    o.padDuckDb = lerp(a.padDuckDb, b.padDuckDb, t);
    o.targetLufs = lerp(a.targetLufs, b.targetLufs, t);
    return o;
}

StyleProfile profileOf(const ParamStore& p)
{
    const int style = p.getInt(p.id(Module::Compose, 0, compose::Style));
    const StyleProfile& base = styleProfile(static_cast<Style>(style));
    const int to = p.getInt(p.id(Module::Compose, 0, compose::MorphTo));
    if (to <= 0) return base;
    return morphProfile(base, styleProfile(static_cast<Style>(to - 1)), p.get(p.id(Module::Compose, 0, compose::Morph)));
}

int drawWeighted(const float* weights, int n, float u)
{
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) sum += std::max(0.0f, weights[i]);
    if (sum <= 0.0f) return 0;
    float x = u * sum;
    int last = 0;
    for (int i = 0; i < n; ++i) {
        const float w = std::max(0.0f, weights[i]);
        if (w <= 0.0f) continue;
        last = i;
        if (x < w) return i;
        x -= w;
    }
    return last;
}

} // namespace parh
