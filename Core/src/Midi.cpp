/**
 * @file Midi.cpp
 * @brief Standard MIDI File writer.
 * @note The encoder is copied from Ephemeris `Core/src/Midi.cpp` at d047d79 (27.09.2026).
 * @note Copied from Totality `Core/src/Midi.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 * @author Rene Weller
 */
#include "parh/Midi.h"
#include "parh/Presets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace parh {

namespace {

/** @brief One event of a track chunk: its tick, its order at equal ticks, its bytes. */
struct Ev {
    int64_t m_tick;   ///< when, in ticks
    int m_order;   ///< at equal ticks: meta first, note-offs before note-ons
    std::vector<uint8_t> m_bytes;   ///< the status and data bytes
};

/**
 * @brief Beat @p beat in ticks (MidiPpq per beat).
 * @param[in] beat  the position in beats
 * @return the position in ticks
 */
int64_t toTick(double beat) { return static_cast<int64_t>(std::llround(beat * MidiPpq)); }

/**
 * @brief Appends @p v as a variable-length quantity.
 * @param[in,out] out  the bytes, appended to
 * @param[in]     v    the value
 */
void putVarLen(std::vector<uint8_t>& out, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = static_cast<uint8_t>(v & 0x7F);
    while ((v >>= 7) != 0) buf[n++] = static_cast<uint8_t>(0x80 | (v & 0x7F));
    while (n > 0) out.push_back(buf[--n]);
}

/**
 * @brief Appends @p v big-endian.
 * @param[in,out] out  the bytes, appended to
 * @param[in]     v    the value
 */
void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

/**
 * @brief A meta event of @p type with @p data at @p tick.
 * @param[in] tick  the position in ticks
 * @param[in] type  the meta event's type
 * @param[in] data  its data
 * @return the event
 */
Ev meta(int64_t tick, uint8_t type, const std::vector<uint8_t>& data)
{
    Ev e{ tick, 0, { 0xFF, type } };
    putVarLen(e.m_bytes, static_cast<uint32_t>(data.size()));
    e.m_bytes.insert(e.m_bytes.end(), data.begin(), data.end());
    return e;
}

/**
 * @brief A text meta event of @p type (a marker, a track name) at @p tick.
 * @param[in] tick  the position in ticks
 * @param[in] type  the meta event's type
 * @param[in] s     its text
 * @return the event
 */
Ev metaText(int64_t tick, uint8_t type, const std::string& s)
{
    return meta(tick, type, std::vector<uint8_t>(s.begin(), s.end()));
}

/**
 * @brief A tempo meta event of @p bpm at @p tick.
 * @param[in] tick  the position in ticks
 * @param[in] bpm   the tempo
 * @return the event
 */
Ev tempoEvent(int64_t tick, double bpm)
{
    const uint32_t us = static_cast<uint32_t>(std::llround(60.0e6 / bpm));
    return meta(tick, 0x51, { static_cast<uint8_t>(us >> 16), static_cast<uint8_t>(us >> 8), static_cast<uint8_t>(us) });
}

/**
 * @brief Sharps (+) or flats (-) of the minor key on @p root (A minor = 0).
 * @param[in] root  the key's root, 0 .. 11
 * @return its sharps (positive) or flats (negative)
 */
int minorKeySharps(int root)
{
    static const int MajorSharps[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
    return MajorSharps[((root % 12) + 12 + 3) % 12];
}

/**
 * @brief Sorts @p evs and appends them to @p file as a track chunk (delta times, end of track).
 * @param[in,out] file  the file's bytes, appended to
 * @param[in,out] evs   the track's events, sorted here
 */
void appendTrack(std::vector<uint8_t>& file, std::vector<Ev>& evs)
{
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
        return a.m_tick != b.m_tick ? a.m_tick < b.m_tick : a.m_order < b.m_order;
    });
    std::vector<uint8_t> body;
    int64_t last = 0;
    for (const Ev& e : evs) {
        const int64_t t = std::max<int64_t>(e.m_tick, last);   // an event before zero (a rim 4 ms early) sits on zero
        putVarLen(body, static_cast<uint32_t>(t - last));
        body.insert(body.end(), e.m_bytes.begin(), e.m_bytes.end());
        last = t;
    }
    body.insert(body.end(), { 0x00, 0xFF, 0x2F, 0x00 });
    file.insert(file.end(), { 'M', 'T', 'r', 'k' });
    put32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
}

