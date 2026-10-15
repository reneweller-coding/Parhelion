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
enum class RoleE : int { AMBIENT_INTRO, INTRO, GROOVE, SHORT_BREAK, DROP1, BREAKDOWN, BUILD, MAIN_DROP, BREAK2, FINAL_DROP, KICK_PAUSE,
                        PLATEAU, OUTRO, AMBIENT_OUTRO };

/** @brief A section of a template: its role and its length. */
struct Spec {
    RoleE m_role;   ///< what it does
    int m_bars;    ///< how long, bars
};

/** @brief The kind of section role @p r is (SectionKind). */
SectionKindE kindOf(RoleE r)
{
    switch (r) {
    case RoleE::AMBIENT_INTRO: case RoleE::INTRO: return SectionKindE::INTRO;
    case RoleE::GROOVE: case RoleE::PLATEAU: return SectionKindE::GROOVE;
    case RoleE::SHORT_BREAK: case RoleE::BREAK2: case RoleE::KICK_PAUSE: return SectionKindE::BREAK;
    case RoleE::BREAKDOWN: return SectionKindE::BREAKDOWN;
    case RoleE::BUILD: return SectionKindE::BUILD;
    case RoleE::DROP1: case RoleE::MAIN_DROP: case RoleE::FINAL_DROP: return SectionKindE::DROP;
    case RoleE::OUTRO: case RoleE::AMBIENT_OUTRO: return SectionKindE::OUTRO;
    }
    return SectionKindE::GROOVE;
}

/** @brief The energy at a role's start and end (Dok. 6, table). */
void energyOf(RoleE r, float& from, float& to)
{
    switch (r) {
    case RoleE::AMBIENT_INTRO: from = 1.0f; to = 2.0f; return;
    case RoleE::INTRO:        from = 2.0f; to = 4.0f; return;
    case RoleE::GROOVE:       from = 5.0f; to = 5.5f; return;
    case RoleE::SHORT_BREAK:   from = 3.0f; to = 6.0f; return;
    case RoleE::DROP1:        from = 8.0f; to = 8.0f; return;
    case RoleE::BREAKDOWN:    from = 1.0f; to = 7.0f; return;
    case RoleE::BUILD:        from = 7.0f; to = 8.0f; return;
    case RoleE::MAIN_DROP:     from = 10.0f; to = 10.0f; return;
    case RoleE::BREAK2:       from = 5.0f; to = 6.0f; return;
    case RoleE::FINAL_DROP:    from = 9.0f; to = 9.0f; return;
    case RoleE::KICK_PAUSE:    from = 4.0f; to = 6.0f; return;
    case RoleE::PLATEAU:      from = 6.0f; to = 7.0f; return;
    case RoleE::OUTRO:        from = 6.0f; to = 2.0f; return;
    case RoleE::AMBIENT_OUTRO: from = 2.0f; to = 1.0f; return;
    }
}

/** @brief One of @p options, drawn from @p r. */
int pick(Rng& r, std::initializer_list<int> options)
{
    const int n = static_cast<int>(options.size());
    return *(options.begin() + r.below(n));
}

