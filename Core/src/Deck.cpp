/**
 * @file Deck.cpp
 * @brief A deck: events on the sample grid, the parameter raster, the low end, the pump, buses, rooms, track bus.
 * @note The raster, the event splitting, the automation cursors, the knob settings, the trims and the stems are copied
 *       from Totality `Core/src/Deck.cpp` at 4d3c0d2 (29.09.2026), which had them from Ephemeris; the voices, the pump and
 *       the rooms are Parhelion's.
 */
#include "parh/Deck.h"
#include "parh/Profile.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace parh {

namespace {
constexpr float BassMinCut = 60.0f;    ///< the mid-bass's lowest low cut: under it only the kick and the sub
constexpr float GlueMix = 0.35f;       ///< the glue in parallel (PLAN 7.4)
/** The rooms' makeup: the FDN and the plate return quietly (Totality: +22 dB for its room at sends of 0.25 to 0.4). The
 *  returns' knobs sit on top; [I] until the reference measurement (PLAN 13.4). */
constexpr float RoomMakeupDb = 0.0f;
constexpr float PlateMakeupDb = 0.0f;   ///< the plate's makeup, dB
constexpr float HallMakeupDb = 0.0f;   ///< the hall's makeup, dB
constexpr double LeadSeconds = 0.5;    ///< a deck starts playing this long before its first note
constexpr double TailSeconds = 20.0;   ///< and plays on this long after its last (rooms, delays)
}

void Deck::prepare(const ParamStore* params, double sampleRate, int index)
{
    m_params = params;
    m_sampleRate = sampleRate;
    m_index = index;
    const int count = m_params->count();
    m_cellValue.assign(static_cast<size_t>(count), 0.0f);
    m_firstOf.resize(static_cast<size_t>(count));
    for (int id = 0; id < count; ++id) m_firstOf[static_cast<size_t>(id)] = m_params->base(m_params->moduleOf(id), m_params->instanceOf(id));
    m_instChanged.assign(static_cast<size_t>(count), 0);
    m_cellFresh = true;
    m_kick.prepare(sampleRate);
    m_sub.prepare(sampleRate);
    m_kit.prepare(sampleRate);
    m_bass.prepare(sampleRate, 0x42415353ull + static_cast<uint64_t>(index));   // "BASS"
    m_acid.prepare(sampleRate, 0x41434944ull + static_cast<uint64_t>(index));   // "ACID"
    for (int i = 0; i < PolyInstances; ++i) {
        m_poly[i].prepare(sampleRate);
        m_gate[i].prepare(sampleRate);
        m_polyDuck[i].prepare(sampleRate);
    }
    m_retDuck.prepare(sampleRate);
    m_piano.prepare(sampleRate);
    m_pianoDuck.prepare(sampleRate);
    m_strings.prepare(sampleRate, 0x5354524Eull + static_cast<uint64_t>(index));   // "STRN"
    m_choir.prepare(sampleRate, 0x43484F49ull + static_cast<uint64_t>(index));     // "CHOI"
    m_brass.prepare(sampleRate, 0x42524153ull + static_cast<uint64_t>(index));     // "BRAS"
    m_timpani.prepare(sampleRate, 0x54494D50ull + static_cast<uint64_t>(index));   // "TIMP"
    for (Ducker& d : m_orchDuck) d.prepare(sampleRate);
    m_cloud.prepare(sampleRate, 0x434C4F5544ull + static_cast<uint64_t>(index));   // "CLOUD"
    m_sfx.prepare(sampleRate);
    m_fxDuck.prepare(sampleRate);
    m_subDropDuck.prepare(sampleRate);
    m_room.prepare(sampleRate);
    m_hall.prepare(sampleRate);
    m_plate.prepare(sampleRate);
    m_glue.prepare(sampleRate);
    const float fs = static_cast<float>(sampleRate);
    m_tiltCoef = 1.0f - std::exp(-2.0f * Pi * 1000.0f / fs);
    m_trimCoef = 1.0f - std::exp(-1.0f / (1.0f * fs));   // a correction glides in over about a second
    for (int p = 0; p < BalParts; ++p) m_balGain[p] = m_balTarget[p] = 1.0f;
    const size_t n = static_cast<size_t>(Raster);
    for (std::vector<float>* b : { &m_kickBuf, &m_bodyBuf, &m_subBuf, &m_bassL, &m_bassR, &m_acidL, &m_acidR, &m_roomInL, &m_roomInR,
                                   &m_plateInL, &m_plateInR, &m_hallInL, &m_hallInR, &m_roomL, &m_roomR, &m_plateL, &m_plateR, &m_hallL,
                                   &m_hallR, &m_drumL, &m_drumR, &m_synthL, &m_synthR, &m_fxL, &m_fxR, &m_fxSub, &m_fxWetL, &m_fxWetR })
        b->assign(n, 0.0f);
    for (int i = 0; i < PolyInstances; ++i) { m_polyL[i].assign(n, 0.0f); m_polyR[i].assign(n, 0.0f); }
    m_pianoL.assign(n, 0.0f);
    m_pianoR.assign(n, 0.0f);
    for (int o = 0; o < Orch; ++o) { m_orchL[o].assign(n, 0.0f); m_orchR[o].assign(n, 0.0f); }
    for (std::vector<float>* b : { &m_cloudInL, &m_cloudInR, &m_cloudL, &m_cloudR }) b->assign(n, 0.0f);
    clear();
}

void Deck::setQuest(bool on)
{
    m_cellFresh = true;   // (the engines' limits are set; the next cell hands every module over again)
    // The Quest's share (PLAN 10): three unison oscillators and four voices a voice, the pad five and six; their filter
    // models with one Newton step (Phase 7, Filters.h NewtonLight; the 303 keeps its three).
    for (int i = 0; i < PolyInstances; ++i) {
        const bool pad = i == static_cast<int>(PolyInstanceE::PAD);
        m_poly[i].setQuality(on ? (pad ? 5 : 3) : PolyUnison, on ? (pad ? 6 : 4) : PolyVoices, on ? NewtonLight : Newton);
    }
    m_piano.setVoiceLimit(on ? 10 : Piano::Voices);
    // The orchestra's share: half the players and singers -- as limits on the knobs (the next cell hands them over).
    m_strings.setPlayerLimit(on ? 3 : StringPlayers);
    m_choir.setSingerLimit(on ? 3 : ChoirSingers);
    m_brass.setPlayerLimit(on ? 2 : BrassPlayers);
}

void Deck::clear()
{
    m_loaded = false;
    m_score = Score{};
    m_events.clear();
    m_tracks.clear();
    m_trackOf.assign(static_cast<size_t>(m_params != nullptr ? m_params->count() : 0), -1);
    m_mixerSteps.clear();
    m_plays.clear();
    m_lateTrims.clear();
    m_lateBal.clear();
    m_base.assign(static_cast<size_t>(m_params != nullptr ? m_params->count() : 0), std::numeric_limits<float>::quiet_NaN());
    m_knobCursor = 0;
    m_knobGroup = -1.0;
    m_newGroup = false;
    m_evCursor = 0;
    m_cellFresh = true;
}

