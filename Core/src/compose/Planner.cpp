/**
 * @file Planner.cpp
 * @brief The planner (Planner.h): form grammar, lengths, the layer matrix, the energy.
 */
#include "parh/compose/Planner.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {

/** @brief What a section does in the drama (finer than SectionKind; the layer rules read it). */
enum class Role : int { AmbientIntro, Intro, Groove, ShortBreak, Drop1, Breakdown, Build, MainDrop, Break2, FinalDrop, KickPause,
                        Plateau, Outro, AmbientOutro };

struct Spec { Role role; int bars; };

SectionKind kindOf(Role r)
{
    switch (r) {
    case Role::AmbientIntro: case Role::Intro: return SectionKind::Intro;
    case Role::Groove: case Role::Plateau: return SectionKind::Groove;
    case Role::ShortBreak: case Role::Break2: case Role::KickPause: return SectionKind::Break;
    case Role::Breakdown: return SectionKind::Breakdown;
    case Role::Build: return SectionKind::Build;
    case Role::Drop1: case Role::MainDrop: case Role::FinalDrop: return SectionKind::Drop;
    case Role::Outro: case Role::AmbientOutro: return SectionKind::Outro;
    }
    return SectionKind::Groove;
}

/** @brief The energy at a role's start and end (Dok. 6, table). */
void energyOf(Role r, float& from, float& to)
{
    switch (r) {
    case Role::AmbientIntro: from = 1.0f; to = 2.0f; return;
    case Role::Intro:        from = 2.0f; to = 4.0f; return;
    case Role::Groove:       from = 5.0f; to = 5.5f; return;
    case Role::ShortBreak:   from = 3.0f; to = 6.0f; return;
    case Role::Drop1:        from = 8.0f; to = 8.0f; return;
    case Role::Breakdown:    from = 1.0f; to = 7.0f; return;
    case Role::Build:        from = 7.0f; to = 8.0f; return;
    case Role::MainDrop:     from = 10.0f; to = 10.0f; return;
    case Role::Break2:       from = 5.0f; to = 6.0f; return;
    case Role::FinalDrop:    from = 9.0f; to = 9.0f; return;
    case Role::KickPause:    from = 4.0f; to = 6.0f; return;
    case Role::Plateau:      from = 6.0f; to = 7.0f; return;
    case Role::Outro:        from = 6.0f; to = 2.0f; return;
    case Role::AmbientOutro: from = 2.0f; to = 1.0f; return;
    }
}

int pick(Rng& r, std::initializer_list<int> options)
{
    const int n = static_cast<int>(options.size());
    return *(options.begin() + r.below(n));
}