/** @brief The sections of a template, lengths drawn (Planner.h). */
std::vector<Spec> drawSpecs(const StyleProfile& prof, FormTemplateE form, Rng& r, bool mixable, bool opener = false)
{
    std::vector<Spec> s;
    int intro = prof.m_introBars * (r.uniform() < prof.m_introLong ? 2 : 1);
    // (The draws in the same order as ever, taken or not: a track alone stays the track it was.)
    if (r.uniform() < prof.m_ambientIntro) {
        const int ambientBars = std::max(8, prof.m_ambientIntroBars + 8 * (r.below(3) - 1));
        // A mix's first track too (01.10.2026, the user: his mix "begann ... wieder mit einer Solo-Kick"): nothing is mixed
        // into its beginning. The references open without the kick in 15 of 30 (Progressive 5 of 6, Dream House 5 of 6,
        // Deep 3 of 5, Acid 2 of 6, Uplifting 1 of 7; the kick alone in 1).
        if (!mixable || opener) s.push_back({ RoleE::AMBIENT_INTRO, ambientBars });
    }
    if (mixable) intro = std::max(intro, 32);
    s.push_back({ RoleE::INTRO, intro });
    switch (form) {
    case FormTemplateE::ANTHEM:
        s.push_back({ RoleE::GROOVE, 32 });
        s.push_back({ RoleE::SHORT_BREAK, pick(r, { 16, 16, 32 }) });
        s.push_back({ RoleE::DROP1, 32 });
        s.push_back({ RoleE::BREAKDOWN, pick(r, { 32, 32, 48, 64 }) });
        s.push_back({ RoleE::BUILD, pick(r, { 8, 8, 16 }) });
        s.push_back({ RoleE::MAIN_DROP, pick(r, { 32, 64, 64 }) });
        if (r.uniform() < 0.5f) {
            s.push_back({ RoleE::BREAK2, 16 });
            s.push_back({ RoleE::FINAL_DROP, 32 });
        }
        break;
    case FormTemplateE::DREAM:
        s.push_back({ RoleE::GROOVE, 32 });
        s.push_back({ RoleE::BREAKDOWN, pick(r, { 16, 32 }) });
        s.push_back({ RoleE::BUILD, 8 });
        s.push_back({ RoleE::MAIN_DROP, 32 });
        if (r.uniform() < 0.6f) {
            s.push_back({ RoleE::BREAK2, 16 });
            s.push_back({ RoleE::FINAL_DROP, 32 });
        }
        break;
    case FormTemplateE::ACID: {
        s.push_back({ RoleE::GROOVE, 32 });
        const int pauses = 1 + r.below(3);
        for (int i = 0; i < pauses; ++i) {
            s.push_back({ RoleE::KICK_PAUSE, 8 });
            s.push_back({ RoleE::GROOVE, pick(r, { 24, 32, 32 }) });
        }
        s.push_back({ RoleE::MAIN_DROP, pick(r, { 32, 64 }) });
        break;
    }
    case FormTemplateE::PLATEAU: {
        s.push_back({ RoleE::GROOVE, 32 });
        const int plateaus = 1 + r.below(2);
        for (int i = 0; i < plateaus; ++i) {
            s.push_back({ RoleE::SHORT_BREAK, pick(r, { 8, 16 }) });
            s.push_back({ RoleE::PLATEAU, 32 });
        }
        s.push_back({ RoleE::BREAKDOWN, pick(r, { 24, 32 }) });   // (16 measured 15 bars, under the references' 16 to 32)
        s.push_back({ RoleE::BUILD, 8 });
        s.push_back({ RoleE::MAIN_DROP, pick(r, { 32, 48 }) });
        break;
    }
    case FormTemplateE::DRIFT:
    default:
        // (Tools/calibrate.py: the references' breakdowns are 11 to 47 bars and their tracks near nine minutes, so the
        // length lies in a longer groove, a plateau and the drop.)
        s.push_back({ RoleE::GROOVE, pick(r, { 32, 48 }) });
        if (r.uniform() < 0.6f) {
            s.push_back({ RoleE::SHORT_BREAK, 16 });
            s.push_back({ RoleE::PLATEAU, 32 });
        }
        s.push_back({ RoleE::BREAKDOWN, pick(r, { 32, 32, 40 }) });   // (48 over the references' 11 to 47)
        s.push_back({ RoleE::BUILD, 8 });
        s.push_back({ RoleE::MAIN_DROP, pick(r, { 32, 48, 64 }) });
        break;
    }
    const int outro = prof.m_outroBars * (r.uniform() < 0.5f ? 2 : 1);
    s.push_back({ RoleE::OUTRO, mixable ? std::max(outro, 32) : outro });
    if (r.uniform() < prof.m_ambientOutro) {
        const int ambientOutBars = pick(r, { 8, 16, 16, 24 });
        if (!mixable) s.push_back({ RoleE::AMBIENT_OUTRO, ambientOutBars });
    }
    return s;
}

/** @brief The bars of every section of @p s together. */
int totalBars(const std::vector<Spec>& s)
{
    int n = 0;
    for (const Spec& x : s) n += x.m_bars;
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
            if (x.m_role == RoleE::OUTRO) outro = &x;
            if ((x.m_role == RoleE::GROOVE || x.m_role == RoleE::PLATEAU) && groove == nullptr) groove = &x;
        }
        if (outro != nullptr && outro->m_bars - over >= minOutro) outro->m_bars -= over;
        else if (outro != nullptr && outro->m_bars + (32 - over) <= 64) outro->m_bars += 32 - over;
        else if (groove != nullptr) groove->m_bars += 32 - over;
        else break;
    }
}

/** @brief The voices a track has at all (drawn once, from the profile). */
struct Cast {
    bool m_acidBass = false;    ///< the 303 carries the bass (and the melody)
    bool m_lead = true;         ///< a lead voice plays the melody (the supersaw, or the piano's stand-in)
    bool m_pluck = true;   ///< a pluck plays
    bool m_arp = false;   ///< an arp plays
    bool m_stab = false;   ///< a stab plays
    bool m_counter = false;   ///< a counter melody plays
    bool m_ride = true;   ///< a ride in the drops
    bool m_perc = false;   ///< a percussion loop
    bool m_beatless = false;   ///< no kick at all (Deep)
    bool m_bassInBreaks = false;   ///< Dream House: the bass never leaves (measured, PLAN 13.4)
};