void Deck::load(const Score& score)
{
    clear();
    m_loaded = true;
    m_score = score;
    m_score.sort();
    m_lateTrims.reserve(m_score.m_levels.size());
    m_lateBal.reserve(m_score.m_levels.size());
    // Events on the sample grid: the first sample at or after the ideal time, and how late that is.
    int id = 0;
    for (const NoteEvent& note : m_score.m_notes) {
        const double x = m_score.m_tempo.secondsAt(note.m_beat) * m_sampleRate;
        const double s = std::ceil(x);
        const double xe = m_score.m_tempo.secondsAt(note.m_beat + note.m_length) * m_sampleRate;
        const int gate = std::max(1, static_cast<int>(std::lround(xe - x)));
        Ev on{ static_cast<int64_t>(s), 1, static_cast<uint8_t>(note.m_part), note.m_accent, note.m_slide, note.m_pitch, note.m_velocity,
               note.m_shift, s - x, id, note.m_length, gate };
        m_events.push_back(on);
        if (!isOneShot(note.m_part)) {
            const double so = std::ceil(xe);
            m_events.push_back(Ev{ static_cast<int64_t>(so), 0, on.m_part, false, false, note.m_pitch, 0.0f, 0, so - xe, id, 0.0, 0 });
        }
        ++id;
    }
    std::stable_sort(m_events.begin(), m_events.end(), [](const Ev& a, const Ev& b) {
        return a.m_sample != b.m_sample ? a.m_sample < b.m_sample : a.m_on < b.m_on;
    });
    // The automation, per parameter; the steps on this deck's mixer channel, for the engine.
    const int mixFirst = m_params->base(ModuleE::DECK, m_index), mixEnd = mixFirst + deck::COUNT;
    for (const Gesture& g : m_score.m_gestures) {
        if (g.m_param < 0 || g.m_param >= m_params->count()) continue;
        int& t = m_trackOf[static_cast<size_t>(g.m_param)];
        if (t < 0) { t = static_cast<int>(m_tracks.size()); m_tracks.push_back(Track{ g.m_param, {}, 0, 0.0f }); }
        m_tracks[static_cast<size_t>(t)].m_gestures.push_back(g);
        if (g.m_param >= mixFirst && g.m_param < mixEnd)
            m_mixerSteps.push_back(static_cast<int64_t>(std::ceil(m_score.m_tempo.secondsAt(g.m_beat) * m_sampleRate)));
    }
    std::sort(m_mixerSteps.begin(), m_mixerSteps.end());
    m_breaks.clear();
    m_builds.clear();
    for (const Section& s : m_score.m_sections)
        if (s.m_kind == SectionKindE::BREAKDOWN || s.m_kind == SectionKindE::BREAK) m_breaks.push_back({ s.m_beat, s.m_beat + s.m_length });
        else if (s.m_kind == SectionKindE::BUILD) m_builds.push_back({ s.m_beat, s.m_beat + s.m_length });
    m_mixerSteps.erase(std::unique(m_mixerSteps.begin(), m_mixerSteps.end()), m_mixerSteps.end());
    // The piano's instrument: the design knobs the track starts with (or the knobs), built here, off the audio thread.
    {
        const int b = m_params->base(ModuleE::PIANO, 0);
        auto value = [&](int param) {
            for (const KnobSet& k : m_score.m_knobs) if (k.m_param == b + param) return k.m_value;
            return m_params->get(b + param);
        };
        PianoSpec spec;
        spec.m_instrument = static_cast<int>(std::lround(value(piano::INSTRUMENT)));
        spec.m_hardness = value(piano::HARDNESS);
        spec.m_strike = value(piano::STRIKE);
        spec.m_unison = value(piano::UNISON);
        spec.m_inharm = value(piano::INHARM);
        spec.m_impedance = value(piano::IMPEDANCE);
        spec.m_stretch = value(piano::STRETCH);
        spec.m_condition = value(piano::CONDITION);
        bool uses = false;
        for (const NoteEvent& note : m_score.m_notes) if (note.m_part == PartE::PIANO) { uses = true; break; }
        if (uses) m_piano.setSpec(spec);
    }
    // Where it plays: around its notes, merged.
    const int64_t lead = static_cast<int64_t>(LeadSeconds * m_sampleRate), tail = static_cast<int64_t>(TailSeconds * m_sampleRate);
    for (const Ev& e : m_events) {
        const int64_t from = std::max<int64_t>(0, e.m_sample - lead), to = e.m_sample + std::max<int64_t>(e.m_gate, 0) + tail;
        if (!m_plays.empty() && from <= m_plays.back().second) m_plays.back().second = std::max(m_plays.back().second, to);
        else m_plays.push_back({ from, to });
    }
    seek(0);
}

bool Deck::playsAt(int64_t sample) const
{
    for (const auto& r : m_plays) {
        if (sample < r.first) return false;
        if (sample < r.second) return true;
    }
    return false;
}

int64_t Deck::nextRestChange(int64_t sample) const
{
    for (const auto& r : m_plays) {
        if (r.first > sample) return r.first;
        if (r.second > sample) return r.second;
    }
    return Never;
}

void Deck::seek(int64_t sample)
{
    m_evCursor = 0;
    while (m_evCursor < m_events.size() && m_events[m_evCursor].m_sample < sample) ++m_evCursor;
    // A jump back to the start plays what is due at it; elsewhere the voices fall silent.
    if (sample == 0) m_evCursor = 0;
    for (Track& t : m_tracks) { t.m_cursor = t.m_gestures.size(); t.m_offset = 0.0f; }
    std::fill(m_base.begin(), m_base.end(), std::numeric_limits<float>::quiet_NaN());
    m_knobCursor = 0;
    m_knobGroup = -1.0;
    m_newGroup = false;
    m_cellFresh = true;   // the engines are reset below: every module is handed over again at the next cell
    m_kick.reset();
    m_sub.reset();
    m_kit.reset();
    m_bass.reset();
    m_acid.reset();
    m_piano.reset();
    m_strings.reset();
    m_choir.reset();
    m_brass.reset();
    m_timpani.reset();
    m_cloud.reset();
    for (int i = 0; i < PolyInstances; ++i) {
        m_poly[i].reset();
        // The same start phases and drift walks whatever came before (Poly.h): a render is a function of the score.
        m_poly[i].seedPhases(0x9E3779B97F4A7C15ull * static_cast<uint64_t>(i + 1) + static_cast<uint64_t>(m_index));
        m_gate[i].reset();
        m_polyDuck[i].reset();
        m_lastDuck[i] = 1.0f;
    }
    m_retDuck.reset();
    m_sfx.reset();
    m_fxDuck.reset();
    m_subDropDuck.reset();
    m_room.reset();
    m_hall.reset();
    m_plate.reset();
    m_glue.reset();
    resetStems();
    for (Svf& f : m_hatsLp) f.reset();
    for (Svf& f : m_percLp) f.reset();
    for (auto& ch : m_groupHp) for (Svf& f : ch) f.reset();
    m_tiltState[0] = m_tiltState[1] = 0.0f;
    m_snapFaders = true;   // the next cell puts the faders where the score has them
    m_haveKick = false;
    m_subNote = m_bassNote = m_acidNote = -1;
    const double b = m_score.m_tempo.beatAt(static_cast<double>(sample) / m_sampleRate);
    m_trimGain = m_trimTarget = dbToGain(m_score.trimAt(b));
    const BalanceDbT bal = m_score.balanceAt(b);
    for (int p = 0; p < BalParts; ++p) m_balGain[p] = m_balTarget[p] = dbToGain(bal[static_cast<size_t>(p)]);
}

float Deck::played(int id) const
{
    const float knob = m_params->get(id);
    float value = knob;
    if (id >= 0 && static_cast<size_t>(id) < m_base.size()) {
        const float b = m_base[static_cast<size_t>(id)];
        if (b == b) {
            // The track's own value; where a hand has turned the knob away from what the engine wrote on it, by as much.
            value = b;
            const float s = m_shown != nullptr ? m_shown[id] : std::numeric_limits<float>::quiet_NaN();
            if (s == s && knob != s)
                value = m_params->fromNormalised(id, m_params->toNormalised(id, b) + m_params->toNormalised(id, knob) - m_params->toNormalised(id, s));
        }
    }
    if (id < 0 || id >= static_cast<int>(m_trackOf.size()) || m_trackOf[static_cast<size_t>(id)] < 0) return value;
    const float off = m_tracks[static_cast<size_t>(m_trackOf[static_cast<size_t>(id)])].m_offset;
    if (off == 0.0f) return value;
    return m_params->fromNormalised(id, m_params->toNormalised(id, value) + off);
}

bool Deck::applyKnobs(double beat)
{
    // One beat early: a track's sounds are in place before its first note (its deck rests until then).
    const size_t was = m_knobCursor;
    while (m_knobCursor < m_score.m_knobs.size() && m_score.m_knobs[m_knobCursor].m_beat <= beat + 1.0) {
        const KnobSet& k = m_score.m_knobs[m_knobCursor++];
        if (k.m_beat != m_knobGroup) {
            std::fill(m_base.begin(), m_base.end(), std::numeric_limits<float>::quiet_NaN());
            m_knobGroup = k.m_beat;
            m_newGroup = true;
        }
        if (k.m_param >= 0 && static_cast<size_t>(k.m_param) < m_base.size()) m_base[static_cast<size_t>(k.m_param)] = k.m_value;
    }
    return m_knobCursor != was;
}