/** @brief The sections of a template, lengths drawn (Planner.h). */
std::vector<Spec> drawSpecs(const StyleProfile& prof, FormTemplate form, Rng& r, bool mixable, bool opener = false)
{
    std::vector<Spec> s;
    int intro = prof.introBars * (r.uniform() < prof.introLong ? 2 : 1);
    // (The draws in the same order as ever, taken or not: a track alone stays the track it was.)
    if (r.uniform() < prof.ambientIntro) {
        const int ambientBars = std::max(8, prof.ambientIntroBars + 8 * (r.below(3) - 1));
        // A mix's first track too (01.10.2026, the user: his mix "begann ... wieder mit einer Solo-Kick"): nothing is mixed
        // into its beginning. The references open without the kick in 15 of 30 (Progressive 5 of 6, Dream House 5 of 6,
        // Deep 3 of 5, Acid 2 of 6, Uplifting 1 of 7; the kick alone in 1).
        if (!mixable || opener) s.push_back({ Role::AmbientIntro, ambientBars });
    }
    if (mixable) intro = std::max(intro, 32);
    s.push_back({ Role::Intro, intro });
    switch (form) {
    case FormTemplate::Anthem:
        s.push_back({ Role::Groove, 32 });
        s.push_back({ Role::ShortBreak, pick(r, { 16, 16, 32 }) });
        s.push_back({ Role::Drop1, 32 });
        s.push_back({ Role::Breakdown, pick(r, { 32, 32, 48, 64 }) });
        s.push_back({ Role::Build, pick(r, { 8, 8, 16 }) });
        s.push_back({ Role::MainDrop, pick(r, { 32, 64, 64 }) });
        if (r.uniform() < 0.5f) {
            s.push_back({ Role::Break2, 16 });
            s.push_back({ Role::FinalDrop, 32 });
        }
        break;
    case FormTemplate::Dream:
        s.push_back({ Role::Groove, 32 });
        s.push_back({ Role::Breakdown, pick(r, { 16, 32 }) });
        s.push_back({ Role::Build, 8 });
        s.push_back({ Role::MainDrop, 32 });
        if (r.uniform() < 0.6f) {
            s.push_back({ Role::Break2, 16 });
            s.push_back({ Role::FinalDrop, 32 });
        }
        break;
    case FormTemplate::Acid: {
        s.push_back({ Role::Groove, 32 });
        const int pauses = 1 + r.below(3);
        for (int i = 0; i < pauses; ++i) {
            s.push_back({ Role::KickPause, 8 });
            s.push_back({ Role::Groove, pick(r, { 24, 32, 32 }) });
        }
        s.push_back({ Role::MainDrop, pick(r, { 32, 64 }) });
        break;
    }
    case FormTemplate::Plateau: {
        s.push_back({ Role::Groove, 32 });
        const int plateaus = 1 + r.below(2);
        for (int i = 0; i < plateaus; ++i) {
            s.push_back({ Role::ShortBreak, pick(r, { 8, 16 }) });
            s.push_back({ Role::Plateau, 32 });
        }
        s.push_back({ Role::Breakdown, pick(r, { 24, 32 }) });   // (16 measured 15 bars, under the references' 16 to 32)
        s.push_back({ Role::Build, 8 });
        s.push_back({ Role::MainDrop, pick(r, { 32, 48 }) });
        break;
    }
    case FormTemplate::Drift:
    default:
        // (Tools/calibrate.py: the references' breakdowns are 11 to 47 bars and their tracks near nine minutes, so the
        // length lies in a longer groove, a plateau and the drop.)
        s.push_back({ Role::Groove, pick(r, { 32, 48 }) });
        if (r.uniform() < 0.6f) {
            s.push_back({ Role::ShortBreak, 16 });
            s.push_back({ Role::Plateau, 32 });
        }
        s.push_back({ Role::Breakdown, pick(r, { 32, 32, 40 }) });   // (48 over the references' 11 to 47)
        s.push_back({ Role::Build, 8 });
        s.push_back({ Role::MainDrop, pick(r, { 32, 48, 64 }) });
        break;
    }
    const int outro = prof.outroBars * (r.uniform() < 0.5f ? 2 : 1);
    s.push_back({ Role::Outro, mixable ? std::max(outro, 32) : outro });
    if (r.uniform() < prof.ambientOutro) {
        const int ambientOutBars = pick(r, { 8, 16, 16, 24 });
        if (!mixable) s.push_back({ Role::AmbientOutro, ambientOutBars });
    }
    return s;
}

int totalBars(const std::vector<Spec>& s)
{
    int n = 0;
    for (const Spec& x : s) n += x.bars;
    return n;
}

/**
 * @brief Brings the total to a multiple of 32 bars: the outro gives or takes first (16 to 64 bars), then the first groove
 *        takes what is left -- never the intro, which would keep the DJ waiting (the first version lengthened it and a
 *        Progressive track ran 104 bars before its groove).
 */
void fitTo32(std::vector<Spec>& s, int minOutro)
{
    for (int guard = 0; guard < 8 && totalBars(s) % 32 != 0; ++guard) {
        const int over = totalBars(s) % 32;   // 8, 16 or 24
        Spec* outro = nullptr;
        Spec* groove = nullptr;
        for (Spec& x : s) {
            if (x.role == Role::Outro) outro = &x;
            if ((x.role == Role::Groove || x.role == Role::Plateau) && groove == nullptr) groove = &x;
        }
        if (outro != nullptr && outro->bars - over >= minOutro) outro->bars -= over;
        else if (outro != nullptr && outro->bars + (32 - over) <= 64) outro->bars += 32 - over;
        else if (groove != nullptr) groove->bars += 32 - over;
        else break;
    }
}

/** @brief The voices a track has at all (drawn once, from the profile). */
struct Cast {
    bool acidBass = false;    ///< the 303 carries the bass (and the melody)
    bool lead = true;         ///< a lead voice plays the melody (the supersaw, or the piano's stand-in)
    bool pluck = true, arp = false, stab = false, counter = false, ride = true, perc = false;
    bool beatless = false;
    bool bassInBreaks = false;   ///< Dream House: the bass never leaves (measured, PLAN 13.4)
};

