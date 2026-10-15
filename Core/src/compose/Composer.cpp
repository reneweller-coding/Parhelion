/**
 * @file Composer.cpp
 * @brief The composer (Composer.h): plan, harmony, parts, effects, automation.
 * @author Rene Weller
 */
#include "parh/compose/Composer.h"
#include "parh/compose/Harmony.h"
#include "parh/compose/Melody.h"
#include "parh/Dsp.h"
#include "parh/Presets.h"
#include "parh/synth/Sfx.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace parh {

/** @brief the units' names, in the order of their streams (a reroll names one) */
const char* const UnitNames[UnitCount] = { "form", "matrix", "energy", "harmony", "motif", "lead", "bass", "acid", "arp",
                                             "pluck", "piano", "orchestra", "drums", "fx", "sounds" };

std::string camelotLabel(int key, bool major)
{
    static const int Minor[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
    static const int Major[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
    const int k = ((key % 12) + 12) % 12;
    return std::to_string(major ? Major[k] : Minor[k]) + (major ? "B" : "A");
}

namespace {

// The kit's lanes (Params.cpp, DefaultKit).
constexpr PartE Ch = PartE::PERC1;       ///< the closed hat's lane
constexpr PartE Oh = PartE::PERC2;       ///< the open hat's lane
constexpr PartE Clap = PartE::PERC3;     ///< the clap's lane
constexpr PartE Snare = PartE::PERC4;    ///< the snare's lane
constexpr PartE Ride = PartE::PERC5;     ///< the ride's lane
constexpr PartE Crash = PartE::PERC6;    ///< the crash's lane
constexpr PartE Shaker = PartE::PERC7;   ///< the shaker's lane
constexpr PartE Tamb = PartE::PERC8;     ///< the tambourine's lane
constexpr PartE Conga = PartE::PERC9;    ///< the conga's lane
constexpr PartE Tom = PartE::PERC10;     ///< the tom's lane

/**
 * @brief Whether bar @p bar of @p p lies in a drop.
 * @param[in] p    the plan
 * @param[in] bar  the bar
 * @return true in a drop
 */
bool isDrop(const Plan& p, int bar) { const int s = p.sectionAt(bar); return s >= 0 && p.m_sections[static_cast<size_t>(s)].m_kind == SectionKindE::DROP; }
/**
 * @brief Whether @p v holds @p x.
 * @param[in] v  the values
 * @param[in] x  the value
 * @return true when it is among them
 */
bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

} // namespace

Score composeTrack(const ParamStore& p, uint64_t seed, const TrackRequest& req, const Curation* cur, const std::string& unit, TrackInfo* info)
{
    StyleProfile prof = req.m_profile != nullptr ? *req.m_profile : profileOf(p);
    if (req.m_prefs != nullptr)   // the player's ratings on the forms (Preferences.h)
        for (int i = 0; i < FormTemplates; ++i) prof.m_forms[static_cast<size_t>(i)] *= req.m_prefs->m_form[i];
    const UnitStreamT stream = [&](const std::string& name) { return unitSeed(seed, name, cur != nullptr ? cur->count(unit + name) : 0); };
    // (The energy draws on its own stream: the pad's opening follows it, -0.4 + 0.07 per point.)
    auto padOpen = [](float e) { return -0.4f + 0.07f * e; };
    const bool autoKnobs = p.getBool(p.id(ModuleE::COMPOSE, 0, compose::AUTO));

    // Tempo, key, length.
    Rng hr;
    hr.seed(mixSeed(stream("harmony"), 0x4B455953ull));   // "KEYS" (the progression draws on the stream itself)
    float bpm = req.m_bpm > 0.0f ? req.m_bpm : p.get(p.id(ModuleE::COMPOSE, 0, compose::BPM));
    if (req.m_bpm <= 0.0f && autoKnobs) bpm = std::round((prof.m_bpmLow + (prof.m_bpmHigh - prof.m_bpmLow) * hr.uniform()) * 2.0f) * 0.5f;
    int key = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::KEY)), scale = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::SCALE));
    if (autoKnobs) drawKey(prof, hr, key, scale);
    if (req.m_key >= 0) key = req.m_key;
    if (req.m_scale >= 0) scale = req.m_scale;
    float minutes = p.get(p.id(ModuleE::COMPOSE, 0, compose::MINUTES));
    Rng fr;
    fr.seed(mixSeed(stream("form"), 0x4D494E53ull));   // "MINS" (the planner draws on the stream itself)
    if (autoKnobs) minutes = prof.m_minutesLow + (prof.m_minutesHigh - prof.m_minutesLow) * fr.uniform();
    const int askBars = req.m_bars > 0 ? req.m_bars : std::max(64, static_cast<int>(std::lround(minutes * bpm / 4.0)));

    std::vector<Rewrite> rewrites = req.m_earlier;
    if (req.m_rewriteBar >= 0) rewrites.push_back({ req.m_rewriteBar, req.m_rewriteKind });
    const Plan plan = planTrackRewritten(prof, askBars, stream, req.m_mixable, rewrites, req.m_opener);
    const Harmony harm = composeHarmony(plan, prof, key, scale, stream("harmony"));

    Score sc;
    sc.clear(bpm);
    sc.m_seed = seed;
    sc.m_keyRoot = key;
    sc.m_scale = scale;
    sc.m_lengthBeats = 4.0 * plan.m_bars;
    sc.m_sections = plan.m_sections;
    sc.m_layers = plan.m_blocks;
    for (const Section& s : plan.m_sections) sc.m_markers.push_back(Marker{ s.m_beat, SectionNames[static_cast<int>(s.m_kind)] });

    MelodyContext mc;
    mc.m_plan = &plan;
    mc.m_harmony = &harm;
    mc.m_score = &sc;
    mc.m_humanize = p.get(p.id(ModuleE::COMPOSE, 0, compose::HUMANIZE)) * 0.01f;
    mc.m_velSeed = mixSeed(seed, 0x56454Cull);   // "VEL"
    mc.m_piano = prof.m_lead == LeadKindE::PIANO;
    mc.m_anthemShare = mc.m_piano ? 1.0f : 0.7f;
    auto note = [&](double beat, double len, PartE part, int pitch, float v, int shift = 0, bool accent = false, bool slide = false) {
        mc.note(beat, len, part, pitch, v, shift, accent, slide);
    };

    // ------------------------------------------------------------------------------------------ drums
    Rng dr;
    dr.seed(stream("drums"));
    const bool hats16 = dr.uniform() < prof.m_hats16;
    const bool shakerLoop = dr.uniform() < prof.m_shaker;
    for (int bar = 0; bar < plan.m_bars; ++bar) {
        const double b0 = 4.0 * bar;
        const bool kick = plan.at(bar, LayerE::KICK) == LayerStateE::ON && !contains(plan.m_miniBreaks, bar);
        const LayerStateE ch = plan.at(bar, LayerE::CLOSED_HAT), oh = plan.at(bar, LayerE::OPEN_HAT);
        const int sec = plan.sectionAt(bar);
        const SectionKindE kind = sec >= 0 ? plan.m_sections[static_cast<size_t>(sec)].m_kind : SectionKindE::GROOVE;
        for (int q = 0; q < 4; ++q) {
            if (!plan.m_beatless) note(b0 + q, 0.25, PartE::GHOST, 36, 1.0f);
            if (kick) note(b0 + q, 0.25, PartE::KICK, 36, 1.0f);
            if (plan.at(bar, LayerE::CLAP) == LayerStateE::ON && (q == 1 || q == 3)) note(b0 + q, 0.25, Clap, 39, 0.9f);
            if (plan.at(bar, LayerE::RIDE) == LayerStateE::ON) {
                note(b0 + q, 0.25, Ride, 51, 0.65f);
                note(b0 + q + 0.5, 0.25, Ride, 51, 0.5f);
            }
            if (oh != LayerStateE::OFF) note(b0 + q + 0.5, 0.25, Oh, 46, 0.85f);
            if (ch != LayerStateE::OFF) {
                static const float Acc16[4] = { 0.55f, 0.35f, 0.8f, 0.4f };
                for (int s = 0; s < (hats16 ? 4 : 2); ++s) {
                    const int step = hats16 ? s : 2 * s;
                    if (step == 2 && oh != LayerStateE::OFF) continue;   // the open hat has the off-beat
                    note(b0 + q + 0.25 * step, 0.1, Ch, 42, hats16 ? Acc16[step] : (step == 0 ? 0.5f : 0.8f));
                }
            }
            if (plan.at(bar, LayerE::PERC) == LayerStateE::ON && kind != SectionKindE::BUILD) {
                if (shakerLoop) for (int s = 0; s < 4; ++s) note(b0 + q + 0.25 * s, 0.1, Shaker, 70, s == 2 ? 0.7f : 0.4f);
                else {
                    static const int CongaSteps[16] = { 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0 };
                    for (int s = 0; s < 4; ++s) if (CongaSteps[q * 4 + s]) note(b0 + q + 0.25 * s, 0.2, Conga, 63, 0.5f + 0.2f * (s == 0));
                    note(b0 + q + 0.5, 0.2, Tamb, 54, 0.45f);
                }
            }
        }
        if (plan.at(bar, LayerE::CRASH) == LayerStateE::ON && bar % 8 == 0) note(b0, 1.0, Crash, 49, 0.9f);
    }
    // A crash on every drop's first beat; a tom fill in the last bar before a groove after a break (Dok. 3).
    for (const Section& s : plan.m_sections) if (s.m_kind == SectionKindE::DROP) note(s.m_beat, 1.0, Crash, 49, 1.0f);
    // The snare rolls: the build's last bars and a break's last four before a drop, quarters to thirty-seconds.
    for (size_t si = 0; si + 1 < plan.m_sections.size(); ++si) {
        const Section& s = plan.m_sections[si];
        const Section& next = plan.m_sections[si + 1];
        if (next.m_kind != SectionKindE::DROP || plan.m_beatless) continue;
        if (s.m_kind != SectionKindE::BUILD && s.m_kind != SectionKindE::BREAK) continue;
        const int endBar = static_cast<int>((s.m_beat + s.m_length) / 4.0);
        const int bars = std::min(s.m_kind == SectionKindE::BUILD ? 8 : 4, static_cast<int>(s.m_length / 4.0));
        const int start = endBar - bars;
        for (int bar = start; bar < endBar; ++bar) {
            const double pos0 = static_cast<double>(bar - start) / bars;
            const double step = pos0 < 0.25 ? 1.0 : pos0 < 0.5 ? 0.5 : pos0 < 0.8 ? 0.25 : 0.125;
            for (double t = 0.0; t < 4.0 - 1e-9; t += step) {
                const double pos = (bar - start + t / 4.0) / bars;
                note(4.0 * bar + t, 0.1, Snare, 38, static_cast<float>(0.35 + 0.65 * pos), static_cast<int>(std::lround(12.0 * pos)));
            }
        }
    }
    (void)Tom;

    // ------------------------------------------------------------------------------------------ bass
    const BassPatternE bassPat = static_cast<BassPatternE>(plan.m_bassPattern);   // the planner's (`bass`), which cast the 303
    writeAcid(mc, stream("acid"));
    for (int bar = 0; bar < plan.m_bars; ++bar) {
        const double b0 = 4.0 * bar;
        const int rootPc = harm.pc(bar, 0);
        const int sub = atOrAbove(rootPc, 28), mid = sub + 12;
        const bool subOn = plan.at(bar, LayerE::SUB) != LayerStateE::OFF;
        const bool bassOn = plan.at(bar, LayerE::BASS) != LayerStateE::OFF;
        for (int q = 0; q < 4; ++q) {
            const double beat = b0 + q;
            switch (bassPat) {
            case BassPatternE::OFFBEAT:
            case BassPatternE::ACID:
                if (subOn) note(beat + 0.5, 0.22, PartE::SUB, sub, 1.0f);
                if (bassOn) note(beat + 0.5, 0.22, PartE::BASS, mid, 0.9f);
                break;
            case BassPatternE::ROLLING:
                if (subOn) note(beat + 0.5, 0.22, PartE::SUB, sub, 1.0f);
                if (bassOn) for (int s = 1; s < 4; ++s) note(beat + 0.25 * s, 0.18, PartE::BASS, mid, s == 2 ? 0.95f : 0.8f);
                break;
            case BassPatternE::GALLOP:
                if (subOn) note(beat + 0.5, 0.22, PartE::SUB, sub, 1.0f);
                if (bassOn) { note(beat + 0.5, 0.18, PartE::BASS, mid, 0.9f); note(beat + 0.75, 0.18, PartE::BASS, mid, 0.8f); }
                break;
            case BassPatternE::WALKING: {
                // Longer notes with syncopes on the "and" of 2 and 4 (Dok. 3), the fifth and the octave on the way.
                static const double At[4] = { 0.5, 0.5, 0.5, 0.5 };
                const int tone = q == 1 ? 7 : q == 3 ? 12 : 0;
                const double len = (q == 1 || q == 3) ? 0.45 : 0.3;
                if (subOn) note(beat + At[q], len, PartE::SUB, sub + (tone == 12 ? 0 : tone == 7 ? 0 : 0), 1.0f);
                if (bassOn) note(beat + At[q], len, PartE::BASS, mid + tone, q == 1 || q == 3 ? 0.95f : 0.8f);
                break;
            }
            case BassPatternE::DRONE:
                if (q == 0 && subOn) note(b0, 3.9, PartE::SUB, sub, 0.9f);
                if (bassOn && (q == 0 || q == 2)) note(beat + 0.5, 1.2, PartE::BASS, mid, 0.7f);
                break;
            default: break;
            }
        }
    }

    // ------------------------------------------------------------------------------------------ chords and melody
    {
        std::array<int, 4> prev{ 60, 64, 67, 72 };
        bool first = true;
        int bar = 0;
        while (bar < plan.m_bars) {
            if (plan.at(bar, LayerE::PAD) == LayerStateE::OFF) { first = true; ++bar; continue; }
            // Hold the chord while it stays and the pad plays (at most two bars a note, so the gate and the pump breathe).
            int len = 1;
            while (bar + len < plan.m_bars && len < 4 && plan.at(bar + len, LayerE::PAD) != LayerStateE::OFF
                   && harm.pc(bar + len, 0) == harm.pc(bar, 0) && harm.pc(bar + len, 1) == harm.pc(bar, 1) && (bar + len) % 8 != 0)
                ++len;
            const std::array<int, 4> v = voicePad(harm, bar, prev, first);
            first = false;
            prev = v;
            for (int k = 0; k < 4; ++k) note(4.0 * bar, 4.0 * len - 0.05, PartE::PAD, v[static_cast<size_t>(k)], 0.75f);
            // In a breakdown without the sub the pad takes the low end the kick and the bass leave: its root under the
            // chord, A1 .. G#2 (Phase 8: E2 .. D#3 before, whose roots over 120 Hz left the low band -- it swung 15 dB
            // with the chords, and the kick came back 31 to 44 dB over it in Progressive, the references 4 to 20).
            // (Tools/eval_report.py, 29.09.2026: under 150 Hz the breakdowns were empty, the drop's low band rose 36 to
            // 51 dB, the references' 3 to 40.)
            const int sec = plan.sectionAt(bar);
            const SectionKindE kind = sec >= 0 ? plan.m_sections[static_cast<size_t>(sec)].m_kind : SectionKindE::GROOVE;
            if ((kind == SectionKindE::BREAKDOWN || kind == SectionKindE::BREAK) && plan.at(bar, LayerE::SUB) == LayerStateE::OFF)
                note(4.0 * bar, 4.0 * len - 0.05, PartE::PAD, atOrAbove(harm.pc(bar, 0), 33), 0.6f);
            bar += len;
        }
    }
    writePluck(mc, stream("pluck"));
    writeStab(mc, mixSeed(stream("pluck"), 0x535441ull));   // "STA"
    writeArp(mc, stream("arp"), prof.m_bpmHigh < 125.0f);
    writeLead(mc, stream("motif"), stream("lead"));
    writeCounter(mc, mixSeed(stream("lead"), 0x434F55ull));   // "COU"
    // The piano's left hand (Phase 8, Dream House: the motif alone left 150 .. 400 Hz under the references' corridor on
    // every track): where the piano plays its motif (the lead's cell on), the chord's root and fifth held in the tenor,
    // C3 .. B3, struck again with every chord and every four bars, softer than the melody. Written after the counter,
    // which answers the melody's held notes, and marked as the accompaniment (NoteEvent::voice) for what reads the
    // melody later (the violins at the breakdown's peak).
    if (mc.m_piano) {
        int bar = 0;
        while (bar < plan.m_bars) {
            if (plan.at(bar, LayerE::LEAD) != LayerStateE::ON) { ++bar; continue; }
            int len = 1;
            while (bar + len < plan.m_bars && plan.at(bar + len, LayerE::LEAD) == LayerStateE::ON && (bar + len) % 4 != 0
                   && harm.pc(bar + len, 0) == harm.pc(bar, 0) && harm.pc(bar + len, 2) == harm.pc(bar, 2))
                ++len;
            const int rootNote = atOrAbove(harm.pc(bar, 0), 48);
            const int fifthNote = rootNote + (harm.pc(bar, 2) - harm.pc(bar, 0) + 12) % 12;
            for (const auto& [pitch, vel] : { std::pair<int, float>{ rootNote, 0.5f }, std::pair<int, float>{ fifthNote, 0.45f } }) {
                const size_t before = sc.m_notes.size();
                mc.note(4.0 * bar, 4.0 * len - 0.1, PartE::PIANO, pitch, vel);
                if (sc.m_notes.size() > before) sc.m_notes.back().m_voice = 1;
            }
            bar += len;
        }
    }


    // ------------------------------------------------------------------------------------------ the orchestra
    // Cinematic (PLAN 5.9, 6.6): the strings and the choir carry the main breakdown's full phrase -- chords from the
    // tease on, the violins with the melody at the peak -- and the main drop; brass (a braam) and timpani mark every
    // drop, the timpani roll into it over the build's last two bars.
    Rng orr;
    orr.seed(stream("orchestra"));
    const bool orchestra = orr.uniform() < prof.m_orchestra;
    if (info != nullptr) info->m_orchestra = orchestra;
    if (orchestra) {
        const int mbIndex = plan.m_mainBreakdown;
        const BreakdownParts* main = nullptr;
        if (mbIndex >= 0)
            for (const BreakdownParts& b : plan.m_breakdowns)
                if (static_cast<double>(b.m_start) * 4.0 == plan.m_sections[static_cast<size_t>(mbIndex)].m_beat) main = &b;
        const Section* mainDrop = plan.m_mainDrop >= 0 ? &plan.m_sections[static_cast<size_t>(plan.m_mainDrop)] : nullptr;
        // Held chords from bar a to bar b (a note each chord, at most four bars), voiced from the pad's voicing.
        auto chords = [&](PartE part, int a, int b, float vel, int shift, bool bassNote) {
            std::array<int, 4> prev{ 57, 60, 64, 69 };
            bool first = true;
            int bar = a;
            while (bar < b) {
                int len = 1;
                while (bar + len < b && len < 4 && harm.pc(bar + len, 0) == harm.pc(bar, 0) && harm.pc(bar + len, 1) == harm.pc(bar, 1)) ++len;
                const std::array<int, 4> v = voicePad(harm, bar, prev, first);
                first = false;
                prev = v;
                for (int k = 0; k < 4; ++k) note(4.0 * bar, 4.0 * len - 0.1, part, v[static_cast<size_t>(k)] + shift, vel);
                if (bassNote) note(4.0 * bar, 4.0 * len - 0.1, part, atOrAbove(harm.pc(bar, 0), 36), vel);
                bar += len;
            }
        };
        if (main != nullptr) {
            chords(PartE::STRINGS, main->m_tease, main->m_end, 0.6f, 0, true);
            chords(PartE::CHOIR, main->m_peak, main->m_end, 0.7f, 0, false);
            // The violins with the melody at the peak (an octave up where it lies low).
            std::vector<NoteEvent> melody;
            for (const NoteEvent& n : sc.m_notes)
                if ((n.m_part == PartE::LEAD || n.m_part == PartE::PIANO) && n.m_voice == 0 && n.m_beat >= 4.0 * main->m_peak && n.m_beat < 4.0 * main->m_end) melody.push_back(n);
            for (const NoteEvent& n : melody) note(n.m_beat, std::max(n.m_length, 0.3), PartE::STRINGS, n.m_pitch < 67 ? n.m_pitch + 12 : n.m_pitch, 0.7f);
            // A soft stroke on the timpani where the peak begins.
            note(4.0 * main->m_peak, 1.0, PartE::TIMPANI, atOrAbove(harm.pc(main->m_peak, 0), 40), 0.5f);
        }
        if (mainDrop != nullptr) {
            const int a = static_cast<int>(mainDrop->m_beat / 4.0), b = static_cast<int>((mainDrop->m_beat + mainDrop->m_length) / 4.0);
            chords(PartE::STRINGS, a, b, 0.6f, 12, true);
            chords(PartE::CHOIR, a + (b - a) / 2, b, 0.75f, 0, false);
        }
        for (size_t si = 1; si < plan.m_sections.size(); ++si) {
            const Section& s = plan.m_sections[si];
            if (s.m_kind != SectionKindE::DROP) continue;
            const int bar = static_cast<int>(s.m_beat / 4.0);
            // The braam: the root low, its fifth and octave, loud and blaring; the timpani on the root.
            const int root = atOrAbove(harm.pc(bar, 0), 33);
            for (int iv : { 0, 7, 12 }) note(s.m_beat, 1.6, PartE::BRASS, root + iv, 0.95f);
            note(s.m_beat, 1.0, PartE::TIMPANI, atOrAbove(harm.pc(bar, 0), 40), 1.0f);
            // The roll: the build's (or the break's) last two bars, sixteenths then thirty-seconds, growing.
            const Section& before = plan.m_sections[si - 1];
            if (before.m_kind == SectionKindE::BUILD || before.m_kind == SectionKindE::BREAK) {
                const int rollPitch = atOrAbove(harm.pc(bar - 1, 0), 40);
                for (double t = s.m_beat - 8.0; t < s.m_beat - 1e-9; ) {
                    const double pos = (t - (s.m_beat - 8.0)) / 8.0;
                    const double step = pos < 0.5 ? 0.25 : 0.125;
                    note(t, step * 0.9, PartE::TIMPANI, rollPitch, static_cast<float>(0.3 + 0.6 * pos));
                    t += step;
                }
            }
        }
    }
    // The sounds (set with the other knobs below): the choir on "aah" or "ooh", the brass's blare, the pad's gate.
    Rng snd;
    snd.seed(stream("sounds"));
    const float orchVowel = snd.uniform() < 0.6f ? 0.0f : 0.5f, orchBlare = 0.35f + 0.4f * snd.uniform();
    const bool gate = snd.uniform() < prof.m_gate;
    static const int GatePatterns[4] = { 6, 7, 8, 0 };
    const int gatePattern = GatePatterns[snd.below(4)];

    // ------------------------------------------------------------------------------------------ effects
    auto fx = [&](SfxTypeE t, double beat, double len, float v) {
        sc.m_notes.push_back(NoteEvent{ beat, len, PartE::FX, SfxBaseNote + static_cast<int>(t), v, 0, false, false });
    };
    // Per section, three draws whatever it is (the stream never shifts): the riser's length, a reverse crash into a
    // breakdown, a sweep into the groove; and whether the lead's delay is thrown into the breakdown (the automation's).
    Rng xr;
    xr.seed(stream("fx"));
    std::vector<char> throwAt(plan.m_sections.size(), 0);
    for (size_t si = 1; si < plan.m_sections.size(); ++si) {
        const Section& s = plan.m_sections[si];
        const Section& prevS = plan.m_sections[si - 1];
        const bool longRise = xr.uniform() < 0.65f, crashIn = xr.uniform() < 0.75f, sweep = xr.uniform() < 0.75f;
        throwAt[si] = xr.uniform() < 0.8f;
        if (s.m_kind == SectionKindE::DROP) {
            // The riser over the build (or the break's last eight bars) or its last four, the reverse crash into the
            // drop, the impact on it.
            const double rise = std::min(prevS.m_length, longRise ? 32.0 : 16.0);
            fx(SfxTypeE::RISER, s.m_beat - rise, rise, 0.9f);
            fx(SfxTypeE::REVERSE_CRASH, s.m_beat - 4.0, 4.0, 0.85f);
            fx(SfxTypeE::IMPACT, s.m_beat, 4.0, 1.0f);
        }
        if ((s.m_kind == SectionKindE::BREAKDOWN || s.m_kind == SectionKindE::BREAK) && prevS.m_kind == SectionKindE::DROP) {
            fx(SfxTypeE::DOWNLIFTER, s.m_beat, 8.0, 0.85f);
            fx(SfxTypeE::SUB_DROP, s.m_beat, 8.0, 0.9f);
            if (crashIn) fx(SfxTypeE::REVERSE_CRASH, s.m_beat - 4.0, 4.0, 0.7f);
        }
        if (s.m_kind == SectionKindE::GROOVE && prevS.m_kind == SectionKindE::INTRO && sweep) fx(SfxTypeE::SWEEP, s.m_beat - 16.0, 16.0, 0.6f);
    }
    // The intro's atmosphere (30.09.2026): one Atmosphere under the whole intro (a beatless opening and the DJ intro after
    // it are one span) -- it swells in, holds and leaves over its last quarter as the groove comes (Sfx.cpp) -- and a
    // reverse swell into some of its 16-bar lines. On a stream of its own beside the effects' ("ATMO"), so the draws above
    // stay what they were.
    {
        Rng ar;
        ar.seed(mixSeed(stream("fx"), 0x41544D4Full));   // "ATMO"
        double from = -1.0, to = -1.0;
        for (const Section& s : plan.m_sections) {
            if (s.m_kind != SectionKindE::INTRO) break;
            if (from < 0.0) from = s.m_beat;
            to = s.m_beat + s.m_length;
        }
        const float level = 0.35f + 0.15f * ar.uniform();   // (round 7: at 0.55 .. 0.75 the intros lay 3 to 5 dB over the references)
        if (to > from && plan.at(static_cast<int>(from / 4.0), LayerE::FX) != LayerStateE::OFF) {
            fx(SfxTypeE::ATMOSPHERE, from, to - from, level);
            for (double b = from + 64.0; b < to - 1e-9; b += 64.0)
                if (ar.uniform() < 0.6f) fx(SfxTypeE::REVERSE_SWELL, b - 8.0, 8.0, 0.45f);
        }
    }

    // ------------------------------------------------------------------------------------------ automation
    auto off = [&](int id, float target) { return p.toNormalised(id, target) - p.toNormalised(id, p.get(id)); };
    auto step = [&](int id, double beat, float to) { sc.m_gestures.push_back(Gesture{ id, beat, 0.0, 0.0f, to, GestureShapeE::STEP, 0 }); };
    auto ramp = [&](int id, double beat, double len, float from, float to, GestureShapeE shape = GestureShapeE::MINIMUM_JERK) {
        sc.m_gestures.push_back(Gesture{ id, beat, len, from, to, shape, 0 });
    };
    auto knob = [&](int id, float value) { sc.m_knobs.push_back(KnobSet{ 0.0, id, value, 1 }); };
    const int pad = static_cast<int>(PolyInstanceE::PAD);
    auto polyId = [&](PolyInstanceE i, int param) { return p.id(ModuleE::POLY, static_cast<int>(i), param); };

    // The sounds (Phase 5b, Presets.h; unit `sounds`): a factory preset for every synth, by the profile's styles -- a
    // lane of the kit by its role --, as a program change at the track's start (the SoundPick, the preset's knobs, its
    // level corrected by its trim); the style's own settings below come after and win where they meet.
    const bool pickSounds = p.getBool(p.id(ModuleE::COMPOSE, 0, compose::PICK_SOUNDS));
    float trimCloud = 0.0f;
    {
        Rng ps;
        ps.seed(mixSeed(stream("sounds"), 0x50524553ull));   // "PRES"
        struct Pick { ModuleE m_m; int m_instance; int m_level; };
        std::vector<Pick> picks = { { ModuleE::KICK, 0, kick::LEVEL }, { ModuleE::SUB, 0, sub::LEVEL }, { ModuleE::BASS, 0, synth::LEVEL },
                                    { ModuleE::ACID, 0, synth::LEVEL } };
        for (int i = 0; i < PolyInstances; ++i) picks.push_back({ ModuleE::POLY, i, poly::LEVEL });
        picks.push_back({ ModuleE::PIANO, 0, piano::LEVEL });
        picks.push_back({ ModuleE::STRINGS, 0, strings::LEVEL });
        picks.push_back({ ModuleE::CHOIR, 0, choir::LEVEL });
        picks.push_back({ ModuleE::BRASS, 0, brass::LEVEL });
        picks.push_back({ ModuleE::TIMPANI, 0, timpani::LEVEL });
        picks.push_back({ ModuleE::SFX, 0, sfx::LEVEL });
        picks.push_back({ ModuleE::CLOUD, 0, cloud::LEVEL });
        for (int l = 0; l < PercLanes; ++l) picks.push_back({ ModuleE::PERC, l, perc::LEVEL });
        for (const Pick& k : picks) {
            const int role = k.m_m == ModuleE::PERC ? p.getInt(p.id(ModuleE::PERC, k.m_instance, perc::ROLE)) : -1;
            const int index = pickPreset(k.m_m, k.m_instance, prof.m_mix.data(), role, ps, req.m_prefs);   // (drawn either way: the stream stays put)
            if (!pickSounds || index < 0) continue;
            const SoundPreset& sp = factoryPresets(k.m_m, k.m_m == ModuleE::PERC ? 0 : k.m_instance)[static_cast<size_t>(index)];
            sc.m_sounds.push_back(SoundPick{ 0.0, static_cast<int>(k.m_m), k.m_instance, index });
            const int base = p.base(k.m_m, k.m_instance);
            for (const auto& [knobIndex, value] : presetKnobs(k.m_m, k.m_instance, sp)) sc.m_knobs.push_back(KnobSet{ 0.0, base + knobIndex, value, 0 });
            if (k.m_m == ModuleE::CLOUD) { trimCloud = sp.m_trimDb; continue; }   // (its level is the style's, below)
            const int level = base + k.m_level;
            sc.m_knobs.push_back(KnobSet{ 0.0, level, std::clamp(p.get(level) + sp.m_trimDb, p.desc(level).m_minValue, p.desc(level).m_maxValue), 0 });
        }
    }

    // The track's own settings (knob sets at its start): the pump's depths, the gate, the hall.
    if (mc.m_piano) {
        // The piano (PLAN 5.8): Dok. 5's slightly dull piano most often, an upright or the grand otherwise; the pedal
        // down, lifted at every change of chord and pressed again an eighth later (legato pedalling: the old chord
        // does not ring into the new one, the new one rings on).
        Rng pr;
        pr.seed(stream("piano"));
        const float u = pr.uniform();
        if (!pickSounds) knob(p.id(ModuleE::PIANO, 0, piano::INSTRUMENT), u < 0.6f ? 3.0f : u < 0.85f ? 2.0f : 0.0f);
        const int pedal = p.id(ModuleE::PIANO, 0, piano::PEDAL);
        knob(pedal, 1.0f);
        // Its listening points closer (Phase 8): at 0.7 the piano, up front with its left hand, made the mix wider above
        // 200 Hz than the references (side against mid -3 .. -5 dB, theirs -6 .. -10).
        knob(p.id(ModuleE::PIANO, 0, piano::WIDTH), 0.4f);
        for (int bar = 1; bar < plan.m_bars; ++bar)
            if (harm.pc(bar, 0) != harm.pc(bar - 1, 0) || harm.pc(bar, 1) != harm.pc(bar - 1, 1)) {
                ramp(pedal, 4.0 * bar - 0.05, 0.04, 0.0f, -1.0f, GestureShapeE::LINEAR);
                ramp(pedal, 4.0 * bar + 0.5, 0.1, -1.0f, 0.0f, GestureShapeE::LINEAR);
            }
    }
    if (orchestra && !pickSounds) {
        knob(p.id(ModuleE::CHOIR, 0, choir::VOWEL), orchVowel);
        knob(p.id(ModuleE::BRASS, 0, brass::BRASSINESS), orchBlare);
    }
    if (prof.m_hatsDb != 0.0f) {
        const int hats = p.id(ModuleE::MIX, 0, mix::HATS_LEVEL);
        knob(hats, p.defaultValue(hats) + prof.m_hatsDb);
    }
    if (prof.m_tiltDb != 0.0f) {
        const int tilt = p.id(ModuleE::MASTER, 0, master::TILT);
        knob(tilt, p.defaultValue(tilt) + prof.m_tiltDb);
    }
    // The track's key on the knobs (the deck tunes the kick, the kit and the effects by them; without this they stayed
    // in the knob's key, A, whatever the track's was: found by Tools/eval_report.py on a set, 29.09.2026).
    knob(p.id(ModuleE::COMPOSE, 0, compose::KEY), static_cast<float>(key));
    knob(p.id(ModuleE::COMPOSE, 0, compose::SCALE), static_cast<float>(scale));
    knob(p.id(ModuleE::BASS, 0, synth::DUCK), prof.m_bassDuckDb);
    knob(p.id(ModuleE::ACID, 0, synth::DUCK), prof.m_bassDuckDb);
    knob(p.id(ModuleE::SUB, 0, sub::DUCK), prof.m_bassDuckDb + 3.0f);
    knob(polyId(PolyInstanceE::PAD, poly::DUCK), prof.m_padDuckDb);
    knob(polyId(PolyInstanceE::STAB, poly::DUCK), prof.m_padDuckDb);
    knob(polyId(PolyInstanceE::PAD, poly::GATE_PATTERN), static_cast<float>(gatePattern));
    if (prof.m_kickSoft > 0.0f && !pickSounds) {   // a softer, rounder kick (Dream House, Deep; the presets' groups weigh it in)
        knob(p.id(ModuleE::KICK, 0, kick::CLICK_LEVEL), 0.4f * (1.0f - prof.m_kickSoft));
        knob(p.id(ModuleE::KICK, 0, kick::TOP_LEVEL), -10.0f - 20.0f * prof.m_kickSoft);
        knob(p.id(ModuleE::KICK, 0, kick::AMP_DECAY), 320.0f - 120.0f * prof.m_kickSoft);
    }

    // Per block: the filtered cells, the pump, the hall, the fader.
    const int hallDecay = p.id(ModuleE::SENDS, 0, sends::HALL_DECAY);
    const int synthLevel = p.id(ModuleE::MIX, 0, mix::SYNTH_LEVEL);
    const int hatsCut = p.id(ModuleE::MIX, 0, mix::HATS_CUT);
    const struct { LayerE m_layer; int m_id; float m_filtered; } Filtered[] = {
        { LayerE::PLUCK, polyId(PolyInstanceE::PLUCK, poly::CUTOFF), -0.3f },
        { LayerE::LEAD, polyId(PolyInstanceE::LEAD, poly::CUTOFF), -0.35f },
        { LayerE::ARP, polyId(PolyInstanceE::ARP, poly::CUTOFF), -0.3f },
        { LayerE::ACID, p.id(ModuleE::ACID, 0, synth::CUTOFF), -0.25f },
        { LayerE::BASS, p.id(ModuleE::BASS, 0, synth::CUTOFF), -0.2f },
    };
    const float hatsDull = off(hatsCut, 3000.0f);
    for (size_t b = 0; b < plan.m_blocks.size(); ++b) {
        const LayerBlock& blk = plan.m_blocks[b];
        for (const auto& f : Filtered) step(f.m_id, blk.m_beat, blk.at(f.m_layer) == LayerStateE::FILTERED ? f.m_filtered : 0.0f);
        step(hatsCut, blk.m_beat, blk.at(LayerE::CLOSED_HAT) == LayerStateE::FILTERED ? hatsDull : 0.0f);
    }
    const int padCut = polyId(PolyInstanceE::PAD, poly::CUTOFF);
    const float hallBreak = off(hallDecay, prof.m_hallBreakS), hallDrop = off(hallDecay, prof.m_hallDropS);
    const float hallMid = off(hallDecay, std::sqrt(prof.m_hallBreakS * prof.m_hallDropS));
    // The pump's depths where the kick rests: to nothing, back over the two bars before it returns.
    const int pumped[] = { polyId(PolyInstanceE::PAD, poly::DUCK), polyId(PolyInstanceE::LEAD, poly::DUCK), polyId(PolyInstanceE::PLUCK, poly::DUCK),
                           polyId(PolyInstanceE::ARP, poly::DUCK), polyId(PolyInstanceE::STAB, poly::DUCK), polyId(PolyInstanceE::COUNTER, poly::DUCK),
                           p.id(ModuleE::PUMP, 0, pump::RETURN_DUCK), p.id(ModuleE::SFX, 0, sfx::DUCK) };
    for (size_t si = 0; si < plan.m_sections.size(); ++si) {
        const Section& s = plan.m_sections[si];
        const int bar = static_cast<int>(s.m_beat / 4.0);
        const bool kickHere = plan.at(bar, LayerE::KICK) == LayerStateE::ON;
        const bool breakdown = s.m_kind == SectionKindE::BREAKDOWN || s.m_kind == SectionKindE::BREAK;
        // The hall (Dok. 7: 2 to 4 s and more in the breakdown, 0.8 to 1.5 s in the drop).
        step(hallDecay, s.m_beat, breakdown ? hallBreak : s.m_kind == SectionKindE::DROP ? hallDrop : hallMid);
        // The pad's high pass: down to 40 Hz where the kick and the sub rest (a breakdown, a break), so its body and its
        // root there (A1 .. G#2) are heard; its floor again where they return.
        {
            const int padHp = polyId(PolyInstanceE::PAD, poly::HP_FLOOR);
            const bool open = (s.m_kind == SectionKindE::BREAKDOWN || s.m_kind == SectionKindE::BREAK) && plan.at(bar, LayerE::SUB) == LayerStateE::OFF;
            step(padHp, s.m_beat, open ? off(padHp, 40.0f) : 0.0f);
        }
        // The pad's filter: filtered in a groove (its cell says so), opening through a breakdown and its build -- and
        // through the intro (30.09.2026: the atmosphere; a beatless opening and the DJ intro after it one ramp, below).
        if (s.m_kind == SectionKindE::INTRO) { /* (the intro's ramp after this loop) */ }
        else if (s.m_kind == SectionKindE::BREAKDOWN) ramp(padCut, s.m_beat, s.m_length, padOpen(s.m_energyFrom), padOpen(s.m_energyTo));
        else if (s.m_kind == SectionKindE::BUILD) ramp(padCut, s.m_beat, s.m_length, padOpen(s.m_energyFrom), padOpen(s.m_energyTo), GestureShapeE::EASE_IN);
        else step(padCut, s.m_beat, plan.at(bar, LayerE::PAD) == LayerStateE::FILTERED ? -0.25f : 0.0f);
        // The fader: a breakdown sits under the drop (Dok. 6, rule 3, as measured: PLAN 13.4); the Leveler refines it.
        const float down = off(synthLevel, p.get(synthLevel) - 3.0f);
        if (breakdown) step(synthLevel, s.m_beat, down);
        else if (s.m_kind == SectionKindE::BUILD) ramp(synthLevel, s.m_beat, s.m_length, down, 0.0f, GestureShapeE::EASE_IN);
        else if (s.m_kind == SectionKindE::INTRO && prof.m_introSynthDb < 0.0f) {
            // The intro under the drop's synths (Phase 8: the sustained sound of the intros -- the pad, the atmosphere --
            // lay up to 8 dB over the references' in Deep, 3 in Progressive; StyleProfile::introSynthDb), rising over the
            // last intro's last eight bars to the groove.
            const float low = off(synthLevel, p.get(synthLevel) + prof.m_introSynthDb);
            const bool last = si + 1 >= plan.m_sections.size() || plan.m_sections[si + 1].m_kind != SectionKindE::INTRO;
            if (last && s.m_length > 32.0) {
                step(synthLevel, s.m_beat, low);
                ramp(synthLevel, s.m_beat + s.m_length - 32.0, 32.0, low, 0.0f, GestureShapeE::EASE_IN);
            } else if (last) {
                ramp(synthLevel, s.m_beat, s.m_length, low, 0.0f, GestureShapeE::EASE_IN);
            } else {
                step(synthLevel, s.m_beat, low);
            }
        }
        else step(synthLevel, s.m_beat, 0.0f);
        // The pump.
        if (!kickHere && !plan.m_beatless) {
            for (int id : pumped) step(id, s.m_beat, off(id, 0.0f) + (id == polyId(PolyInstanceE::PAD, poly::DUCK) ? 0.0f : 0.0f));
            // Back over the last two bars before the next section with a kick.
            const double end = s.m_beat + s.m_length;
            const int nextBar = static_cast<int>(end / 4.0);
            if (nextBar < plan.m_bars && plan.at(nextBar, LayerE::KICK) == LayerStateE::ON)
                for (int id : pumped) ramp(id, end - 8.0, 8.0, off(id, 0.0f), 0.0f, GestureShapeE::LINEAR);
        } else {
            for (int id : pumped) step(id, s.m_beat, 0.0f);
        }
        // The build: the group's high pass drawn up over its last four bars, released on the drop.
        if (s.m_kind == SectionKindE::BUILD || (s.m_kind == SectionKindE::BREAK && si + 1 < plan.m_sections.size()
                                             && plan.m_sections[si + 1].m_kind == SectionKindE::DROP)) {
            const int lowCut = p.id(ModuleE::MIX, 0, mix::LOW_CUT);
            const double end = s.m_beat + s.m_length;
            ramp(lowCut, end - 16.0, 16.0, 0.0f, off(lowCut, 220.0f), GestureShapeE::EASE_IN);
            step(lowCut, end, 0.0f);
        }
        // The trance gate on the pad in the drops.
        if (gate) step(polyId(PolyInstanceE::PAD, poly::GATE), s.m_beat, s.m_kind == SectionKindE::DROP ? 1.0f : 0.0f);
        // A delay throw on the lead before a breakdown (Dok. 7: "Delay-Throws als Uebergangseffekt").
        if (s.m_kind == SectionKindE::BREAKDOWN && si > 0 && plan.m_sections[si - 1].m_kind == SectionKindE::DROP && throwAt[si]) {
            const int fb = polyId(PolyInstanceE::LEAD, poly::DELAY_FEEDBACK), send = polyId(PolyInstanceE::LEAD, poly::DELAY_SEND);
            ramp(fb, s.m_beat - 4.0, 2.0, 0.0f, off(fb, 0.8f), GestureShapeE::EASE_IN);
            ramp(send, s.m_beat - 4.0, 2.0, 0.0f, off(send, 0.7f), GestureShapeE::EASE_IN);
            ramp(fb, s.m_beat + 8.0, 8.0, off(fb, 0.8f), 0.0f);
            ramp(send, s.m_beat + 8.0, 8.0, off(send, 0.7f), 0.0f);
        }
    }
    (void)pad;
    // The intro's pad: from dark to the groove's filtered colour over the whole intro span, easing in.
    {
        double from = -1.0, to = -1.0;
        for (const Section& s : plan.m_sections) {
            if (s.m_kind != SectionKindE::INTRO) break;
            if (from < 0.0) from = s.m_beat;
            to = s.m_beat + s.m_length;
        }
        if (to > from) ramp(padCut, from, to - from, -0.55f, -0.3f, GestureShapeE::EASE_IN);
    }
    // The 303's four curves (PLAN 5.3, Dok. 5): cutoff, resonance, envelope amount and decay rise over a phrase of 16,
    // 32 or 64 bars and jump back at its end; how far they rise follows the planner's energy, so the filter's peaks are
    // the track's (Dok. 6: "die Hoehepunkte eines Acid-Tracks sind die Filterpeaks"). A phrase never crosses a drop:
    // the drop starts one.
    {
        const int cut = p.id(ModuleE::ACID, 0, synth::CUTOFF), res = p.id(ModuleE::ACID, 0, synth::RESONANCE);
        const int env = p.id(ModuleE::ACID, 0, synth::ENV_AMOUNT), dec = p.id(ModuleE::ACID, 0, synth::DECAY);
        Rng ar;
        ar.seed(mixSeed(stream("acid"), 0x43555256ull));   // "CURV"
        static const int Phrase[3] = { 16, 32, 64 };
        const int phrase = Phrase[drawWeighted(std::array<float, 3>{ 0.4f, 0.4f, 0.2f }.data(), 3, ar.uniform())];
        std::vector<int> starts;
        for (const Section& s : plan.m_sections) starts.push_back(static_cast<int>(s.m_beat / 4.0));
        int bar = 0;
        while (bar < plan.m_bars) {
            int len = phrase;
            for (int s : starts) if (s > bar && s < bar + len) len = s - bar;   // up to the next section
            if (plan.at(bar, LayerE::ACID) != LayerStateE::OFF) {
                const float e = plan.energyAt(std::min(bar + len - 1, plan.m_bars - 1)) / 10.0f;
                const double beats = 4.0 * len;
                ramp(cut, 4.0 * bar, beats, -0.22f, 0.05f + 0.3f * e, GestureShapeE::EASE_IN);
                ramp(res, 4.0 * bar, beats, -0.05f, 0.05f + 0.17f * e, GestureShapeE::LINEAR);
                ramp(env, 4.0 * bar, beats, -0.1f, 0.1f + 0.2f * e, GestureShapeE::EASE_IN);
                ramp(dec, 4.0 * bar, beats, -0.1f, 0.15f * e, GestureShapeE::LINEAR);
            }
            bar += len;
        }
    }
    // Deep (PLAN 5.7): the pad is the instrument -- its filter and its place drift in slow waves of 8 or 16 bars, the
    // granular cloud grains its past (and the keys') into the plate.
    if (prof.m_lead == LeadKindE::PAD) {
        const int cut = polyId(PolyInstanceE::PAD, poly::CUTOFF), pan = polyId(PolyInstanceE::PAD, poly::PAN);
        Rng dr2;
        dr2.seed(mixSeed(stream("sounds"), 0x44524946ull));   // "DRIF"
        const int wave = dr2.uniform() < 0.5f ? 8 : 16;
        float lastCut = 0.0f, lastPan = 0.0f;
        // From the intro's end: the intro's pad is the atmosphere's, opening under the ramp above (30.09.2026: the drift
        // from bar 1 opened it at once, and Deep's intros lay 9 dB over the references' sustained sound).
        int introEnd = 0;
        for (const Section& s : plan.m_sections) { if (s.m_kind != SectionKindE::INTRO) break; introEnd = static_cast<int>((s.m_beat + s.m_length) / 4.0); }
        for (int bar = introEnd; bar < plan.m_bars; bar += wave) {
            const float c = 0.12f * dr2.bipolar(), q = 0.25f * dr2.bipolar();
            ramp(cut, 4.0 * bar, 4.0 * wave, lastCut, c);
            ramp(pan, 4.0 * bar, 4.0 * wave, lastPan, q);
            lastCut = c;
            lastPan = q;
        }
        knob(p.id(ModuleE::CLOUD, 0, cloud::LEVEL), -15.0f + trimCloud);   // (at -8 dB the correlation fell to 0.4, Tools/calibrate.py)
        const float size = 220.0f + 200.0f * dr2.uniform(), density = 8.0f + 10.0f * dr2.uniform();
        if (!pickSounds) {
            knob(p.id(ModuleE::CLOUD, 0, cloud::SIZE), size);
            knob(p.id(ModuleE::CLOUD, 0, cloud::DENSITY), density);
        }
    } else if (mc.m_piano) {
        knob(p.id(ModuleE::CLOUD, 0, cloud::LEVEL), -16.0f + trimCloud);   // a breath of it under the piano (Dream House)
    }

    // The loudness mark: the main drop.
    LevelMark lm;
    lm.m_beat = 0.0;
    lm.m_peakBeat = plan.m_mainDrop >= 0 ? plan.m_sections[static_cast<size_t>(plan.m_mainDrop)].m_beat : 0.0;
    lm.m_targetLufs = prof.m_targetLufs;
    lm.m_gapLu = prof.m_gapLu;
    lm.m_windowDb = 6.0f * prof.m_kickSoft;
    sc.m_levels.push_back(lm);
    sc.sort();

    if (info != nullptr) {
        info->m_style = prof.m_name;
        info->m_form = plan.m_form;
        info->m_bpm = bpm;
        info->m_key = key;
        info->m_scale = scale;
        info->m_bars = plan.m_bars;
        info->m_mainDropBar = plan.m_mainDrop >= 0 ? static_cast<int>(plan.m_sections[static_cast<size_t>(plan.m_mainDrop)].m_beat / 4.0) : -1;
        info->m_breakdownBar = plan.m_mainBreakdown >= 0 ? static_cast<int>(plan.m_sections[static_cast<size_t>(plan.m_mainBreakdown)].m_beat / 4.0) : -1;
        info->m_firstLeadBar = plan.m_firstLeadBar;
        info->m_keyChangeBar = harm.m_changeBar;
        info->m_progression = ProgressionNames[harm.m_progression];
        info->m_bass = BassPatternNames[static_cast<int>(bassPat)];
        info->m_camelot = camelotLabel(key, scale == static_cast<int>(ScaleE::IONIAN));
        info->m_beatless = plan.m_beatless;
        // Where a DJ needs to know: the intro's end, the bass's entry, the outro, every breakdown and break.
        info->m_introBars = 0;
        for (const Section& s : plan.m_sections) { if (s.m_kind != SectionKindE::INTRO) break; info->m_introBars += static_cast<int>(s.m_length / 4.0); }
        info->m_bassBar = 0;
        while (info->m_bassBar < plan.m_bars && plan.at(info->m_bassBar, LayerE::SUB) == LayerStateE::OFF) ++info->m_bassBar;
        info->m_outroBar = plan.m_bars;
        for (const Section& s : plan.m_sections) if (s.m_kind == SectionKindE::OUTRO) { info->m_outroBar = static_cast<int>(s.m_beat / 4.0); break; }
        info->m_drops.clear();
        for (const Section& s : plan.m_sections) if (s.m_kind == SectionKindE::DROP) info->m_drops.push_back(static_cast<int>(s.m_beat / 4.0));
        info->m_breakdowns.clear();
        for (const Section& s : plan.m_sections)
            if (s.m_kind == SectionKindE::BREAKDOWN || s.m_kind == SectionKindE::BREAK)
                info->m_breakdowns.push_back({ static_cast<int>(s.m_beat / 4.0), static_cast<int>((s.m_beat + s.m_length) / 4.0) });
    }
    (void)&isDrop;
    return sc;
}

} // namespace parh