void Deck::readPlayed(ModuleE m, int instance, float* out) const
{
    const int b = m_params->base(m, instance);
    const int n = ParamStore::moduleCount(m);
    for (int i = 0; i < n; ++i) out[i] = played(b + i);
}

void Deck::refreshCell(bool all)
{
    // Bit for bit: a value that went from 0 to -0 is a change as well (what an engine makes of it is its business).
    auto put = [this](int id) {
        const size_t k = static_cast<size_t>(id);
        const float v = played(id);
        if (std::memcmp(&v, &m_cellValue[k], sizeof v) != 0) {
            m_cellValue[k] = v;
            m_instChanged[static_cast<size_t>(m_firstOf[k])] = 1;
        }
    };
    if (all) {
        for (int id = 0; id < m_params->count(); ++id) put(id);
        if (m_cellFresh) std::fill(m_instChanged.begin(), m_instChanged.end(), uint8_t{ 1 });
    } else {
        for (int id : m_staleIds) put(id);
    }
    m_staleIds.clear();
    m_cellFresh = false;
}

bool Deck::readCell(ModuleE m, int instance, float* out)
{
    const int b = m_params->base(m, instance);
    const int n = ParamStore::moduleCount(m);
    std::memcpy(out, m_cellValue.data() + b, static_cast<size_t>(n) * sizeof(float));
    const bool changed = m_instChanged[static_cast<size_t>(b)] != 0;
    m_instChanged[static_cast<size_t>(b)] = 0;
    return changed;
}