/** @brief Every layer off. */
std::array<LayerStateE, NumLayers> none() { return std::array<LayerStateE, NumLayers>{}; }
/** @brief Sets layer @p l of @p a to @p s. */
void set(std::array<LayerStateE, NumLayers>& a, LayerE l, LayerStateE s) { a[static_cast<size_t>(l)] = s; }
/** @brief Layer @p l of @p a. */
LayerStateE get(const std::array<LayerStateE, NumLayers>& a, LayerE l) { return a[static_cast<size_t>(l)]; }

/** @brief The rhythm section: kick, the low end, the hats, the clap, the percussion. */
void rhythm(std::array<LayerStateE, NumLayers>& a, const Cast& c, bool withLow = true)
{
    if (!c.m_beatless) set(a, LayerE::KICK, LayerStateE::ON);
    if (withLow) {
        set(a, LayerE::SUB, LayerStateE::ON);
        set(a, c.m_acidBass ? LayerE::ACID : LayerE::BASS, LayerStateE::ON);
    }
    set(a, LayerE::CLOSED_HAT, LayerStateE::ON);
    set(a, LayerE::OPEN_HAT, LayerStateE::ON);
    set(a, LayerE::CLAP, LayerStateE::ON);
}

/**
 * @brief A section's own variations (its stream, `section<n>`): when the voices that vary enter, the intro's order, the
 *        mini-break. Every field is drawn for every section, whatever its role, so a stream never shifts; none touches the
 *        kick or the low end (the breakdown share stays the form's).
 */
struct Scatter {
    int m_arpFrom = 1;          ///< the groove's block where the arp enters (1 or 2)
    int m_stabFrom = 2;         ///< the groove's block where the stab enters (2 or 3)
    int m_percFrom = 0;         ///< the groove's block where the percussion enters (0 or 1)
    int m_dropArpLate = 0;      ///< a drop's arp a block later (0 or 1)
    int m_dropRideLate = 0;     ///< the first drop's ride a block later (0 or 1)
    bool m_counterEarly = false;   ///< the main drop's counter from its first quarter, not its half
    int m_stabParity = 1;       ///< the main drop's stab on the odd blocks (1) or the even ones (0)
    bool m_introSwap = false;   ///< the intro's percussion before its open hat
    bool m_counterInPeak = true;   ///< the counter in the breakdown's peak
    bool m_arpInPeak = true;       ///< the arp (filtered) in the breakdown's peak
    int m_outroPercCut = 2;     ///< the outro's percussion leaves this many blocks before its end (2 or 3)
    bool m_miniBreak = false;   ///< a mini-break (a long groove or the first drop)
    int m_miniBreakBar = 15;    ///< its bar in the section (15 or 7)
    int m_vacuum = 0;           ///< a drop's vacuum before it: the last beat, two beats, the bar, the kick alone on 4
};

/** @brief A section's variations, every field drawn from @p r. */
Scatter drawScatter(Rng& r)
{
    Scatter s;
    s.m_arpFrom = r.uniform() < 0.6f ? 1 : 2;
    s.m_stabFrom = r.uniform() < 0.6f ? 2 : 3;
    s.m_percFrom = r.uniform() < 0.7f ? 0 : 1;
    s.m_dropArpLate = r.uniform() < 0.35f ? 1 : 0;
    s.m_dropRideLate = r.uniform() < 0.35f ? 1 : 0;
    s.m_counterEarly = r.uniform() < 0.35f;
    s.m_stabParity = r.uniform() < 0.6f ? 1 : 0;
    s.m_introSwap = r.uniform() < 0.4f;
    s.m_counterInPeak = r.uniform() < 0.7f;
    s.m_arpInPeak = r.uniform() < 0.6f;
    s.m_outroPercCut = r.uniform() < 0.6f ? 2 : 3;
    s.m_miniBreak = r.uniform() < 0.6f;
    s.m_miniBreakBar = r.uniform() < 0.7f ? 15 : 7;
    const float uv = r.uniform();
    s.m_vacuum = uv < 0.35f ? 0 : uv < 0.6f ? 1 : uv < 0.8f ? 2 : 3;
    return s;
}