/**
 * @brief The part a chosen sound plays on (Part::Count: none, the cloud has no notes of its own).
 * @param[in] s  the sound chosen
 * @return its part, Count for none
 */
PartE partOfSound(const SoundPick& s)
{
    switch (static_cast<ModuleE>(s.m_module)) {
    case ModuleE::KICK: return PartE::KICK;
    case ModuleE::SUB: return PartE::SUB;
    case ModuleE::BASS: return PartE::BASS;
    case ModuleE::ACID: return PartE::ACID;
    case ModuleE::POLY: return static_cast<PartE>(static_cast<int>(PartE::LEAD) + s.m_instance);
    case ModuleE::PERC: return static_cast<PartE>(static_cast<int>(PartE::PERC1) + s.m_instance);
    case ModuleE::PIANO: return PartE::PIANO;
    case ModuleE::STRINGS: return PartE::STRINGS;
    case ModuleE::CHOIR: return PartE::CHOIR;
    case ModuleE::BRASS: return PartE::BRASS;
    case ModuleE::TIMPANI: return PartE::TIMPANI;
    case ModuleE::SFX: return PartE::FX;
    default: return PartE::COUNT;
    }
}

} // namespace

int midiChannelOf(PartE part)
{
    if (part == PartE::SUB) return 0;
    if (part == PartE::BASS) return 1;
    if (part == PartE::ACID) return 2;
    if (const int p = polyOf(part); p >= 0) return 3 + p;   // lead 3, counter 4, pluck 5, arp 6, pad 7, stab 8
    if (part == PartE::PIANO) return 10;                     // the piano
    if (part == PartE::STRINGS) return 11;                   // the orchestra: strings, choir, brass
    if (part == PartE::CHOIR) return 12;
    if (part == PartE::BRASS) return 14;
    if (part == PartE::TIMPANI) return 13;                   // the timpani share the effects' channel (tracks of their own)
    if (part == PartE::GHOST) return 15;                     // the ghost kick: its own channel, for a host's sidechain
    if (part == PartE::FX) return 13;                        // the effects
    return 9;   // the kick and the kit's lanes: the drum channel
}