std::array<LayerState, kNumLayers> none() { return std::array<LayerState, kNumLayers>{}; }
void set(std::array<LayerState, kNumLayers>& a, Layer l, LayerState s) { a[static_cast<size_t>(l)] = s; }
LayerState get(const std::array<LayerState, kNumLayers>& a, Layer l) { return a[static_cast<size_t>(l)]; }

/** @brief The rhythm section: kick, the low end, the hats, the clap, the percussion. */
void rhythm(std::array<LayerState, kNumLayers>& a, const Cast& c, bool withLow = true)
{
    if (!c.beatless) set(a, Layer::Kick, LayerState::On);
    if (withLow) {
        set(a, Layer::Sub, LayerState::On);
        set(a, c.acidBass ? Layer::Acid : Layer::Bass, LayerState::On);
    }
    set(a, Layer::ClosedHat, LayerState::On);
    set(a, Layer::OpenHat, LayerState::On);
    set(a, Layer::Clap, LayerState::On);
}

/**
 * @brief A section's own variations (its stream, `section<n>`): when the voices that vary enter, the intro's order, the
 *        mini-break. Every field is drawn for every section, whatever its role, so a stream never shifts; none touches the
 *        kick or the low end (the breakdown share stays the form's).
 */
struct Scatter {
    int arpFrom = 1;          ///< the groove's block where the arp enters (1 or 2)
    int stabFrom = 2;         ///< the groove's block where the stab enters (2 or 3)
    int percFrom = 0;         ///< the groove's block where the percussion enters (0 or 1)
    int dropArpLate = 0;      ///< a drop's arp a block later (0 or 1)
    int dropRideLate = 0;     ///< the first drop's ride a block later (0 or 1)
    bool counterEarly = false;   ///< the main drop's counter from its first quarter, not its half
    int stabParity = 1;       ///< the main drop's stab on the odd blocks (1) or the even ones (0)
    bool introSwap = false;   ///< the intro's percussion before its open hat
    bool counterInPeak = true;   ///< the counter in the breakdown's peak
    bool arpInPeak = true;       ///< the arp (filtered) in the breakdown's peak
    int outroPercCut = 2;     ///< the outro's percussion leaves this many blocks before its end (2 or 3)
    bool miniBreak = false;   ///< a mini-break (a long groove or the first drop)
    int miniBreakBar = 15;    ///< its bar in the section (15 or 7)
    int vacuum = 0;           ///< a drop's vacuum before it: the last beat, two beats, the bar, the kick alone on 4
};

Scatter drawScatter(Rng& r)
{
    Scatter s;
    s.arpFrom = r.uniform() < 0.6f ? 1 : 2;
    s.stabFrom = r.uniform() < 0.6f ? 2 : 3;
    s.percFrom = r.uniform() < 0.7f ? 0 : 1;
    s.dropArpLate = r.uniform() < 0.35f ? 1 : 0;
    s.dropRideLate = r.uniform() < 0.35f ? 1 : 0;
    s.counterEarly = r.uniform() < 0.35f;
    s.stabParity = r.uniform() < 0.6f ? 1 : 0;
    s.introSwap = r.uniform() < 0.4f;
    s.counterInPeak = r.uniform() < 0.7f;
    s.arpInPeak = r.uniform() < 0.6f;
    s.outroPercCut = r.uniform() < 0.6f ? 2 : 3;
    s.miniBreak = r.uniform() < 0.6f;
    s.miniBreakBar = r.uniform() < 0.7f ? 15 : 7;
    const float uv = r.uniform();
    s.vacuum = uv < 0.35f ? 0 : uv < 0.6f ? 1 : uv < 0.8f ? 2 : 3;
    return s;
}