/** @brief The layer states of block @p k of @p n in a section of role @p role. */
std::array<LayerStateE, NumLayers> blockStates(RoleE role, int k, int n, const Cast& c, const BreakdownParts* bd, int bar, const Scatter& v)
{
    using ST = LayerStateE;
    auto a = none();
    const LayerE bassLayer = c.m_acidBass ? LayerE::ACID : LayerE::BASS;
    switch (role) {
    case RoleE::AMBIENT_INTRO:
        // The pad filtered, opening over the intro (the composer's ramp), and the atmosphere (30.09.2026: open, the pad
        // of a beatless opening lay 7 dB over the references' sustained sound, measured as analyze_ref.py's intro_sus).
        set(a, LayerE::PAD, ST::FILTERED);
        set(a, LayerE::FX, ST::ON);
        if (c.m_lead && k >= n / 2 && n >= 2) set(a, LayerE::LEAD, ST::FILTERED);   // the piano's motif hinted (Dream House)
        else if (c.m_arp) set(a, LayerE::ARP, ST::FILTERED);
        break;
    case RoleE::INTRO: {
        // The atmosphere over it all (30.09.2026, the user: "Fangen typische Trance-Songs nicht eher mit einer Atmosphaere
        // oder einem Pad an?"): the pad filtered from the first bar, opening over the intro, and the effects' atmosphere
        // -- the references' intros carry sustained sound 7 to 12 dB under their drop's from the first bar on (intro_sus
        // in analyze_ref.py), Parhelion's had none. In the intro's second half the arp's hint as well.
        set(a, LayerE::PAD, ST::FILTERED);
        set(a, LayerE::FX, ST::ON);
        if (c.m_arp && n >= 2 && k >= n / 2) set(a, LayerE::ARP, ST::FILTERED);
        // Dok. 6: kick alone, hats, bass, open hat, percussion, the filtered pluck; spread over the intro's blocks (the
        // percussion now and then before the open hat: PLAN 6.3's "Streuung"). The kick never alone (01.10.2026, measured
        // on the references' first eight bars: where the kick plays, the hats play with it, 2 to 13 dB under the drop's;
        // the kick alone in 1 of 30): the closed hat from the first bar.
        LayerE order[] = { LayerE::KICK, LayerE::CLOSED_HAT, bassLayer, LayerE::OPEN_HAT, LayerE::PERC, LayerE::PLUCK };
        if (v.m_introSwap && c.m_perc) std::swap(order[3], order[4]);
        const int steps = 6;
        const int upto = n <= 1 ? 2 : std::max(2, std::min(steps, 1 + (k * (steps - 1) + (n - 2)) / std::max(1, n - 1)));
        for (int i = 0; i < upto; ++i) {
            const LayerE l = order[i];
            if (l == LayerE::KICK && c.m_beatless) continue;
            if (l == LayerE::PERC && !c.m_perc) continue;
            if (l == LayerE::PLUCK) { set(a, c.m_pluck ? LayerE::PLUCK : LayerE::ARP, ST::FILTERED); continue; }
            if (l == bassLayer) { set(a, LayerE::SUB, ST::ON); set(a, bassLayer, c.m_acidBass ? ST::FILTERED : ST::ON); continue; }
            set(a, l, ST::ON);
        }
        break;
    }
    case RoleE::GROOVE:
    case RoleE::PLATEAU:
        rhythm(a, c);
        if (c.m_perc && k >= v.m_percFrom) set(a, LayerE::PERC, ST::ON);
        if (c.m_pluck) set(a, LayerE::PLUCK, ST::ON);
        set(a, LayerE::PAD, role == RoleE::PLATEAU ? ST::ON : ST::FILTERED);
        if (c.m_arp && (k >= v.m_arpFrom || role == RoleE::PLATEAU)) set(a, LayerE::ARP, ST::ON);
        if (c.m_stab && k >= v.m_stabFrom) set(a, LayerE::STAB, ST::ON);
        if (k == 0 && bar > 0) set(a, LayerE::CRASH, ST::ON);
        break;
    case RoleE::SHORT_BREAK:
    case RoleE::BREAK2:
        set(a, LayerE::PAD, ST::ON);
        if (c.m_bassInBreaks) { set(a, LayerE::SUB, ST::ON); set(a, LayerE::BASS, ST::ON); }
        if (c.m_lead) set(a, LayerE::LEAD, ST::FILTERED);   // the melody's fragment (Dok. 6: "Pad + Melodie-Fragment")
        if (c.m_pluck) set(a, LayerE::PLUCK, ST::FILTERED);
        if (c.m_acidBass) set(a, LayerE::ACID, ST::FILTERED);
        break;
    case RoleE::KICK_PAUSE:
        set(a, LayerE::SUB, ST::ON);
        set(a, bassLayer, ST::ON);
        set(a, LayerE::CLOSED_HAT, ST::FILTERED);
        set(a, LayerE::PAD, ST::FILTERED);
        break;
    case RoleE::DROP1:
    case RoleE::MAIN_DROP:
    case RoleE::FINAL_DROP: {
        rhythm(a, c);
        if (c.m_perc) set(a, LayerE::PERC, ST::ON);
        if (c.m_pluck) set(a, LayerE::PLUCK, ST::ON);
        set(a, LayerE::PAD, ST::ON);
        if (c.m_lead) set(a, LayerE::LEAD, ST::ON);
        if (k == 0) set(a, LayerE::CRASH, ST::ON);
        // A variation every block (Dok. 6: "Variation nach 16 Takten"): the ride, the arp, the counter, the stab.
        const bool main = role != RoleE::DROP1;
        if (c.m_ride && (main || k >= 1 + v.m_dropRideLate)) set(a, LayerE::RIDE, ST::ON);
        if (c.m_arp && (role == RoleE::FINAL_DROP || k >= (main ? 1 : 2) + v.m_dropArpLate)) set(a, LayerE::ARP, ST::ON);
        if (c.m_counter && main && k >= (v.m_counterEarly ? std::max(1, n / 4) : n / 2)) set(a, LayerE::COUNTER, ST::ON);
        // The stab: the offbeat chords of the drops (30.09.2026: it sounded in 0 to 5 % of the blocks) -- the first drop
        // on every other block, the main drop from its second block on, the final drop throughout.
        if (c.m_stab && (role == RoleE::FINAL_DROP || (main && k >= 1) || (!main && k % 2 == v.m_stabParity))) set(a, LayerE::STAB, ST::ON);
        if (!c.m_lead && c.m_acidBass) set(a, LayerE::ACID, ST::ON);
        break;
    }
    case RoleE::BREAKDOWN: {
        set(a, LayerE::PAD, ST::ON);
        if (c.m_bassInBreaks) { set(a, LayerE::SUB, ST::ON); set(a, LayerE::BASS, ST::ON); }
        const int part = bd == nullptr ? 2 : (bar < bd->m_tease ? 0 : bar < bd->m_peak ? 1 : 2);
        if (part >= 1) {
            if (c.m_lead) set(a, LayerE::LEAD, part == 1 ? ST::FILTERED : ST::ON);
            if (c.m_pluck) set(a, LayerE::PLUCK, ST::FILTERED);
            if (c.m_acidBass) set(a, LayerE::ACID, ST::FILTERED);
            if (!c.m_lead && c.m_arp) set(a, LayerE::ARP, part == 1 ? ST::FILTERED : ST::ON);
        }
        if (part == 2) {
            if (c.m_counter && v.m_counterInPeak) set(a, LayerE::COUNTER, ST::ON);
            if (c.m_arp && (v.m_arpInPeak || !c.m_lead)) set(a, LayerE::ARP, ST::FILTERED);
        }
        break;
    }
    case RoleE::BUILD:
        // Dok. 6: the rebuild -- the kick back, the snare roll, the riser; the lead teased, the pad on.
        if (!c.m_beatless) set(a, LayerE::KICK, ST::ON);
        set(a, LayerE::CLOSED_HAT, ST::FILTERED);
        set(a, LayerE::PAD, ST::ON);
        if (c.m_lead) set(a, LayerE::LEAD, ST::FILTERED);
        if (c.m_pluck) set(a, LayerE::PLUCK, ST::ON);
        if (c.m_acidBass) set(a, LayerE::ACID, ST::FILTERED);
        set(a, LayerE::PERC, ST::ON);   // the snare roll lives in the percussion row
        set(a, LayerE::FX, ST::ON);
        break;
    case RoleE::OUTRO: {
        // The intro reversed: the melody ghosts through the first 16 bars filtered, then the rhythm alone thins out.
        rhythm(a, c, k < n - 1 || n == 1);
        if (k < 2 && c.m_lead) set(a, LayerE::LEAD, ST::FILTERED);
        if (k < 2 && c.m_pluck) set(a, LayerE::PLUCK, k == 0 ? ST::ON : ST::FILTERED);
        if (k == 0) set(a, LayerE::PAD, ST::FILTERED);
        if (c.m_perc && k < n - v.m_outroPercCut) set(a, LayerE::PERC, ST::ON);
        if (k >= n - 2) set(a, LayerE::CLAP, ST::OFF);
        if (k == n - 1) set(a, LayerE::OPEN_HAT, ST::OFF);
        break;
    }
    case RoleE::AMBIENT_OUTRO:
        set(a, LayerE::PAD, ST::FILTERED);
        break;
    }
    return a;
}

