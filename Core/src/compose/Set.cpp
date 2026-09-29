/**
 * @file Set.cpp
 * @brief The set composer (Set.h).
 *
 * @note Built from Totality `Core/src/compose/Set.cpp` at 4d3c0d2 (29.09.2026): the tempo map, the decks, the knobs
 *       homed between tracks and the mixer's gestures are its; the dramaturgies, the keys, the placement and the
 *       tease are trance's.
 */
#include "parh/compose/Set.h"
#include "parh/Dsp.h"
#include "parh/compose/Style.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace parh {

const char* const kDramaturgyNames[] = { "Warm-up", "Peak", "Closing", "Sunrise", "Journey" };

namespace {

constexpr double kBar = 4.0;
constexpr double kPiD = 3.14159265358979323846;

uint64_t hashName(const std::string& s)
{
    uint64_t h = 1469598103934665603ull;   // FNV-1a
    for (char c : s) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
    return h;
}

/** @brief The style ladder (Set.h), a place on it morphed between neighbours. */
StyleProfile ladderProfile(float x)
{
    static const Style kLadder[5] = { Style::Deep, Style::Progressive, Style::DreamHouse, Style::Uplifting, Style::Acid };
    x = std::clamp(x, 0.0f, 4.0f);
    const int i = std::min(3, static_cast<int>(x));
    return morphProfile(styleProfile(kLadder[i]), styleProfile(kLadder[i + 1]), x - static_cast<float>(i));
}

/** @brief A minor mode after the profile's weights (Ionian left out), or Ionian for a major key. */
int scaleFor(const StyleProfile& prof, bool minor, float u)
{
    if (!minor) return static_cast<int>(Scale::Ionian);
    std::array<float, 6> w = prof.scales;
    w[static_cast<size_t>(Scale::Ionian)] = 0.0f;
    float sum = 0.0f;
    for (float x : w) sum += x;
    if (sum <= 0.0f) return static_cast<int>(Scale::Aeolian);
    return drawWeighted(w.data(), 6, u);
}

void appendShifted(Score& dst, const Score& src, double offset)
{
    for (NoteEvent n : src.notes) { n.beat += offset; dst.notes.push_back(n); }
    for (Gesture g : src.gestures) { g.beat += offset; dst.gestures.push_back(g); }
    for (Marker m : src.markers) { m.beat += offset; dst.markers.push_back(m); }
    for (LevelMark l : src.levels) { l.beat += offset; l.peakBeat += offset; dst.levels.push_back(l); }
    for (KnobSet k : src.knobs) { k.beat += offset; dst.knobs.push_back(k); }
    for (SoundPick s : src.sounds) { s.beat += offset; dst.sounds.push_back(s); }
    for (Section s : src.sections) { s.beat += offset; dst.sections.push_back(s); }
}

Gesture stepOf(const ParamStore& p, int id, double beat, float value)
{
    Gesture g;
    g.param = id;
    g.beat = beat;
    g.length = 0.0;
    g.from = g.to = p.toNormalised(id, value) - p.toNormalised(id, p.get(id));
    g.shape = GestureShape::Step;
    return g;
}

Gesture rampOf(const ParamStore& p, int id, double beat, double length, float from, float to, GestureShape shape)
{
    Gesture g;
    g.param = id;
    g.beat = beat;
    g.length = length;
    g.from = p.toNormalised(id, from) - p.toNormalised(id, p.get(id));
    g.to = p.toNormalised(id, to) - p.toNormalised(id, p.get(id));
    g.shape = shape;
    return g;
}

/**
 * @brief Before each piece on a deck, every knob any piece on that deck moves goes back to the knob (a Step to offset 0
 *        at the piece's start, written before the piece's own), so no piece inherits the last one's automation.
 */
void homeBetween(Score& deck, const std::vector<std::pair<double, std::vector<Gesture>>>& pieces, int mixFirst, int mixEnd)
{
    std::set<int> moved;
    for (const auto& piece : pieces) for (const Gesture& g : piece.second) if (g.param < mixFirst || g.param >= mixEnd) moved.insert(g.param);
    for (const auto& piece : pieces) {
        for (int id : moved) {
            Gesture g;
            g.param = id;
            g.beat = piece.first;
            g.length = 0.0;
            g.shape = GestureShape::Step;
            deck.gestures.push_back(g);
        }
        for (const Gesture& g : piece.second) deck.gestures.push_back(g);
    }
}

bool minorScale(int scale) { return scale != static_cast<int>(Scale::Ionian); }

} // namespace