/** @brief The layer states of block @p k of @p n in a section of role @p role. */
std::array<LayerState, kNumLayers> blockStates(Role role, int k, int n, const Cast& c, const BreakdownParts* bd, int bar, const Scatter& v)
{
    using S = LayerState;
    auto a = none();
    const Layer bassLayer = c.acidBass ? Layer::Acid : Layer::Bass;
    switch (role) {
    case Role::AmbientIntro:
        // The pad filtered, opening over the intro (the composer's ramp), and the atmosphere (30.09.2026: open, the pad
        // of a beatless opening lay 7 dB over the references' sustained sound, measured as analyze_ref.py's intro_sus).
        set(a, Layer::Pad, S::Filtered);
        set(a, Layer::Fx, S::On);
        if (c.lead && k >= n / 2 && n >= 2) set(a, Layer::Lead, S::Filtered);   // the piano's motif hinted (Dream House)
        else if (c.arp) set(a, Layer::Arp, S::Filtered);
        break;
    case Role::Intro: {
        // The atmosphere over it all (30.09.2026, the user: "Fangen typische Trance-Songs nicht eher mit einer Atmosphaere
        // oder einem Pad an?"): the pad filtered from the first bar, opening over the intro, and the effects' atmosphere
        // -- the references' intros carry sustained sound 7 to 12 dB under their drop's from the first bar on (intro_sus
        // in analyze_ref.py), Parhelion's had none. In the intro's second half the arp's hint as well.
        set(a, Layer::Pad, S::Filtered);
        set(a, Layer::Fx, S::On);
        if (c.arp && n >= 2 && k >= n / 2) set(a, Layer::Arp, S::Filtered);
        // Dok. 6: kick alone, hats, bass, open hat, percussion, the filtered pluck; spread over the intro's blocks (the
        // percussion now and then before the open hat: PLAN 6.3's "Streuung"). The kick never alone (01.10.2026, measured
        // on the references' first eight bars: where the kick plays, the hats play with it, 2 to 13 dB under the drop's;
        // the kick alone in 1 of 30): the closed hat from the first bar.
        Layer order[] = { Layer::Kick, Layer::ClosedHat, bassLayer, Layer::OpenHat, Layer::Perc, Layer::Pluck };
        if (v.introSwap && c.perc) std::swap(order[3], order[4]);
        const int steps = 6;
        const int upto = n <= 1 ? 2 : std::max(2, std::min(steps, 1 + (k * (steps - 1) + (n - 2)) / std::max(1, n - 1)));
        for (int i = 0; i < upto; ++i) {
            const Layer l = order[i];
            if (l == Layer::Kick && c.beatless) continue;
            if (l == Layer::Perc && !c.perc) continue;
            if (l == Layer::Pluck) { set(a, c.pluck ? Layer::Pluck : Layer::Arp, S::Filtered); continue; }
            if (l == bassLayer) { set(a, Layer::Sub, S::On); set(a, bassLayer, c.acidBass ? S::Filtered : S::On); continue; }
            set(a, l, S::On);
        }
        break;
    }
    case Role::Groove:
    case Role::Plateau:
        rhythm(a, c);
        if (c.perc && k >= v.percFrom) set(a, Layer::Perc, S::On);
        if (c.pluck) set(a, Layer::Pluck, S::On);
        set(a, Layer::Pad, role == Role::Plateau ? S::On : S::Filtered);
        if (c.arp && (k >= v.arpFrom || role == Role::Plateau)) set(a, Layer::Arp, S::On);
        if (c.stab && k >= v.stabFrom) set(a, Layer::Stab, S::On);
        if (k == 0 && bar > 0) set(a, Layer::Crash, S::On);
        break;
    case Role::ShortBreak:
    case Role::Break2:
        set(a, Layer::Pad, S::On);
        if (c.bassInBreaks) { set(a, Layer::Sub, S::On); set(a, Layer::Bass, S::On); }
        if (c.lead) set(a, Layer::Lead, S::Filtered);   // the melody's fragment (Dok. 6: "Pad + Melodie-Fragment")
        if (c.pluck) set(a, Layer::Pluck, S::Filtered);
        if (c.acidBass) set(a, Layer::Acid, S::Filtered);
        break;
    case Role::KickPause:
        set(a, Layer::Sub, S::On);
        set(a, bassLayer, S::On);
        set(a, Layer::ClosedHat, S::Filtered);
        set(a, Layer::Pad, S::Filtered);
        break;
    case Role::Drop1:
    case Role::MainDrop:
    case Role::FinalDrop: {
        rhythm(a, c);
        if (c.perc) set(a, Layer::Perc, S::On);
        if (c.pluck) set(a, Layer::Pluck, S::On);
        set(a, Layer::Pad, S::On);
        if (c.lead) set(a, Layer::Lead, S::On);
        if (k == 0) set(a, Layer::Crash, S::On);
        // A variation every block (Dok. 6: "Variation nach 16 Takten"): the ride, the arp, the counter, the stab.
        const bool main = role != Role::Drop1;
        if (c.ride && (main || k >= 1 + v.dropRideLate)) set(a, Layer::Ride, S::On);
        if (c.arp && (role == Role::FinalDrop || k >= (main ? 1 : 2) + v.dropArpLate)) set(a, Layer::Arp, S::On);
        if (c.counter && main && k >= (v.counterEarly ? std::max(1, n / 4) : n / 2)) set(a, Layer::Counter, S::On);
        // The stab: the offbeat chords of the drops (30.09.2026: it sounded in 0 to 5 % of the blocks) -- the first drop
        // on every other block, the main drop from its second block on, the final drop throughout.
        if (c.stab && (role == Role::FinalDrop || (main && k >= 1) || (!main && k % 2 == v.stabParity))) set(a, Layer::Stab, S::On);
        if (!c.lead && c.acidBass) set(a, Layer::Acid, S::On);
        break;
    }
    case Role::Breakdown: {
        set(a, Layer::Pad, S::On);
        if (c.bassInBreaks) { set(a, Layer::Sub, S::On); set(a, Layer::Bass, S::On); }
        const int part = bd == nullptr ? 2 : (bar < bd->tease ? 0 : bar < bd->peak ? 1 : 2);
        if (part >= 1) {
            if (c.lead) set(a, Layer::Lead, part == 1 ? S::Filtered : S::On);
            if (c.pluck) set(a, Layer::Pluck, S::Filtered);
            if (c.acidBass) set(a, Layer::Acid, S::Filtered);
            if (!c.lead && c.arp) set(a, Layer::Arp, part == 1 ? S::Filtered : S::On);
        }
        if (part == 2) {
            if (c.counter && v.counterInPeak) set(a, Layer::Counter, S::On);
            if (c.arp && (v.arpInPeak || !c.lead)) set(a, Layer::Arp, S::Filtered);
        }
        break;
    }
    case Role::Build:
        // Dok. 6: the rebuild -- the kick back, the snare roll, the riser; the lead teased, the pad on.
        if (!c.beatless) set(a, Layer::Kick, S::On);
        set(a, Layer::ClosedHat, S::Filtered);
        set(a, Layer::Pad, S::On);
        if (c.lead) set(a, Layer::Lead, S::Filtered);
        if (c.pluck) set(a, Layer::Pluck, S::On);
        if (c.acidBass) set(a, Layer::Acid, S::Filtered);
        set(a, Layer::Perc, S::On);   // the snare roll lives in the percussion row
        set(a, Layer::Fx, S::On);
        break;
    case Role::Outro: {
        // The intro reversed: the melody ghosts through the first 16 bars filtered, then the rhythm alone thins out.
        rhythm(a, c, k < n - 1 || n == 1);
        if (k < 2 && c.lead) set(a, Layer::Lead, S::Filtered);
        if (k < 2 && c.pluck) set(a, Layer::Pluck, k == 0 ? S::On : S::Filtered);
        if (k == 0) set(a, Layer::Pad, S::Filtered);
        if (c.perc && k < n - v.outroPercCut) set(a, Layer::Perc, S::On);
        if (k >= n - 2) set(a, Layer::Clap, S::Off);
        if (k == n - 1) set(a, Layer::OpenHat, S::Off);
        break;
    }
    case Role::AmbientOutro:
        set(a, Layer::Pad, S::Filtered);
        break;
    }
    return a;
}

