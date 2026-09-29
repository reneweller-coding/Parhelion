/**
 * @file Midi.cpp
 * @brief Standard MIDI File writer.
 * @note The encoder is copied from Ephemeris `Core/src/Midi.cpp` at d047d79 (27.09.2026).
 * @note Copied from Totality `Core/src/Midi.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#include "parh/Midi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace parh {

namespace {

struct Ev {
    int64_t tick;
    int order;   ///< at equal ticks: meta first, note-offs before note-ons
    std::vector<uint8_t> bytes;
};

int64_t toTick(double beat) { return static_cast<int64_t>(std::llround(beat * kMidiPpq)); }

void putVarLen(std::vector<uint8_t>& out, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = static_cast<uint8_t>(v & 0x7F);
    while ((v >>= 7) != 0) buf[n++] = static_cast<uint8_t>(0x80 | (v & 0x7F));
    while (n > 0) out.push_back(buf[--n]);
}

void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

Ev meta(int64_t tick, uint8_t type, const std::vector<uint8_t>& data)
{
    Ev e{ tick, 0, { 0xFF, type } };
    putVarLen(e.bytes, static_cast<uint32_t>(data.size()));
    e.bytes.insert(e.bytes.end(), data.begin(), data.end());
    return e;
}

Ev metaText(int64_t tick, uint8_t type, const std::string& s)
{
    return meta(tick, type, std::vector<uint8_t>(s.begin(), s.end()));
}

Ev tempoEvent(int64_t tick, double bpm)
{
    const uint32_t us = static_cast<uint32_t>(std::llround(60.0e6 / bpm));
    return meta(tick, 0x51, { static_cast<uint8_t>(us >> 16), static_cast<uint8_t>(us >> 8), static_cast<uint8_t>(us) });
}

/** @brief Sharps (+) or flats (-) of the minor key on @p root (A minor = 0). */
int minorKeySharps(int root)
{
    static const int kMajorSharps[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
    return kMajorSharps[((root % 12) + 12 + 3) % 12];
}

void appendTrack(std::vector<uint8_t>& file, std::vector<Ev>& evs)
{
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    std::vector<uint8_t> body;
    int64_t last = 0;
    for (const Ev& e : evs) {
        const int64_t t = std::max<int64_t>(e.tick, last);   // an event before zero (a rim 4 ms early) sits on zero
        putVarLen(body, static_cast<uint32_t>(t - last));
        body.insert(body.end(), e.bytes.begin(), e.bytes.end());
        last = t;
    }
    body.insert(body.end(), { 0x00, 0xFF, 0x2F, 0x00 });
    file.insert(file.end(), { 'M', 'T', 'r', 'k' });
    put32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
}

} // namespace

int midiChannelOf(Part part)
{
    if (part == Part::Sub) return 0;
    if (part == Part::Bass) return 1;
    if (part == Part::Acid) return 2;
    if (const int p = polyOf(part); p >= 0) return 3 + p;   // lead 3, counter 4, pluck 5, arp 6, pad 7, stab 8
    if (part == Part::Ghost) return 15;                     // the ghost kick: its own channel, for a host's sidechain
    return 9;   // the kick and the kit's lanes: the drum channel
}