void Deck::updateCell(int64_t sample)
{
    const double seconds = static_cast<double>(sample) / m_sampleRate;
    const double beat = m_score.m_tempo.beatAt(seconds);
    const bool knobsTaken = applyKnobs(beat);
    // The store's count before its values are read (ParamStore::version): a write during the read is read at the next cell.
    const uint32_t version = m_params->version();
    for (Track& t : m_tracks) {
        const size_t none = t.m_gestures.size();
        size_t c = t.m_cursor, next = c == none ? 0 : c + 1;
        while (next < t.m_gestures.size() && t.m_gestures[next].m_beat <= beat) { c = next; ++next; }
        t.m_cursor = c;
        const float was = t.m_offset;
        t.m_offset = c == none ? 0.0f : gestureValue(t.m_gestures[c], beat);
        if (std::memcmp(&was, &t.m_offset, sizeof was) != 0) m_staleIds.push_back(t.m_param);
    }
    refreshCell(m_cellFresh || knobsTaken || version != m_seenVersion);
    m_seenVersion = version;
    m_mutes = 0;
    if (m_live)
        for (int k = 0; k < perform::Mutes; ++k)
            if (m_params->getBool(m_params->id(ModuleE::PERFORM, 0, perform::MUTE_KICK + k))) m_mutes |= 1u << k;
    // The keyboard (live only, 01.10.2026): what it plays, whether that replaces the composer's notes, whether the
    // composer plays at all.
    m_keyTarget = m_live ? m_params->getInt(m_params->id(ModuleE::PERFORM, 0, perform::KEYBOARD_PART)) : perform::keys::OFF;
    m_keyReplace = !m_live || m_params->getInt(m_params->id(ModuleE::PERFORM, 0, perform::KEYBOARD_MODE)) == 0;
    m_composerOff = m_live && !m_params->getBool(m_params->id(ModuleE::PERFORM, 0, perform::COMPOSER));

    float c[compose::COUNT];
    readCell(ModuleE::COMPOSE, 0, c);
    m_keyRoot = static_cast<int>(std::lround(c[compose::KEY]));
    m_scale = static_cast<int>(std::lround(c[compose::SCALE]));
    const double bpm = m_score.m_tempo.bpmAt(beat);
    // What the modules are handed over with besides their values: a change of it hands them over again.
    const bool keyMoved = m_keyRoot != m_cellKey || m_scale != m_cellScale;
    const bool bpmMoved = bpm != m_cellBpm;
    m_cellKey = m_keyRoot;
    m_cellScale = m_scale;
    m_cellBpm = bpm;
    m_beat = beat;
    m_cellSample = sample;
    m_beatsPerSample = bpm / (60.0 * m_sampleRate);
    const float msToBeats = static_cast<float>(bpm / 60000.0);

    float v[128];
    static_assert(poly::COUNT <= 128 && perc::COUNT <= 128 && kick::COUNT <= 128 && synth::COUNT <= 128 && piano::COUNT <= 128
                  && strings::COUNT <= 128 && choir::COUNT <= 128 && brass::COUNT <= 128 && timpani::COUNT <= 128,
                  "the cell's scratch holds a module");
    // The modulation's shared sources (Phase 5b, Modulation.h): the player's wheel and pressure, the score's energy.
    ModGlobals glob;
    {
        float perf[perform::COUNT];
        readCell(ModuleE::PERFORM, 0, perf);
        glob.m_wheel = std::clamp(perf[perform::WHEEL], 0.0f, 1.0f);
        glob.m_pressure = std::clamp(perf[perform::PRESSURE], 0.0f, 1.0f);
        for (const Section& s : m_score.m_sections)
            if (beat >= s.m_beat && beat < s.m_beat + s.m_length) {
                const double t = s.m_length > 0.0 ? (beat - s.m_beat) / s.m_length : 0.0;
                glob.m_energy = std::clamp(static_cast<float>(s.m_energyFrom + (s.m_energyTo - s.m_energyFrom) * t) * 0.1f, 0.0f, 1.0f);
                break;
            }
    }
    if (readCell(ModuleE::KICK, 0, v) || keyMoved) {
        Kick::constrain(v, 0.0, m_keyRoot);
        m_kick.update(v, m_keyRoot);
    }

    float pump[pump::COUNT];
    const bool pumpMoved = readCell(ModuleE::PUMP, 0, pump);

    if (readCell(ModuleE::SUB, 0, v)) m_sub.update(v);

    m_kit.setTempo(bpm);
    for (int l = 0; l < PercLanes; ++l) {
        if (!readCell(ModuleE::PERC, l, v) && !keyMoved) continue;
        const PercRoleE r = static_cast<PercRoleE>(std::lround(v[perc::ROLE]));
        m_kit.update(l, v, m_keyRoot, m_scale);
        m_laneIsHat[l] = r == PercRoleE::CLOSED_HAT || r == PercRoleE::ROLLING_HAT || r == PercRoleE::OPEN_HAT
                     || r == PercRoleE::RIDE || r == PercRoleE::SHAKER || r == PercRoleE::TAMBOURINE || r == PercRoleE::CRASH;
    }

    const bool bassMoved = readCell(ModuleE::BASS, 0, v);
    m_bass.setModGlobals(glob);
    m_bass.setClock(beat, m_beatsPerSample);
    if (bassMoved) m_bass.update(v, BassMinCut);
    else m_bass.refreshModulation();   // (update's tail: every cell re-applies the matrix, as it always did)
    m_bassSends = Sends{ v[synth::ROOM_SEND], v[synth::PLATE_SEND], 0.0f };
    const bool acidMoved = readCell(ModuleE::ACID, 0, v);
    m_acid.setModGlobals(glob);
    m_acid.setClock(beat, m_beatsPerSample);
    if (acidMoved) m_acid.update(v, BassMinCut);
    else m_acid.refreshModulation();
    m_acidSends = Sends{ v[synth::ROOM_SEND], v[synth::PLATE_SEND], 0.0f };

    for (int i = 0; i < PolyInstances; ++i) {
        const bool moved = readCell(ModuleE::POLY, i, v);
        m_poly[i].setModGlobals(glob);
        // update() gives every voice the written envelope times at every cell -- which also takes back, within a cell,
        // the attack a voice's drift gave it at its note (Poly::noteOn). Kept exactly as it was: done here alone where the
        // values stand.
        if (moved || bpmMoved) m_poly[i].update(v, bpm);
        else m_poly[i].refreshEnvelopeTimes();
        m_poly[i].setClock(beat, m_beatsPerSample);
        PolyStrip& s = m_strip[i];
        s.m_gate = v[poly::GATE] >= 0.5f;
        s.m_pattern = static_cast<int>(std::lround(v[poly::GATE_PATTERN]));
        s.m_depth = v[poly::GATE_DEPTH];
        s.m_duty = v[poly::GATE_DUTY];
        s.m_attackBeats = v[poly::GATE_ATTACK] * msToBeats;
        s.m_releaseBeats = v[poly::GATE_RELEASE] * msToBeats;
        s.m_tone = v[poly::GATE_TONE];
        s.m_sends = Sends{ v[poly::ROOM_SEND], v[poly::PLATE_SEND], v[poly::HALL_SEND] };
        // The pump's depth on this voice (poly.duck, dB) along the shared curve.
        if (moved || pumpMoved)
            m_polyDuck[i].set(1.0f - dbToGain(-v[poly::DUCK]), pump[pump::ATTACK], pump[pump::HOLD], pump[pump::RELEASE]);
    }
    if (pumpMoved) m_retDuck.set(1.0f - dbToGain(-pump[pump::RETURN_DUCK]), pump[pump::ATTACK], pump[pump::HOLD], pump[pump::RELEASE]);
    // The keys and the orchestra: the same, a module handed over where it changed.
    auto voice = [&](ModuleE m, auto& engine, Sends& sends, Ducker& duck, int room, int plate, int hall, int duckKnob) {
        const bool moved = readCell(m, 0, v);
        engine.setModGlobals(glob);
        engine.setClock(beat, m_beatsPerSample);
        if (moved) engine.update(v);
        sends = Sends{ v[room], v[plate], v[hall] };
        if (moved || pumpMoved) duck.set(1.0f - dbToGain(-v[duckKnob]), pump[pump::ATTACK], pump[pump::HOLD], pump[pump::RELEASE]);
    };
    voice(ModuleE::PIANO, m_piano, m_pianoSends, m_pianoDuck, piano::ROOM_SEND, piano::PLATE_SEND, piano::HALL_SEND, piano::DUCK);
    voice(ModuleE::STRINGS, m_strings, m_orchSends[0], m_orchDuck[0], strings::ROOM_SEND, strings::PLATE_SEND, strings::HALL_SEND, strings::DUCK);
    voice(ModuleE::CHOIR, m_choir, m_orchSends[1], m_orchDuck[1], choir::ROOM_SEND, choir::PLATE_SEND, choir::HALL_SEND, choir::DUCK);
    voice(ModuleE::BRASS, m_brass, m_orchSends[2], m_orchDuck[2], brass::ROOM_SEND, brass::PLATE_SEND, brass::HALL_SEND, brass::DUCK);
    if (readCell(ModuleE::CLOUD, 0, v)) m_cloud.update(v);
    m_cloudPad = v[cloud::PAD_SEND];
    m_cloudKeys = v[cloud::KEYS_SEND];
    m_cloudPlate = v[cloud::PLATE_SEND];
    voice(ModuleE::TIMPANI, m_timpani, m_orchSends[3], m_orchDuck[3], timpani::ROOM_SEND, timpani::PLATE_SEND, timpani::HALL_SEND, timpani::DUCK);
    const bool fxMoved = readCell(ModuleE::SFX, 0, v);
    if (fxMoved || keyMoved) m_sfx.update(v, m_keyRoot);
    m_sfx.setScale(m_scale);
    m_fxSends = Sends{ v[sfx::ROOM_SEND], v[sfx::PLATE_SEND], v[sfx::HALL_SEND] };
    if (fxMoved || pumpMoved) m_fxDuck.set(1.0f - dbToGain(-v[sfx::DUCK]), pump[pump::ATTACK], pump[pump::HOLD], pump[pump::RELEASE]);
    if (fxMoved) m_subDropDuck.set(v[sfx::SUB_DUCK], 0.5f, 60.0f, 90.0f);

    const float fs = static_cast<float>(m_sampleRate);
    const bool mixMoved = readCell(ModuleE::MIX, 0, v);
    m_hatsGain = dbToGain(v[mix::HATS_LEVEL]);
    m_percGain = dbToGain(v[mix::PERC_LEVEL]);
    // The Leveler's correction of the breakdowns (LevelMark::breakDb) on the synths and the effects, where one plays.
    float breakGain = 1.0f;
    for (const auto& b : m_breaks) if (beat >= b.first && beat < b.second) {
        size_t k = 0;
        for (size_t i = 0; i < m_score.m_levels.size(); ++i) if (m_score.m_levels[i].m_beat <= beat) k = i;
        if (k < m_score.m_levels.size()) breakGain = dbToGain(m_score.m_levels[k].m_breakDb);
    }
    m_percBuild = 1.0f;
    for (const auto& b : m_builds) if (beat >= b.first && beat < b.second) {
        size_t k = 0;
        for (size_t i = 0; i < m_score.m_levels.size(); ++i) if (m_score.m_levels[i].m_beat <= beat) k = i;
        if (k < m_score.m_levels.size()) { breakGain = dbToGain(m_score.m_levels[k].m_buildDb); m_percBuild = breakGain; }
    }
    m_synthTarget = dbToGain(v[mix::SYNTH_LEVEL]) * breakGain;
    m_fxTarget = breakGain;
    m_percGain *= m_percBuild;
    if (m_snapFaders) { m_synthGain = m_synthTarget; m_fxGain = m_fxTarget; m_snapFaders = false; }
    if (mixMoved) {
        for (Svf& f : m_hatsLp) f.setQ(std::min(v[mix::HATS_CUT], 0.45f * fs), 0.7071f, fs);
        for (Svf& f : m_percLp) f.setQ(std::min(v[mix::PERC_CUT], 0.45f * fs), 0.7071f, fs);
    }
    m_drumSat = v[mix::DRUM_SAT];
    m_groupHpOn = v[mix::LOW_CUT] > 20.5f;
    if (!m_groupHpOn) {
        for (auto& ch : m_groupHp) for (Svf& f : ch) f.reset();
        for (StemBus& s : m_stemBus) for (auto& ch : s.m_hp) for (Svf& f : ch) f.reset();
    }
    if (mixMoved) {
        for (auto& ch : m_groupHp) {
            ch[0].setK(v[mix::LOW_CUT], 1.8477590f, fs);
            ch[1].setK(v[mix::LOW_CUT], 0.7653669f, fs);
        }
    }

    const bool sendsMoved = readCell(ModuleE::SENDS, 0, v);
    const float lowCut = v[sends::LOW_CUT], highCut = std::min(v[sends::HIGH_CUT], 0.45f * fs);
    if (sendsMoved) {
        m_room.set(v[sends::ROOM_SIZE], v[sends::ROOM_DECAY], 0.5f, 0.008f * fs, lowCut, highCut);
        m_plate.set(v[sends::PLATE_DECAY], v[sends::PLATE_DAMPING], v[sends::PLATE_PRE_DELAY] * 0.001f * fs, lowCut, highCut);
        m_hall.set(v[sends::HALL_SIZE], v[sends::HALL_DECAY], v[sends::HALL_DAMPING], v[sends::HALL_PRE_DELAY] * 0.001f * fs, lowCut,
                  highCut);
    }
    m_roomReturn = v[sends::ROOM_RETURN] <= -59.9f ? 0.0f : dbToGain(v[sends::ROOM_RETURN] + RoomMakeupDb);
    m_plateReturn = v[sends::PLATE_RETURN] <= -59.9f ? 0.0f : dbToGain(v[sends::PLATE_RETURN] + PlateMakeupDb);
    m_hallReturn = v[sends::HALL_RETURN] <= -59.9f ? 0.0f : dbToGain(v[sends::HALL_RETURN] + HallMakeupDb);
    m_hatsSend = v[sends::HATS_SEND];
    m_percSend = v[sends::PERC_SEND];

    if (readCell(ModuleE::MASTER, 0, v)) m_glue.set(v[master::THRESHOLD], v[master::RATIO], 6.0f, 20.0f, 200.0f);
    m_tiltHigh = dbToGain(v[master::TILT]);

    float trim = m_score.trimAt(beat);
    if (!m_lateTrims.empty()) {
        size_t k = 0;
        for (size_t i = 0; i < m_score.m_levels.size(); ++i) if (m_score.m_levels[i].m_beat <= beat) k = i;
        if (k < m_lateTrims.size()) trim = m_lateTrims[k];
    }
    m_trimTarget = dbToGain(trim);
    BalanceDbT bal = m_score.balanceAt(beat);
    if (!m_lateBal.empty()) {
        size_t k = 0;
        for (size_t i = 0; i < m_score.m_levels.size(); ++i) if (m_score.m_levels[i].m_beat <= beat) k = i;
        if (k < m_lateBal.size()) bal = m_lateBal[k];
    }
    for (int p = 0; p < BalParts; ++p) m_balTarget[p] = dbToGain(bal[static_cast<size_t>(p)]);
}