/** @brief The whole plan from a list of sections; the matrix's and the energy's streams from @p stream. */
Plan buildPlan(const StyleProfile& prof, FormTemplate form, const std::vector<Spec>& specs, const Cast& cast, const UnitStream& stream)
{
    Plan p;
    p.form = form;
    p.beatless = cast.beatless;
    int bar = 0;
    std::vector<Role> roles;
    // The energy: the table's, scattered by half a point (the main drop stays the peak), rising through a breakdown and
    // its build.
    Rng er;
    er.seed(stream("energy"));
    for (const Spec& s : specs) {
        Section sec;
        sec.beat = 4.0 * bar;
        sec.length = 4.0 * s.bars;
        sec.kind = kindOf(s.role);
        energyOf(s.role, sec.energyFrom, sec.energyTo);
        const float jf = 0.5f * er.bipolar(), jt = 0.5f * er.bipolar();
        if (s.role != Role::MainDrop) {
            sec.energyFrom = std::clamp(sec.energyFrom + jf, 0.0f, 9.5f);
            sec.energyTo = std::clamp(sec.energyTo + jt, 0.0f, 9.5f);
        }
        if (s.role == Role::Breakdown || s.role == Role::Build) sec.energyTo = std::max(sec.energyTo, sec.energyFrom);
        if (s.role == Role::Build && !p.sections.empty()) {
            sec.energyFrom = std::max(sec.energyFrom, p.sections.back().energyTo);
            sec.energyTo = std::max(sec.energyTo, sec.energyFrom);
        }
        if (s.role == Role::MainDrop) p.mainDrop = static_cast<int>(p.sections.size());
        if (s.role == Role::Breakdown && (p.mainBreakdown < 0 || s.bars >= static_cast<int>(p.sections[static_cast<size_t>(p.mainBreakdown)].length / 4.0)))
            p.mainBreakdown = static_cast<int>(p.sections.size());
        if (s.role == Role::Breakdown) {
            // Intro, tease, peak (Dok. 6): a quarter, a quarter, a half, on 8-bar lines.
            BreakdownParts bd;
            bd.start = bar;
            const int q = std::max(8, (s.bars / 4) / 8 * 8);
            bd.tease = bar + q;
            bd.peak = std::min(bar + s.bars, bd.tease + q);
            bd.end = bar + s.bars;
            p.breakdowns.push_back(bd);
        }
        p.sections.push_back(sec);
        roles.push_back(s.role);
        bar += s.bars;
    }
    p.bars = bar;
    // The layer matrix: every section's variations on its own stream.
    const uint64_t matrixSeed = stream("matrix");
    std::vector<Scatter> scatter;
    for (size_t i = 0; i < specs.size(); ++i) {
        Rng sr;
        sr.seed(mixSeed(matrixSeed, stream("section" + std::to_string(i + 1))));
        scatter.push_back(drawScatter(sr));
        // The vacuum before a drop (Phosphene's four).
        if (kindOf(roles[i]) == SectionKind::Drop) {
            static const double kFrom[4] = { 3.0, 2.0, 0.0, 3.0 };
            Vacuum v;
            v.bar = static_cast<int>(p.sections[i].beat / 4.0) - 1;
            v.from = kFrom[scatter.back().vacuum];
            v.kickOn4 = scatter.back().vacuum == 3;
            p.vacuums.push_back(v);
        }
    }
    for (size_t i = 0; i < specs.size(); ++i) {
        const int start = static_cast<int>(p.sections[i].beat / 4.0), n = specs[i].bars / 8;
        const BreakdownParts* bd = nullptr;
        for (const BreakdownParts& b : p.breakdowns) if (b.start == start) bd = &b;
        for (int k = 0; k < n; ++k) {
            LayerBlock lb;
            lb.beat = 4.0 * (start + 8 * k);
            lb.state = blockStates(roles[i], k, n, cast, bd, start + 8 * k, scatter[i]);
            const float t = (k + 0.5f) / static_cast<float>(n);
            lb.energy = p.sections[i].energyFrom + (p.sections[i].energyTo - p.sections[i].energyFrom) * t;
            p.blocks.push_back(lb);
        }
    }
    // Rule 1: every block changes something. Where two blocks are the same, the percussion (or the arp) enters or leaves.
    for (size_t b = 1; b < p.blocks.size(); ++b) {
        if (p.blocks[b].state != p.blocks[b - 1].state) continue;
        auto& st = p.blocks[b].state;
        const bool rhythmOn = get(st, Layer::ClosedHat) != LayerState::Off;
        const Layer l = rhythmOn ? Layer::Perc : (cast.arp ? Layer::Arp : Layer::Pluck);
        set(st, l, get(st, l) == LayerState::Off ? (rhythmOn ? LayerState::On : LayerState::Filtered) : LayerState::Off);
    }
    // The lead's first entry; mini-breaks in the long grooves and the first drop (Dok. 6: "Kick raus fuer 1 Takt").
    for (size_t b = 0; b < p.blocks.size(); ++b)
        if (get(p.blocks[b].state, Layer::Lead) != LayerState::Off) { p.firstLeadBar = static_cast<int>(p.blocks[b].beat / 4.0); break; }
    for (size_t i = 0; i < specs.size(); ++i) {
        const int start = static_cast<int>(p.sections[i].beat / 4.0);
        if ((roles[i] == Role::Groove || roles[i] == Role::Drop1) && specs[i].bars >= 32 && !cast.beatless && scatter[i].miniBreak)
            p.miniBreaks.push_back(start + scatter[i].miniBreakBar);
    }
    (void)prof;
    return p;
}

} // namespace