std::vector<uint8_t> encodeMidi(const Score& score, const char* title, const ParamStore* params)
{
    // The automation as controllers on the controls track, one number per automated knob.
    std::vector<Ev> controls;
    if (params != nullptr) {
        std::map<int, int> ccOf;
        for (const Gesture& g : score.gestures)
            if (g.param >= 0 && g.param < params->count()) ccOf.emplace(g.param, 0);
        int next = 20;
        for (auto& kv : ccOf) kv.second = next <= 119 ? next++ : -1;
        std::map<int, int> last;
        for (const Gesture& g : score.gestures) {
            const auto it = ccOf.find(g.param);
            if (it == ccOf.end() || it->second < 0) continue;
            // From the track's own value where it sets the knob (Score::knobs), else from the knob.
            const float set = score.knobAt(g.param, g.beat);
            const float knob = params->toNormalised(g.param, set == set ? set : params->get(g.param));
            const double end = g.beat + std::max(0.0, g.length);
            for (double b = g.beat; ; b += 0.125) {
                const double at = std::min(b, end);
                const float v = std::clamp(knob + gestureValue(g, at + (g.length <= 0.0 ? 1.0 : 0.0)), 0.0f, 1.0f);
                const int value = static_cast<int>(std::lround(v * 127.0f));
                const auto l = last.find(g.param);
                if (l == last.end() || l->second != value) {
                    controls.push_back(Ev{ toTick(at), 1, { 0xBF, static_cast<uint8_t>(it->second), static_cast<uint8_t>(value) } });
                    last[g.param] = value;
                }
                if (at >= end) break;
            }
        }
        for (const auto& kv : ccOf) {
            if (kv.second < 0) continue;
            controls.push_back(metaText(0, 0x01, "CC " + std::to_string(kv.second) + " = " + params->key(kv.first)));
        }
    }
    bool used[kNumParts] = {};
    for (const NoteEvent& n : score.notes) used[static_cast<int>(n.part)] = true;
    int tracks = 1 + (controls.empty() ? 0 : 1);
    for (bool u : used) tracks += u ? 1 : 0;

    std::vector<uint8_t> file = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1 };
    file.push_back(static_cast<uint8_t>(tracks >> 8));
    file.push_back(static_cast<uint8_t>(tracks & 0xFF));
    file.push_back(static_cast<uint8_t>(kMidiPpq >> 8));
    file.push_back(static_cast<uint8_t>(kMidiPpq & 0xFF));

    std::vector<Ev> cond;
    cond.push_back(metaText(0, 0x03, title != nullptr ? title : "Parhelion"));
    cond.push_back(meta(0, 0x58, { 4, 2, 24, 8 }));
    cond.push_back(meta(0, 0x59, { static_cast<uint8_t>(static_cast<int8_t>(minorKeySharps(score.keyRoot))), 1 }));
    const std::vector<TempoPoint>& pts = score.tempo.points();
    const double end = std::max(score.lengthBeats, pts.back().beat);
    for (size_t i = 0; i < pts.size(); ++i) {
        const TempoPoint& p = pts[i];
        if (p.rampToNext && i + 1 < pts.size()) {
            const double b1 = pts[i + 1].beat;
            for (double b = p.beat; b < b1; b += 1.0) {
                const double bn = std::min(b + 1.0, b1);
                const double sec = score.tempo.secondsAt(bn) - score.tempo.secondsAt(b);
                cond.push_back(tempoEvent(toTick(b), 60.0 * (bn - b) / sec));
            }
        } else {
            cond.push_back(tempoEvent(toTick(p.beat), p.bpm));
        }
    }
    for (const Marker& m : score.markers) cond.push_back(metaText(toTick(m.beat), 0x06, m.text));
    for (const Section& s : score.sections)
        cond.push_back(metaText(toTick(s.beat), 0x01, std::string("section: ") + kSectionNames[static_cast<int>(s.kind)]));
    cond.push_back(meta(toTick(end), 0x01, {}));
    appendTrack(file, cond);

    for (int pi = 0; pi < kNumParts; ++pi) {
        if (!used[pi]) continue;
        const Part part = static_cast<Part>(pi);
        const uint8_t ch = static_cast<uint8_t>(midiChannelOf(part));
        std::vector<Ev> evs;
        evs.push_back(metaText(0, 0x03, kPartNames[pi]));
        for (const NoteEvent& n : score.notes) {
            if (n.part != part) continue;
            const int pitch = std::clamp(n.pitch, 0, 127);
            const int vel = std::clamp(static_cast<int>(std::lround(n.velocity * 127.0f)), 1, 127);
            const int64_t on = std::max<int64_t>(0, toTick(n.beat));
            const int64_t off = std::max(on + 1, toTick(n.beat + n.length));
            evs.push_back(Ev{ on, 2, { static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(pitch), static_cast<uint8_t>(vel) } });
            evs.push_back(Ev{ off, 1, { static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(pitch), 0 } });
        }
        appendTrack(file, evs);
    }
    if (!controls.empty()) {
        controls.push_back(metaText(0, 0x03, "controls"));
        appendTrack(file, controls);
    }
    return file;
}

bool writeMidiFile(const Score& score, const char* path, const char* title, const ParamStore* params)
{
    const std::vector<uint8_t> bytes = encodeMidi(score, title, params);
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace parh
