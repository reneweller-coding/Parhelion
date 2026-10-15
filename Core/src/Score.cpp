/**
 * @file Score.cpp
 * @brief Automation curves and score housekeeping.
 * @note The curves are copied from Ephemeris `Core/src/Score.cpp` at d047d79 (27.09.2026).
 * @note Copied from Totality `Core/src/Score.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#include "parh/Score.h"
#include <algorithm>
#include <limits>

namespace parh {

const char* const PartNames[NumParts] = {
    "kick", "sub", "perc1", "perc2", "perc3", "perc4", "perc5", "perc6", "perc7", "perc8", "perc9", "perc10", "perc11",
    "perc12", "bass", "acid", "lead", "counter", "pluck", "arp", "pad", "stab", "piano", "strings", "choir", "brass", "timpani", "ghost", "fx",
};

const char* const SectionNames[] = { "Intro", "Groove", "Break", "Breakdown", "Build", "Drop", "Outro" };
static_assert(sizeof(SectionNames) / sizeof(SectionNames[0]) == static_cast<size_t>(SectionKindE::COUNT), "one name per section kind");

const char* const LayerNames[NumLayers] = { "kick", "sub", "bass", "acid", "closed hat", "open hat", "clap", "ride", "perc",
                                              "crash", "pluck", "arp", "pad", "lead", "counter", "stab", "fx" };

double gestureShape(GestureShapeE shape, double t)
{
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    switch (shape) {
    case GestureShapeE::MINIMUM_JERK: return t * t * t * (10.0 + t * (-15.0 + 6.0 * t));
    case GestureShapeE::LINEAR:      return t;
    case GestureShapeE::EASE_IN:      return t * t;
    case GestureShapeE::EASE_OUT:     return 1.0 - (1.0 - t) * (1.0 - t);
    case GestureShapeE::STEP:        return 1.0;
    }
    return t;
}

float gestureValue(const Gesture& g, double beat)
{
    if (beat < g.m_beat) return g.m_from;
    if (g.m_length <= 0.0 || g.m_shape == GestureShapeE::STEP || beat >= g.m_beat + g.m_length) return g.m_to;
    const double p = gestureShape(g.m_shape, (beat - g.m_beat) / g.m_length);
    return static_cast<float>(static_cast<double>(g.m_from) + p * (static_cast<double>(g.m_to) - static_cast<double>(g.m_from)));
}

void Score::sort()
{
    std::stable_sort(m_notes.begin(), m_notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.m_beat < b.m_beat; });
    std::stable_sort(m_gestures.begin(), m_gestures.end(), [](const Gesture& a, const Gesture& b) { return a.m_beat < b.m_beat; });
    std::stable_sort(m_sections.begin(), m_sections.end(), [](const Section& a, const Section& b) { return a.m_beat < b.m_beat; });
    std::stable_sort(m_layers.begin(), m_layers.end(), [](const LayerBlock& a, const LayerBlock& b) { return a.m_beat < b.m_beat; });
    std::stable_sort(m_markers.begin(), m_markers.end(), [](const Marker& a, const Marker& b) { return a.m_beat < b.m_beat; });
    std::stable_sort(m_levels.begin(), m_levels.end(), [](const LevelMark& a, const LevelMark& b) { return a.m_beat < b.m_beat; });
    std::stable_sort(m_knobs.begin(), m_knobs.end(), [](const KnobSet& a, const KnobSet& b) { return a.m_beat < b.m_beat; });
    std::stable_sort(m_sounds.begin(), m_sounds.end(), [](const SoundPick& a, const SoundPick& b) { return a.m_beat < b.m_beat; });
}

float Score::knobAt(int param, double beat) const
{
    float v = std::numeric_limits<float>::quiet_NaN();
    for (const KnobSet& k : m_knobs) {
        if (k.m_beat > beat + 1e-9) break;
        if (k.m_param == param) v = k.m_value;
    }
    return v;
}

const char* const BalPartNames[BalParts] = { "perc1", "perc2", "perc3", "perc4", "perc5", "perc6", "perc7", "perc8", "perc9",
                                               "perc10", "perc11", "perc12", "bass", "acid", "lead", "counter", "pluck", "arp",
                                               "pad", "stab", "piano", "strings", "choir", "brass", "timpani", "room", "fx" };

BalanceDbT Score::balanceAt(double beat) const
{
    BalanceDbT b{};
    for (const LevelMark& m : m_levels) {
        if (m.m_beat > beat) break;
        b = m.m_balDb;
    }
    return b;
}

float Score::trimAt(double beat) const
{
    float t = 0.0f;
    for (const LevelMark& m : m_levels) {
        if (m.m_beat > beat) break;
        t = m.m_trimDb;
    }
    return t;
}

void Score::clear(double bpm)
{
    m_tempo.setConstant(bpm);
    m_lengthBeats = 0.0;
    m_notes.clear();
    m_gestures.clear();
    m_sections.clear();
    m_layers.clear();
    m_markers.clear();
    m_levels.clear();
    m_knobs.clear();
    m_sounds.clear();
}

float Score::gestureOffset(int param, double beat) const
{
    const Gesture* latest = nullptr;
    for (const Gesture& g : m_gestures) {
        if (g.m_param != param || g.m_beat > beat) continue;
        if (latest == nullptr || g.m_beat >= latest->m_beat) latest = &g;
    }
    return latest == nullptr ? 0.0f : gestureValue(*latest, beat);
}

} // namespace parh
