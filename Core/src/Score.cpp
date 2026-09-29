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

const char* const kPartNames[kNumParts] = {
    "kick", "sub", "perc1", "perc2", "perc3", "perc4", "perc5", "perc6", "perc7", "perc8", "perc9", "perc10", "perc11",
    "perc12", "bass", "acid", "lead", "counter", "pluck", "arp", "pad", "stab", "piano", "ghost", "fx",
};

const char* const kSectionNames[] = { "Intro", "Groove", "Break", "Breakdown", "Build", "Drop", "Outro" };
static_assert(sizeof(kSectionNames) / sizeof(kSectionNames[0]) == static_cast<size_t>(SectionKind::Count), "one name per section kind");

const char* const kLayerNames[kNumLayers] = { "kick", "sub", "bass", "acid", "closed hat", "open hat", "clap", "ride", "perc",
                                              "crash", "pluck", "arp", "pad", "lead", "counter", "stab", "fx" };

double gestureShape(GestureShape shape, double t)
{
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    switch (shape) {
    case GestureShape::MinimumJerk: return t * t * t * (10.0 + t * (-15.0 + 6.0 * t));
    case GestureShape::Linear:      return t;
    case GestureShape::EaseIn:      return t * t;
    case GestureShape::EaseOut:     return 1.0 - (1.0 - t) * (1.0 - t);
    case GestureShape::Step:        return 1.0;
    }
    return t;
}

float gestureValue(const Gesture& g, double beat)
{
    if (beat < g.beat) return g.from;
    if (g.length <= 0.0 || g.shape == GestureShape::Step || beat >= g.beat + g.length) return g.to;
    const double p = gestureShape(g.shape, (beat - g.beat) / g.length);
    return static_cast<float>(static_cast<double>(g.from) + p * (static_cast<double>(g.to) - static_cast<double>(g.from)));
}

void Score::sort()
{
    std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.beat < b.beat; });
    std::stable_sort(gestures.begin(), gestures.end(), [](const Gesture& a, const Gesture& b) { return a.beat < b.beat; });
    std::stable_sort(sections.begin(), sections.end(), [](const Section& a, const Section& b) { return a.beat < b.beat; });
    std::stable_sort(layers.begin(), layers.end(), [](const LayerBlock& a, const LayerBlock& b) { return a.beat < b.beat; });
    std::stable_sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.beat < b.beat; });
    std::stable_sort(levels.begin(), levels.end(), [](const LevelMark& a, const LevelMark& b) { return a.beat < b.beat; });
    std::stable_sort(knobs.begin(), knobs.end(), [](const KnobSet& a, const KnobSet& b) { return a.beat < b.beat; });
    std::stable_sort(sounds.begin(), sounds.end(), [](const SoundPick& a, const SoundPick& b) { return a.beat < b.beat; });
}

float Score::knobAt(int param, double beat) const
{
    float v = std::numeric_limits<float>::quiet_NaN();
    for (const KnobSet& k : knobs) {
        if (k.beat > beat + 1e-9) break;
        if (k.param == param) v = k.value;
    }
    return v;
}

const char* const kBalPartNames[kBalParts] = { "perc1", "perc2", "perc3", "perc4", "perc5", "perc6", "perc7", "perc8", "perc9",
                                               "perc10", "perc11", "perc12", "bass", "acid", "lead", "counter", "pluck", "arp",
                                               "pad", "stab", "piano", "room", "fx" };

BalanceDb Score::balanceAt(double beat) const
{
    BalanceDb b{};
    for (const LevelMark& m : levels) {
        if (m.beat > beat) break;
        b = m.balDb;
    }
    return b;
}

float Score::trimAt(double beat) const
{
    float t = 0.0f;
    for (const LevelMark& m : levels) {
        if (m.beat > beat) break;
        t = m.trimDb;
    }
    return t;
}

void Score::clear(double bpm)
{
    tempo.setConstant(bpm);
    lengthBeats = 0.0;
    notes.clear();
    gestures.clear();
    sections.clear();
    layers.clear();
    markers.clear();
    levels.clear();
    knobs.clear();
    sounds.clear();
}

float Score::gestureOffset(int param, double beat) const
{
    const Gesture* latest = nullptr;
    for (const Gesture& g : gestures) {
        if (g.param != param || g.beat > beat) continue;
        if (latest == nullptr || g.beat >= latest->beat) latest = &g;
    }
    return latest == nullptr ? 0.0f : gestureValue(*latest, beat);
}

} // namespace parh
