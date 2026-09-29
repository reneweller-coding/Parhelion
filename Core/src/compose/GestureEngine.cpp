/**
 * @file GestureEngine.cpp
 * @brief The player's hands.
 * @note Copied from Ephemeris `Core/src/compose/GestureEngine.cpp` (namespace eph) at d047d79 (27.09.2026); the grid added.
 * @note Copied from Totality `Core/src/compose/GestureEngine.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#include "parh/compose/GestureEngine.h"
#include <algorithm>
#include <cmath>

namespace parh {

void playHands(Score& score, const std::vector<HandKnob>& knobs, const HandStyle& style,
               const std::function<float(double)>& energy, double start, double end, Rng& rng)
{
    if (knobs.empty() || end <= start) return;
    const double grid = style.gridBeats;
    // On the grid: the next line at or after t, and a length rounded to whole cells (at least one).
    const auto snap = [&](double t) { return grid > 0.0 ? start + std::ceil((t - start) / grid - 1e-9) * grid : t; };
    const auto cells = [&](double len) { return grid > 0.0 ? std::max(grid, std::round(len / grid) * grid) : len; };
    std::vector<float> value(knobs.size(), 0.0f);        // where each knob was left
    std::vector<double> busyUntil(knobs.size(), start);  // beat until which a hand holds it
    // Each knob starts at its resting centre, set once and silently (a step at the start).
    for (size_t k = 0; k < knobs.size(); ++k) {
        value[k] = std::clamp(knobs[k].atRest, knobs[k].low, knobs[k].high);
        score.gestures.push_back({ knobs[k].param, start, 0.0, value[k], value[k], GestureShape::Step, 0 });
    }
    double freeAt[2] = { snap(start + grid), snap(start + 2.0 * grid) };   // the second hand joins a little later
    const auto beatsOf = [&](double beat, double seconds) { return seconds * score.tempo.bpmAt(beat) / 60.0; };

    for (int guard = 0; guard < 100000; ++guard) {
        const int h = freeAt[0] <= freeAt[1] ? 0 : 1;
        const double t = freeAt[h];
        if (t >= end) break;
        // Choose a knob nobody holds and that may be touched now.
        float total = 0.0f;
        const auto free = [&](size_t k) { return busyUntil[k] <= t && knobs[k].from <= t && knobs[k].until > t; };
        for (size_t k = 0; k < knobs.size(); ++k) if (free(k)) total += knobs[k].weight;
        if (total <= 0.0f) { freeAt[h] = snap(t + std::max(grid, 4.0)); continue; }
        float u = rng.uniform() * total;
        size_t pick = 0;
        for (size_t k = 0; k < knobs.size(); ++k) {
            if (!free(k)) continue;
            pick = k;
            u -= knobs[k].weight;
            if (u <= 0.0f) break;
        }
        const HandKnob& knob = knobs[pick];

        // How long, and which shape.
        double seconds;
        GestureShape shape = GestureShape::MinimumJerk;
        const float kind = rng.uniform();
        if (knob.throws || kind < style.quickChance) {
            seconds = 1.0 + rng.uniform();
            shape = GestureShape::EaseOut;
        } else if (kind < style.quickChance + style.longChance) {
            seconds = style.medianSeconds * (2.0 + 2.0 * rng.uniform());
        } else {
            seconds = style.medianSeconds * std::exp(style.spread * rng.gaussian());
            if (rng.uniform() < 0.2f) shape = GestureShape::EaseIn;
        }
        double length = knob.throws ? beatsOf(t, seconds) : cells(beatsOf(t, seconds));
        length = std::min(length, std::min(end, knob.until) - t);
        if (length < 0.25) { freeAt[h] = snap(t + std::max(grid, 4.0)); continue; }

        // Where to: the energy's centre with scatter; a throw goes up and is caught back after it.
        const float e = std::clamp(energy(t + length), 0.0f, 1.0f);
        const float centre = knob.atRest + e * (knob.atPeak - knob.atRest);
        float target = centre + knob.scatter * static_cast<float>(rng.gaussian());
        if (knob.throws) target = std::max(value[pick], centre) + knob.scatter * (1.0f + rng.uniform());
        target = std::clamp(target, knob.low, knob.high);
        score.gestures.push_back({ knob.param, t, length, value[pick], target, shape, static_cast<uint8_t>(h) });
        value[pick] = target;
        double done = t + length;
        if (knob.throws) {
            // Caught: back to the centre over two to four seconds, by the same hand.
            const double back = std::min(beatsOf(done, 2.0 + 2.0 * rng.uniform()), end - done);
            if (back > 0.25) {
                const float home = std::clamp(centre, knob.low, knob.high);
                score.gestures.push_back({ knob.param, done, back, target, home, GestureShape::MinimumJerk, static_cast<uint8_t>(h) });
                value[pick] = home;
                done += back;
            }
        }
        busyUntil[pick] = done;
        freeAt[h] = snap(done + beatsOf(done, style.restSeconds * std::exp(0.6 * rng.gaussian())));
    }
}

} // namespace parh
