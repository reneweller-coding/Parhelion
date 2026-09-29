/**
 * @file Leveler.cpp
 * @brief Every track as loud as its style means (Leveler.h).
 * @note After Totality `Core/src/Leveler.cpp` at 4d3c0d2 (29.09.2026): the readings, the ranked corrections and the
 *       two-pass loudness are its; the windows and the breakdown's correction are Parhelion's.
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
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;
constexpr double kWarm = 4.0;          ///< seconds before a measured part: the rooms fill, the notes sounding on are found
constexpr float kMostDb = 6.0f;        ///< the largest loudness correction either way (Deep's soft drops need more than 4)
constexpr double kBalSeconds = 12.0;   ///< how much the balance reads in each of its places
constexpr double kBalWarm = 2.0;       ///< and before it
constexpr float kBalDown = -8.0f, kBalUp = 10.0f;   ///< the largest corrections of a part
constexpr float kBreakMost = 8.0f;     ///< the breakdown's correction, either way

/** @brief The loudest samples of the kick and of every part. */
struct PartPeaks {
    float kick = 0.0f;
    float part[kBalParts] = {};
};

/** @brief Plays @p seconds from @p beat (after kBalWarm before it) and keeps the loudest samples. False if stopped. */
bool readParts(Engine& e, const Score& s, double beat, double seconds, PartPeaks& peaks, const std::function<bool()>& stop)
{
    const double at = s.tempo.secondsAt(beat);
    e.seek(s.tempo.beatAt(std::max(0.0, at - kBalWarm)));
    std::vector<float> l(kBlock), r(kBlock);
    const double warm = std::min(kBalWarm, at);
    for (int done = 0; done < static_cast<int>(warm * kRate); done += kBlock) {
        if (stop && stop()) return false;
        e.process(l.data(), r.data(), kBlock);
    }
    e.watchPeaks(true);
    for (int done = 0; done < static_cast<int>(seconds * kRate); done += kBlock) {
        if (stop && stop()) { e.watchPeaks(false); return false; }
        e.process(l.data(), r.data(), kBlock);
    }
    e.watchPeaks(false);
    const Deck& d = e.deck(0);
    peaks.kick = std::max(peaks.kick, d.kickPeak());
    for (int p = 0; p < kBalParts; ++p) peaks.part[p] = std::max(peaks.part[p], d.partPeak(p));
    return true;
}

/** @brief Every part's loudest sample against the loudest kick, dB (NaN: more than 60 dB under it). */
BalanceDb against(const PartPeaks& peaks)
{
    BalanceDb out;
    for (int p = 0; p < kBalParts; ++p) {
        const float v = peaks.part[p];
        out[static_cast<size_t>(p)] = peaks.kick > 1e-4f && v > peaks.kick * 1e-3f ? 20.0f * std::log10(v / peaks.kick)
                                                                                   : std::numeric_limits<float>::quiet_NaN();
    }
    return out;
}

/**
 * @brief The corrections that bring @p found into the windows. A rank within the kit's two kinds (the hat-like lanes, the
 *        percussion): the two loudest keep their windows, the third's lies 3 dB lower, the others' 6 (Totality's rule:
 *        ten parts pushed up at once flood the mids). @p windowDb moves the voices' windows (LevelMark::windowDb).
 */