int Plan::sectionAt(int bar) const
{
    for (size_t i = 0; i < sections.size(); ++i)
        if (bar * 4.0 >= sections[i].beat && bar * 4.0 < sections[i].beat + sections[i].length) return static_cast<int>(i);
    return -1;
}

LayerState Plan::at(int bar, Layer l) const
{
    const int b = bar / 8;
    if (b < 0 || b >= static_cast<int>(blocks.size())) return LayerState::Off;
    return blocks[static_cast<size_t>(b)].state[static_cast<size_t>(l)];
}

float Plan::energyAt(int bar) const
{
    const int i = sectionAt(bar);
    if (i < 0) return 0.0f;
    const Section& s = sections[static_cast<size_t>(i)];
    const float t = static_cast<float>((bar * 4.0 - s.beat) / std::max(4.0, s.length));
    return s.energyFrom + (s.energyTo - s.energyFrom) * t;
}

float breakdownShare(const Plan& plan)
{
    // As the reference measurement counts (PLAN 13.4): bars without a kick and without a bass, between the first and the
    // last bar that has one.
    auto low = [&](const LayerBlock& b) {
        return b.at(Layer::Kick) != LayerState::Off || b.at(Layer::Sub) != LayerState::Off || b.at(Layer::Bass) != LayerState::Off
            || b.at(Layer::Acid) != LayerState::Off;
    };
    int first = -1, last = -1;
    for (size_t i = 0; i < plan.blocks.size(); ++i) if (low(plan.blocks[i])) { if (first < 0) first = static_cast<int>(i); last = static_cast<int>(i); }
    if (first < 0) return 0.0f;
    int n = 0;
    for (int i = first; i <= last; ++i) if (!low(plan.blocks[static_cast<size_t>(i)])) ++n;
    return plan.bars > 0 ? static_cast<float>(8.0 * n / plan.bars) : 0.0f;
}