void Deck::watchPeaks(bool on)
{
    m_watch = on;
    if (!on) return;
    m_kickPeak = 0.0f;
    for (float& p : m_partPeak) p = 0.0f;
}

void Deck::dispatchUntil(int64_t sample)
{
    while (m_evCursor < m_events.size() && m_events[m_evCursor].m_sample <= sample) dispatch(m_events[m_evCursor++]);
}

void Deck::dispatch(const Ev& e)
{
    const PartE part = static_cast<PartE>(e.m_part);
    if (e.m_on != 0 && !m_liveEvent && silenced(part)) return;   // the keyboard's or nobody's (01.10.2026)
    // The family jam (02.10.2026): a pitched part's composed note moves by the jam's transposition; its end by the shift
    // its start had, so a voice found by its pitch is found. The keys a player plays are their own.
    if (!m_liveEvent) {
        switch (part) {
        case PartE::SUB: case PartE::BASS: case PartE::ACID: case PartE::LEAD: case PartE::COUNTER: case PartE::PLUCK: case PartE::ARP: case PartE::PAD: case PartE::STAB: case PartE::PIANO: case PartE::STRINGS: case PartE::CHOIR: case PartE::BRASS: case PartE::TIMPANI: {
            int8_t& was = m_jamShift[static_cast<size_t>(e.m_part)][static_cast<size_t>(e.m_pitch & 127)];
            const int shift = e.m_on != 0 ? m_jamTranspose : was;
            if (e.m_on != 0) was = static_cast<int8_t>(m_jamTranspose);
            if (shift != 0) {
                Ev t = e;
                t.m_pitch = std::clamp(e.m_pitch + shift, 0, 127);
                dispatchPlayed(t);
                return;
            }
            break;
        }
        default: break;
        }
    }
    dispatchPlayed(e);
}

void Deck::dispatchPlayed(const Ev& e)
{
    const PartE part = static_cast<PartE>(e.m_part);
    // MIDI out (02.10.2026): the composer's notes as they are played -- a muted group's note-ons are not, offs always.
    // A poly voice ends with its gate, the kick, the ghost kick and the kit 50 ms after the on.
    if (m_noteTap != nullptr && !m_liveEvent && (e.m_on == 0 || muteOf(part) < 0 || !muted(muteOf(part)))) {
        const int64_t off = !isOneShot(part) ? -1
                          : polyOf(part) >= 0 && e.m_gate > 0 ? e.m_sample + e.m_gate : e.m_sample + static_cast<int64_t>(m_sampleRate * 0.05);
        m_noteTap->add(e.m_sample, e.m_part, e.m_pitch, e.m_velocity, e.m_on != 0, off);
    }
    switch (part) {
    case PartE::GHOST:
        // The pump (PLAN 7.3): every duck starts on the ghost kick, whether a kick plays or not.
        m_sub.kick(e.m_late);
        m_bass.kick(e.m_late);
        m_acid.kick(e.m_late);
        for (Ducker& d : m_polyDuck) d.trigger(e.m_late);
        m_pianoDuck.trigger(e.m_late);
        for (Ducker& d : m_orchDuck) d.trigger(e.m_late);
        m_retDuck.trigger(e.m_late);
        m_fxDuck.trigger(e.m_late);
        m_subDropDuck.trigger(e.m_late);
        return;
    case PartE::FX: {
        if (e.m_on == 0) return;
        const int type = e.m_pitch - SfxBaseNote;
        if (type < 0 || type >= NumSfxTypes) return;
        m_sfx.trigger(static_cast<SfxTypeE>(type), e.m_gate, e.m_velocity, e.m_late, e.m_shift);   // the shift names the bank preset
        return;
    }
    case PartE::KICK: {
        if (muted(perform::MUTE_KICK)) return;
        m_kick.trigger(e.m_velocity, e.m_late);
        m_haveKick = true;
        m_kickTime = static_cast<double>(e.m_sample) - e.m_late;
        m_kickC = m_kick.asymptoticPhase();
        m_kickF0 = m_kick.tunedEndHz();
        return;
    }
    case PartE::SUB: {
        if (e.m_on == 0) { if (e.m_id == m_subNote) { m_sub.noteOff(); m_subNote = -1; } return; }
        if (muted(perform::MUTE_BASS)) return;
        double phase = -1.0;
        if (m_sub.locked() && m_haveKick) {
            // The kick's phase at the note's ideal start, f0 dt + c, carried to the note's pitch.
            const double dt = (static_cast<double>(e.m_sample) - e.m_late - m_kickTime) / m_sampleRate;
            const double hz = m_sub.noteHz(e.m_pitch);
            const double kickPhase = m_kickF0 * dt + m_kickC;
            const double p = hz / m_kickF0 * kickPhase - m_sub.chainPhase(hz);
            phase = p - std::floor(p);
        }
        m_sub.noteOn(e.m_pitch, e.m_velocity, e.m_late, phase);
        m_subNote = e.m_id;
        return;
    }
    case PartE::BASS:
        if (e.m_on == 0) { if (e.m_id == m_bassNote) { m_bass.noteOff(); m_bassNote = -1; } return; }
        if (muted(perform::MUTE_BASS)) return;
        m_bass.noteOn(e.m_pitch, e.m_velocity, e.m_late, e.m_accent, e.m_slide);
        m_bassNote = e.m_id;
        return;
    case PartE::STRINGS:
        if (e.m_on == 0) { m_strings.noteOff(e.m_pitch); return; }
        if (muted(perform::MUTE_PADS)) return;
        m_strings.noteOn(e.m_pitch, e.m_velocity, e.m_lengthBeats < 0.5, e.m_late);   // a short note is bitten staccato
        return;
    case PartE::CHOIR:
        if (e.m_on == 0) { m_choir.noteOff(e.m_pitch); return; }
        if (muted(perform::MUTE_PADS)) return;
        m_choir.noteOn(e.m_pitch, e.m_velocity, false, e.m_late);
        return;
    case PartE::BRASS:
        if (e.m_on == 0) { m_brass.noteOff(e.m_pitch); return; }
        if (muted(perform::MUTE_SYNTHS)) return;
        m_brass.noteOn(e.m_pitch, e.m_velocity, false, e.m_late);
        return;
    case PartE::TIMPANI:
        if (e.m_on == 0) return;
        if (muted(perform::MUTE_PERC)) return;
        m_timpani.noteOn(e.m_pitch, e.m_velocity, false, e.m_late);
        return;
    case PartE::PIANO:
        if (e.m_on == 0) { m_piano.noteOff(e.m_pitch); return; }
        if (muted(perform::MUTE_LEAD)) return;
        m_piano.noteOn(e.m_pitch, e.m_velocity, e.m_late);
        return;
    case PartE::ACID:
        if (e.m_on == 0) { if (e.m_id == m_acidNote) { m_acid.noteOff(); m_acidNote = -1; } return; }
        if (muted(perform::MUTE_BASS)) return;
        m_acid.noteOn(e.m_pitch, e.m_velocity, e.m_late, e.m_accent, e.m_slide);
        m_acidNote = e.m_id;
        return;
    default: {
        if (const int pi = polyOf(part); pi >= 0) {
            const int group = pi <= static_cast<int>(PolyInstanceE::COUNTER) ? perform::MUTE_LEAD
                            : pi <= static_cast<int>(PolyInstanceE::ARP) ? perform::MUTE_SYNTHS : perform::MUTE_PADS;
            if (muted(group)) return;
            m_poly[pi].noteOnLimited(e.m_pitch, e.m_velocity, e.m_lengthBeats, e.m_gate, e.m_late, e.m_accent, e.m_slide);
            return;
        }
        const int lane = laneOf(part);
        if (lane >= 0 && !muted(m_laneIsHat[lane] ? perform::MUTE_HATS : perform::MUTE_PERC)) m_kit.trigger(lane, e.m_velocity, e.m_shift, e.m_late);
        return;
    }
    }
}