BalanceDb corrections(const Engine& e, const BalanceDb& found, float windowDb)
{
    const ParamStore& ps = e.params();
    int role[kBalLanes] = {};
    for (int l = 0; l < kBalLanes; ++l) role[l] = static_cast<int>(std::lround(e.deck(0).played(ps.id(Module::Perc, l, perc::Role))));
    auto hatLike = [](int r) {
        const PercRole pr = static_cast<PercRole>(r);
        return pr == PercRole::ClosedHat || pr == PercRole::RollingHat || pr == PercRole::OpenHat || pr == PercRole::Ride
            || pr == PercRole::Shaker || pr == PercRole::Tambourine || pr == PercRole::Crash;
    };
    float lower[kBalParts] = {};
    for (int kind = 0; kind < 2; ++kind) {
        std::vector<std::pair<float, int>> heard;
        for (int p = 0; p < kBalLanes; ++p)
            if ((hatLike(role[p]) ? 0 : 1) == kind && !std::isnan(found[static_cast<size_t>(p)])) heard.emplace_back(found[static_cast<size_t>(p)], p);
        std::stable_sort(heard.begin(), heard.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t k = 0; k < heard.size(); ++k) lower[heard[k].second] = k < 2 ? 0.0f : k == 2 ? 3.0f : 6.0f;
    }
    BalanceDb out{};
    for (int p = 0; p < kBalParts; ++p) {
        const float f = found[static_cast<size_t>(p)];
        if (std::isnan(f) || p == static_cast<int>(BalPart::Room)) continue;
        float lo, hi;
        balanceWindow(p, p < kBalLanes ? role[p] : -1, lo, hi);
        lo -= lower[p];
        hi -= lower[p];
        if (p > static_cast<int>(BalPart::Bass) && p != static_cast<int>(BalPart::Fx)) { lo += windowDb; hi += windowDb; }
        out[static_cast<size_t>(p)] = std::clamp(f < lo ? lo - f : f > hi ? hi - f : 0.0f, kBalDown, kBalUp);
    }
    return out;
}

/** @brief The loudness of @p seconds from @p beat (after kWarm before it): integrated and the short-term maximum. */
std::optional<LoudnessReport> measure(Engine& e, const Score& s, double beat, double seconds, const std::function<bool()>& stop,
                                      double warmSeconds = kWarm)
{
    const double at = s.tempo.secondsAt(beat);
    e.seek(s.tempo.beatAt(std::max(0.0, at - warmSeconds)));
    std::vector<float> l(kBlock), r(kBlock);
    const double warm = std::min(warmSeconds, at);
    for (int done = 0; done < static_cast<int>(warm * kRate); done += kBlock) {
        if (stop && stop()) return std::nullopt;
        e.process(l.data(), r.data(), kBlock);
    }
    LoudnessMeter meter;
    meter.prepare(kRate);
    for (int done = 0; done < static_cast<int>(seconds * kRate); done += kBlock) {
        if (stop && stop()) return std::nullopt;
        e.process(l.data(), r.data(), kBlock);
        meter.process(l.data(), r.data(), kBlock);
    }
    return meter.report();
}

/**
 * @brief The drop as the references were measured (Tools/analyze_ref.py): the whole of it from @p beat for @p seconds
 *        (at most 150), every 20-second window hopping 5 s -- the loudest window's integrated loudness is the drop's
 *        (the references' loud20), the loudest 3 s anywhere in it its short-term maximum. A drop that grows (its second
 *        half an octave up, the choir coming in) is measured where it is loudest, not where it begins.
 */
std::optional<LoudnessReport> measureDrop(Engine& e, const Score& s, double beat, double seconds, const std::function<bool()>& stop)
{
    seconds = std::clamp(seconds, 20.0, 150.0);
    const double at = s.tempo.secondsAt(beat);
    e.seek(s.tempo.beatAt(std::max(0.0, at - kWarm)));
    std::vector<float> l(kBlock), r(kBlock);
    const double warm = std::min(kWarm, at);
    for (int done = 0; done < static_cast<int>(warm * kRate); done += kBlock) {
        if (stop && stop()) return std::nullopt;
        e.process(l.data(), r.data(), kBlock);
    }
    const int windows = static_cast<int>((seconds - 20.0) / 5.0) + 1;
    std::vector<LoudnessMeter> meters(static_cast<size_t>(windows));
    for (LoudnessMeter& m : meters) m.prepare(kRate);
    LoudnessMeter whole;
    whole.prepare(kRate);
    const int total = static_cast<int>(seconds * kRate);
    for (int done = 0; done < total; done += kBlock) {
        if (stop && stop()) return std::nullopt;
        e.process(l.data(), r.data(), kBlock);
        whole.process(l.data(), r.data(), kBlock);
        const double t = static_cast<double>(done) / kRate;
        for (int w = 0; w < windows; ++w)
            if (t >= 5.0 * w && t < 5.0 * w + 20.0) meters[static_cast<size_t>(w)].process(l.data(), r.data(), kBlock);
    }
    LoudnessReport rep = whole.report();
    double loudest = -120.0;
    for (LoudnessMeter& m : meters) loudest = std::max(loudest, m.report().integrated);
    rep.integrated = loudest;
    return rep;
}