uint64_t unitSeed(uint64_t seed, const std::string& name, int rerolls)
{
    uint64_t h = 1469598103934665603ull;
    for (char ch : name) { h ^= static_cast<uint8_t>(ch); h *= 1099511628211ull; }
    return mixSeed(mixSeed(seed, h), static_cast<uint64_t>(rerolls));
}

Plan planTrack(const StyleProfile& prof, int bars, uint64_t seed, bool mixable)
{
    return planTrack(prof, bars, [seed](const std::string& name) { return unitSeed(seed, name); }, mixable);
}

namespace {

/** @brief The specs cut at @p bar and a breakdown (build, drop) or a drop at once after it, then the outro. */
void rewriteSpecs(std::vector<Spec>& s, int bar, SectionKind kind)
{
    std::vector<Spec> out;
    bool mainBefore = false;
    int at = 0;
    for (const Spec& x : s) {
        if (at + x.bars <= bar) {
            out.push_back(x);
            mainBefore = mainBefore || x.role == Role::MainDrop;
            at += x.bars;
            continue;
        }
        if (bar - at >= 8) out.push_back({ x.role, bar - at });   // (the section it cuts, up to the line)
        break;
    }
    if (kind == SectionKind::Breakdown) {
        out.push_back({ mainBefore ? Role::Break2 : Role::Breakdown, mainBefore ? 16 : 32 });
        out.push_back({ Role::Build, 8 });
    }
    out.push_back({ mainBefore ? Role::FinalDrop : Role::MainDrop, 32 });
    out.push_back({ Role::Outro, 32 });
    s = out;
}

} // namespace

Plan planTrack(const StyleProfile& prof, int bars, const UnitStream& stream, bool mixable)
{
    return planTrackRewritten(prof, bars, stream, mixable, std::vector<Rewrite>{});
}

Plan planTrackRewritten(const StyleProfile& prof, int bars, const UnitStream& stream, bool mixable, int rewriteBar, SectionKind rewriteKind)
{
    std::vector<Rewrite> one;
    if (rewriteBar >= 0) one.push_back({ rewriteBar, rewriteKind });
    return planTrackRewritten(prof, bars, stream, mixable, one);
}