float setEnergy(Dramaturgy d, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    switch (d) {
    case Dramaturgy::WarmUp: return 0.3f + 0.4f * t;
    case Dramaturgy::Closing: return 0.85f - 0.45f * t;
    case Dramaturgy::Sunrise: return 0.45f + 0.55f * t;
    case Dramaturgy::Journey: return 0.45f + 0.15f * t + 0.3f * static_cast<float>(std::sin(2.0 * kPiD * 1.5 * t));
    default: return t < 0.7f ? 0.55f + 0.45f * t / 0.7f : 1.0f - 0.25f * (t - 0.7f) / 0.3f;   // Peak
    }
}

float setTempo(Dramaturgy d, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    switch (d) {
    case Dramaturgy::WarmUp: return 130.0f + 6.0f * t;
    case Dramaturgy::Closing: return 139.0f - 7.0f * t;
    case Dramaturgy::Sunrise: return 134.0f + 4.0f * t;
    case Dramaturgy::Journey: return 132.0f + 6.0f * t;
    default: return t < 0.7f ? 136.0f + 4.0f * t / 0.7f : 140.0f;   // Peak
    }
}

float setLadder(Dramaturgy d, float t, float e)
{
    t = std::clamp(t, 0.0f, 1.0f);
    switch (d) {
    case Dramaturgy::WarmUp: return 1.2f * t;                 // Deep -> Progressive
    case Dramaturgy::Closing: return 3.0f - 2.0f * t;         // Uplifting -> Progressive
    case Dramaturgy::Sunrise: return 1.0f + 2.0f * t;         // Progressive -> Uplifting
    case Dramaturgy::Journey: return 4.0f * std::clamp(e, 0.0f, 1.0f);
    default: return 2.6f + 1.4f * std::clamp(e, 0.0f, 1.0f); // Peak: Uplifting and Acid
    }
}