/** @brief The length in seconds of the section that begins at or contains @p beat. */
double sectionSeconds(const Score& s, double beat)
{
    for (const Section& sec : s.sections)
        if (beat >= sec.beat - 1e-9 && beat < sec.beat + sec.length) return s.tempo.secondsAt(sec.beat + sec.length) - s.tempo.secondsAt(beat);
    return 20.0;
}

/** @brief The main breakdown of the track that begins at @p from: the longest Breakdown section (else Break) after it. */
const Section* mainBreakdown(const Score& s, double from, double to)
{
    const Section* best = nullptr;
    for (const Section& sec : s.sections) {
        if (sec.beat < from || sec.beat >= to) continue;
        const bool bd = sec.kind == SectionKind::Breakdown, br = sec.kind == SectionKind::Break;
        if (!bd && !br) continue;
        const bool bestBd = best != nullptr && best->kind == SectionKind::Breakdown;
        if (best == nullptr || (bd && !bestBd) || (bd == bestBd && sec.length > best->length)) best = &sec;
    }
    return best;
}

} // namespace

std::vector<LevelReading> levelSet(SetScore& set, const ParamStore& params, double seconds, const std::function<bool()>& stop)
{
    std::vector<LevelReading> out;
    for (int d = 0; d < 2; ++d) {
        if (set.decks[d].levels.empty()) continue;
        const std::vector<LevelReading> r = levelScore(set.decks[d], params, seconds, stop);
        if (r.empty() && stop && stop()) return {};
        out.insert(out.end(), r.begin(), r.end());
    }
    // The teases: the corrections of their source.
    for (LevelMark& m : set.decks[2].levels) {
        if (!std::isnan(m.targetLufs)) continue;
        for (int d = 0; d < 2; ++d)
            for (const LevelMark& src : set.decks[d].levels)
                if (src.peakBeat == m.peakBeat) { m.trimDb = src.trimDb; m.balDb = src.balDb; m.breakDb = src.breakDb; m.buildDb = src.buildDb; }
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
    if (part < kBalLanes) {
        //                                 closed rolling open  ride  clap  ghost snare  rim shaker  tom conga noise crash tamb
        static const float kLaneLo[kNumPercRoles] = { -14, -16, -16, -18, -10, -18, -12, -16, -20, -16, -18, -20, -18, -20 };
        static const float kLaneHi[kNumPercRoles] = {  -8, -10, -10, -12,  -5, -12,  -6, -10, -14, -10, -12, -12, -10, -14 };
        const int r = std::clamp(role, 0, kNumPercRoles - 1);
        lo = kLaneLo[r];
        hi = kLaneHi[r];
        return;
    }
    switch (static_cast<BalPart>(part)) {
    case BalPart::Bass:    lo = -9.0f;  hi = -4.0f;  return;
    case BalPart::Acid:    lo = -8.0f;  hi = -3.0f;  return;
    case BalPart::Lead:    lo = -7.0f;  hi = -2.0f;  return;
    case BalPart::Counter: lo = -15.0f; hi = -9.0f;  return;
    case BalPart::Pluck:   lo = -11.0f; hi = -6.0f;  return;
    case BalPart::Arp:     lo = -13.0f; hi = -8.0f;  return;
    case BalPart::Pad:     lo = -13.0f; hi = -8.0f;  return;
    case BalPart::Stab:    lo = -12.0f; hi = -7.0f;  return;
    case BalPart::Piano:   lo = -9.0f;  hi = -3.0f;  return;
    case BalPart::Strings: lo = -16.0f; hi = -10.0f; return;   // (sustained: their energy is more than their peak says)
    case BalPart::Choir:   lo = -17.0f; hi = -11.0f; return;
    case BalPart::Brass:   lo = -10.0f; hi = -4.0f;  return;
    case BalPart::Timpani: lo = -12.0f; hi = -5.0f;  return;
    case BalPart::Fx:      lo = -12.0f; hi = -5.0f;  return;
    default:               lo = -60.0f; hi = 0.0f;   return;
    }
}

std::vector<LevelReading> levelScore(Score& score, const ParamStore& params, double seconds, const std::function<bool()>& stop)
{
    std::vector<LevelReading> out;
    if (score.levels.empty()) return out;
    auto e = std::make_unique<Engine>();
    ParamStore& p = e->params();
    p.copyValuesFrom(params);
    const int master = p.id(Module::Master, 0, master::Level);
    p.set(master, p.defaultValue(master));   // the player's master level comes on top of the correction
    e->prepare(kRate, kBlock);
    Score s = score;
    for (LevelMark& m : s.levels) if (!std::isnan(m.targetLufs)) { m.trimDb = 0.0f; m.balDb = BalanceDb{}; m.breakDb = 0.0f; m.buildDb = 0.0f; }
    out.resize(s.levels.size());

    // 1. The balance: the main drop and 32 bars before it, against the loudest kick of both.
    e->load(s);
    for (size_t i = 0; i < s.levels.size(); ++i) {
        const LevelMark& m = s.levels[i];
        out[i].beat = m.beat;
        out[i].target = m.targetLufs;
        out[i].found.fill(std::numeric_limits<float>::quiet_NaN());
        if (std::isnan(m.targetLufs)) continue;
        PartPeaks peaks;
        for (const double back : { 0.0, 128.0 }) {
            const double at = m.peakBeat + (back > 0.0 ? 64.0 : 0.0);   // the drop's start and its second half
            if (!readParts(*e, s, at, kBalSeconds, peaks, stop)) return {};
        }
        out[i].found = against(peaks);
        out[i].bal = corrections(*e, out[i].found, m.windowDb);
        s.levels[i].balDb = out[i].bal;
    }

    // 2. The breakdown against the drop (the correction, checked and refined, and checked again: the gap reported is the
    // one the last correction left).
    const double trackEnd = s.lengthBeats;
    for (size_t i = 0; i < s.levels.size(); ++i) {
        LevelMark& m = s.levels[i];
        out[i].gapBefore = out[i].gapAfter = std::numeric_limits<float>::quiet_NaN();
        if (std::isnan(m.targetLufs)) continue;
        const double end = i + 1 < s.levels.size() ? s.levels[i + 1].beat : trackEnd;
        const Section* bd = mainBreakdown(s, m.beat, end);
        if (bd == nullptr) continue;
        for (int round = 0; round < 3; ++round) {
            e->load(s);
            // The breakdown's own loudness begins after its first three seconds (the meter's short-term window).
            const double bdSeconds = s.tempo.secondsAt(bd->beat + bd->length) - s.tempo.secondsAt(bd->beat) - 3.0;
            const std::optional<LoudnessReport> rb = measure(*e, s, s.tempo.beatAt(s.tempo.secondsAt(bd->beat) + 3.0),
                                                             std::max(3.5, bdSeconds), stop, 3.0);
            const std::optional<LoudnessReport> rd = measureDrop(*e, s, m.peakBeat, std::max(seconds, sectionSeconds(s, m.peakBeat)), stop);
            if (!rb || !rd) return {};
            const float gap = static_cast<float>(rd->shortTermMax - rb->shortTermMax);
            if (round == 0) out[i].gapBefore = gap;
            out[i].gapAfter = gap;
            if (std::fabs(gap - m.gapLu) < 0.4f || round == 2) break;
            m.breakDb = std::clamp(m.breakDb + (gap - m.gapLu), -kBreakMost, kBreakMost);
        }
        out[i].breakDb = m.breakDb;
    }

    // 2b. Every build under the drop by a LU at least (the energy script: build 7 -> 8, drop 10; Dok. 6): the riser, the
    // snare roll and the opening pad must not outshout the drop they announce.
    for (size_t i = 0; i < s.levels.size(); ++i) {
        LevelMark& m = s.levels[i];
        if (std::isnan(m.targetLufs)) continue;
        const double end = i + 1 < s.levels.size() ? s.levels[i + 1].beat : trackEnd;
        for (int round = 0; round < 3; ++round) {
            e->load(s);
            float worst = -100.0f;
            for (const Section& sec : s.sections) {
                if (sec.kind != SectionKind::Build || sec.beat < m.beat || sec.beat >= end) continue;
                const double secs = s.tempo.secondsAt(sec.beat + sec.length) - s.tempo.secondsAt(sec.beat) - 3.0;
                const std::optional<LoudnessReport> rb = measure(*e, s, s.tempo.beatAt(s.tempo.secondsAt(sec.beat) + 3.0),
                                                                 std::max(3.5, secs), stop, 3.0);
                if (!rb) return {};
                worst = std::max(worst, static_cast<float>(rb->shortTermMax));
            }
            if (worst <= -99.0f) break;
            const std::optional<LoudnessReport> rd = measureDrop(*e, s, m.peakBeat, std::max(seconds, sectionSeconds(s, m.peakBeat)), stop);
            if (!rd) return {};
            const float over = worst - static_cast<float>(rd->shortTermMax - 1.0);
            if (over <= 0.25f) break;
            m.buildDb = std::max(-kBreakMost, m.buildDb - over - 0.3f);
        }
    }

    // 3. The loudness: the main drop as composed (with the balance and the breakdown), then with the correction.
    e->load(s);
    for (size_t i = 0; i < s.levels.size(); ++i) {
        const LevelMark& m = s.levels[i];
        if (std::isnan(m.targetLufs)) continue;
        const std::optional<LoudnessReport> got = measureDrop(*e, s, m.peakBeat, std::max(seconds, sectionSeconds(s, m.peakBeat)), stop);
        if (!got) return {};
        out[i].measured = static_cast<float>(got->integrated);
        out[i].trim = out[i].measured > -70.0f ? std::clamp(m.targetLufs - out[i].measured, -kMostDb, kMostDb) : 0.0f;
        s.levels[i].trimDb = out[i].trim;
    }
    e->load(s);
    for (size_t i = 0; i < s.levels.size(); ++i) {
        if (std::isnan(s.levels[i].targetLufs)) continue;
        const std::optional<LoudnessReport> got = measureDrop(*e, s, s.levels[i].peakBeat,
                                                              std::max(seconds, sectionSeconds(s, s.levels[i].peakBeat)), stop);
        if (!got) return {};
        const float again = static_cast<float>(got->integrated);
        if (again > -70.0f) out[i].trim = std::clamp(out[i].trim + (out[i].target - again), -kMostDb, kMostDb);
        out[i].after = again;
    }
    for (size_t i = 0; i < out.size() && i < s.levels.size(); ++i) out[i].buildDb = s.levels[i].buildDb;
    for (size_t i = 0; i < score.levels.size() && i < out.size(); ++i) {
        score.levels[i].trimDb = out[i].trim;
        score.levels[i].balDb = out[i].bal;
        score.levels[i].breakDb = s.levels[i].breakDb;
        score.levels[i].buildDb = s.levels[i].buildDb;
    }
    return out;
}

} // namespace parh
