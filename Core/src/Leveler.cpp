/**
 * @file Leveler.cpp
 * @brief Every track as loud as its style means (Leveler.h).
 * @note After Totality `Core/src/Leveler.cpp` at 4d3c0d2 (29.09.2026): the readings, the ranked corrections and the
 *       two-pass loudness are its; the windows and the breakdown's correction are Parhelion's.
 * @author Rene Weller
 */
#include "parh/Leveler.h"
#include "parh/Dsp.h"
#include "parh/Engine.h"
#include "parh/Loudness.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>

namespace parh {

namespace {
constexpr double Rate = 48000.0;   ///< the rate the parts are rendered at to be measured, Hz
constexpr int Block = 512;   ///< the block they are rendered in
constexpr double Warm = 4.0;          ///< seconds before a measured part: the rooms fill, the notes sounding on are found
constexpr float MostDb = 6.0f;        ///< the largest loudness correction either way (Deep's soft drops need more than 4)
constexpr double BalSeconds = 12.0;   ///< how much the balance reads in each of its places
constexpr double BalWarm = 2.0;       ///< and before it
constexpr float BalDown = -8.0f;   ///< the largest correction of a part down, dB
constexpr float BalUp = 10.0f;   ///< the largest correction of a part up, dB
constexpr float BreakMost = 8.0f;     ///< the breakdown's correction, either way

/** @brief The loudest samples of the kick and of every part. */
struct PartPeaks {
    float m_kick = 0.0f;   ///< the kick's loudest sample
    float m_part[BalParts] = {};   ///< every part's loudest sample
};

/**
 * @brief Plays @p seconds from @p beat (after BalWarm before it) and keeps the loudest samples. False if stopped.
 * @param[in,out] e        the engine
 * @param[in]     s        the score
 * @param[in]     beat     the first beat
 * @param[in]     seconds  the seconds played
 * @param[out]    peaks    the loudest samples per part
 * @param[in]     stop     asked between blocks: true breaks off
 * @return false if stopped
 */
bool readParts(Engine& e, const Score& s, double beat, double seconds, PartPeaks& peaks, const std::function<bool()>& stop)
{
    const double at = s.m_tempo.secondsAt(beat);
    e.seek(s.m_tempo.beatAt(std::max(0.0, at - BalWarm)));
    std::vector<float> l(Block), r(Block);
    const double warm = std::min(BalWarm, at);
    for (int done = 0; done < static_cast<int>(warm * Rate); done += Block) {
        if (stop && stop()) return false;
        e.process(l.data(), r.data(), Block);
    }
    e.watchPeaks(true);
    for (int done = 0; done < static_cast<int>(seconds * Rate); done += Block) {
        if (stop && stop()) { e.watchPeaks(false); return false; }
        e.process(l.data(), r.data(), Block);
    }
    e.watchPeaks(false);
    const Deck& d = e.deck(0);
    peaks.m_kick = std::max(peaks.m_kick, d.kickPeak());
    for (int p = 0; p < BalParts; ++p) peaks.m_part[p] = std::max(peaks.m_part[p], d.partPeak(p));
    return true;
}

/**
 * @brief Every part's loudest sample against the loudest kick, dB (NaN: more than 60 dB under it).
 * @param[in] peaks  the parts' loudest samples
 * @return each part against the loudest kick in dB, NaN more than 60 dB under it
 */
BalanceDbT against(const PartPeaks& peaks)
{
    BalanceDbT out;
    for (int p = 0; p < BalParts; ++p) {
        const float v = peaks.m_part[p];
        out[static_cast<size_t>(p)] = peaks.m_kick > 1e-4f && v > peaks.m_kick * 1e-3f ? 20.0f * std::log10(v / peaks.m_kick)
                                                                                   : std::numeric_limits<float>::quiet_NaN();
    }
    return out;
}

/**
 * @brief The corrections that bring @p found into the windows. A rank within the kit's two kinds (the hat-like lanes, the
 *        percussion): the two loudest keep their windows, the third's lies 3 dB lower, the others' 6 (Totality's rule:
 *        ten parts pushed up at once flood the mids). @p windowDb moves the voices' windows (LevelMark::windowDb).
 * @param[in] e         the engine
 * @param[in] found     the levels found, dB
 * @param[in] windowDb  the windows' width, dB
 * @return the corrections, dB
 */
BalanceDbT corrections(const Engine& e, const BalanceDbT& found, float windowDb)
{
    const ParamStore& ps = e.params();
    int role[BalLanes] = {};
    for (int l = 0; l < BalLanes; ++l) role[l] = static_cast<int>(std::lround(e.deck(0).played(ps.id(ModuleE::PERC, l, perc::ROLE))));
    auto hatLike = [](int r) {
        const PercRoleE pr = static_cast<PercRoleE>(r);
        return pr == PercRoleE::CLOSED_HAT || pr == PercRoleE::ROLLING_HAT || pr == PercRoleE::OPEN_HAT || pr == PercRoleE::RIDE
            || pr == PercRoleE::SHAKER || pr == PercRoleE::TAMBOURINE || pr == PercRoleE::CRASH;
    };
    float lower[BalParts] = {};
    for (int kind = 0; kind < 2; ++kind) {
        std::vector<std::pair<float, int>> heard;
        for (int p = 0; p < BalLanes; ++p)
            if ((hatLike(role[p]) ? 0 : 1) == kind && !std::isnan(found[static_cast<size_t>(p)])) heard.emplace_back(found[static_cast<size_t>(p)], p);
        std::stable_sort(heard.begin(), heard.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t k = 0; k < heard.size(); ++k) lower[heard[k].second] = k < 2 ? 0.0f : k == 2 ? 3.0f : 6.0f;
    }
    BalanceDbT out{};
    for (int p = 0; p < BalParts; ++p) {
        const float f = found[static_cast<size_t>(p)];
        if (std::isnan(f) || p == static_cast<int>(BalPartE::ROOM)) continue;
        float lo, hi;
        balanceWindow(p, p < BalLanes ? role[p] : -1, lo, hi);
        lo -= lower[p];
        hi -= lower[p];
        if (p > static_cast<int>(BalPartE::BASS) && p != static_cast<int>(BalPartE::FX)) { lo += windowDb; hi += windowDb; }
        out[static_cast<size_t>(p)] = std::clamp(f < lo ? lo - f : f > hi ? hi - f : 0.0f, BalDown, BalUp);
    }
    return out;
}

/**
 * @brief The loudness of @p seconds from @p beat (after Warm before it): integrated and the short-term maximum.
 * @param[in,out] e            the engine
 * @param[in]     s            the score
 * @param[in]     beat         the first beat
 * @param[in]     seconds      the seconds measured
 * @param[in]     stop         asked between blocks: true breaks off
 * @param[in]     warmSeconds  the seconds played before
 * @return the loudness, nothing if stopped
 */
std::optional<LoudnessReport> measure(Engine& e, const Score& s, double beat, double seconds, const std::function<bool()>& stop,
                                      double warmSeconds = Warm)
{
    const double at = s.m_tempo.secondsAt(beat);
    e.seek(s.m_tempo.beatAt(std::max(0.0, at - warmSeconds)));
    std::vector<float> l(Block), r(Block);
    const double warm = std::min(warmSeconds, at);
    for (int done = 0; done < static_cast<int>(warm * Rate); done += Block) {
        if (stop && stop()) return std::nullopt;
        e.process(l.data(), r.data(), Block);
    }
    LoudnessMeter meter;
    meter.prepare(Rate);
    for (int done = 0; done < static_cast<int>(seconds * Rate); done += Block) {
        if (stop && stop()) return std::nullopt;
        e.process(l.data(), r.data(), Block);
        meter.process(l.data(), r.data(), Block);
    }
    return meter.report();
}

/**
 * @brief The drop as the references were measured (Tools/analyze_ref.py): the whole of it from @p beat for @p seconds
 *        (at most 150), every 20-second window hopping 5 s -- the loudest window's integrated loudness is the drop's
 *        (the references' loud20), the loudest 3 s anywhere in it its short-term maximum. A drop that grows (its second
 *        half an octave up, the choir coming in) is measured where it is loudest, not where it begins.
 * @param[in,out] e        the engine
 * @param[in]     s        the score
 * @param[in]     beat     the drop's first beat
 * @param[in]     seconds  its seconds
 * @param[in]     stop     asked between blocks: true breaks off
 * @return the drop's loudness, nothing if stopped
 */
std::optional<LoudnessReport> measureDrop(Engine& e, const Score& s, double beat, double seconds, const std::function<bool()>& stop)
{
    seconds = std::clamp(seconds, 20.0, 150.0);
    const double at = s.m_tempo.secondsAt(beat);
    e.seek(s.m_tempo.beatAt(std::max(0.0, at - Warm)));
    std::vector<float> l(Block), r(Block);
    const double warm = std::min(Warm, at);
    for (int done = 0; done < static_cast<int>(warm * Rate); done += Block) {
        if (stop && stop()) return std::nullopt;
        e.process(l.data(), r.data(), Block);
    }
    const int windows = static_cast<int>((seconds - 20.0) / 5.0) + 1;
    std::vector<LoudnessMeter> meters(static_cast<size_t>(windows));
    for (LoudnessMeter& m : meters) m.prepare(Rate);
    LoudnessMeter whole;
    whole.prepare(Rate);
    const int total = static_cast<int>(seconds * Rate);
    for (int done = 0; done < total; done += Block) {
        if (stop && stop()) return std::nullopt;
        e.process(l.data(), r.data(), Block);
        whole.process(l.data(), r.data(), Block);
        const double t = static_cast<double>(done) / Rate;
        for (int w = 0; w < windows; ++w)
            if (t >= 5.0 * w && t < 5.0 * w + 20.0) meters[static_cast<size_t>(w)].process(l.data(), r.data(), Block);
    }
    LoudnessReport rep = whole.report();
    double loudest = -120.0;
    for (LoudnessMeter& m : meters) loudest = std::max(loudest, m.report().m_integrated);
    rep.m_integrated = loudest;
    return rep;
}

/**
 * @brief The length in seconds of the section that begins at or contains @p beat.
 * @param[in] s     the score
 * @param[in] beat  the position in beats
 * @return the section's length in seconds
 */
double sectionSeconds(const Score& s, double beat)
{
    for (const Section& sec : s.m_sections)
        if (beat >= sec.m_beat - 1e-9 && beat < sec.m_beat + sec.m_length) return s.m_tempo.secondsAt(sec.m_beat + sec.m_length) - s.m_tempo.secondsAt(beat);
    return 20.0;
}

/**
 * @brief The main breakdown of the track that begins at @p from: the longest Breakdown section (else Break) after it.
 * @param[in] s     the score
 * @param[in] from  the track's first beat
 * @param[in] to    its end in beats
 * @return the main breakdown, or null
 */
const Section* mainBreakdown(const Score& s, double from, double to)
{
    const Section* best = nullptr;
    for (const Section& sec : s.m_sections) {
        if (sec.m_beat < from || sec.m_beat >= to) continue;
        const bool bd = sec.m_kind == SectionKindE::BREAKDOWN, br = sec.m_kind == SectionKindE::BREAK;
        if (!bd && !br) continue;
        const bool bestBd = best != nullptr && best->m_kind == SectionKindE::BREAKDOWN;
        if (best == nullptr || (bd && !bestBd) || (bd == bestBd && sec.m_length > best->m_length)) best = &sec;
    }
    return best;
}

} // namespace

std::vector<LevelReading> levelSet(SetScore& set, const ParamStore& params, double seconds, const std::function<bool()>& stop)
{
    std::vector<LevelReading> out;
    for (int d = 0; d < 2; ++d) {
        if (set.m_decks[d].m_levels.empty()) continue;
        const std::vector<LevelReading> r = levelScore(set.m_decks[d], params, seconds, stop);
        if (r.empty() && stop && stop()) return {};
        out.insert(out.end(), r.begin(), r.end());
    }
    // The teases: the corrections of their source.
    for (LevelMark& m : set.m_decks[2].m_levels) {
        if (!std::isnan(m.m_targetLufs)) continue;
        for (int d = 0; d < 2; ++d)
            for (const LevelMark& src : set.m_decks[d].m_levels)
                if (src.m_peakBeat == m.m_peakBeat) { m.m_trimDb = src.m_trimDb; m.m_balDb = src.m_balDb; m.m_breakDb = src.m_breakDb; m.m_buildDb = src.m_buildDb; }
    }
    return out;
}

void balanceWindow(int part, int role, float& lo, float& hi)
{
    // The loudest sample against the kick's, dB. The lanes after Totality's research fader levels (closed hat -8, open hat
    // -10, conga -15) -- a trance mix carries its hats like a techno one; the clap at the snare's; the crash and the ride
    // under the hats. The voices after Dok. 5 and 7: the lead up front, a few dB under the kick's peak (a supersaw's peak
    // is far above its level); the pluck and the arp behind it; the pad and the counter further back; the bass's harmonics
    // near the kick; the effects a swell under the lead. [I], to be heard.
    if (part < BalLanes) {
        //                                 closed rolling open  ride  clap  ghost snare  rim shaker  tom conga noise crash tamb
        static const float LaneLo[NumPercRoles] = { -14, -16, -16, -18, -10, -18, -12, -16, -20, -16, -18, -20, -18, -20 };
        static const float LaneHi[NumPercRoles] = {  -8, -10, -10, -12,  -5, -12,  -6, -10, -14, -10, -12, -12, -10, -14 };
        const int r = std::clamp(role, 0, NumPercRoles - 1);
        lo = LaneLo[r];
        hi = LaneHi[r];
        return;
    }
    switch (static_cast<BalPartE>(part)) {
    case BalPartE::BASS:    lo = -9.0f;  hi = -4.0f;  return;
    case BalPartE::ACID:    lo = -8.0f;  hi = -3.0f;  return;
    case BalPartE::LEAD:    lo = -7.0f;  hi = -2.0f;  return;
    case BalPartE::COUNTER: lo = -15.0f; hi = -9.0f;  return;
    case BalPartE::PLUCK:   lo = -11.0f; hi = -6.0f;  return;
    case BalPartE::ARP:     lo = -13.0f; hi = -8.0f;  return;
    case BalPartE::PAD:     lo = -13.0f; hi = -8.0f;  return;
    case BalPartE::STAB:    lo = -12.0f; hi = -7.0f;  return;
    // The piano by its loudest sample reads its hammer: after the voicing against Pianoteq (Phase 8; the knock, the
    // harder felt) its attack stands far over its body, which at -9 .. -3 sat 20 dB under the mix in Dream House, whose
    // melody it carries (and the low mids, 150 .. 400 Hz, under the references' corridor).
    case BalPartE::PIANO:   lo = -5.0f;  hi = -1.0f;  return;
    case BalPartE::STRINGS: lo = -16.0f; hi = -10.0f; return;   // (sustained: their energy is more than their peak says)
    case BalPartE::CHOIR:   lo = -17.0f; hi = -11.0f; return;
    case BalPartE::BRASS:   lo = -10.0f; hi = -4.0f;  return;
    case BalPartE::TIMPANI: lo = -12.0f; hi = -5.0f;  return;
    case BalPartE::FX:      lo = -12.0f; hi = -5.0f;  return;
    default:               lo = -60.0f; hi = 0.0f;   return;
    }
}

std::vector<LevelReading> levelScore(Score& score, const ParamStore& params, double seconds, const std::function<bool()>& stop)
{
    std::vector<LevelReading> out;
    if (score.m_levels.empty()) return out;
    auto e = std::make_unique<Engine>();
    ParamStore& p = e->params();
    p.copyValuesFrom(params);
    const int master = p.id(ModuleE::MASTER, 0, master::LEVEL);
    p.set(master, p.defaultValue(master));   // the player's master level comes on top of the correction
    e->prepare(Rate, Block);
    Score s = score;
    for (LevelMark& m : s.m_levels) if (!std::isnan(m.m_targetLufs)) { m.m_trimDb = 0.0f; m.m_balDb = BalanceDbT{}; m.m_breakDb = 0.0f; m.m_buildDb = 0.0f; }
    out.resize(s.m_levels.size());

    // 1. The balance: the main drop and 32 bars before it, against the loudest kick of both.
    e->load(s);
    for (size_t i = 0; i < s.m_levels.size(); ++i) {
        const LevelMark& m = s.m_levels[i];
        out[i].m_beat = m.m_beat;
        out[i].m_target = m.m_targetLufs;
        out[i].m_found.fill(std::numeric_limits<float>::quiet_NaN());
        if (std::isnan(m.m_targetLufs)) continue;
        PartPeaks peaks;
        for (const double back : { 0.0, 128.0 }) {
            const double at = m.m_peakBeat + (back > 0.0 ? 64.0 : 0.0);   // the drop's start and its second half
            if (!readParts(*e, s, at, BalSeconds, peaks, stop)) return {};
        }
        out[i].m_found = against(peaks);
        out[i].m_bal = corrections(*e, out[i].m_found, m.m_windowDb);
        s.m_levels[i].m_balDb = out[i].m_bal;
    }

    // 2. The breakdown against the drop (the correction, checked and refined, and checked again: the gap reported is the
    // one the last correction left).
    const double trackEnd = s.m_lengthBeats;
    for (size_t i = 0; i < s.m_levels.size(); ++i) {
        LevelMark& m = s.m_levels[i];
        out[i].m_gapBefore = out[i].m_gapAfter = std::numeric_limits<float>::quiet_NaN();
        if (std::isnan(m.m_targetLufs)) continue;
        const double end = i + 1 < s.m_levels.size() ? s.m_levels[i + 1].m_beat : trackEnd;
        const Section* bd = mainBreakdown(s, m.m_beat, end);
        if (bd == nullptr) continue;
        for (int round = 0; round < 3; ++round) {
            e->load(s);
            // The breakdown's own loudness begins after its first three seconds (the meter's short-term window).
            const double bdSeconds = s.m_tempo.secondsAt(bd->m_beat + bd->m_length) - s.m_tempo.secondsAt(bd->m_beat) - 3.0;
            const std::optional<LoudnessReport> rb = measure(*e, s, s.m_tempo.beatAt(s.m_tempo.secondsAt(bd->m_beat) + 3.0),
                                                             std::max(3.5, bdSeconds), stop, 3.0);
            const std::optional<LoudnessReport> rd = measureDrop(*e, s, m.m_peakBeat, std::max(seconds, sectionSeconds(s, m.m_peakBeat)), stop);
            if (!rb || !rd) return {};
            const float gap = static_cast<float>(rd->m_shortTermMax - rb->m_shortTermMax);
            if (round == 0) out[i].m_gapBefore = gap;
            out[i].m_gapAfter = gap;
            if (std::fabs(gap - m.m_gapLu) < 0.4f || round == 2) break;
            m.m_breakDb = std::clamp(m.m_breakDb + (gap - m.m_gapLu), -BreakMost, BreakMost);
        }
        out[i].m_breakDb = m.m_breakDb;
    }

    // 2b. Every build under the drop by a LU at least (the energy script: build 7 -> 8, drop 10; Dok. 6): the riser, the
    // snare roll and the opening pad must not outshout the drop they announce.
    for (size_t i = 0; i < s.m_levels.size(); ++i) {
        LevelMark& m = s.m_levels[i];
        if (std::isnan(m.m_targetLufs)) continue;
        const double end = i + 1 < s.m_levels.size() ? s.m_levels[i + 1].m_beat : trackEnd;
        for (int round = 0; round < 3; ++round) {
            e->load(s);
            float worst = -100.0f;
            for (const Section& sec : s.m_sections) {
                if (sec.m_kind != SectionKindE::BUILD || sec.m_beat < m.m_beat || sec.m_beat >= end) continue;
                const double secs = s.m_tempo.secondsAt(sec.m_beat + sec.m_length) - s.m_tempo.secondsAt(sec.m_beat) - 3.0;
                const std::optional<LoudnessReport> rb = measure(*e, s, s.m_tempo.beatAt(s.m_tempo.secondsAt(sec.m_beat) + 3.0),
                                                                 std::max(3.5, secs), stop, 3.0);
                if (!rb) return {};
                worst = std::max(worst, static_cast<float>(rb->m_shortTermMax));
            }
            if (worst <= -99.0f) break;
            const std::optional<LoudnessReport> rd = measureDrop(*e, s, m.m_peakBeat, std::max(seconds, sectionSeconds(s, m.m_peakBeat)), stop);
            if (!rd) return {};
            const float over = worst - static_cast<float>(rd->m_shortTermMax - 1.0);
            if (over <= 0.25f) break;
            m.m_buildDb = std::max(-BreakMost, m.m_buildDb - over - 0.3f);
        }
    }

    // 3. The loudness: the main drop as composed (with the balance and the breakdown), then with the correction.
    e->load(s);
    for (size_t i = 0; i < s.m_levels.size(); ++i) {
        const LevelMark& m = s.m_levels[i];
        if (std::isnan(m.m_targetLufs)) continue;
        const std::optional<LoudnessReport> got = measureDrop(*e, s, m.m_peakBeat, std::max(seconds, sectionSeconds(s, m.m_peakBeat)), stop);
        if (!got) return {};
        out[i].m_measured = static_cast<float>(got->m_integrated);
        out[i].m_trim = out[i].m_measured > -70.0f ? std::clamp(m.m_targetLufs - out[i].m_measured, -MostDb, MostDb) : 0.0f;
        s.m_levels[i].m_trimDb = out[i].m_trim;
    }
    e->load(s);
    for (size_t i = 0; i < s.m_levels.size(); ++i) {
        if (std::isnan(s.m_levels[i].m_targetLufs)) continue;
        const std::optional<LoudnessReport> got = measureDrop(*e, s, s.m_levels[i].m_peakBeat,
                                                              std::max(seconds, sectionSeconds(s, s.m_levels[i].m_peakBeat)), stop);
        if (!got) return {};
        const float again = static_cast<float>(got->m_integrated);
        if (again > -70.0f) out[i].m_trim = std::clamp(out[i].m_trim + (out[i].m_target - again), -MostDb, MostDb);
        out[i].m_after = again;
    }
    for (size_t i = 0; i < out.size() && i < s.m_levels.size(); ++i) out[i].m_buildDb = s.m_levels[i].m_buildDb;
    for (size_t i = 0; i < score.m_levels.size() && i < out.size(); ++i) {
        score.m_levels[i].m_trimDb = out[i].m_trim;
        score.m_levels[i].m_balDb = out[i].m_bal;
        score.m_levels[i].m_breakDb = s.m_levels[i].m_breakDb;
        score.m_levels[i].m_buildDb = s.m_levels[i].m_buildDb;
    }
    return out;
}

} // namespace parh
