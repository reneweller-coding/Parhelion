/**
 * @file Style.cpp
 * @brief The five style profiles (Style.h).
 * @author Rene Weller
 */
#include "parh/compose/Style.h"
#include <algorithm>
#include <cmath>

namespace parh {

const int ProgressionDegrees[Progressions][4] = {
    { 0, 5, 2, 6 },   // i-VI-III-VII: the Uplifting standard (Dok. 4)
    { 0, 6, 5, 6 },   // i-VII-VI-VII: circling, hypnotic
    { 5, 6, 0, 0 },   // VI-VII-i: the lift, the last bars before a drop
    { 0, 3, 5, 6 },   // i-iv-VI-VII: melancholic, the piano's
    { 0, 5, 6, 4 },   // i-VI-VII-v: the minor dominant, Cinematic
};
const char* const FormTemplateNames[FormTemplates] = { "Anthem", "Dream", "Acid", "Plateau", "Drift" };
const char* const ProgressionNames[Progressions] = { "i-VI-III-VII", "i-VII-VI-VII", "VI-VII-i", "i-iv-VI-VII", "i-VI-VII-v" };
/** @brief the bass figures, in the order of BassPattern */
const char* const BassPatternNames[BassPatterns] = { "Off-beat", "Rolling", "Gallop", "Walking", "303", "Drone" };

namespace {

using ScT = std::array<float, static_cast<int>(ScaleE::COUNT)>;   ///< Aeolian, Dorian, HarmonicMinor, MinorPentatonic, Phrygian, Ionian
using PrT = std::array<float, Progressions>;   ///< Anthem, Circling, Lift, Melancholy, Tension
using BaT = std::array<float, BassPatterns>;   ///< Offbeat, Rolling, Gallop, Walking, Acid, Drone
using FoT = std::array<float, FormTemplates>;   ///< Weights of the form templates: Anthem, Dream, Acid, Plateau, Drift.
using MxT = std::array<float, 5>;   ///< Uplifting, Progressive, Dream House, Acid, Deep                   // Anthem, Dream, Acid, Plateau, Drift

/**
 * The profiles. Tempo and length after the references (Tools/ref_stats.json, 29.09.2026); the rest after Dok. 2 to 10
 * [Q, I]: Uplifting with Cinematic's orchestral breakdown and key change; Progressive's plateaus, Dorian and walking bass;
 * Dream House's piano, one chord a bar, a dry short kick; Acid's static harmony, the 303 as bass and lead; Deep's drones,
 * long rooms, a soft or absent kick.
 */
const StyleProfile Profiles[static_cast<int>(StyleE::COUNT)] = {
    { .m_name = "Uplifting", .m_bpmLow = 136.0f, .m_bpmHigh = 140.0f, .m_minutesLow = 7.5f, .m_minutesHigh = 9.5f,
      .m_breakdownLow = 0.1f, .m_breakdownHigh = 0.26f, .m_forms = FoT{ 1, 0, 0, 0, 0 }, .m_introBars = 32, .m_outroBars = 32, .m_introLong = 0.5f,
      .m_ambientIntro = 0.25f, .m_ambientIntroBars = 16, .m_ambientOutro = 0.1f, .m_gapLu = 2.0f, .m_introSynthDb = -1.0f,
      .m_minor = 0.85f, .m_scales = ScT{ 0.75f, 0.05f, 0.15f, 0.0f, 0.05f, 0.0f }, .m_progressions = PrT{ 0.5f, 0.15f, 0.0f, 0.15f, 0.2f },
      .m_barsPerChord = 1, .m_barsPerChordBreak = 2, .m_keyChange = 0.25f,
      .m_bass = BaT{ 0.15f, 0.5f, 0.35f, 0.0f, 0.0f, 0.0f }, .m_hats16 = 0.7f, .m_ride = 0.8f, .m_perc = 0.5f, .m_shaker = 0.4f,
      .m_lead = LeadKindE::SUPERSAW, .m_pluck = 0.8f, .m_arp = 0.7f, .m_stab = 0.55f, .m_counter = 0.5f, .m_gate = 0.6f, .m_orchestra = 0.5f,
      .m_hallBreakS = 6.0f, .m_hallDropS = 1.5f, .m_bassDuckDb = 10.0f, .m_padDuckDb = 4.0f, .m_targetLufs = -8.0f, .m_mix = MxT{ 1, 0, 0, 0, 0 } },
    { .m_name = "Progressive", .m_bpmLow = 128.0f, .m_bpmHigh = 138.0f, .m_minutesLow = 7.0f, .m_minutesHigh = 9.5f,
      .m_breakdownLow = 0.07f, .m_breakdownHigh = 0.24f, .m_forms = FoT{ 0.1f, 0, 0, 0.9f, 0 }, .m_introBars = 32, .m_outroBars = 32, .m_introLong = 0.5f,
      .m_ambientIntro = 0.6f, .m_ambientIntroBars = 24, .m_ambientOutro = 0.6f, .m_gapLu = 3.0f, .m_introSynthDb = -3.0f,
      .m_minor = 0.85f, .m_scales = ScT{ 0.55f, 0.3f, 0.05f, 0.0f, 0.1f, 0.0f }, .m_progressions = PrT{ 0.2f, 0.4f, 0.0f, 0.2f, 0.2f },
      .m_barsPerChord = 2, .m_barsPerChordBreak = 4, .m_keyChange = 0.0f,
      .m_bass = BaT{ 0.2f, 0.5f, 0.1f, 0.2f, 0.0f, 0.0f }, .m_hats16 = 0.5f, .m_ride = 0.5f, .m_perc = 0.8f, .m_shaker = 0.6f,
      .m_lead = LeadKindE::PLUCK_ARP, .m_pluck = 0.9f, .m_arp = 0.8f, .m_stab = 0.4f, .m_counter = 0.2f, .m_gate = 0.5f, .m_orchestra = 0.0f, .m_hatsDb = 6.0f, .m_tiltDb = 2.5f,
      .m_hallBreakS = 3.0f, .m_hallDropS = 1.2f, .m_bassDuckDb = 6.5f, .m_padDuckDb = 3.0f, .m_targetLufs = -8.0f, .m_mix = MxT{ 0, 1, 0, 0, 0 } },
    { .m_name = "Dream House", .m_bpmLow = 134.0f, .m_bpmHigh = 140.0f, .m_minutesLow = 6.0f, .m_minutesHigh = 8.0f,
      .m_breakdownLow = 0.0f, .m_breakdownHigh = 0.12f, .m_forms = FoT{ 0, 1, 0, 0, 0 }, .m_introBars = 16, .m_outroBars = 16, .m_introLong = 0.3f,
      .m_ambientIntro = 0.85f, .m_ambientIntroBars = 48, .m_ambientOutro = 0.1f, .m_gapLu = 6.3f, .m_introSynthDb = -1.0f,
      .m_minor = 0.8f, .m_scales = ScT{ 0.8f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f }, .m_progressions = PrT{ 0.3f, 0.2f, 0.1f, 0.4f, 0.0f },
      .m_barsPerChord = 1, .m_barsPerChordBreak = 2, .m_keyChange = 0.0f,
      .m_bass = BaT{ 0.2f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f }, .m_hats16 = 0.3f, .m_ride = 0.3f, .m_perc = 0.3f, .m_shaker = 0.3f, .m_kickSoft = 0.45f,
      .m_lead = LeadKindE::PIANO, .m_pluck = 0.3f, .m_arp = 0.45f, .m_stab = 0.2f, .m_counter = 0.2f, .m_gate = 0.3f, .m_orchestra = 0.25f, .m_hatsDb = 4.0f, .m_tiltDb = 1.5f,
      .m_hallBreakS = 3.0f, .m_hallDropS = 1.2f, .m_bassDuckDb = 8.0f, .m_padDuckDb = 3.0f, .m_targetLufs = -10.0f, .m_mix = MxT{ 0, 0, 1, 0, 0 } },
    { .m_name = "Acid", .m_bpmLow = 130.0f, .m_bpmHigh = 140.0f, .m_minutesLow = 6.0f, .m_minutesHigh = 9.0f,
      .m_breakdownLow = 0.04f, .m_breakdownHigh = 0.16f, .m_forms = FoT{ 0, 0, 1, 0, 0 }, .m_introBars = 32, .m_outroBars = 32, .m_introLong = 0.5f,
      .m_ambientIntro = 0.2f, .m_ambientIntroBars = 16, .m_ambientOutro = 0.2f, .m_gapLu = 2.5f,
      .m_minor = 0.7f, .m_scales = ScT{ 0.4f, 0.2f, 0.0f, 0.1f, 0.3f, 0.0f }, .m_progressions = PrT{ 0.2f, 0.6f, 0.0f, 0.1f, 0.1f },
      .m_barsPerChord = 16, .m_barsPerChordBreak = 16, .m_keyChange = 0.0f,
      .m_bass = BaT{ 0.3f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f }, .m_hats16 = 0.7f, .m_ride = 0.5f, .m_perc = 0.6f, .m_shaker = 0.3f,
      .m_lead = LeadKindE::ACID, .m_pluck = 0.1f, .m_arp = 0.3f, .m_stab = 0.5f, .m_counter = 0.0f, .m_gate = 0.2f, .m_orchestra = 0.0f,
      .m_hallBreakS = 1.5f, .m_hallDropS = 0.8f, .m_bassDuckDb = 4.5f, .m_padDuckDb = 3.0f, .m_targetLufs = -8.5f, .m_mix = MxT{ 0, 0, 0, 1, 0 } },
    { .m_name = "Deep", .m_bpmLow = 128.0f, .m_bpmHigh = 134.0f, .m_minutesLow = 6.5f, .m_minutesHigh = 10.0f,
      .m_breakdownLow = 0.1f, .m_breakdownHigh = 0.32f, .m_forms = FoT{ 0, 0, 0, 0, 1 }, .m_introBars = 16, .m_outroBars = 16, .m_introLong = 0.5f,
      .m_ambientIntro = 0.5f, .m_ambientIntroBars = 16, .m_ambientOutro = 0.8f, .m_gapLu = 4.0f, .m_introSynthDb = -6.0f,
      .m_minor = 0.75f, .m_scales = ScT{ 0.5f, 0.35f, 0.0f, 0.15f, 0.0f, 0.0f }, .m_progressions = PrT{ 0.3f, 0.3f, 0.0f, 0.4f, 0.0f },
      .m_barsPerChord = 2, .m_barsPerChordBreak = 4, .m_keyChange = 0.0f,
      .m_bass = BaT{ 0.2f, 0.5f, 0.0f, 0.2f, 0.0f, 0.1f }, .m_hats16 = 0.2f, .m_ride = 0.1f, .m_perc = 0.6f, .m_shaker = 0.8f, .m_kickSoft = 0.8f,
      .m_beatless = 0.1f, .m_lead = LeadKindE::PAD, .m_pluck = 0.5f, .m_arp = 0.8f, .m_stab = 0.1f, .m_counter = 0.2f, .m_gate = 0.4f, .m_orchestra = 0.15f, .m_hatsDb = 3.0f, .m_tiltDb = 1.0f,
      .m_hallBreakS = 9.0f, .m_hallDropS = 3.0f, .m_bassDuckDb = 3.0f, .m_padDuckDb = 2.0f, .m_targetLufs = -9.0f, .m_mix = MxT{ 0, 0, 0, 0, 1 } },
};

/**
 * @brief @p a to @p b at @p t.
 * @param[in] a  the start
 * @param[in] b  the end
 * @param[in] t  the position, 0..1
 * @return the value between
 */
float lerp(float a, float b, float t) { return a + (b - a) * t; }

template <size_t N>
/**
 * @brief @p a to @p b at @p t, element by element.
 * @param[in] a  the values at 0
 * @param[in] b  the values at 1
 * @param[in] t  the place between, 0..1
 * @return the values between
 */
std::array<float, N> mixArrays(const std::array<float, N>& a, const std::array<float, N>& b, float t)
{
    std::array<float, N> o{};
    for (size_t i = 0; i < N; ++i) o[i] = lerp(a[i], b[i], t);
    return o;
}

} // namespace

const StyleProfile& styleProfile(StyleE s)
{
    const int i = std::clamp(static_cast<int>(s), 0, static_cast<int>(StyleE::COUNT) - 1);
    return Profiles[i];
}

StyleProfile morphProfile(const StyleProfile& a, const StyleProfile& b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const StyleProfile& near = t < 0.5f ? a : b;
    StyleProfile o = near;
    o.m_bpmLow = lerp(a.m_bpmLow, b.m_bpmLow, t);
    o.m_bpmHigh = lerp(a.m_bpmHigh, b.m_bpmHigh, t);
    o.m_minutesLow = lerp(a.m_minutesLow, b.m_minutesLow, t);
    o.m_minutesHigh = lerp(a.m_minutesHigh, b.m_minutesHigh, t);
    o.m_breakdownLow = lerp(a.m_breakdownLow, b.m_breakdownLow, t);
    o.m_breakdownHigh = lerp(a.m_breakdownHigh, b.m_breakdownHigh, t);
    o.m_forms = mixArrays(a.m_forms, b.m_forms, t);
    o.m_introLong = lerp(a.m_introLong, b.m_introLong, t);
    o.m_ambientIntro = lerp(a.m_ambientIntro, b.m_ambientIntro, t);
    o.m_ambientIntroBars = static_cast<int>(std::lround(lerp(static_cast<float>(a.m_ambientIntroBars), static_cast<float>(b.m_ambientIntroBars), t) / 8.0f)) * 8;
    o.m_ambientOutro = lerp(a.m_ambientOutro, b.m_ambientOutro, t);
    o.m_gapLu = lerp(a.m_gapLu, b.m_gapLu, t);
    o.m_introSynthDb = lerp(a.m_introSynthDb, b.m_introSynthDb, t);
    o.m_minor = lerp(a.m_minor, b.m_minor, t);
    o.m_scales = mixArrays(a.m_scales, b.m_scales, t);
    o.m_progressions = mixArrays(a.m_progressions, b.m_progressions, t);
    o.m_keyChange = lerp(a.m_keyChange, b.m_keyChange, t);
    o.m_bass = mixArrays(a.m_bass, b.m_bass, t);
    o.m_hats16 = lerp(a.m_hats16, b.m_hats16, t);
    o.m_ride = lerp(a.m_ride, b.m_ride, t);
    o.m_perc = lerp(a.m_perc, b.m_perc, t);
    o.m_shaker = lerp(a.m_shaker, b.m_shaker, t);
    o.m_kickSoft = lerp(a.m_kickSoft, b.m_kickSoft, t);
    o.m_beatless = lerp(a.m_beatless, b.m_beatless, t);
    o.m_pluck = lerp(a.m_pluck, b.m_pluck, t);
    o.m_arp = lerp(a.m_arp, b.m_arp, t);
    o.m_stab = lerp(a.m_stab, b.m_stab, t);
    o.m_counter = lerp(a.m_counter, b.m_counter, t);
    o.m_gate = lerp(a.m_gate, b.m_gate, t);
    o.m_orchestra = lerp(a.m_orchestra, b.m_orchestra, t);
    o.m_hatsDb = lerp(a.m_hatsDb, b.m_hatsDb, t);
    o.m_tiltDb = lerp(a.m_tiltDb, b.m_tiltDb, t);
    for (size_t i = 0; i < o.m_mix.size(); ++i) o.m_mix[i] = lerp(a.m_mix[i], b.m_mix[i], t);
    o.m_hallBreakS = std::exp(lerp(std::log(a.m_hallBreakS), std::log(b.m_hallBreakS), t));
    o.m_hallDropS = std::exp(lerp(std::log(a.m_hallDropS), std::log(b.m_hallDropS), t));
    o.m_bassDuckDb = lerp(a.m_bassDuckDb, b.m_bassDuckDb, t);
    o.m_padDuckDb = lerp(a.m_padDuckDb, b.m_padDuckDb, t);
    o.m_targetLufs = lerp(a.m_targetLufs, b.m_targetLufs, t);
    return o;
}

StyleProfile profileOf(const ParamStore& p)
{
    const int style = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::STYLE));
    const StyleProfile& base = styleProfile(static_cast<StyleE>(style));
    const int to = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::MORPH_TO));
    if (to <= 0) return base;
    return morphProfile(base, styleProfile(static_cast<StyleE>(to - 1)), p.get(p.id(ModuleE::COMPOSE, 0, compose::MORPH)));
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