/** @brief The whole plan from a list of sections; the matrix's and the energy's streams from @p stream. */
Plan buildPlan(const StyleProfile& prof, FormTemplateE form, const std::vector<Spec>& specs, const Cast& cast, const UnitStreamT& stream)
{
    Plan p;
    p.m_form = form;
    p.m_beatless = cast.m_beatless;
    int bar = 0;
    std::vector<RoleE> roles;
    // The energy: the table's, scattered by half a point (the main drop stays the peak), rising through a breakdown and
    // its build.
    Rng er;
    er.seed(stream("energy"));
    for (const Spec& s : specs) {
        Section sec;
        sec.m_beat = 4.0 * bar;
        sec.m_length = 4.0 * s.m_bars;
        sec.m_kind = kindOf(s.m_role);
        energyOf(s.m_role, sec.m_energyFrom, sec.m_energyTo);
        const float jf = 0.5f * er.bipolar(), jt = 0.5f * er.bipolar();
        if (s.m_role != RoleE::MAIN_DROP) {
            sec.m_energyFrom = std::clamp(sec.m_energyFrom + jf, 0.0f, 9.5f);
            sec.m_energyTo = std::clamp(sec.m_energyTo + jt, 0.0f, 9.5f);
        }
        if (s.m_role == RoleE::BREAKDOWN || s.m_role == RoleE::BUILD) sec.m_energyTo = std::max(sec.m_energyTo, sec.m_energyFrom);
        if (s.m_role == RoleE::BUILD && !p.m_sections.empty()) {
            sec.m_energyFrom = std::max(sec.m_energyFrom, p.m_sections.back().m_energyTo);
            sec.m_energyTo = std::max(sec.m_energyTo, sec.m_energyFrom);
        }
        if (s.m_role == RoleE::MAIN_DROP) p.m_mainDrop = static_cast<int>(p.m_sections.size());
        if (s.m_role == RoleE::BREAKDOWN && (p.m_mainBreakdown < 0 || s.m_bars >= static_cast<int>(p.m_sections[static_cast<size_t>(p.m_mainBreakdown)].m_length / 4.0)))
            p.m_mainBreakdown = static_cast<int>(p.m_sections.size());
        if (s.m_role == RoleE::BREAKDOWN) {
            // Intro, tease, peak (Dok. 6): a quarter, a quarter, a half, on 8-bar lines.
            BreakdownParts bd;
            bd.m_start = bar;
            const int q = std::max(8, (s.m_bars / 4) / 8 * 8);
            bd.m_tease = bar + q;
            bd.m_peak = std::min(bar + s.m_bars, bd.m_tease + q);
            bd.m_end = bar + s.m_bars;
            p.m_breakdowns.push_back(bd);
        }
        p.m_sections.push_back(sec);
        roles.push_back(s.m_role);
        bar += s.m_bars;
    }
    p.m_bars = bar;
    // The layer matrix: every section's variations on its own stream.
    const uint64_t matrixSeed = stream("matrix");
    std::vector<Scatter> scatter;
    for (size_t i = 0; i < specs.size(); ++i) {
        Rng sr;
        sr.seed(mixSeed(matrixSeed, stream("section" + std::to_string(i + 1))));
        scatter.push_back(drawScatter(sr));
        // The vacuum before a drop (Phosphene's four).
        if (kindOf(roles[i]) == SectionKindE::DROP) {
            static const double From[4] = { 3.0, 2.0, 0.0, 3.0 };
            Vacuum v;
            v.m_bar = static_cast<int>(p.m_sections[i].m_beat / 4.0) - 1;
            v.m_from = From[scatter.back().m_vacuum];
            v.m_kickOn4 = scatter.back().m_vacuum == 3;
            p.m_vacuums.push_back(v);
        }
    }
    for (size_t i = 0; i < specs.size(); ++i) {
        const int start = static_cast<int>(p.m_sections[i].m_beat / 4.0), n = specs[i].m_bars / 8;
        const BreakdownParts* bd = nullptr;
        for (const BreakdownParts& b : p.m_breakdowns) if (b.m_start == start) bd = &b;
        for (int k = 0; k < n; ++k) {
            LayerBlock lb;
            lb.m_beat = 4.0 * (start + 8 * k);
            lb.m_state = blockStates(roles[i], k, n, cast, bd, start + 8 * k, scatter[i]);
            const float t = (k + 0.5f) / static_cast<float>(n);
            lb.m_energy = p.m_sections[i].m_energyFrom + (p.m_sections[i].m_energyTo - p.m_sections[i].m_energyFrom) * t;
            p.m_blocks.push_back(lb);
        }
    }
    // Rule 1: every block changes something. Where two blocks are the same, the percussion (or the arp) enters or leaves.
    for (size_t b = 1; b < p.m_blocks.size(); ++b) {
        if (p.m_blocks[b].m_state != p.m_blocks[b - 1].m_state) continue;
        auto& st = p.m_blocks[b].m_state;
        const bool rhythmOn = get(st, LayerE::CLOSED_HAT) != LayerStateE::OFF;
        const LayerE l = rhythmOn ? LayerE::PERC : (cast.m_arp ? LayerE::ARP : LayerE::PLUCK);
        set(st, l, get(st, l) == LayerStateE::OFF ? (rhythmOn ? LayerStateE::ON : LayerStateE::FILTERED) : LayerStateE::OFF);
    }
    // The lead's first entry; mini-breaks in the long grooves and the first drop (Dok. 6: "Kick raus fuer 1 Takt").
    for (size_t b = 0; b < p.m_blocks.size(); ++b)
        if (get(p.m_blocks[b].m_state, LayerE::LEAD) != LayerStateE::OFF) { p.m_firstLeadBar = static_cast<int>(p.m_blocks[b].m_beat / 4.0); break; }
    for (size_t i = 0; i < specs.size(); ++i) {
        const int start = static_cast<int>(p.m_sections[i].m_beat / 4.0);
        if ((roles[i] == RoleE::GROOVE || roles[i] == RoleE::DROP1) && specs[i].m_bars >= 32 && !cast.m_beatless && scatter[i].m_miniBreak)
            p.m_miniBreaks.push_back(start + scatter[i].m_miniBreakBar);
    }
    (void)prof;
    return p;
}

} // namespace