void Deck::resetStems()
{
    for (StemBus& s : m_stemBus) {
        for (auto& ch : s.m_hp) for (Svf& f : ch) f.reset();
        s.m_tilt[0] = s.m_tilt[1] = 0.0f;
    }
}

void Deck::render(int64_t sample, float* L, float* R, int n, float* const* stemL, float* const* stemR)
{
    const bool stems = stemL != nullptr;
    // Where this span starts in its cell: spans end at notes too, so it need not start on the raster line.
    const double spanOffset = static_cast<double>(sample - m_cellSample);
    // The sources.
    { PARH_PROF(KICK); m_kick.process(m_kickBuf.data(), m_bodyBuf.data(), n); }
    { PARH_PROF(SUB); m_sub.process(m_subBuf.data(), n); }
    { PARH_PROF(KIT); m_kit.processLanes(n); }
    { PARH_PROF(BASS); m_bass.process(m_bassL.data(), m_bassR.data(), n); m_acid.process(m_acidL.data(), m_acidR.data(), n); }
    { PARH_PROF(POLY); for (int i = 0; i < PolyInstances; ++i) m_poly[i].process(m_polyL[i].data(), m_polyR[i].data(), n); }
    { PARH_PROF(PIANO); m_piano.process(m_pianoL.data(), m_pianoR.data(), n); }
    { PARH_PROF(STRINGS); m_strings.process(m_orchL[0].data(), m_orchR[0].data(), n); }
    { PARH_PROF(CHOIR); m_choir.process(m_orchL[1].data(), m_orchR[1].data(), n); }
    { PARH_PROF(BRASS); m_brass.process(m_orchL[2].data(), m_orchR[2].data(), n); }
    { PARH_PROF(TIMPANI); m_timpani.process(m_orchL[3].data(), m_orchR[3].data(), n); }
    { PARH_PROF(SFX); m_sfx.processSplit(m_fxL.data(), m_fxR.data(), m_fxSub.data(), m_fxWetL.data(), m_fxWetR.data(), n); }
    PARH_PROF_BEGIN(Buses);

    const float* ll = m_kit.laneL();
    const float* lr = m_kit.laneR();
    float busH[2][Raster], busP[2][Raster];
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        for (int p = 0; p < BalParts; ++p) m_balGain[p] += (m_balTarget[p] - m_balGain[p]) * m_trimCoef;
        m_synthGain += (m_synthTarget - m_synthGain) * 0.002f;   // the fader glides (a few ms), no zipper on a ride
        m_fxGain += (m_fxTarget - m_fxGain) * 0.002f;
        const auto most = [](float& m, float a, float b) { m = std::max(m, std::max(std::fabs(a), std::fabs(b))); };
        // The mono synths, after their own ducks.
        const float gb = m_balGain[static_cast<int>(BalPartE::BASS)], ga = m_balGain[static_cast<int>(BalPartE::ACID)];
        m_bassL[k] *= gb; m_bassR[k] *= gb;
        m_acidL[k] *= ga; m_acidR[k] *= ga;
        // The polyphonic voices: gate, sends, pump.
        const double beat = m_beat + (spanOffset + static_cast<double>(i)) * m_beatsPerSample;
        float rl = 0.0f, rr = 0.0f, pl = 0.0f, pr = 0.0f, hl = 0.0f, hr = 0.0f, sl = 0.0f, sr = 0.0f;
        m_cloudInL[k] = m_cloudInR[k] = 0.0f;
        for (int v = 0; v < PolyInstances; ++v) {
            float l = m_polyL[v][k] * m_balGain[static_cast<int>(BalPartE::LEAD) + v] * m_synthGain;
            float r = m_polyR[v][k] * m_balGain[static_cast<int>(BalPartE::LEAD) + v] * m_synthGain;
            const PolyStrip& s = m_strip[v];
            if (s.m_gate) {
                const float o = TranceGate::open(beat, s.m_pattern, s.m_duty, s.m_attackBeats, s.m_releaseBeats);
                m_gate[v].apply(l, r, o, s.m_depth, s.m_tone);
            }
            rl += l * s.m_sends.m_room; rr += r * s.m_sends.m_room;
            pl += l * s.m_sends.m_plate; pr += r * s.m_sends.m_plate;
            hl += l * s.m_sends.m_hall; hr += r * s.m_sends.m_hall;
            if (v == static_cast<int>(PolyInstanceE::PAD)) { m_cloudInL[k] += l * m_cloudPad; m_cloudInR[k] += r * m_cloudPad; }
            if (v == static_cast<int>(PolyInstanceE::PLUCK)) { m_cloudInL[k] += l * m_cloudKeys; m_cloudInR[k] += r * m_cloudKeys; }
            const float g = m_polyDuck[v].next();
            m_lastDuck[v] = g;
            l *= g;
            r *= g;
            m_polyL[v][k] = l;
            m_polyR[v][k] = r;
            sl += l;
            sr += r;
            if (m_watch) most(m_partPeak[static_cast<int>(BalPartE::LEAD) + v], l, r);
        }
        {
            // The piano: its sends, its duck, into the synth bus.
            const float gp = m_balGain[static_cast<int>(BalPartE::PIANO)] * m_synthGain;
            float l = m_pianoL[k] * gp, r = m_pianoR[k] * gp;
            m_cloudInL[k] += l * m_cloudKeys;
            m_cloudInR[k] += r * m_cloudKeys;
            rl += l * m_pianoSends.m_room; rr += r * m_pianoSends.m_room;
            pl += l * m_pianoSends.m_plate; pr += r * m_pianoSends.m_plate;
            hl += l * m_pianoSends.m_hall; hr += r * m_pianoSends.m_hall;
            const float g = m_pianoDuck.next();
            l *= g;
            r *= g;
            m_pianoL[k] = l;
            m_pianoR[k] = r;
            sl += l;
            sr += r;
            if (m_watch) most(m_partPeak[static_cast<int>(BalPartE::PIANO)], l, r);
        }
        for (int o = 0; o < Orch; ++o) {
            // The orchestra: as the piano (the timpani outside the synth fader: they are percussion).
            const int bp = static_cast<int>(BalPartE::STRINGS) + o;
            const float go = m_balGain[bp] * (o == 3 ? 1.0f : m_synthGain);
            float l = m_orchL[o][k] * go, r = m_orchR[o][k] * go;
            const Sends& s = m_orchSends[o];
            rl += l * s.m_room; rr += r * s.m_room;
            pl += l * s.m_plate; pr += r * s.m_plate;
            hl += l * s.m_hall; hr += r * s.m_hall;
            const float g = m_orchDuck[o].next();
            l *= g;
            r *= g;
            m_orchL[o][k] = l;
            m_orchR[o][k] = r;
            sl += l;
            sr += r;
            if (m_watch) most(m_partPeak[bp], l, r);
        }
        m_synthL[k] = sl;
        m_synthR[k] = sr;
        if (m_watch) {
            most(m_kickPeak, m_kickBuf[k], m_kickBuf[k]);
            most(m_partPeak[static_cast<int>(BalPartE::BASS)], m_bassL[k], m_bassR[k]);
            most(m_partPeak[static_cast<int>(BalPartE::ACID)], m_acidL[k], m_acidR[k]);
        }
        // The kit's buses.
        float hsl = 0.0f, hsr = 0.0f, psl = 0.0f, psr = 0.0f;
        for (int l = 0; l < PercLanes; ++l) {
            const size_t q = static_cast<size_t>(i * PercKit::Stride + l);
            const float a = ll[q] * m_balGain[l], b = lr[q] * m_balGain[l];
            if (m_laneIsHat[l]) { hsl += a; hsr += b; }
            else { psl += a; psr += b; }
            if (m_watch) m_partPeak[l] = std::max(m_partPeak[l], std::max(std::fabs(a), std::fabs(b)) * (m_laneIsHat[l] ? m_hatsGain : m_percGain));
        }
        busH[0][i] = m_hatsLp[0].lp(hsl) * m_hatsGain;
        busH[1][i] = m_hatsLp[1].lp(hsr) * m_hatsGain;
        busP[0][i] = m_percLp[0].lp(psl) * m_percGain;
        busP[1][i] = m_percLp[1].lp(psr) * m_percGain;
        m_roomInL[k] = rl + busH[0][i] * m_hatsSend + m_bassL[k] * m_bassSends.m_room + m_acidL[k] * m_acidSends.m_room;
        m_roomInR[k] = rr + busH[1][i] * m_hatsSend + m_bassR[k] * m_bassSends.m_room + m_acidR[k] * m_acidSends.m_room;
        m_plateInL[k] = pl + busP[0][i] * m_percSend + m_bassL[k] * m_bassSends.m_plate + m_acidL[k] * m_acidSends.m_plate;
        m_plateInR[k] = pr + busP[1][i] * m_percSend + m_bassR[k] * m_bassSends.m_plate + m_acidR[k] * m_acidSends.m_plate;
        // The effects: sends before their duck, the wandering share straight into the hall; the sub drop under the kick.
        {
            const float gf = m_balGain[static_cast<int>(BalPartE::FX)] * m_fxGain;
            const float fl = m_fxL[k] * gf, fr = m_fxR[k] * gf;
            rl += fl * m_fxSends.m_room; rr += fr * m_fxSends.m_room;
            pl += fl * m_fxSends.m_plate; pr += fr * m_fxSends.m_plate;
            hl += fl * m_fxSends.m_hall + m_fxWetL[k] * gf; hr += fr * m_fxSends.m_hall + m_fxWetR[k] * gf;
            const float d = m_fxDuck.next();
            const float sub = m_fxSub[k] * gf * m_subDropDuck.next();
            m_fxL[k] = fl * d + sub;
            m_fxR[k] = fr * d + sub;
            if (m_watch) most(m_partPeak[static_cast<int>(BalPartE::FX)], m_fxL[k], m_fxR[k]);
        }
        m_hallInL[k] = hl;
        m_hallInR[k] = hr;
        // The drum bus: a gentle saturation, two stages mixed in.
        const float mono = m_kickBuf[k];
        float dl = mono + busH[0][i] + busP[0][i], dr = mono + busH[1][i] + busP[1][i];
        const float inL = dl, inR = dr;
        if (m_drumSat > 0.0f) {
            const float m = 0.43f * m_drumSat;
            static const float Drive[2] = { 1.5f, 2.0f };
            for (float d : Drive) {
                dl += m * (std::tanh(d * dl) / d - dl);
                dr += m * (std::tanh(d * dr) / d - dr);
            }
        }
        if (stems) {
            m_satGain[0][i] = inL != 0.0f ? dl / inL : 1.0f;
            m_satGain[1][i] = inR != 0.0f ? dr / inR : 1.0f;
        }
        m_drumL[k] = dl;
        m_drumR[k] = dr;
    }
    PARH_PROF_END(BUSES);
    { PARH_PROF(CLOUD); m_cloud.process(m_cloudInL.data(), m_cloudInR.data(), m_cloudL.data(), m_cloudR.data(), n); }
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        m_plateInL[k] += m_cloudL[k] * m_cloudPlate;
        m_plateInR[k] += m_cloudR[k] * m_cloudPlate;
    }
    { PARH_PROF(ROOMS);
      m_room.process(m_roomInL.data(), m_roomInR.data(), m_roomL.data(), m_roomR.data(), n);
      std::fill(m_plateL.begin(), m_plateL.begin() + n, 0.0f);   // the plate adds its return (Plate.h)
      std::fill(m_plateR.begin(), m_plateR.begin() + n, 0.0f);
      m_plate.process(m_plateInL.data(), m_plateInR.data(), m_plateL.data(), m_plateR.data(), n);
      m_hall.process(m_hallInL.data(), m_hallInR.data(), m_hallL.data(), m_hallR.data(), n); }
    // The returns, ducked by the pump (Dok. 7: "sonst fuellen die Tails genau den Platz des Kick-Transienten").
    const float gRoomBal = m_balGain[static_cast<int>(BalPartE::ROOM)];
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        const float g = m_retDuck.next() * gRoomBal;
        m_roomL[k] *= m_roomReturn * g; m_roomR[k] *= m_roomReturn * g;
        m_plateL[k] *= m_plateReturn * g; m_plateR[k] *= m_plateReturn * g;
        m_hallL[k] *= m_hallReturn * g; m_hallR[k] *= m_hallReturn * g;
        m_cloudL[k] *= g; m_cloudR[k] *= g;
    }
    if (m_meter != nullptr) {
        // The mixer page's meters, while it looks: the stems' signals, before the glue and the master.
        float pk[STEMS] = {};
        double ss[STEMS] = {};
        for (int i = 0; i < n; ++i) {
            const size_t k = static_cast<size_t>(i);
            auto put = [&](int s, float l, float r) {
                pk[s] = std::max(pk[s], std::max(std::fabs(l), std::fabs(r)));
                ss[s] += 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r);
            };
            put(STEM_KICK, m_kickBuf[k], m_kickBuf[k]);
            put(STEM_SUB, m_subBuf[k], m_subBuf[k]);
            put(STEM_BASS, m_bassL[k], m_bassR[k]);
            put(STEM_ACID, m_acidL[k], m_acidR[k]);
            put(STEM_HATS, busH[0][i], busH[1][i]);
            put(STEM_PERC, busP[0][i], busP[1][i]);
            for (int v = 0; v < PolyInstances; ++v) put(STEM_LEAD + v, m_polyL[v][k], m_polyR[v][k]);
            put(STEM_PIANO, m_pianoL[k], m_pianoR[k]);
            for (int o = 0; o < Orch; ++o) put(STEM_STRINGS + o, m_orchL[o][k], m_orchR[o][k]);
            put(STEM_ROOM, m_roomL[k], m_roomR[k]);
            put(STEM_PLATE, m_plateL[k], m_plateR[k]);
            put(STEM_HALL, m_hallL[k], m_hallR[k]);
            put(STEM_CLOUD, m_cloudL[k], m_cloudR[k]);
            put(STEM_FX, m_fxL[k], m_fxR[k]);
        }
        m_meter->add(pk, ss);
    }
    if (stems) {
        for (int i = 0; i < n; ++i) {
            const size_t k = static_cast<size_t>(i);
            auto put = [&](int s, float l, float r) { stemL[s][i] = l; stemR[s][i] = r; };
            put(STEM_KICK, m_kickBuf[k] * m_satGain[0][i], m_kickBuf[k] * m_satGain[1][i]);
            put(STEM_SUB, m_subBuf[k], m_subBuf[k]);
            put(STEM_BASS, m_bassL[k], m_bassR[k]);
            put(STEM_ACID, m_acidL[k], m_acidR[k]);
            put(STEM_HATS, busH[0][i] * m_satGain[0][i], busH[1][i] * m_satGain[1][i]);
            put(STEM_PERC, busP[0][i] * m_satGain[0][i], busP[1][i] * m_satGain[1][i]);
            for (int v = 0; v < PolyInstances; ++v) put(STEM_LEAD + v, m_polyL[v][k], m_polyR[v][k]);
            put(STEM_PIANO, m_pianoL[k], m_pianoR[k]);
            for (int o = 0; o < Orch; ++o) put(STEM_STRINGS + o, m_orchL[o][k], m_orchR[o][k]);
            put(STEM_ROOM, m_roomL[k], m_roomR[k]);
            put(STEM_PLATE, m_plateL[k], m_plateR[k]);
            put(STEM_HALL, m_hallL[k], m_hallR[k]);
            put(STEM_CLOUD, m_cloudL[k], m_cloudR[k]);
            put(STEM_FX, m_fxL[k], m_fxR[k]);
        }
    }

    PARH_PROF_BEGIN(TrackBus);
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        float l = m_drumL[k] + m_subBuf[k] + m_bassL[k] + m_acidL[k] + m_synthL[k] + m_roomL[k] + m_plateL[k] + m_hallL[k] + m_cloudL[k] + m_fxL[k];
        float r = m_drumR[k] + m_subBuf[k] + m_bassR[k] + m_acidR[k] + m_synthR[k] + m_roomR[k] + m_plateR[k] + m_hallR[k] + m_cloudR[k] + m_fxR[k];
        if (m_groupHpOn) {
            float lp, bp, hp;
            m_groupHp[0][0].tick(l, lp, bp, hp); m_groupHp[0][1].tick(hp, lp, bp, l);
            m_groupHp[1][0].tick(r, lp, bp, hp); m_groupHp[1][1].tick(hp, lp, bp, r);
        }
        m_tiltState[0] += m_tiltCoef * (l - m_tiltState[0]);
        m_tiltState[1] += m_tiltCoef * (r - m_tiltState[1]);
        L[i] = m_tiltState[0] + m_tiltHigh * (l - m_tiltState[0]);
        R[i] = m_tiltState[1] + m_tiltHigh * (r - m_tiltState[1]);
        m_drumL[k] = L[i];   // the glue's dry signal (the drum bus is spent)
        m_drumR[k] = R[i];
    }
    PARH_PROF_END(TRACK_BUS);
    PARH_PROF_BEGIN(Glue);
    m_glue.process(L, R, n, stems ? m_glueGains : nullptr);
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        m_trimGain += (m_trimTarget - m_trimGain) * m_trimCoef;
        m_trimGains[i] = m_trimGain;
        L[i] = (m_drumL[k] + GlueMix * (L[i] - m_drumL[k])) * m_trimGain;
        R[i] = (m_drumR[k] + GlueMix * (R[i] - m_drumR[k])) * m_trimGain;
    }
    PARH_PROF_END(GLUE);
    if (!stems) return;
    for (int s = 0; s < STEMS; ++s) {
        StemBus& b = m_stemBus[s];
        for (int c = 0; c < 2; ++c) for (int q = 0; q < 2; ++q) b.m_hp[c][q].copyCoefficients(m_groupHp[c][q]);
        float* ch[2] = { stemL[s], stemR[s] };
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < n; ++i) {
                float x = ch[c][i];
                if (m_groupHpOn) {
                    float lp, bp, hp;
                    b.m_hp[c][0].tick(x, lp, bp, hp); b.m_hp[c][1].tick(hp, lp, bp, x);
                }
                b.m_tilt[c] += m_tiltCoef * (x - b.m_tilt[c]);
                x = b.m_tilt[c] + m_tiltHigh * (x - b.m_tilt[c]);
                ch[c][i] = x * (1.0f + GlueMix * (m_glueGains[i] - 1.0f)) * m_trimGains[i];
            }
        }
    }
}