Plan planTrackRewritten(const StyleProfile& prof, int bars, const UnitStream& stream, bool mixable, const std::vector<Rewrite>& rewrites,
                        bool opener)
{
    const uint64_t formSeed = stream("form");
    Rng r;
    r.seed(formSeed);
    const FormTemplate form = static_cast<FormTemplate>(drawWeighted(prof.forms.data(), kFormTemplates, r.uniform()));
    Cast cast;
    cast.beatless = r.uniform() < prof.beatless && !mixable;   // (drawn either way: the stream stays put)
    // The bass figure; the 303 carries the bass where it says so, and always where it is the profile's lead (Acid).
    Rng br;
    br.seed(stream("bass"));
    const int bassPattern = drawWeighted(prof.bass.data(), kBassPatterns, br.uniform());
    cast.acidBass = bassPattern == static_cast<int>(BassPattern::Acid) || prof.lead == LeadKind::Acid;
    cast.lead = prof.lead == LeadKind::Supersaw || prof.lead == LeadKind::Piano;
    // The cast: which voices the track has (the same for every candidate).
    Rng mr;
    mr.seed(mixSeed(stream("matrix"), 0x43415354ull));   // "CAST"
    const float uPluck = mr.uniform(), uArp = mr.uniform(), uStab = mr.uniform(), uCounter = mr.uniform();
    const float uRide = mr.uniform(), uPerc = mr.uniform();
    cast.pluck = uPluck < prof.pluck || prof.lead == LeadKind::PluckArp;
    cast.arp = uArp < prof.arp || prof.lead == LeadKind::PluckArp || prof.lead == LeadKind::Pad;
    cast.stab = uStab < prof.stab;
    cast.counter = cast.lead && uCounter < prof.counter;
    cast.ride = uRide < prof.ride;
    cast.perc = uPerc < prof.perc;
    cast.bassInBreaks = form == FormTemplate::Dream;
    // Candidates: the nearest to the length asked for, inside the profile's breakdown share.
    Plan best;
    double bestScore = 1e30;
    int bestC = 0;
    for (int c = 0; c < 8; ++c) {
        Rng cr;
        cr.seed(mixSeed(formSeed, 0x43414E44ull + static_cast<uint64_t>(c)));   // "CAND"
        std::vector<Spec> specs = drawSpecs(prof, form, cr, mixable, opener);
        fitTo32(specs, mixable ? 32 : 16);
        Plan p = buildPlan(prof, form, specs, cast, stream);
        const float share = breakdownShare(p);
        int introBars = 0;
        for (const Section& s : p.sections) { if (s.kind != SectionKind::Intro) break; introBars += static_cast<int>(s.length / 4.0); }
        const double score = std::fabs(static_cast<double>(p.bars - bars)) / 32.0
                           + 4.0 * std::max(0.0f, share - prof.breakdownHigh) + 4.0 * std::max(0.0f, prof.breakdownLow - share)
                           + 4.0 * std::max(0.0, static_cast<double>(introBars) / std::max(1, p.bars) - 0.3);
        if (score < bestScore) { bestScore = score; best = std::move(p); bestC = c; }
    }
    // A mix's first track opens on its atmosphere at least as often as not (01.10.2026: the beginning of the whole mix;
    // the candidates' search favours the shorter intros, so it is decided here, on a stream of its own).
    bool openOnAtmosphere = false;
    if (opener) {
        Rng orr;
        orr.seed(mixSeed(formSeed, 0x4F50454Eull));   // "OPEN"
        openOnAtmosphere = orr.uniform() < std::max(prof.ambientIntro, 0.6f);
    }
    const bool hasAmbient = !best.sections.empty() && best.sections.front().kind == SectionKind::Intro && best.sections.size() > 1
                         && best.sections[1].kind == SectionKind::Intro;
    if (!rewrites.empty() || (openOnAtmosphere && !hasAmbient)) {
        // The chosen candidate's specs again (the same draws), cut and continued at each line in turn (fitTo32 evens
        // the length out in the outro, after every line).
        Rng cr;
        cr.seed(mixSeed(formSeed, 0x43414E44ull + static_cast<uint64_t>(bestC)));
        std::vector<Spec> specs = drawSpecs(prof, form, cr, mixable, opener);
        if (openOnAtmosphere && (specs.empty() || specs.front().role != Role::AmbientIntro))
            specs.insert(specs.begin(), Spec{ Role::AmbientIntro, std::max(16, prof.ambientIntroBars / 8 * 8) });
        fitTo32(specs, mixable ? 32 : 16);
        int last = -1;
        for (const Rewrite& w : rewrites) {
            if (w.bar < 0 || w.bar <= last) continue;
            rewriteSpecs(specs, w.bar, w.kind);
            fitTo32(specs, mixable ? 32 : 16);
            last = w.bar;
        }
        best = buildPlan(prof, form, specs, cast, stream);
    }
    best.bassPattern = cast.acidBass ? static_cast<int>(BassPattern::Acid) : bassPattern;
    return best;
}

} // namespace parh