int Plan::sectionAt(int bar) const
{
    for (size_t i = 0; i < m_sections.size(); ++i)
        if (bar * 4.0 >= m_sections[i].m_beat && bar * 4.0 < m_sections[i].m_beat + m_sections[i].m_length) return static_cast<int>(i);
    return -1;
}

LayerStateE Plan::at(int bar, LayerE l) const
{
    const int b = bar / 8;
    if (b < 0 || b >= static_cast<int>(m_blocks.size())) return LayerStateE::OFF;
    return m_blocks[static_cast<size_t>(b)].m_state[static_cast<size_t>(l)];
}

float Plan::energyAt(int bar) const
{
    const int i = sectionAt(bar);
    if (i < 0) return 0.0f;
    const Section& s = m_sections[static_cast<size_t>(i)];
    const float t = static_cast<float>((bar * 4.0 - s.m_beat) / std::max(4.0, s.m_length));
    return s.m_energyFrom + (s.m_energyTo - s.m_energyFrom) * t;
}

float breakdownShare(const Plan& plan)
{
    // As the reference measurement counts (PLAN 13.4): bars without a kick and without a bass, between the first and the
    // last bar that has one.
    auto low = [&](const LayerBlock& b) {
        return b.at(LayerE::KICK) != LayerStateE::OFF || b.at(LayerE::SUB) != LayerStateE::OFF || b.at(LayerE::BASS) != LayerStateE::OFF
            || b.at(LayerE::ACID) != LayerStateE::OFF;
    };
    int first = -1, last = -1;
    for (size_t i = 0; i < plan.m_blocks.size(); ++i) if (low(plan.m_blocks[i])) { if (first < 0) first = static_cast<int>(i); last = static_cast<int>(i); }
    if (first < 0) return 0.0f;
    int n = 0;
    for (int i = first; i <= last; ++i) if (!low(plan.m_blocks[static_cast<size_t>(i)])) ++n;
    return plan.m_bars > 0 ? static_cast<float>(8.0 * n / plan.m_bars) : 0.0f;
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
void rewriteSpecs(std::vector<Spec>& s, int bar, SectionKindE kind)
{
    std::vector<Spec> out;
    bool mainBefore = false;
    int at = 0;
    for (const Spec& x : s) {
        if (at + x.m_bars <= bar) {
            out.push_back(x);
            mainBefore = mainBefore || x.m_role == RoleE::MAIN_DROP;
            at += x.m_bars;
            continue;
        }
        if (bar - at >= 8) out.push_back({ x.m_role, bar - at });   // (the section it cuts, up to the line)
        break;
    }
    if (kind == SectionKindE::BREAKDOWN) {
        out.push_back({ mainBefore ? RoleE::BREAK2 : RoleE::BREAKDOWN, mainBefore ? 16 : 32 });
        out.push_back({ RoleE::BUILD, 8 });
    }
    out.push_back({ mainBefore ? RoleE::FINAL_DROP : RoleE::MAIN_DROP, 32 });
    out.push_back({ RoleE::OUTRO, 32 });
    s = out;
}

} // namespace