int Deck::targetOf(PartE part)
{
    switch (part) {
    case PartE::KICK: return perform::keys::KIT;
    case PartE::SUB:
    case PartE::BASS: return perform::keys::BASS;
    case PartE::ACID: return perform::keys::ACID;
    case PartE::PIANO: return perform::keys::PIANO;
    case PartE::STRINGS: return perform::keys::STRINGS;
    case PartE::CHOIR: return perform::keys::CHOIR;
    case PartE::BRASS: return perform::keys::BRASS;
    default:
        if (const int pi = polyOf(part); pi >= 0) return perform::keys::LEAD + pi;
        return laneOf(part) >= 0 ? perform::keys::KIT : perform::keys::OFF;
    }
}

int Deck::muteOf(PartE part) const
{
    switch (part) {
    case PartE::KICK: return perform::MUTE_KICK;
    case PartE::SUB:
    case PartE::BASS:
    case PartE::ACID: return perform::MUTE_BASS;
    case PartE::STRINGS:
    case PartE::CHOIR: return perform::MUTE_PADS;
    case PartE::BRASS: return perform::MUTE_SYNTHS;
    case PartE::TIMPANI: return perform::MUTE_PERC;
    case PartE::PIANO: return perform::MUTE_LEAD;
    default: {
        if (const int pi = polyOf(part); pi >= 0)
            return pi <= static_cast<int>(PolyInstanceE::COUNTER) ? perform::MUTE_LEAD
                 : pi <= static_cast<int>(PolyInstanceE::ARP) ? perform::MUTE_SYNTHS : perform::MUTE_PADS;
        const int lane = laneOf(part);
        return lane < 0 ? -1 : m_laneIsHat[lane] ? perform::MUTE_HATS : perform::MUTE_PERC;   // the ghost kick, the effects: none
    }
    }
}