std::vector<uint8_t> encodeMidi(const Score& score, const char* title, const ParamStore* params)
{
    // The automation as controllers on the controls track, one number per automated knob.
    std::vector<Ev> controls;
    if (params != nullptr) {
        std::map<int, int> ccOf;
        for (const Gesture& g : score.m_gestures)
            if (g.m_param >= 0 && g.m_param < params->count()) ccOf.emplace(g.m_param, 0);
        int next = 20;
        for (auto& kv : ccOf) kv.second = next <= 119 ? next++ : -1;
        std::map<int, int> last;
        for (const Gesture& g : score.m_gestures) {
            const auto it = ccOf.find(g.m_param);
            if (it == ccOf.end() || it->second < 0) continue;
            // From the track's own value where it sets the knob (Score::knobs), else from the knob.
            const float set = score.knobAt(g.m_param, g.m_beat);
            const float knob = params->toNormalised(g.m_param, set == set ? set : params->get(g.m_param));
            const double end = g.m_beat + std::max(0.0, g.m_length);
            for (double b = g.m_beat; ; b += 0.125) {
                const double at = std::min(b, end);
                const float v = std::clamp(knob + gestureValue(g, at + (g.m_length <= 0.0 ? 1.0 : 0.0)), 0.0f, 1.0f);
                const int value = static_cast<int>(std::lround(v * 127.0f));
                const auto l = last.find(g.m_param);
                if (l == last.end() || l->second != value) {
                    controls.push_back(Ev{ toTick(at), 1, { 0xBF, static_cast<uint8_t>(it->second), static_cast<uint8_t>(value) } });
                    last[g.m_param] = value;
                }
                if (at >= end) break;
            }
        }
        for (const auto& kv : ccOf) {
            if (kv.second < 0) continue;
            controls.push_back(metaText(0, 0x01, "CC " + std::to_string(kv.second) + " = " + params->key(kv.first)));
        }
    }
    bool used[NumParts] = {};
    for (const NoteEvent& n : score.m_notes) used[static_cast<int>(n.m_part)] = true;
    int tracks = 1 + (controls.empty() ? 0 : 1);
    for (bool u : used) tracks += u ? 1 : 0;

    std::vector<uint8_t> file = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1 };
    file.push_back(static_cast<uint8_t>(tracks >> 8));
    file.push_back(static_cast<uint8_t>(tracks & 0xFF));
    file.push_back(static_cast<uint8_t>(MidiPpq >> 8));
    file.push_back(static_cast<uint8_t>(MidiPpq & 0xFF));

    std::vector<Ev> cond;
    cond.push_back(metaText(0, 0x03, title != nullptr ? title : "Parhelion"));
    cond.push_back(meta(0, 0x58, { 4, 2, 24, 8 }));
    cond.push_back(meta(0, 0x59, { static_cast<uint8_t>(static_cast<int8_t>(minorKeySharps(score.m_keyRoot))), 1 }));
    const std::vector<TempoPoint>& pts = score.m_tempo.points();
    const double end = std::max(score.m_lengthBeats, pts.back().m_beat);
    for (size_t i = 0; i < pts.size(); ++i) {
        const TempoPoint& p = pts[i];
        if (p.m_rampToNext && i + 1 < pts.size()) {
            const double b1 = pts[i + 1].m_beat;
            for (double b = p.m_beat; b < b1; b += 1.0) {
                const double bn = std::min(b + 1.0, b1);
                const double sec = score.m_tempo.secondsAt(bn) - score.m_tempo.secondsAt(b);
                cond.push_back(tempoEvent(toTick(b), 60.0 * (bn - b) / sec));
            }
        } else {
            cond.push_back(tempoEvent(toTick(p.m_beat), p.m_bpm));
        }
    }
    for (const Marker& m : score.m_markers) cond.push_back(metaText(toTick(m.m_beat), 0x06, m.m_text));
    for (const Section& s : score.m_sections)
        cond.push_back(metaText(toTick(s.m_beat), 0x01, std::string("section: ") + SectionNames[static_cast<int>(s.m_kind)]));
    cond.push_back(meta(toTick(end), 0x01, {}));
    appendTrack(file, cond);

    for (int pi = 0; pi < NumParts; ++pi) {
        if (!used[pi]) continue;
        const PartE part = static_cast<PartE>(pi);
        const uint8_t ch = static_cast<uint8_t>(midiChannelOf(part));
        std::vector<Ev> evs;
        evs.push_back(metaText(0, 0x03, PartNames[pi]));
        // The sound the composer chose (Presets.h) where each track begins: its name, and on a voice's own channel the
        // program change (bank select 0 = preset / 128, program = preset % 128); the kick and the kit share the drum
        // channel, where a program change would switch a host's kit, so they carry the name only.
        for (const SoundPick& s : score.m_sounds) {
            if (partOfSound(s) != part) continue;
            const std::vector<SoundPreset>& ps = factoryPresets(static_cast<ModuleE>(s.m_module), s.m_instance);
            if (s.m_preset < 0 || static_cast<size_t>(s.m_preset) >= ps.size()) continue;
            const int64_t at = std::max<int64_t>(0, toTick(s.m_beat));
            const SoundPreset& sp = ps[static_cast<size_t>(s.m_preset)];
            evs.push_back(metaText(at, 0x01, "preset: " + sp.m_group + " / " + sp.m_name));
            if (ch == 9) continue;
            evs.push_back(Ev{ at, 0, { static_cast<uint8_t>(0xB0 | ch), 0, static_cast<uint8_t>(s.m_preset / 128) } });
            evs.push_back(Ev{ at, 0, { static_cast<uint8_t>(0xB0 | ch), 32, 0 } });
            evs.push_back(Ev{ at, 0, { static_cast<uint8_t>(0xC0 | ch), static_cast<uint8_t>(s.m_preset % 128) } });
        }
        for (const NoteEvent& n : score.m_notes) {
            if (n.m_part != part) continue;
            const int pitch = std::clamp(n.m_pitch, 0, 127);
            const int vel = std::clamp(static_cast<int>(std::lround(n.m_velocity * 127.0f)), 1, 127);
            const int64_t on = std::max<int64_t>(0, toTick(n.m_beat));
            const int64_t off = std::max(on + 1, toTick(n.m_beat + n.m_length));
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