Plan planTrack(const StyleProfile& prof, int bars, const UnitStreamT& stream, bool mixable)
{
    return planTrackRewritten(prof, bars, stream, mixable, std::vector<Rewrite>{});
}

Plan planTrackRewritten(const StyleProfile& prof, int bars, const UnitStreamT& stream, bool mixable, int rewriteBar, SectionKindE rewriteKind)
{
    std::vector<Rewrite> one;
    if (rewriteBar >= 0) one.push_back({ rewriteBar, rewriteKind });
    return planTrackRewritten(prof, bars, stream, mixable, one);
}

Plan planTrackRewritten(const StyleProfile& prof, int bars, const UnitStreamT& stream, bool mixable, const std::vector<Rewrite>& rewrites,
                        bool opener)
{
    const uint64_t formSeed = stream("form");
    Rng r;
    r.seed(formSeed);
    const FormTemplateE form = static_cast<FormTemplateE>(drawWeighted(prof.m_forms.data(), FormTemplates, r.uniform()));
    Cast cast;
    cast.m_beatless = r.uniform() < prof.m_beatless && !mixable;   // (drawn either way: the stream stays put)
    // The bass figure; the 303 carries the bass where it says so, and always where it is the profile's lead (Acid).
    Rng br;
    br.seed(stream("bass"));
    const int bassPattern = drawWeighted(prof.m_bass.data(), BassPatterns, br.uniform());
    cast.m_acidBass = bassPattern == static_cast<int>(BassPatternE::ACID) || prof.m_lead == LeadKindE::ACID;
    cast.m_lead = prof.m_lead == LeadKindE::SUPERSAW || prof.m_lead == LeadKindE::PIANO;
    // The cast: which voices the track has (the same for every candidate).
    Rng mr;
    mr.seed(mixSeed(stream("matrix"), 0x43415354ull));   // "CAST"
    const float uPluck = mr.uniform(), uArp = mr.uniform(), uStab = mr.uniform(), uCounter = mr.uniform();
    const float uRide = mr.uniform(), uPerc = mr.uniform();
    cast.m_pluck = uPluck < prof.m_pluck || prof.m_lead == LeadKindE::PLUCK_ARP;
    cast.m_arp = uArp < prof.m_arp || prof.m_lead == LeadKindE::PLUCK_ARP || prof.m_lead == LeadKindE::PAD;
    cast.m_stab = uStab < prof.m_stab;
    cast.m_counter = cast.m_lead && uCounter < prof.m_counter;
    cast.m_ride = uRide < prof.m_ride;
    cast.m_perc = uPerc < prof.m_perc;
    cast.m_bassInBreaks = form == FormTemplateE::DREAM;
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
        for (const Section& s : p.m_sections) { if (s.m_kind != SectionKindE::INTRO) break; introBars += static_cast<int>(s.m_length / 4.0); }
        const double score = std::fabs(static_cast<double>(p.m_bars - bars)) / 32.0
                           + 4.0 * std::max(0.0f, share - prof.m_breakdownHigh) + 4.0 * std::max(0.0f, prof.m_breakdownLow - share)
                           + 4.0 * std::max(0.0, static_cast<double>(introBars) / std::max(1, p.m_bars) - 0.3);
        if (score < bestScore) { bestScore = score; best = std::move(p); bestC = c; }
    }
    // A mix's first track opens on its atmosphere at least as often as not (01.10.2026: the beginning of the whole mix;
    // the candidates' search favours the shorter intros, so it is decided here, on a stream of its own).
    bool openOnAtmosphere = false;
    if (opener) {
        Rng orr;
        orr.seed(mixSeed(formSeed, 0x4F50454Eull));   // "OPEN"
        openOnAtmosphere = orr.uniform() < std::max(prof.m_ambientIntro, 0.6f);
    }
    const bool hasAmbient = !best.m_sections.empty() && best.m_sections.front().m_kind == SectionKindE::INTRO && best.m_sections.size() > 1
                         && best.m_sections[1].m_kind == SectionKindE::INTRO;
    if (!rewrites.empty() || (openOnAtmosphere && !hasAmbient)) {
        // The chosen candidate's specs again (the same draws), cut and continued at each line in turn (fitTo32 evens
        // the length out in the outro, after every line).
        Rng cr;
        cr.seed(mixSeed(formSeed, 0x43414E44ull + static_cast<uint64_t>(bestC)));
        std::vector<Spec> specs = drawSpecs(prof, form, cr, mixable, opener);
        if (openOnAtmosphere && (specs.empty() || specs.front().m_role != RoleE::AMBIENT_INTRO))
            specs.insert(specs.begin(), Spec{ RoleE::AMBIENT_INTRO, std::max(16, prof.m_ambientIntroBars / 8 * 8) });
        fitTo32(specs, mixable ? 32 : 16);
        int last = -1;
        for (const Rewrite& w : rewrites) {
            if (w.m_bar < 0 || w.m_bar <= last) continue;
            rewriteSpecs(specs, w.m_bar, w.m_kind);
            fitTo32(specs, mixable ? 32 : 16);
            last = w.m_bar;
        }
        best = buildPlan(prof, form, specs, cast, stream);
    }
    best.m_bassPattern = cast.m_acidBass ? static_cast<int>(BassPatternE::ACID) : bassPattern;
    return best;
}

} // namespace parh