bool Deck::silenced(PartE part) const
{
    if (m_composerOff) return true;
    if (m_keyTarget == perform::keys::OFF || !m_keyReplace) return false;
    const int t = targetOf(part);
    if (t == perform::keys::OFF) return false;
    // By channel, a part is the player's from its first played key on (until liveAllOff).
    if (m_keyTarget == perform::keys::BY_CHANNEL) return ((m_keyPlayed >> t) & 1u) != 0;
    return m_keyTarget == t;
}

void Deck::liveNote(int64_t sample, int target, int pitch, float velocity, bool on)
{
    if (!m_loaded || target <= perform::keys::OFF || target >= perform::keys::BY_CHANNEL || pitch < 0 || pitch > 127) return;
    constexpr int Held = 1 << 30;   // a gate that does not run out: the key's release ends the note
    Ev e{};
    e.m_sample = sample;
    e.m_on = on ? 1 : 0;
    e.m_pitch = pitch;
    e.m_velocity = std::clamp(velocity, 0.0f, 1.0f);
    e.m_accent = velocity > 0.86f;   // a hard key is the 303's accent
    e.m_id = 0x40000000 + pitch;     // a key's own id: its release finds its note
    e.m_lengthBeats = 1.0;           // not short: a string section is not bitten staccato
    e.m_gate = Held;
    switch (target) {
    case perform::keys::KIT:
        if (!on) return;   // one-shots: a release does nothing
        if (pitch == 36) e.m_part = static_cast<uint8_t>(PartE::KICK);
        else if (pitch > 36 && pitch <= 36 + PercLanes) e.m_part = static_cast<uint8_t>(percPart(pitch - 37));
        else return;
        break;
    case perform::keys::BASS: e.m_part = static_cast<uint8_t>(PartE::BASS); break;
    case perform::keys::ACID: e.m_part = static_cast<uint8_t>(PartE::ACID); break;
    case perform::keys::PIANO: e.m_part = static_cast<uint8_t>(PartE::PIANO); break;
    case perform::keys::STRINGS: e.m_part = static_cast<uint8_t>(PartE::STRINGS); break;
    case perform::keys::CHOIR: e.m_part = static_cast<uint8_t>(PartE::CHOIR); break;
    case perform::keys::BRASS: e.m_part = static_cast<uint8_t>(PartE::BRASS); break;
    default: {
        // A polyphonic voice: its note holds until the key's release, which its gate does not know of.
        const int pi = target - perform::keys::LEAD;
        if (pi < 0 || pi >= PolyInstances) return;
        if (!on) {
            m_poly[pi].noteOff(pitch);
            m_liveKey[pitch] = 0;
            return;
        }
        e.m_part = static_cast<uint8_t>(PartE::LEAD) + static_cast<uint8_t>(pi);
        break;
    }
    }
    if (target == perform::keys::BASS || target == perform::keys::ACID) {
        // A mono voice: one id for all its keys; a key pressed while another is held slides there, the release of a
        // key that no longer sounds does nothing.
        int& held = m_liveHeld[target];
        e.m_id = 0x40000000;
        if (on) { e.m_slide = held != 0; held = pitch + 1; }
        else if (held != pitch + 1) return;
        else held = 0;
    } else if (target != perform::keys::KIT) {
        m_liveKey[pitch] = on ? static_cast<uint8_t>(target + 1) : 0;
    }
    m_liveEvent = true;
    dispatch(e);
    m_liveEvent = false;
    if (on) m_keyPlayed |= 1u << target;
}

void Deck::liveAllOff()
{
    for (int t : { perform::keys::BASS, perform::keys::ACID })
        if (m_liveHeld[t] != 0) liveNote(0, t, m_liveHeld[t] - 1, 0.0f, false);
    for (int k = 0; k < 128; ++k)
        if (m_liveKey[k] != 0) liveNote(0, m_liveKey[k] - 1, k, 0.0f, false);
    m_keyPlayed = 0;
}

} // namespace parh