SetScore composeSet(const ParamStore& p, uint64_t seed, double minutes, const Curation* cur, SetInfo* info,
                    const std::function<void(Score&)>& prepare, const std::function<bool()>& stop)
{
    const Dramaturgy dram = static_cast<Dramaturgy>(p.getInt(p.id(Module::Set, 0, set::Dramaturgy)));
    const bool wander = p.getInt(p.id(Module::Set, 0, set::Journey)) != 0;
    const int blendBars = p.getInt(p.id(Module::Set, 0, set::BlendBars)) == 0 ? 32 : 64;
    const float trackMinutes = p.get(p.id(Module::Set, 0, set::TrackMinutes));
    const double totalSeconds = std::max(1.0, minutes) * 60.0;
    Rng sr;
    sr.seed(mixSeed(mixSeed(seed, hashName("set")), static_cast<uint64_t>(cur != nullptr ? cur->count("set") : 0)));
    const StyleProfile fixed = profileOf(p);

    SetInfo si;
    si.dramaturgy = dram;
    std::vector<Score> scores;
    TempoMap tempo;
    double seconds = 0.0;
    float bpmPrev = 0.0f;
    int keyPrev = -1;
    bool minorPrev = true;
    for (int i = 0; i < 256; ++i) {
        if (i > 0 && stop && stop()) break;
        const float t = static_cast<float>(std::min(1.0, seconds / totalSeconds));
        const float e = setEnergy(dram, t);
        StyleProfile prof = wander ? ladderProfile(setLadder(dram, t, e)) : fixed;
        // Sunrise: the last track (the one that will end the set) the most euphoric -- Uplifting with its orchestra.
        const bool last = seconds + 0.85 * trackMinutes * 60.0 >= totalSeconds;
        if (dram == Dramaturgy::Sunrise && last) { prof = styleProfile(Style::Uplifting); prof.orchestra = 1.0f; }
        float bpm = std::round(2.0f * setTempo(dram, t)) * 0.5f;
        if (i > 0) bpm = std::clamp(bpm, bpmPrev - 2.0f, bpmPrev + 2.0f);
        // The key round the Camelot wheel (drawn every time, so the stream stays put).
        const float uk = sr.uniform(), ud = sr.uniform(), um = sr.uniform(), us = sr.uniform();
        const int anyKey = sr.below(12);
        int key = anyKey;
        bool minor = um < prof.minor;
        if (i > 0) {
            minor = minorPrev;
            if (uk < 0.3f) key = keyPrev;                                                   // the same
            else if (uk < 0.75f) key = (keyPrev + (ud < 0.5f ? 7 : 5)) % 12;                // a fifth either way
            // The relative: from minor now and then, from major more often (trance lives in minor: 84.8 %, Knees et al.).
            else if (uk < (minorPrev ? 0.9f : 1.0f)) { key = minorPrev ? (keyPrev + 3) % 12 : (keyPrev + 9) % 12; minor = !minorPrev; }
            else if (uk < 0.95f) key = (keyPrev + (ud < 0.5f ? 1 : 2)) % 12;                // the energy boost
        }
        const int scale = scaleFor(prof, minor, us);
        const std::string unit = "track" + std::to_string(i + 1) + ".";
        const uint64_t trackSeed = mixSeed(mixSeed(seed, 0x545241434Bull + static_cast<uint64_t>(i)),
                                           static_cast<uint64_t>(cur != nullptr ? cur->count("track" + std::to_string(i + 1)) : 0));
        TrackRequest req;
        req.profile = &prof;
        req.bpm = bpm;
        req.key = key;
        req.scale = scale;
        req.bars = std::max(96, static_cast<int>(std::lround(trackMinutes * bpm / 4.0 / 32.0)) * 32);
        req.energy = e;
        req.mixable = true;
        SetTrack st;
        st.energy = e;
        st.seed = trackSeed;
        Score sc = composeTrack(p, trackSeed, req, cur, unit, &st.info);
        if (prepare) prepare(sc);
        if (i == 0) {
            st.deck = 0;
            st.start = 0.0;
            st.swapIn = 0.0;
            tempo.setConstant(bpm);
        } else {
            SetTrack& prev = si.tracks.back();
            st.deck = 1 - prev.deck;
            // The incoming intro over the outgoing end; the swap 16 bars before the incoming groove.
            st.start = prev.end - st.info.introBars * kBar;
            const double swap = st.start + std::max(st.info.bassBar, st.info.introBars - 16) * kBar;
            prev.swapOut = swap;
            st.swapIn = swap;
            const double open = std::max(st.start, swap - blendBars * kBar);
            tempo.add(open, bpmPrev, true);
            tempo.add(swap, bpm, false);
        }
        st.end = st.start + st.info.bars * kBar;
        st.swapOut = st.end;
        seconds = tempo.secondsAt(st.start + st.info.outroBar * kBar);
        si.tracks.push_back(st);
        scores.push_back(std::move(sc));
        bpmPrev = bpm;
        keyPrev = key;
        minorPrev = minor;
        if (seconds >= totalSeconds) break;
    }
    const size_t n = si.tracks.size();

    SetScore out;
    out.lengthBeats = si.tracks.back().end;
    for (Score& d : out.decks) { d.clear(130.0); d.tempo = tempo; d.lengthBeats = out.lengthBeats; }
    std::vector<std::pair<double, std::vector<Gesture>>> pieces[kDecks];
    const int mixFirst = p.base(Module::Deck, 0), mixEnd = mixFirst + kDecks * deck::Count;
    for (size_t i = 0; i < n; ++i) {
        const SetTrack& st = si.tracks[i];
        Score& deckScore = out.decks[st.deck];
        Score shifted;
        appendShifted(shifted, scores[i], st.start);
        deckScore.notes.insert(deckScore.notes.end(), shifted.notes.begin(), shifted.notes.end());
        deckScore.markers.insert(deckScore.markers.end(), shifted.markers.begin(), shifted.markers.end());
        deckScore.levels.insert(deckScore.levels.end(), shifted.levels.begin(), shifted.levels.end());
        deckScore.knobs.insert(deckScore.knobs.end(), shifted.knobs.begin(), shifted.knobs.end());
        deckScore.sounds.insert(deckScore.sounds.end(), shifted.sounds.begin(), shifted.sounds.end());
        deckScore.sections.insert(deckScore.sections.end(), shifted.sections.begin(), shifted.sections.end());
        deckScore.keyRoot = scores[i].keyRoot;
        deckScore.scale = scores[i].scale;
        std::vector<Gesture> g = shifted.gestures;
        const int d = st.deck;
        const int fader = p.id(Module::Deck, d, deck::Fader), low = p.id(Module::Deck, d, deck::Low);
        const int mid = p.id(Module::Deck, d, deck::Mid), high = p.id(Module::Deck, d, deck::High);
        const int filter = p.id(Module::Deck, d, deck::Filter), send = p.id(Module::Deck, d, deck::FxSend);
        g.push_back(stepOf(p, filter, st.start, 0.0f));
        g.push_back(stepOf(p, send, st.start, 0.0f));
        if (i > 0) {
            // In: the low band killed until the swap; the fader opens the blend's bars before it (or at the start) with the
            // highs 8 dB and the mids 10 dB down, back over the blend.
            const double open = std::max(st.start, st.swapIn - blendBars * kBar);
            g.push_back(stepOf(p, low, st.start, -60.0f));
            g.push_back(stepOf(p, fader, st.start, -60.0f));
            g.push_back(stepOf(p, high, st.start, -8.0f));
            g.push_back(stepOf(p, mid, st.start, -10.0f));
            g.push_back(rampOf(p, fader, open, 8.0 * kBar, -60.0f, 0.0f, GestureShape::EaseOut));
            g.push_back(rampOf(p, high, open, st.swapIn - open, -8.0f, 0.0f, GestureShape::Linear));
            g.push_back(rampOf(p, mid, open, st.swapIn - open, -10.0f, 0.0f, GestureShape::EaseIn));
            g.push_back(stepOf(p, low, st.swapIn, 0.0f));
        } else {
            for (int id : { low, fader, mid, high }) g.push_back(stepOf(p, id, st.start, 0.0f));
        }
        if (i + 1 < n) {
            // Out: the low band closed at the swap; the highs and hats play on, the mids down 6 dB over 8 bars, the fader
            // falling through the track's last 8 bars.
            g.push_back(stepOf(p, low, st.swapOut, -60.0f));
            g.push_back(rampOf(p, mid, st.swapOut, 8.0 * kBar, 0.0f, -6.0f, GestureShape::EaseOut));
            if (st.end - st.swapOut > 8.0 * kBar)
                g.push_back(rampOf(p, fader, st.end - 8.0 * kBar, 8.0 * kBar, 0.0f, -60.0f, GestureShape::EaseIn));
        }
        pieces[d].push_back({ st.start, g });
        out.markers.push_back(Marker{ st.start, "T" + std::to_string(i + 1) + " " + st.info.style + " " + st.info.camelot });
        if (i > 0) out.markers.push_back(Marker{ st.swapIn, "Swap T" + std::to_string(i) + " > T" + std::to_string(i + 1) });
    }
    // The teases on deck C: the next track's hook before its blend, where the keys agree.
    Score& c = out.decks[2];
    for (size_t i = 1; i < n; ++i) {
        const SetTrack& in = si.tracks[i];
        const SetTrack& prev = si.tracks[i - 1];
        const int diff = ((in.info.key - prev.info.key) % 12 + 12) % 12;
        const bool agree = in.info.scale == prev.info.scale || minorScale(in.info.scale) == minorScale(prev.info.scale);
        if (!(agree && (diff == 0 || diff == 5 || diff == 7)) || in.info.mainDropBar < 0 || sr.uniform() > 0.6f) continue;
        const double open = std::max(in.start, in.swapIn - blendBars * kBar);
        const double from = open - 16.0 * kBar;
        if (from < prev.start + prev.info.introBars * kBar) continue;
        const double hook = in.info.mainDropBar * kBar;
        std::vector<NoteEvent> notes;
        for (Part part : { Part::Lead, Part::Piano, Part::Pluck }) {
            for (const NoteEvent& note : scores[i].notes)
                if (note.part == part && note.beat >= hook && note.beat < hook + 8.0 * kBar) notes.push_back(note);
            if (!notes.empty()) break;
        }
        if (notes.empty()) continue;
        for (int pass = 0; pass < 2; ++pass)
            for (NoteEvent note : notes) { note.beat = note.beat - hook + from + pass * 8.0 * kBar; c.notes.push_back(note); }
        for (KnobSet k : scores[i].knobs) { k.beat = from; c.knobs.push_back(k); }
        for (SoundPick s : scores[i].sounds) { s.beat = from; c.sounds.push_back(s); }
        std::vector<Gesture> g;
        const int fader = p.id(Module::Deck, 2, deck::Fader), low = p.id(Module::Deck, 2, deck::Low);
        const int filter = p.id(Module::Deck, 2, deck::Filter);
        g.push_back(stepOf(p, low, from, -60.0f));
        g.push_back(stepOf(p, fader, from, -8.0f));
        g.push_back(rampOf(p, filter, from, 16.0 * kBar, 0.6f, 0.15f, GestureShape::EaseIn));
        g.push_back(stepOf(p, fader, from + 16.0 * kBar, -60.0f));
        pieces[2].push_back({ from, g });
        si.teases.push_back(SetTease{ static_cast<int>(i), from, from + 16.0 * kBar });
        out.markers.push_back(Marker{ from, "Tease T" + std::to_string(i + 1) });
    }
    for (int d = 0; d < kDecks; ++d) homeBetween(out.decks[d], pieces[d], mixFirst, mixEnd);
    for (Score& d : out.decks) d.sort();
    std::sort(out.markers.begin(), out.markers.end(), [](const Marker& a, const Marker& b) { return a.beat < b.beat; });
    if (info != nullptr) *info = si;
    return out;
}

Score flattenSet(const SetScore& set)
{
    Score s = set.decks[0];
    for (int d = 1; d < kDecks; ++d) {
        const Score& x = set.decks[d];
        s.notes.insert(s.notes.end(), x.notes.begin(), x.notes.end());
        s.markers.insert(s.markers.end(), x.markers.begin(), x.markers.end());
    }
    s.markers.insert(s.markers.end(), set.markers.begin(), set.markers.end());
    s.gestures.clear();
    s.lengthBeats = set.lengthBeats;
    s.sort();
    return s;
}

} // namespace parh
