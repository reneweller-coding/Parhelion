/**
 * @file Composer.cpp
 * @brief The composer (Composer.h): plan, harmony, parts, effects, automation.
 */
#include "parh/compose/Composer.h"
#include "parh/compose/Harmony.h"
#include "parh/compose/Melody.h"
#include "parh/Dsp.h"
#include "parh/synth/Sfx.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace parh {

const char* const kUnitNames[8] = { "form", "harmony", "drums", "bass", "chords", "melody", "fx", "mix" };

std::string camelotLabel(int key, bool major)
{
    static const int kMinor[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
    static const int kMajor[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
    const int k = ((key % 12) + 12) % 12;
    return std::to_string(major ? kMajor[k] : kMinor[k]) + (major ? "B" : "A");
}

namespace {

// The kit's lanes (Params.cpp, kDefaultKit).
constexpr Part kCh = Part::Perc1, kOh = Part::Perc2, kClap = Part::Perc3, kSnare = Part::Perc4, kRide = Part::Perc5,
               kCrash = Part::Perc6, kShaker = Part::Perc7, kTamb = Part::Perc8, kConga = Part::Perc9, kTom = Part::Perc10;

/** @brief The seed of a unit: the track's seed, the unit's name and its rerolls (SetFile.h). */
uint64_t unitSeed(uint64_t seed, const std::string& unit, const std::string& name, const Curation* cur)
{
    uint64_t h = 1469598103934665603ull;
    for (char ch : name) { h ^= static_cast<uint8_t>(ch); h *= 1099511628211ull; }
    const int n = cur != nullptr ? cur->count(unit + name) : 0;
    return mixSeed(mixSeed(seed, h), static_cast<uint64_t>(n));
}

bool isDrop(const Plan& p, int bar) { const int s = p.sectionAt(bar); return s >= 0 && p.sections[static_cast<size_t>(s)].kind == SectionKind::Drop; }
bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

} // namespace

Score composeTrack(const ParamStore& p, uint64_t seed, const TrackRequest& req, const Curation* cur, const std::string& unit, TrackInfo* info)
{
    const StyleProfile prof = req.profile != nullptr ? *req.profile : profileOf(p);
    auto stream = [&](const char* name) { return unitSeed(seed, unit, name, cur); };
    const bool autoKnobs = p.getBool(p.id(Module::Compose, 0, compose::Auto));

    // Tempo, key, length.
    Rng hr;
    hr.seed(stream("harmony"));
    float bpm = req.bpm > 0.0f ? req.bpm : p.get(p.id(Module::Compose, 0, compose::Bpm));
    if (req.bpm <= 0.0f && autoKnobs) bpm = std::round((prof.bpmLow + (prof.bpmHigh - prof.bpmLow) * hr.uniform()) * 2.0f) * 0.5f;
    int key = p.getInt(p.id(Module::Compose, 0, compose::Key)), scale = p.getInt(p.id(Module::Compose, 0, compose::Scale));
    if (autoKnobs) drawKey(prof, hr, key, scale);
    if (req.key >= 0) key = req.key;
    if (req.scale >= 0) scale = req.scale;
    float minutes = p.get(p.id(Module::Compose, 0, compose::Minutes));
    Rng fr;
    fr.seed(stream("form"));
    if (autoKnobs) minutes = prof.minutesLow + (prof.minutesHigh - prof.minutesLow) * fr.uniform();
    const int askBars = req.bars > 0 ? req.bars : std::max(64, static_cast<int>(std::lround(minutes * bpm / 4.0)));

    const Plan plan = planTrack(prof, askBars, stream("form"));
    const Harmony harm = composeHarmony(plan, prof, key, scale, stream("harmony"));

    Score sc;
    sc.clear(bpm);
    sc.seed = seed;
    sc.keyRoot = key;
    sc.scale = scale;
    sc.lengthBeats = 4.0 * plan.bars;
    sc.sections = plan.sections;
    sc.layers = plan.blocks;
    for (const Section& s : plan.sections) sc.markers.push_back(Marker{ s.beat, kSectionNames[static_cast<int>(s.kind)] });

    Rng velRng;
    velRng.seed(stream("mix") ^ 0x56454Cull);
    MelodyContext mc;
    mc.plan = &plan;
    mc.harmony = &harm;
    mc.score = &sc;
    mc.humanize = p.get(p.id(Module::Compose, 0, compose::Humanize)) * 0.01f;
    mc.vel = &velRng;
    mc.piano = prof.lead == LeadKind::Piano;
    mc.anthemShare = mc.piano ? 1.0f : 0.7f;
    auto note = [&](double beat, double len, Part part, int pitch, float v, int shift = 0, bool accent = false, bool slide = false) {
        mc.note(beat, len, part, pitch, v, shift, accent, slide);
    };

    // ------------------------------------------------------------------------------------------ drums
    Rng dr;
    dr.seed(stream("drums"));
    const bool hats16 = dr.uniform() < prof.hats16;
    const bool shakerLoop = dr.uniform() < prof.shaker;
    for (int bar = 0; bar < plan.bars; ++bar) {
        const double b0 = 4.0 * bar;
        const bool kick = plan.at(bar, Layer::Kick) == LayerState::On && !contains(plan.miniBreaks, bar);
        const LayerState ch = plan.at(bar, Layer::ClosedHat), oh = plan.at(bar, Layer::OpenHat);
        const int sec = plan.sectionAt(bar);
        const SectionKind kind = sec >= 0 ? plan.sections[static_cast<size_t>(sec)].kind : SectionKind::Groove;
        for (int q = 0; q < 4; ++q) {
            if (!plan.beatless) note(b0 + q, 0.25, Part::Ghost, 36, 1.0f);
            if (kick) note(b0 + q, 0.25, Part::Kick, 36, 1.0f);
            if (plan.at(bar, Layer::Clap) == LayerState::On && (q == 1 || q == 3)) note(b0 + q, 0.25, kClap, 39, 0.9f);
            if (plan.at(bar, Layer::Ride) == LayerState::On) {
                note(b0 + q, 0.25, kRide, 51, 0.65f);
                note(b0 + q + 0.5, 0.25, kRide, 51, 0.5f);
            }
            if (oh != LayerState::Off) note(b0 + q + 0.5, 0.25, kOh, 46, 0.85f);
            if (ch != LayerState::Off) {
                static const float kAcc16[4] = { 0.55f, 0.35f, 0.8f, 0.4f };
                for (int s = 0; s < (hats16 ? 4 : 2); ++s) {
                    const int step = hats16 ? s : 2 * s;
                    if (step == 2 && oh != LayerState::Off) continue;   // the open hat has the off-beat
                    note(b0 + q + 0.25 * step, 0.1, kCh, 42, hats16 ? kAcc16[step] : (step == 0 ? 0.5f : 0.8f));
                }
            }
            if (plan.at(bar, Layer::Perc) == LayerState::On && kind != SectionKind::Build) {
                if (shakerLoop) for (int s = 0; s < 4; ++s) note(b0 + q + 0.25 * s, 0.1, kShaker, 70, s == 2 ? 0.7f : 0.4f);
                else {
                    static const int kCongaSteps[16] = { 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0 };
                    for (int s = 0; s < 4; ++s) if (kCongaSteps[q * 4 + s]) note(b0 + q + 0.25 * s, 0.2, kConga, 63, 0.5f + 0.2f * (s == 0));
                    note(b0 + q + 0.5, 0.2, kTamb, 54, 0.45f);
                }
            }
        }
        if (plan.at(bar, Layer::Crash) == LayerState::On && bar % 8 == 0) note(b0, 1.0, kCrash, 49, 0.9f);
    }
    // A crash on every drop's first beat; a tom fill in the last bar before a groove after a break (Dok. 3).
    for (const Section& s : plan.sections) if (s.kind == SectionKind::Drop) note(s.beat, 1.0, kCrash, 49, 1.0f);
    // The snare rolls: the build's last bars and a break's last four before a drop, quarters to thirty-seconds.
    for (size_t si = 0; si + 1 < plan.sections.size(); ++si) {
        const Section& s = plan.sections[si];
        const Section& next = plan.sections[si + 1];
        if (next.kind != SectionKind::Drop || plan.beatless) continue;
        if (s.kind != SectionKind::Build && s.kind != SectionKind::Break) continue;
        const int endBar = static_cast<int>((s.beat + s.length) / 4.0);
        const int bars = std::min(s.kind == SectionKind::Build ? 8 : 4, static_cast<int>(s.length / 4.0));
        const int start = endBar - bars;
        for (int bar = start; bar < endBar; ++bar) {
            const double pos0 = static_cast<double>(bar - start) / bars;
            const double step = pos0 < 0.25 ? 1.0 : pos0 < 0.5 ? 0.5 : pos0 < 0.8 ? 0.25 : 0.125;
            for (double t = 0.0; t < 4.0 - 1e-9; t += step) {
                const double pos = (bar - start + t / 4.0) / bars;
                note(4.0 * bar + t, 0.1, kSnare, 38, static_cast<float>(0.35 + 0.65 * pos), static_cast<int>(std::lround(12.0 * pos)));
            }
        }
    }
    (void)kTom;

    // ------------------------------------------------------------------------------------------ bass
    Rng br;
    br.seed(stream("bass"));
    const BassPattern bassPat = static_cast<BassPattern>(plan.bassPattern);   // the planner's, which cast the 303
    writeAcid(mc, stream("bass") ^ 0x333033ull);
    for (int bar = 0; bar < plan.bars; ++bar) {
        const double b0 = 4.0 * bar;
        const int rootPc = harm.pc(bar, 0);
        const int sub = atOrAbove(rootPc, 28), mid = sub + 12;
        const bool subOn = plan.at(bar, Layer::Sub) != LayerState::Off;
        const bool bassOn = plan.at(bar, Layer::Bass) != LayerState::Off;
        for (int q = 0; q < 4; ++q) {
            const double beat = b0 + q;
            switch (bassPat) {
            case BassPattern::Offbeat:
            case BassPattern::Acid:
                if (subOn) note(beat + 0.5, 0.22, Part::Sub, sub, 1.0f);
                if (bassOn) note(beat + 0.5, 0.22, Part::Bass, mid, 0.9f);
                break;
            case BassPattern::Rolling:
                if (subOn) note(beat + 0.5, 0.22, Part::Sub, sub, 1.0f);
                if (bassOn) for (int s = 1; s < 4; ++s) note(beat + 0.25 * s, 0.18, Part::Bass, mid, s == 2 ? 0.95f : 0.8f);
                break;
            case BassPattern::Gallop:
                if (subOn) note(beat + 0.5, 0.22, Part::Sub, sub, 1.0f);
                if (bassOn) { note(beat + 0.5, 0.18, Part::Bass, mid, 0.9f); note(beat + 0.75, 0.18, Part::Bass, mid, 0.8f); }
                break;
            case BassPattern::Walking: {
                // Longer notes with syncopes on the "and" of 2 and 4 (Dok. 3), the fifth and the octave on the way.
                static const double kAt[4] = { 0.5, 0.5, 0.5, 0.5 };
                const int tone = q == 1 ? 7 : q == 3 ? 12 : 0;
                const double len = (q == 1 || q == 3) ? 0.45 : 0.3;
                if (subOn) note(beat + kAt[q], len, Part::Sub, sub + (tone == 12 ? 0 : tone == 7 ? 0 : 0), 1.0f);
                if (bassOn) note(beat + kAt[q], len, Part::Bass, mid + tone, q == 1 || q == 3 ? 0.95f : 0.8f);
                break;
            }
            case BassPattern::Drone:
                if (q == 0 && subOn) note(b0, 3.9, Part::Sub, sub, 0.9f);
                if (bassOn && (q == 0 || q == 2)) note(beat + 0.5, 1.2, Part::Bass, mid, 0.7f);
                break;
            default: break;
            }
        }
    }

    // ------------------------------------------------------------------------------------------ chords and melody
    Rng cr;
    cr.seed(stream("chords"));
    {
        std::array<int, 4> prev{ 60, 64, 67, 72 };
        bool first = true;
        int bar = 0;
        while (bar < plan.bars) {
            if (plan.at(bar, Layer::Pad) == LayerState::Off) { first = true; ++bar; continue; }
            // Hold the chord while it stays and the pad plays (at most two bars a note, so the gate and the pump breathe).
            int len = 1;
            while (bar + len < plan.bars && len < 4 && plan.at(bar + len, Layer::Pad) != LayerState::Off
                   && harm.pc(bar + len, 0) == harm.pc(bar, 0) && harm.pc(bar + len, 1) == harm.pc(bar, 1) && (bar + len) % 8 != 0)
                ++len;
            const std::array<int, 4> v = voicePad(harm, bar, prev, first);
            first = false;
            prev = v;
            for (int k = 0; k < 4; ++k) note(4.0 * bar, 4.0 * len - 0.05, Part::Pad, v[static_cast<size_t>(k)], 0.75f);
            bar += len;
        }
    }
    writePluck(mc, stream("chords") ^ 0x504C55ull);
    writeStab(mc, stream("chords") ^ 0x535441ull);
    writeArp(mc, stream("melody") ^ 0x415250ull, prof.bpmHigh < 125.0f);
    writeLead(mc, stream("melody"));
    writeCounter(mc, stream("melody") ^ 0x434F55ull);


    // ------------------------------------------------------------------------------------------ the orchestra
    // Cinematic (PLAN 5.9, 6.6): the strings and the choir carry the main breakdown's full phrase -- chords from the
    // tease on, the violins with the melody at the peak -- and the main drop; brass (a braam) and timpani mark every
    // drop, the timpani roll into it over the build's last two bars.
    Rng orr;
    orr.seed(stream("chords") ^ 0x4F524348ull);   // "ORCH"
    const bool orchestra = orr.uniform() < prof.orchestra;
    if (info != nullptr) info->orchestra = orchestra;
    if (orchestra) {
        const int mbIndex = plan.mainBreakdown;
        const BreakdownParts* main = nullptr;
        if (mbIndex >= 0)
            for (const BreakdownParts& b : plan.breakdowns)
                if (static_cast<double>(b.start) * 4.0 == plan.sections[static_cast<size_t>(mbIndex)].beat) main = &b;
        const Section* mainDrop = plan.mainDrop >= 0 ? &plan.sections[static_cast<size_t>(plan.mainDrop)] : nullptr;
        // Held chords from bar a to bar b (a note each chord, at most four bars), voiced from the pad's voicing.
        auto chords = [&](Part part, int a, int b, float vel, int shift, bool bassNote) {
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
            chords(Part::Strings, main->tease, main->end, 0.6f, 0, true);
            chords(Part::Choir, main->peak, main->end, 0.7f, 0, false);
            // The violins with the melody at the peak (an octave up where it lies low).
            std::vector<NoteEvent> melody;
            for (const NoteEvent& n : sc.notes)
                if ((n.part == Part::Lead || n.part == Part::Piano) && n.beat >= 4.0 * main->peak && n.beat < 4.0 * main->end) melody.push_back(n);
            for (const NoteEvent& n : melody) note(n.beat, std::max(n.length, 0.3), Part::Strings, n.pitch < 67 ? n.pitch + 12 : n.pitch, 0.7f);
            // A soft stroke on the timpani where the peak begins.
            note(4.0 * main->peak, 1.0, Part::Timpani, atOrAbove(harm.pc(main->peak, 0), 40), 0.5f);
        }
        if (mainDrop != nullptr) {
            const int a = static_cast<int>(mainDrop->beat / 4.0), b = static_cast<int>((mainDrop->beat + mainDrop->length) / 4.0);
            chords(Part::Strings, a, b, 0.75f, 12, true);
            chords(Part::Choir, a + (b - a) / 2, b, 0.75f, 0, false);
        }
        for (size_t si = 1; si < plan.sections.size(); ++si) {
            const Section& s = plan.sections[si];
            if (s.kind != SectionKind::Drop) continue;
            const int bar = static_cast<int>(s.beat / 4.0);
            // The braam: the root low, its fifth and octave, loud and blaring; the timpani on the root.
            const int root = atOrAbove(harm.pc(bar, 0), 33);
            for (int iv : { 0, 7, 12 }) note(s.beat, 1.6, Part::Brass, root + iv, 0.95f);
            note(s.beat, 1.0, Part::Timpani, atOrAbove(harm.pc(bar, 0), 40), 1.0f);
            // The roll: the build's (or the break's) last two bars, sixteenths then thirty-seconds, growing.
            const Section& before = plan.sections[si - 1];
            if (before.kind == SectionKind::Build || before.kind == SectionKind::Break) {
                const int rollPitch = atOrAbove(harm.pc(bar - 1, 0), 40);
                for (double t = s.beat - 8.0; t < s.beat - 1e-9; ) {
                    const double pos = (t - (s.beat - 8.0)) / 8.0;
                    const double step = pos < 0.5 ? 0.25 : 0.125;
                    note(t, step * 0.9, Part::Timpani, rollPitch, static_cast<float>(0.3 + 0.6 * pos));
                    t += step;
                }
            }
        }
    }
    // The orchestra's sound (set with the other knobs below): the choir on "aah" or "ooh", the brass's blare.
    const float orchVowel = orr.uniform() < 0.6f ? 0.0f : 0.5f, orchBlare = 0.35f + 0.4f * orr.uniform();

    // ------------------------------------------------------------------------------------------ effects
    auto fx = [&](SfxType t, double beat, double len, float v) {
        sc.notes.push_back(NoteEvent{ beat, len, Part::Fx, kSfxBaseNote + static_cast<int>(t), v, 0, false, false });
    };
    for (size_t si = 1; si < plan.sections.size(); ++si) {
        const Section& s = plan.sections[si];
        const Section& prevS = plan.sections[si - 1];
        if (s.kind == SectionKind::Drop) {
            // The riser over the build (or the break's last eight bars), the reverse crash into the drop, the impact on it.
            const double rise = std::min(prevS.length, 32.0);
            fx(SfxType::Riser, s.beat - rise, rise, 0.9f);
            fx(SfxType::ReverseCrash, s.beat - 4.0, 4.0, 0.85f);
            fx(SfxType::Impact, s.beat, 4.0, 1.0f);
        }
        if ((s.kind == SectionKind::Breakdown || s.kind == SectionKind::Break) && prevS.kind == SectionKind::Drop) {
            fx(SfxType::Downlifter, s.beat, 8.0, 0.85f);
            fx(SfxType::SubDrop, s.beat, 8.0, 0.9f);
            fx(SfxType::ReverseCrash, s.beat - 4.0, 4.0, 0.7f);
        }
        if (s.kind == SectionKind::Groove && prevS.kind == SectionKind::Intro) fx(SfxType::Sweep, s.beat - 16.0, 16.0, 0.6f);
    }

    // ------------------------------------------------------------------------------------------ automation
    auto off = [&](int id, float target) { return p.toNormalised(id, target) - p.toNormalised(id, p.get(id)); };
    auto step = [&](int id, double beat, float to) { sc.gestures.push_back(Gesture{ id, beat, 0.0, 0.0f, to, GestureShape::Step, 0 }); };
    auto ramp = [&](int id, double beat, double len, float from, float to, GestureShape shape = GestureShape::MinimumJerk) {
        sc.gestures.push_back(Gesture{ id, beat, len, from, to, shape, 0 });
    };
    auto knob = [&](int id, float value) { sc.knobs.push_back(KnobSet{ 0.0, id, value, 1 }); };
    const int pad = static_cast<int>(PolyInstance::Pad);
    auto polyId = [&](PolyInstance i, int param) { return p.id(Module::Poly, static_cast<int>(i), param); };

    // The track's own settings (knob sets at its start): the pump's depths, the gate, the hall.
    Rng mr;
    mr.seed(stream("mix"));
    if (mc.piano) {
        // The piano (PLAN 5.8): Dok. 5's slightly dull piano most often, an upright or the grand otherwise; the pedal
        // down, lifted at every change of chord and pressed again an eighth later (legato pedalling: the old chord
        // does not ring into the new one, the new one rings on).
        const float u = mr.uniform();
        knob(p.id(Module::Piano, 0, piano::Instrument), u < 0.6f ? 3.0f : u < 0.85f ? 2.0f : 0.0f);
        const int pedal = p.id(Module::Piano, 0, piano::Pedal);
        knob(pedal, 1.0f);
        for (int bar = 1; bar < plan.bars; ++bar)
            if (harm.pc(bar, 0) != harm.pc(bar - 1, 0) || harm.pc(bar, 1) != harm.pc(bar - 1, 1)) {
                ramp(pedal, 4.0 * bar - 0.05, 0.04, 0.0f, -1.0f, GestureShape::Linear);
                ramp(pedal, 4.0 * bar + 0.5, 0.1, -1.0f, 0.0f, GestureShape::Linear);
            }
    }
    if (orchestra) {
        knob(p.id(Module::Choir, 0, choir::Vowel), orchVowel);
        knob(p.id(Module::Brass, 0, brass::Brassiness), orchBlare);
    }
    knob(p.id(Module::Bass, 0, synth::Duck), prof.bassDuckDb);
    knob(p.id(Module::Acid, 0, synth::Duck), prof.bassDuckDb);
    knob(p.id(Module::Sub, 0, sub::Duck), prof.bassDuckDb + 3.0f);
    knob(polyId(PolyInstance::Pad, poly::Duck), prof.padDuckDb);
    knob(polyId(PolyInstance::Stab, poly::Duck), prof.padDuckDb);
    const bool gate = mr.uniform() < prof.gate;
    static const int kGatePatterns[4] = { 6, 7, 8, 0 };
    knob(polyId(PolyInstance::Pad, poly::GatePattern), static_cast<float>(kGatePatterns[mr.below(4)]));
    if (prof.kickSoft > 0.0f) {   // a softer, rounder kick (Dream House, Deep)
        knob(p.id(Module::Kick, 0, kick::ClickLevel), 0.4f * (1.0f - prof.kickSoft));
        knob(p.id(Module::Kick, 0, kick::TopLevel), -10.0f - 20.0f * prof.kickSoft);
        knob(p.id(Module::Kick, 0, kick::AmpDecay), 320.0f - 120.0f * prof.kickSoft);
    }

    // Per block: the filtered cells, the pump, the hall, the fader.
    const int hallDecay = p.id(Module::Sends, 0, sends::HallDecay);
    const int synthLevel = p.id(Module::Mix, 0, mix::SynthLevel);
    const int hatsCut = p.id(Module::Mix, 0, mix::HatsCut);
    const struct { Layer layer; int id; float filtered; } kFiltered[] = {
        { Layer::Pluck, polyId(PolyInstance::Pluck, poly::Cutoff), -0.3f },
        { Layer::Lead, polyId(PolyInstance::Lead, poly::Cutoff), -0.35f },
        { Layer::Arp, polyId(PolyInstance::Arp, poly::Cutoff), -0.3f },
        { Layer::Acid, p.id(Module::Acid, 0, synth::Cutoff), -0.25f },
        { Layer::Bass, p.id(Module::Bass, 0, synth::Cutoff), -0.2f },
    };
    const float hatsDull = off(hatsCut, 3000.0f);
    for (size_t b = 0; b < plan.blocks.size(); ++b) {
        const LayerBlock& blk = plan.blocks[b];
        for (const auto& f : kFiltered) step(f.id, blk.beat, blk.at(f.layer) == LayerState::Filtered ? f.filtered : 0.0f);
        step(hatsCut, blk.beat, blk.at(Layer::ClosedHat) == LayerState::Filtered ? hatsDull : 0.0f);
    }
    const int padCut = polyId(PolyInstance::Pad, poly::Cutoff);
    const float hallBreak = off(hallDecay, prof.hallBreakS), hallDrop = off(hallDecay, prof.hallDropS);
    const float hallMid = off(hallDecay, std::sqrt(prof.hallBreakS * prof.hallDropS));
    // The pump's depths where the kick rests: to nothing, back over the two bars before it returns.
    const int pumped[] = { polyId(PolyInstance::Pad, poly::Duck), polyId(PolyInstance::Lead, poly::Duck), polyId(PolyInstance::Pluck, poly::Duck),
                           polyId(PolyInstance::Arp, poly::Duck), polyId(PolyInstance::Stab, poly::Duck), polyId(PolyInstance::Counter, poly::Duck),
                           p.id(Module::Pump, 0, pump::ReturnDuck), p.id(Module::Sfx, 0, sfx::Duck) };
    for (size_t si = 0; si < plan.sections.size(); ++si) {
        const Section& s = plan.sections[si];
        const int bar = static_cast<int>(s.beat / 4.0);
        const bool kickHere = plan.at(bar, Layer::Kick) == LayerState::On;
        const bool breakdown = s.kind == SectionKind::Breakdown || s.kind == SectionKind::Break;
        // The hall (Dok. 7: 2 to 4 s and more in the breakdown, 0.8 to 1.5 s in the drop).
        step(hallDecay, s.beat, breakdown ? hallBreak : s.kind == SectionKind::Drop ? hallDrop : hallMid);
        // The pad's filter: filtered in a groove (its cell says so), opening through a breakdown and its build.
        if (s.kind == SectionKind::Breakdown) ramp(padCut, s.beat, s.length, -0.3f, 0.1f);
        else if (s.kind == SectionKind::Build) ramp(padCut, s.beat, s.length, 0.1f, 0.2f, GestureShape::EaseIn);
        else step(padCut, s.beat, plan.at(bar, Layer::Pad) == LayerState::Filtered ? -0.25f : 0.0f);
        // The fader: a breakdown sits under the drop (Dok. 6, rule 3, as measured: PLAN 13.4); the Leveler refines it.
        const float down = off(synthLevel, p.get(synthLevel) - 3.0f);
        if (breakdown) step(synthLevel, s.beat, down);
        else if (s.kind == SectionKind::Build) ramp(synthLevel, s.beat, s.length, down, 0.0f, GestureShape::EaseIn);
        else step(synthLevel, s.beat, 0.0f);
        // The pump.
        if (!kickHere && !plan.beatless) {
            for (int id : pumped) step(id, s.beat, off(id, 0.0f) + (id == polyId(PolyInstance::Pad, poly::Duck) ? 0.0f : 0.0f));
            // Back over the last two bars before the next section with a kick.
            const double end = s.beat + s.length;
            const int nextBar = static_cast<int>(end / 4.0);
            if (nextBar < plan.bars && plan.at(nextBar, Layer::Kick) == LayerState::On)
                for (int id : pumped) ramp(id, end - 8.0, 8.0, off(id, 0.0f), 0.0f, GestureShape::Linear);
        } else {
            for (int id : pumped) step(id, s.beat, 0.0f);
        }
        // The build: the group's high pass drawn up over its last four bars, released on the drop.
        if (s.kind == SectionKind::Build || (s.kind == SectionKind::Break && si + 1 < plan.sections.size()
                                             && plan.sections[si + 1].kind == SectionKind::Drop)) {
            const int lowCut = p.id(Module::Mix, 0, mix::LowCut);
            const double end = s.beat + s.length;
            ramp(lowCut, end - 16.0, 16.0, 0.0f, off(lowCut, 220.0f), GestureShape::EaseIn);
            step(lowCut, end, 0.0f);
        }
        // The trance gate on the pad in the drops.
        if (gate) step(polyId(PolyInstance::Pad, poly::Gate), s.beat, s.kind == SectionKind::Drop ? 1.0f : 0.0f);
        // A delay throw on the lead before a breakdown (Dok. 7: "Delay-Throws als Uebergangseffekt").
        if (s.kind == SectionKind::Breakdown && si > 0 && plan.sections[si - 1].kind == SectionKind::Drop) {
            const int fb = polyId(PolyInstance::Lead, poly::DelayFeedback), send = polyId(PolyInstance::Lead, poly::DelaySend);
            ramp(fb, s.beat - 4.0, 2.0, 0.0f, off(fb, 0.8f), GestureShape::EaseIn);
            ramp(send, s.beat - 4.0, 2.0, 0.0f, off(send, 0.7f), GestureShape::EaseIn);
            ramp(fb, s.beat + 8.0, 8.0, off(fb, 0.8f), 0.0f);
            ramp(send, s.beat + 8.0, 8.0, off(send, 0.7f), 0.0f);
        }
    }
    (void)pad;
    // The 303's filter over sixteen bars at a time, falling back on the phrase line (Dok. 5).
    {
        const int acidCut = p.id(Module::Acid, 0, synth::Cutoff), acidRes = p.id(Module::Acid, 0, synth::Resonance);
        for (int bar = 0; bar < plan.bars; bar += 16) {
            if (plan.at(bar, Layer::Acid) == LayerState::Off) continue;
            const float peak = 0.12f + 0.2f * (plan.energyAt(bar) / 10.0f);
            ramp(acidCut, 4.0 * bar, 64.0, -0.2f, peak, GestureShape::EaseIn);
            ramp(acidRes, 4.0 * bar, 64.0, 0.0f, 0.12f, GestureShape::Linear);
        }
    }

    // The loudness mark: the main drop.
    LevelMark lm;
    lm.beat = 0.0;
    lm.peakBeat = plan.mainDrop >= 0 ? plan.sections[static_cast<size_t>(plan.mainDrop)].beat : 0.0;
    lm.targetLufs = prof.targetLufs;
    lm.gapLu = prof.gapLu;
    sc.levels.push_back(lm);
    sc.sort();

    if (info != nullptr) {
        info->style = prof.name;
        info->form = plan.form;
        info->bpm = bpm;
        info->key = key;
        info->scale = scale;
        info->bars = plan.bars;
        info->mainDropBar = plan.mainDrop >= 0 ? static_cast<int>(plan.sections[static_cast<size_t>(plan.mainDrop)].beat / 4.0) : -1;
        info->breakdownBar = plan.mainBreakdown >= 0 ? static_cast<int>(plan.sections[static_cast<size_t>(plan.mainBreakdown)].beat / 4.0) : -1;
        info->firstLeadBar = plan.firstLeadBar;
        info->keyChangeBar = harm.changeBar;
        info->progression = kProgressionNames[harm.progression];
        info->bass = kBassPatternNames[static_cast<int>(bassPat)];
        info->camelot = camelotLabel(key, scale == static_cast<int>(Scale::Ionian));
        info->beatless = plan.beatless;
    }
    (void)&isDrop;
    return sc;
}

} // namespace parh
