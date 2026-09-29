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

namespace parh {

namespace {
constexpr float kBassMinCut = 60.0f;    ///< the mid-bass's lowest low cut: under it only the kick and the sub
constexpr float kGlueMix = 0.35f;       ///< the glue in parallel (PLAN 7.4)
/** The rooms' makeup: the FDN and the plate return quietly (Totality: +22 dB for its room at sends of 0.25 to 0.4). The
 *  returns' knobs sit on top; [I] until the reference measurement (PLAN 13.4). */
constexpr float kRoomMakeupDb = 0.0f;
constexpr float kPlateMakeupDb = 0.0f;
constexpr float kHallMakeupDb = 0.0f;
constexpr double kLeadSeconds = 0.5;    ///< a deck starts playing this long before its first note
constexpr double kTailSeconds = 20.0;   ///< and plays on this long after its last (rooms, delays)
}

void Deck::prepare(const ParamStore* params, double sampleRate, int index)
{
    params_ = params;
    sampleRate_ = sampleRate;
    index_ = index;
    kick_.prepare(sampleRate);
    sub_.prepare(sampleRate);
    kit_.prepare(sampleRate);
    bass_.prepare(sampleRate);
    acid_.prepare(sampleRate);
    for (int i = 0; i < kPolyInstances; ++i) {
        poly_[i].prepare(sampleRate);
        gate_[i].prepare(sampleRate);
        polyDuck_[i].prepare(sampleRate);
    }
    retDuck_.prepare(sampleRate);
    piano_.prepare(sampleRate);
    pianoDuck_.prepare(sampleRate);
    strings_.prepare(sampleRate, 0x5354524Eull + static_cast<uint64_t>(index));   // "STRN"
    choir_.prepare(sampleRate, 0x43484F49ull + static_cast<uint64_t>(index));     // "CHOI"
    brass_.prepare(sampleRate, 0x42524153ull + static_cast<uint64_t>(index));     // "BRAS"
    timpani_.prepare(sampleRate, 0x54494D50ull + static_cast<uint64_t>(index));   // "TIMP"
    for (Ducker& d : orchDuck_) d.prepare(sampleRate);
    sfx_.prepare(sampleRate);
    fxDuck_.prepare(sampleRate);
    subDropDuck_.prepare(sampleRate);
    room_.prepare(sampleRate);
    hall_.prepare(sampleRate);
    plate_.prepare(sampleRate);
    glue_.prepare(sampleRate);
    const float fs = static_cast<float>(sampleRate);
    tiltCoef_ = 1.0f - std::exp(-2.0f * kPi * 1000.0f / fs);
    trimCoef_ = 1.0f - std::exp(-1.0f / (1.0f * fs));   // a correction glides in over about a second
    for (int p = 0; p < kBalParts; ++p) balGain_[p] = balTarget_[p] = 1.0f;
    const size_t n = static_cast<size_t>(kRaster);
    for (std::vector<float>* b : { &kickBuf_, &bodyBuf_, &subBuf_, &bassL_, &bassR_, &acidL_, &acidR_, &roomInL_, &roomInR_,
                                   &plateInL_, &plateInR_, &hallInL_, &hallInR_, &roomL_, &roomR_, &plateL_, &plateR_, &hallL_,
                                   &hallR_, &drumL_, &drumR_, &synthL_, &synthR_, &fxL_, &fxR_, &fxSub_, &fxWetL_, &fxWetR_ })
        b->assign(n, 0.0f);
    for (int i = 0; i < kPolyInstances; ++i) { polyL_[i].assign(n, 0.0f); polyR_[i].assign(n, 0.0f); }
    pianoL_.assign(n, 0.0f);
    pianoR_.assign(n, 0.0f);
    for (int o = 0; o < kOrch; ++o) { orchL_[o].assign(n, 0.0f); orchR_[o].assign(n, 0.0f); }
    clear();
}

void Deck::setQuest(bool on)
{
    // The Quest's share (PLAN 10): three unison oscillators and four voices a voice, the pad five and six.
    for (int i = 0; i < kPolyInstances; ++i) {
        const bool pad = i == static_cast<int>(PolyInstance::Pad);
        poly_[i].setQuality(on ? (pad ? 5 : 3) : kPolyUnison, on ? (pad ? 6 : 4) : kPolyVoices);
    }
    piano_.setVoiceLimit(on ? 10 : Piano::kVoices);
    // The orchestra's share: half the players and singers.
    strings_.setPlayers(on ? 3 : kStringPlayers);
    choir_.setSingers(on ? 3 : kChoirSingers);
    brass_.setPlayers(on ? 2 : 3);
}

void Deck::clear()
{
    loaded_ = false;
    score_ = Score{};
    events_.clear();
    tracks_.clear();
    trackOf_.assign(static_cast<size_t>(params_ != nullptr ? params_->count() : 0), -1);
    mixerSteps_.clear();
    plays_.clear();
    lateTrims_.clear();
    lateBal_.clear();
    base_.assign(static_cast<size_t>(params_ != nullptr ? params_->count() : 0), std::numeric_limits<float>::quiet_NaN());
    knobCursor_ = 0;
    knobGroup_ = -1.0;
    newGroup_ = false;
    evCursor_ = 0;
}

void Deck::load(const Score& score)
{
    clear();
    loaded_ = true;
    score_ = score;
    score_.sort();
    lateTrims_.reserve(score_.levels.size());
    lateBal_.reserve(score_.levels.size());
    // Events on the sample grid: the first sample at or after the ideal time, and how late that is.
    int id = 0;
    for (const NoteEvent& note : score_.notes) {
        const double x = score_.tempo.secondsAt(note.beat) * sampleRate_;
        const double s = std::ceil(x);
        const double xe = score_.tempo.secondsAt(note.beat + note.length) * sampleRate_;
        const int gate = std::max(1, static_cast<int>(std::lround(xe - x)));
        Ev on{ static_cast<int64_t>(s), 1, static_cast<uint8_t>(note.part), note.accent, note.slide, note.pitch, note.velocity,
               note.shift, s - x, id, note.length, gate };
        events_.push_back(on);
        if (!isOneShot(note.part)) {
            const double so = std::ceil(xe);
            events_.push_back(Ev{ static_cast<int64_t>(so), 0, on.part, false, false, note.pitch, 0.0f, 0, so - xe, id, 0.0, 0 });
        }
        ++id;
    }
    std::stable_sort(events_.begin(), events_.end(), [](const Ev& a, const Ev& b) {
        return a.sample != b.sample ? a.sample < b.sample : a.on < b.on;
    });
    // The automation, per parameter; the steps on this deck's mixer channel, for the engine.
    const int mixFirst = params_->base(Module::Deck, index_), mixEnd = mixFirst + deck::Count;
    for (const Gesture& g : score_.gestures) {
        if (g.param < 0 || g.param >= params_->count()) continue;
        int& t = trackOf_[static_cast<size_t>(g.param)];
        if (t < 0) { t = static_cast<int>(tracks_.size()); tracks_.push_back(Track{ g.param, {}, 0, 0.0f }); }
        tracks_[static_cast<size_t>(t)].gestures.push_back(g);
        if (g.param >= mixFirst && g.param < mixEnd)
            mixerSteps_.push_back(static_cast<int64_t>(std::ceil(score_.tempo.secondsAt(g.beat) * sampleRate_)));
    }
    std::sort(mixerSteps_.begin(), mixerSteps_.end());
    breaks_.clear();
    builds_.clear();
    for (const Section& s : score_.sections)
        if (s.kind == SectionKind::Breakdown || s.kind == SectionKind::Break) breaks_.push_back({ s.beat, s.beat + s.length });
        else if (s.kind == SectionKind::Build) builds_.push_back({ s.beat, s.beat + s.length });
    mixerSteps_.erase(std::unique(mixerSteps_.begin(), mixerSteps_.end()), mixerSteps_.end());
    // The piano's instrument: the design knobs the track starts with (or the knobs), built here, off the audio thread.
    {
        const int b = params_->base(Module::Piano, 0);
        auto value = [&](int param) {
            for (const KnobSet& k : score_.knobs) if (k.param == b + param) return k.value;
            return params_->get(b + param);
        };
        PianoSpec spec;
        spec.instrument = static_cast<int>(std::lround(value(piano::Instrument)));
        spec.hardness = value(piano::Hardness);
        spec.strike = value(piano::Strike);
        spec.unison = value(piano::Unison);
        spec.inharm = value(piano::Inharm);
        spec.impedance = value(piano::Impedance);
        spec.stretch = value(piano::Stretch);
        spec.condition = value(piano::Condition);
        bool uses = false;
        for (const NoteEvent& note : score_.notes) if (note.part == Part::Piano) { uses = true; break; }
        if (uses) piano_.setSpec(spec);
    }
    // Where it plays: around its notes, merged.
    const int64_t lead = static_cast<int64_t>(kLeadSeconds * sampleRate_), tail = static_cast<int64_t>(kTailSeconds * sampleRate_);
    for (const Ev& e : events_) {
        const int64_t from = std::max<int64_t>(0, e.sample - lead), to = e.sample + std::max<int64_t>(e.gate, 0) + tail;
        if (!plays_.empty() && from <= plays_.back().second) plays_.back().second = std::max(plays_.back().second, to);
        else plays_.push_back({ from, to });
    }
    seek(0);
}

bool Deck::playsAt(int64_t sample) const
{
    for (const auto& r : plays_) {
        if (sample < r.first) return false;
        if (sample < r.second) return true;
    }
    return false;
}

int64_t Deck::nextRestChange(int64_t sample) const
{
    for (const auto& r : plays_) {
        if (r.first > sample) return r.first;
        if (r.second > sample) return r.second;
    }
    return kNever;
}

void Deck::seek(int64_t sample)
{
    evCursor_ = 0;
    while (evCursor_ < events_.size() && events_[evCursor_].sample < sample) ++evCursor_;
    // A jump back to the start plays what is due at it; elsewhere the voices fall silent.
    if (sample == 0) evCursor_ = 0;
    for (Track& t : tracks_) { t.cursor = t.gestures.size(); t.offset = 0.0f; }
    std::fill(base_.begin(), base_.end(), std::numeric_limits<float>::quiet_NaN());
    knobCursor_ = 0;
    knobGroup_ = -1.0;
    newGroup_ = false;
    kick_.reset();
    sub_.reset();
    kit_.reset();
    bass_.reset();
    acid_.reset();
    piano_.reset();
    strings_.reset();
    choir_.reset();
    brass_.reset();
    timpani_.reset();
    for (int i = 0; i < kPolyInstances; ++i) {
        poly_[i].reset();
        // The same start phases and drift walks whatever came before (Poly.h): a render is a function of the score.
        poly_[i].seedPhases(0x9E3779B97F4A7C15ull * static_cast<uint64_t>(i + 1) + static_cast<uint64_t>(index_));
        gate_[i].reset();
        polyDuck_[i].reset();
        lastDuck_[i] = 1.0f;
    }
    retDuck_.reset();
    sfx_.reset();
    fxDuck_.reset();
    subDropDuck_.reset();
    room_.reset();
    hall_.reset();
    plate_.reset();
    glue_.reset();
    resetStems();
    for (Svf& f : hatsLp_) f.reset();
    for (Svf& f : percLp_) f.reset();
    for (auto& ch : groupHp_) for (Svf& f : ch) f.reset();
    tiltState_[0] = tiltState_[1] = 0.0f;
    snapFaders_ = true;   // the next cell puts the faders where the score has them
    haveKick_ = false;
    subNote_ = bassNote_ = acidNote_ = -1;
    const double b = score_.tempo.beatAt(static_cast<double>(sample) / sampleRate_);
    trimGain_ = trimTarget_ = dbToGain(score_.trimAt(b));
    const BalanceDb bal = score_.balanceAt(b);
    for (int p = 0; p < kBalParts; ++p) balGain_[p] = balTarget_[p] = dbToGain(bal[static_cast<size_t>(p)]);
}

float Deck::played(int id) const
{
    const float knob = params_->get(id);
    float value = knob;
    if (id >= 0 && static_cast<size_t>(id) < base_.size()) {
        const float b = base_[static_cast<size_t>(id)];
        if (b == b) {
            // The track's own value; where a hand has turned the knob away from what the engine wrote on it, by as much.
            value = b;
            const float s = shown_ != nullptr ? shown_[id] : std::numeric_limits<float>::quiet_NaN();
            if (s == s && knob != s)
                value = params_->fromNormalised(id, params_->toNormalised(id, b) + params_->toNormalised(id, knob) - params_->toNormalised(id, s));
        }
    }
    if (id < 0 || id >= static_cast<int>(trackOf_.size()) || trackOf_[static_cast<size_t>(id)] < 0) return value;
    const float off = tracks_[static_cast<size_t>(trackOf_[static_cast<size_t>(id)])].offset;
    if (off == 0.0f) return value;
    return params_->fromNormalised(id, params_->toNormalised(id, value) + off);
}

void Deck::applyKnobs(double beat)
{
    // One beat early: a track's sounds are in place before its first note (its deck rests until then).
    while (knobCursor_ < score_.knobs.size() && score_.knobs[knobCursor_].beat <= beat + 1.0) {
        const KnobSet& k = score_.knobs[knobCursor_++];
        if (k.beat != knobGroup_) {
            std::fill(base_.begin(), base_.end(), std::numeric_limits<float>::quiet_NaN());
            knobGroup_ = k.beat;
            newGroup_ = true;
        }
        if (k.param >= 0 && static_cast<size_t>(k.param) < base_.size()) base_[static_cast<size_t>(k.param)] = k.value;
    }
}

void Deck::readPlayed(Module m, int instance, float* out) const
{
    const int b = params_->base(m, instance);
    const int n = ParamStore::moduleCount(m);
    for (int i = 0; i < n; ++i) out[i] = played(b + i);
}

void Deck::updateCell(int64_t sample)
{
    const double seconds = static_cast<double>(sample) / sampleRate_;
    const double beat = score_.tempo.beatAt(seconds);
    applyKnobs(beat);
    for (Track& t : tracks_) {
        const size_t none = t.gestures.size();
        size_t c = t.cursor, next = c == none ? 0 : c + 1;
        while (next < t.gestures.size() && t.gestures[next].beat <= beat) { c = next; ++next; }
        t.cursor = c;
        t.offset = c == none ? 0.0f : gestureValue(t.gestures[c], beat);
    }
    mutes_ = 0;
    if (live_)
        for (int k = 0; k < perform::kMutes; ++k)
            if (params_->getBool(params_->id(Module::Perform, 0, perform::MuteKick + k))) mutes_ |= 1u << k;

    float c[compose::Count];
    readPlayed(Module::Compose, 0, c);
    keyRoot_ = static_cast<int>(std::lround(c[compose::Key]));
    scale_ = static_cast<int>(std::lround(c[compose::Scale]));
    const double bpm = score_.tempo.bpmAt(beat);
    beat_ = beat;
    cellSample_ = sample;
    beatsPerSample_ = bpm / (60.0 * sampleRate_);
    const float msToBeats = static_cast<float>(bpm / 60000.0);

    float v[128];
    static_assert(poly::Count <= 128 && perc::Count <= 128 && kick::Count <= 128, "the cell's scratch holds a module");
    readPlayed(Module::Kick, 0, v);
    Kick::constrain(v, 0.0, keyRoot_);
    kick_.update(v, keyRoot_);

    float pump[pump::Count];
    readPlayed(Module::Pump, 0, pump);

    readPlayed(Module::Sub, 0, v);
    sub_.update(v);

    kit_.setTempo(bpm);
    for (int l = 0; l < kPercLanes; ++l) {
        readPlayed(Module::Perc, l, v);
        const PercRole r = static_cast<PercRole>(std::lround(v[perc::Role]));
        kit_.update(l, v, keyRoot_, scale_);
        laneIsHat_[l] = r == PercRole::ClosedHat || r == PercRole::RollingHat || r == PercRole::OpenHat
                     || r == PercRole::Ride || r == PercRole::Shaker || r == PercRole::Tambourine || r == PercRole::Crash;
    }

    readPlayed(Module::Bass, 0, v);
    bass_.update(v, kBassMinCut);
    bassSends_ = Sends{ v[synth::RoomSend], v[synth::PlateSend], 0.0f };
    readPlayed(Module::Acid, 0, v);
    acid_.update(v, kBassMinCut);
    acidSends_ = Sends{ v[synth::RoomSend], v[synth::PlateSend], 0.0f };

    for (int i = 0; i < kPolyInstances; ++i) {
        readPlayed(Module::Poly, i, v);
        poly_[i].update(v, bpm);
        poly_[i].setClock(beat, beatsPerSample_);
        PolyStrip& s = strip_[i];
        s.gate = v[poly::Gate] >= 0.5f;
        s.pattern = static_cast<int>(std::lround(v[poly::GatePattern]));
        s.depth = v[poly::GateDepth];
        s.duty = v[poly::GateDuty];
        s.attackBeats = v[poly::GateAttack] * msToBeats;
        s.releaseBeats = v[poly::GateRelease] * msToBeats;
        s.tone = v[poly::GateTone];
        s.sends = Sends{ v[poly::RoomSend], v[poly::PlateSend], v[poly::HallSend] };
        // The pump's depth on this voice (poly.duck, dB) along the shared curve.
        polyDuck_[i].set(1.0f - dbToGain(-v[poly::Duck]), pump[pump::Attack], pump[pump::Hold], pump[pump::Release]);
    }
    retDuck_.set(1.0f - dbToGain(-pump[pump::ReturnDuck]), pump[pump::Attack], pump[pump::Hold], pump[pump::Release]);
    readPlayed(Module::Piano, 0, v);
    piano_.update(v);
    pianoSends_ = Sends{ v[piano::RoomSend], v[piano::PlateSend], v[piano::HallSend] };
    pianoDuck_.set(1.0f - dbToGain(-v[piano::Duck]), pump[pump::Attack], pump[pump::Hold], pump[pump::Release]);
    readPlayed(Module::Strings, 0, v);
    strings_.update(v);
    orchSends_[0] = Sends{ v[strings::RoomSend], v[strings::PlateSend], v[strings::HallSend] };
    orchDuck_[0].set(1.0f - dbToGain(-v[strings::Duck]), pump[pump::Attack], pump[pump::Hold], pump[pump::Release]);
    readPlayed(Module::Choir, 0, v);
    choir_.update(v);
    orchSends_[1] = Sends{ v[choir::RoomSend], v[choir::PlateSend], v[choir::HallSend] };
    orchDuck_[1].set(1.0f - dbToGain(-v[choir::Duck]), pump[pump::Attack], pump[pump::Hold], pump[pump::Release]);
    readPlayed(Module::Brass, 0, v);
    brass_.update(v);
    orchSends_[2] = Sends{ v[brass::RoomSend], v[brass::PlateSend], v[brass::HallSend] };
    orchDuck_[2].set(1.0f - dbToGain(-v[brass::Duck]), pump[pump::Attack], pump[pump::Hold], pump[pump::Release]);
    readPlayed(Module::Timpani, 0, v);
    timpani_.update(v);
    orchSends_[3] = Sends{ v[timpani::RoomSend], v[timpani::PlateSend], v[timpani::HallSend] };
    orchDuck_[3].set(1.0f - dbToGain(-v[timpani::Duck]), pump[pump::Attack], pump[pump::Hold], pump[pump::Release]);
    readPlayed(Module::Sfx, 0, v);
    sfx_.update(v, keyRoot_);
    sfx_.setScale(scale_);
    fxSends_ = Sends{ v[sfx::RoomSend], v[sfx::PlateSend], v[sfx::HallSend] };
    fxDuck_.set(1.0f - dbToGain(-v[sfx::Duck]), pump[pump::Attack], pump[pump::Hold], pump[pump::Release]);
    subDropDuck_.set(v[sfx::SubDuck], 0.5f, 60.0f, 90.0f);

    const float fs = static_cast<float>(sampleRate_);
    readPlayed(Module::Mix, 0, v);
    hatsGain_ = dbToGain(v[mix::HatsLevel]);
    percGain_ = dbToGain(v[mix::PercLevel]);
    // The Leveler's correction of the breakdowns (LevelMark::breakDb) on the synths and the effects, where one plays.
    float breakGain = 1.0f;
    for (const auto& b : breaks_) if (beat >= b.first && beat < b.second) {
        size_t k = 0;
        for (size_t i = 0; i < score_.levels.size(); ++i) if (score_.levels[i].beat <= beat) k = i;
        if (k < score_.levels.size()) breakGain = dbToGain(score_.levels[k].breakDb);
    }
    percBuild_ = 1.0f;
    for (const auto& b : builds_) if (beat >= b.first && beat < b.second) {
        size_t k = 0;
        for (size_t i = 0; i < score_.levels.size(); ++i) if (score_.levels[i].beat <= beat) k = i;
        if (k < score_.levels.size()) { breakGain = dbToGain(score_.levels[k].buildDb); percBuild_ = breakGain; }
    }
    synthTarget_ = dbToGain(v[mix::SynthLevel]) * breakGain;
    fxTarget_ = breakGain;
    percGain_ *= percBuild_;
    if (snapFaders_) { synthGain_ = synthTarget_; fxGain_ = fxTarget_; snapFaders_ = false; }
    for (Svf& f : hatsLp_) f.setQ(std::min(v[mix::HatsCut], 0.45f * fs), 0.7071f, fs);
    for (Svf& f : percLp_) f.setQ(std::min(v[mix::PercCut], 0.45f * fs), 0.7071f, fs);
    drumSat_ = v[mix::DrumSat];
    groupHpOn_ = v[mix::LowCut] > 20.5f;
    if (!groupHpOn_) {
        for (auto& ch : groupHp_) for (Svf& f : ch) f.reset();
        for (StemBus& s : stemBus_) for (auto& ch : s.hp) for (Svf& f : ch) f.reset();
    }
    for (auto& ch : groupHp_) {
        ch[0].setK(v[mix::LowCut], 1.8477590f, fs);
        ch[1].setK(v[mix::LowCut], 0.7653669f, fs);
    }

    readPlayed(Module::Sends, 0, v);
    const float lowCut = v[sends::LowCut], highCut = std::min(v[sends::HighCut], 0.45f * fs);
    room_.set(v[sends::RoomSize], v[sends::RoomDecay], 0.5f, 0.008f * fs, lowCut, highCut);
    plate_.set(v[sends::PlateDecay], v[sends::PlateDamping], v[sends::PlatePreDelay] * 0.001f * fs, lowCut, highCut);
    hall_.set(v[sends::HallSize], v[sends::HallDecay], v[sends::HallDamping], v[sends::HallPreDelay] * 0.001f * fs, lowCut,
              highCut);
    roomReturn_ = v[sends::RoomReturn] <= -59.9f ? 0.0f : dbToGain(v[sends::RoomReturn] + kRoomMakeupDb);
    plateReturn_ = v[sends::PlateReturn] <= -59.9f ? 0.0f : dbToGain(v[sends::PlateReturn] + kPlateMakeupDb);
    hallReturn_ = v[sends::HallReturn] <= -59.9f ? 0.0f : dbToGain(v[sends::HallReturn] + kHallMakeupDb);
    hatsSend_ = v[sends::HatsSend];
    percSend_ = v[sends::PercSend];

    readPlayed(Module::Master, 0, v);
    glue_.set(v[master::Threshold], v[master::Ratio], 6.0f, 20.0f, 200.0f);
    tiltHigh_ = dbToGain(v[master::Tilt]);

    float trim = score_.trimAt(beat);
    if (!lateTrims_.empty()) {
        size_t k = 0;
        for (size_t i = 0; i < score_.levels.size(); ++i) if (score_.levels[i].beat <= beat) k = i;
        if (k < lateTrims_.size()) trim = lateTrims_[k];
    }
    trimTarget_ = dbToGain(trim);
    BalanceDb bal = score_.balanceAt(beat);
    if (!lateBal_.empty()) {
        size_t k = 0;
        for (size_t i = 0; i < score_.levels.size(); ++i) if (score_.levels[i].beat <= beat) k = i;
        if (k < lateBal_.size()) bal = lateBal_[k];
    }
    for (int p = 0; p < kBalParts; ++p) balTarget_[p] = dbToGain(bal[static_cast<size_t>(p)]);
}

void Deck::watchPeaks(bool on)
{
    watch_ = on;
    if (!on) return;
    kickPeak_ = 0.0f;
    for (float& p : partPeak_) p = 0.0f;
}

void Deck::dispatchUntil(int64_t sample)
{
    while (evCursor_ < events_.size() && events_[evCursor_].sample <= sample) dispatch(events_[evCursor_++]);
}

void Deck::dispatch(const Ev& e)
{
    const Part part = static_cast<Part>(e.part);
    switch (part) {
    case Part::Ghost:
        // The pump (PLAN 7.3): every duck starts on the ghost kick, whether a kick plays or not.
        sub_.kick(e.late);
        bass_.kick(e.late);
        acid_.kick(e.late);
        for (Ducker& d : polyDuck_) d.trigger(e.late);
        pianoDuck_.trigger(e.late);
        for (Ducker& d : orchDuck_) d.trigger(e.late);
        retDuck_.trigger(e.late);
        fxDuck_.trigger(e.late);
        subDropDuck_.trigger(e.late);
        return;
    case Part::Fx: {
        if (e.on == 0) return;
        const int type = e.pitch - kSfxBaseNote;
        if (type < 0 || type >= kNumSfxTypes) return;
        sfx_.trigger(static_cast<SfxType>(type), e.gate, e.velocity, e.late, e.shift);   // the shift names the bank preset
        return;
    }
    case Part::Kick: {
        if (muted(perform::MuteKick)) return;
        kick_.trigger(e.velocity, e.late);
        haveKick_ = true;
        kickTime_ = static_cast<double>(e.sample) - e.late;
        kickC_ = kick_.asymptoticPhase();
        kickF0_ = kick_.tunedEndHz();
        return;
    }
    case Part::Sub: {
        if (e.on == 0) { if (e.id == subNote_) { sub_.noteOff(); subNote_ = -1; } return; }
        if (muted(perform::MuteBass)) return;
        double phase = -1.0;
        if (sub_.locked() && haveKick_) {
            // The kick's phase at the note's ideal start, f0 dt + c, carried to the note's pitch.
            const double dt = (static_cast<double>(e.sample) - e.late - kickTime_) / sampleRate_;
            const double hz = sub_.noteHz(e.pitch);
            const double kickPhase = kickF0_ * dt + kickC_;
            const double p = hz / kickF0_ * kickPhase - sub_.chainPhase(hz);
            phase = p - std::floor(p);
        }
        sub_.noteOn(e.pitch, e.velocity, e.late, phase);
        subNote_ = e.id;
        return;
    }
    case Part::Bass:
        if (e.on == 0) { if (e.id == bassNote_) { bass_.noteOff(); bassNote_ = -1; } return; }
        if (muted(perform::MuteBass)) return;
        bass_.noteOn(e.pitch, e.velocity, e.late, e.accent, e.slide);
        bassNote_ = e.id;
        return;
    case Part::Strings:
        if (e.on == 0) { strings_.noteOff(e.pitch); return; }
        if (muted(perform::MutePads)) return;
        strings_.noteOn(e.pitch, e.velocity, e.lengthBeats < 0.5, e.late);   // a short note is bitten staccato
        return;
    case Part::Choir:
        if (e.on == 0) { choir_.noteOff(e.pitch); return; }
        if (muted(perform::MutePads)) return;
        choir_.noteOn(e.pitch, e.velocity, false, e.late);
        return;
    case Part::Brass:
        if (e.on == 0) { brass_.noteOff(e.pitch); return; }
        if (muted(perform::MuteSynths)) return;
        brass_.noteOn(e.pitch, e.velocity, false, e.late);
        return;
    case Part::Timpani:
        if (e.on == 0) return;
        if (muted(perform::MutePerc)) return;
        timpani_.noteOn(e.pitch, e.velocity, false, e.late);
        return;
    case Part::Piano:
        if (e.on == 0) { piano_.noteOff(e.pitch); return; }
        if (muted(perform::MuteLead)) return;
        piano_.noteOn(e.pitch, e.velocity, e.late);
        return;
    case Part::Acid:
        if (e.on == 0) { if (e.id == acidNote_) { acid_.noteOff(); acidNote_ = -1; } return; }
        if (muted(perform::MuteBass)) return;
        acid_.noteOn(e.pitch, e.velocity, e.late, e.accent, e.slide);
        acidNote_ = e.id;
        return;
    default: {
        if (const int pi = polyOf(part); pi >= 0) {
            const int group = pi <= static_cast<int>(PolyInstance::Counter) ? perform::MuteLead
                            : pi <= static_cast<int>(PolyInstance::Arp) ? perform::MuteSynths : perform::MutePads;
            if (muted(group)) return;
            poly_[pi].noteOnLimited(e.pitch, e.velocity, e.lengthBeats, e.gate, e.late, e.accent, e.slide);
            return;
        }
        const int lane = laneOf(part);
        if (lane >= 0 && !muted(laneIsHat_[lane] ? perform::MuteHats : perform::MutePerc)) kit_.trigger(lane, e.velocity, e.shift, e.late);
        return;
    }
    }
}

void Deck::resetStems()
{
    for (StemBus& s : stemBus_) {
        for (auto& ch : s.hp) for (Svf& f : ch) f.reset();
        s.tilt[0] = s.tilt[1] = 0.0f;
    }
}

void Deck::render(int64_t sample, float* L, float* R, int n, float* const* stemL, float* const* stemR)
{
    const bool stems = stemL != nullptr;
    // Where this span starts in its cell: spans end at notes too, so it need not start on the raster line.
    const double spanOffset = static_cast<double>(sample - cellSample_);
    // The sources.
    { PARH_PROF(Kick); kick_.process(kickBuf_.data(), bodyBuf_.data(), n); }
    { PARH_PROF(Sub); sub_.process(subBuf_.data(), n); }
    { PARH_PROF(Kit); kit_.processLanes(n); }
    { PARH_PROF(Bass); bass_.process(bassL_.data(), bassR_.data(), n); acid_.process(acidL_.data(), acidR_.data(), n); }
    { PARH_PROF(Poly); for (int i = 0; i < kPolyInstances; ++i) poly_[i].process(polyL_[i].data(), polyR_[i].data(), n); }
    piano_.process(pianoL_.data(), pianoR_.data(), n);
    strings_.process(orchL_[0].data(), orchR_[0].data(), n);
    choir_.process(orchL_[1].data(), orchR_[1].data(), n);
    brass_.process(orchL_[2].data(), orchR_[2].data(), n);
    timpani_.process(orchL_[3].data(), orchR_[3].data(), n);
    sfx_.processSplit(fxL_.data(), fxR_.data(), fxSub_.data(), fxWetL_.data(), fxWetR_.data(), n);
    PARH_PROF_BEGIN(Buses);

    const float* ll = kit_.laneL();
    const float* lr = kit_.laneR();
    float busH[2][kRaster], busP[2][kRaster];
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        for (int p = 0; p < kBalParts; ++p) balGain_[p] += (balTarget_[p] - balGain_[p]) * trimCoef_;
        synthGain_ += (synthTarget_ - synthGain_) * 0.002f;   // the fader glides (a few ms), no zipper on a ride
        fxGain_ += (fxTarget_ - fxGain_) * 0.002f;
        const auto most = [](float& m, float a, float b) { m = std::max(m, std::max(std::fabs(a), std::fabs(b))); };
        // The mono synths, after their own ducks.
        const float gb = balGain_[static_cast<int>(BalPart::Bass)], ga = balGain_[static_cast<int>(BalPart::Acid)];
        bassL_[k] *= gb; bassR_[k] *= gb;
        acidL_[k] *= ga; acidR_[k] *= ga;
        // The polyphonic voices: gate, sends, pump.
        const double beat = beat_ + (spanOffset + static_cast<double>(i)) * beatsPerSample_;
        float rl = 0.0f, rr = 0.0f, pl = 0.0f, pr = 0.0f, hl = 0.0f, hr = 0.0f, sl = 0.0f, sr = 0.0f;
        for (int v = 0; v < kPolyInstances; ++v) {
            float l = polyL_[v][k] * balGain_[static_cast<int>(BalPart::Lead) + v] * synthGain_;
            float r = polyR_[v][k] * balGain_[static_cast<int>(BalPart::Lead) + v] * synthGain_;
            const PolyStrip& s = strip_[v];
            if (s.gate) {
                const float o = TranceGate::open(beat, s.pattern, s.duty, s.attackBeats, s.releaseBeats);
                gate_[v].apply(l, r, o, s.depth, s.tone);
            }
            rl += l * s.sends.room; rr += r * s.sends.room;
            pl += l * s.sends.plate; pr += r * s.sends.plate;
            hl += l * s.sends.hall; hr += r * s.sends.hall;
            const float g = polyDuck_[v].next();
            lastDuck_[v] = g;
            l *= g;
            r *= g;
            polyL_[v][k] = l;
            polyR_[v][k] = r;
            sl += l;
            sr += r;
            if (watch_) most(partPeak_[static_cast<int>(BalPart::Lead) + v], l, r);
        }
        {
            // The piano: its sends, its duck, into the synth bus.
            const float gp = balGain_[static_cast<int>(BalPart::Piano)] * synthGain_;
            float l = pianoL_[k] * gp, r = pianoR_[k] * gp;
            rl += l * pianoSends_.room; rr += r * pianoSends_.room;
            pl += l * pianoSends_.plate; pr += r * pianoSends_.plate;
            hl += l * pianoSends_.hall; hr += r * pianoSends_.hall;
            const float g = pianoDuck_.next();
            l *= g;
            r *= g;
            pianoL_[k] = l;
            pianoR_[k] = r;
            sl += l;
            sr += r;
            if (watch_) most(partPeak_[static_cast<int>(BalPart::Piano)], l, r);
        }
        for (int o = 0; o < kOrch; ++o) {
            // The orchestra: as the piano (the timpani outside the synth fader: they are percussion).
            const int bp = static_cast<int>(BalPart::Strings) + o;
            const float go = balGain_[bp] * (o == 3 ? 1.0f : synthGain_);
            float l = orchL_[o][k] * go, r = orchR_[o][k] * go;
            const Sends& s = orchSends_[o];
            rl += l * s.room; rr += r * s.room;
            pl += l * s.plate; pr += r * s.plate;
            hl += l * s.hall; hr += r * s.hall;
            const float g = orchDuck_[o].next();
            l *= g;
            r *= g;
            orchL_[o][k] = l;
            orchR_[o][k] = r;
            sl += l;
            sr += r;
            if (watch_) most(partPeak_[bp], l, r);
        }
        synthL_[k] = sl;
        synthR_[k] = sr;
        if (watch_) {
            most(kickPeak_, kickBuf_[k], kickBuf_[k]);
            most(partPeak_[static_cast<int>(BalPart::Bass)], bassL_[k], bassR_[k]);
            most(partPeak_[static_cast<int>(BalPart::Acid)], acidL_[k], acidR_[k]);
        }
        // The kit's buses.
        float hsl = 0.0f, hsr = 0.0f, psl = 0.0f, psr = 0.0f;
        for (int l = 0; l < kPercLanes; ++l) {
            const size_t q = static_cast<size_t>(i * PercKit::kStride + l);
            const float a = ll[q] * balGain_[l], b = lr[q] * balGain_[l];
            if (laneIsHat_[l]) { hsl += a; hsr += b; }
            else { psl += a; psr += b; }
            if (watch_) partPeak_[l] = std::max(partPeak_[l], std::max(std::fabs(a), std::fabs(b)) * (laneIsHat_[l] ? hatsGain_ : percGain_));
        }
        busH[0][i] = hatsLp_[0].lp(hsl) * hatsGain_;
        busH[1][i] = hatsLp_[1].lp(hsr) * hatsGain_;
        busP[0][i] = percLp_[0].lp(psl) * percGain_;
        busP[1][i] = percLp_[1].lp(psr) * percGain_;
        roomInL_[k] = rl + busH[0][i] * hatsSend_ + bassL_[k] * bassSends_.room + acidL_[k] * acidSends_.room;
        roomInR_[k] = rr + busH[1][i] * hatsSend_ + bassR_[k] * bassSends_.room + acidR_[k] * acidSends_.room;
        plateInL_[k] = pl + busP[0][i] * percSend_ + bassL_[k] * bassSends_.plate + acidL_[k] * acidSends_.plate;
        plateInR_[k] = pr + busP[1][i] * percSend_ + bassR_[k] * bassSends_.plate + acidR_[k] * acidSends_.plate;
        // The effects: sends before their duck, the wandering share straight into the hall; the sub drop under the kick.
        {
            const float gf = balGain_[static_cast<int>(BalPart::Fx)] * fxGain_;
            const float fl = fxL_[k] * gf, fr = fxR_[k] * gf;
            rl += fl * fxSends_.room; rr += fr * fxSends_.room;
            pl += fl * fxSends_.plate; pr += fr * fxSends_.plate;
            hl += fl * fxSends_.hall + fxWetL_[k] * gf; hr += fr * fxSends_.hall + fxWetR_[k] * gf;
            const float d = fxDuck_.next();
            const float sub = fxSub_[k] * gf * subDropDuck_.next();
            fxL_[k] = fl * d + sub;
            fxR_[k] = fr * d + sub;
            if (watch_) most(partPeak_[static_cast<int>(BalPart::Fx)], fxL_[k], fxR_[k]);
        }
        hallInL_[k] = hl;
        hallInR_[k] = hr;
        // The drum bus: a gentle saturation, two stages mixed in.
        const float mono = kickBuf_[k];
        float dl = mono + busH[0][i] + busP[0][i], dr = mono + busH[1][i] + busP[1][i];
        const float inL = dl, inR = dr;
        if (drumSat_ > 0.0f) {
            const float m = 0.43f * drumSat_;
            static const float kDrive[2] = { 1.5f, 2.0f };
            for (float d : kDrive) {
                dl += m * (std::tanh(d * dl) / d - dl);
                dr += m * (std::tanh(d * dr) / d - dr);
            }
        }
        if (stems) {
            satGain_[0][i] = inL != 0.0f ? dl / inL : 1.0f;
            satGain_[1][i] = inR != 0.0f ? dr / inR : 1.0f;
        }
        drumL_[k] = dl;
        drumR_[k] = dr;
    }
    PARH_PROF_END(Buses);
    { PARH_PROF(Rooms);
      room_.process(roomInL_.data(), roomInR_.data(), roomL_.data(), roomR_.data(), n);
      std::fill(plateL_.begin(), plateL_.begin() + n, 0.0f);   // the plate adds its return (Plate.h)
      std::fill(plateR_.begin(), plateR_.begin() + n, 0.0f);
      plate_.process(plateInL_.data(), plateInR_.data(), plateL_.data(), plateR_.data(), n);
      hall_.process(hallInL_.data(), hallInR_.data(), hallL_.data(), hallR_.data(), n); }
    // The returns, ducked by the pump (Dok. 7: "sonst fuellen die Tails genau den Platz des Kick-Transienten").
    const float gRoomBal = balGain_[static_cast<int>(BalPart::Room)];
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        const float g = retDuck_.next() * gRoomBal;
        roomL_[k] *= roomReturn_ * g; roomR_[k] *= roomReturn_ * g;
        plateL_[k] *= plateReturn_ * g; plateR_[k] *= plateReturn_ * g;
        hallL_[k] *= hallReturn_ * g; hallR_[k] *= hallReturn_ * g;
    }
    if (stems) {
        for (int i = 0; i < n; ++i) {
            const size_t k = static_cast<size_t>(i);
            auto put = [&](int s, float l, float r) { stemL[s][i] = l; stemR[s][i] = r; };
            put(kStemKick, kickBuf_[k] * satGain_[0][i], kickBuf_[k] * satGain_[1][i]);
            put(kStemSub, subBuf_[k], subBuf_[k]);
            put(kStemBass, bassL_[k], bassR_[k]);
            put(kStemAcid, acidL_[k], acidR_[k]);
            put(kStemHats, busH[0][i] * satGain_[0][i], busH[1][i] * satGain_[1][i]);
            put(kStemPerc, busP[0][i] * satGain_[0][i], busP[1][i] * satGain_[1][i]);
            for (int v = 0; v < kPolyInstances; ++v) put(kStemLead + v, polyL_[v][k], polyR_[v][k]);
            put(kStemPiano, pianoL_[k], pianoR_[k]);
            for (int o = 0; o < kOrch; ++o) put(kStemStrings + o, orchL_[o][k], orchR_[o][k]);
            put(kStemRoom, roomL_[k], roomR_[k]);
            put(kStemPlate, plateL_[k], plateR_[k]);
            put(kStemHall, hallL_[k], hallR_[k]);
            put(kStemFx, fxL_[k], fxR_[k]);
        }
    }

    PARH_PROF_BEGIN(TrackBus);
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        float l = drumL_[k] + subBuf_[k] + bassL_[k] + acidL_[k] + synthL_[k] + roomL_[k] + plateL_[k] + hallL_[k] + fxL_[k];
        float r = drumR_[k] + subBuf_[k] + bassR_[k] + acidR_[k] + synthR_[k] + roomR_[k] + plateR_[k] + hallR_[k] + fxR_[k];
        if (groupHpOn_) {
            float lp, bp, hp;
            groupHp_[0][0].tick(l, lp, bp, hp); groupHp_[0][1].tick(hp, lp, bp, l);
            groupHp_[1][0].tick(r, lp, bp, hp); groupHp_[1][1].tick(hp, lp, bp, r);
        }
        tiltState_[0] += tiltCoef_ * (l - tiltState_[0]);
        tiltState_[1] += tiltCoef_ * (r - tiltState_[1]);
        L[i] = tiltState_[0] + tiltHigh_ * (l - tiltState_[0]);
        R[i] = tiltState_[1] + tiltHigh_ * (r - tiltState_[1]);
        drumL_[k] = L[i];   // the glue's dry signal (the drum bus is spent)
        drumR_[k] = R[i];
    }
    PARH_PROF_END(TrackBus);
    PARH_PROF_BEGIN(Glue);
    glue_.process(L, R, n, stems ? glueGains_ : nullptr);
    for (int i = 0; i < n; ++i) {
        const size_t k = static_cast<size_t>(i);
        trimGain_ += (trimTarget_ - trimGain_) * trimCoef_;
        trimGains_[i] = trimGain_;
        L[i] = (drumL_[k] + kGlueMix * (L[i] - drumL_[k])) * trimGain_;
        R[i] = (drumR_[k] + kGlueMix * (R[i] - drumR_[k])) * trimGain_;
    }
    PARH_PROF_END(Glue);
    if (!stems) return;
    for (int s = 0; s < kStems; ++s) {
        StemBus& b = stemBus_[s];
        for (int c = 0; c < 2; ++c) for (int q = 0; q < 2; ++q) b.hp[c][q].copyCoefficients(groupHp_[c][q]);
        float* ch[2] = { stemL[s], stemR[s] };
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < n; ++i) {
                float x = ch[c][i];
                if (groupHpOn_) {
                    float lp, bp, hp;
                    b.hp[c][0].tick(x, lp, bp, hp); b.hp[c][1].tick(hp, lp, bp, x);
                }
                b.tilt[c] += tiltCoef_ * (x - b.tilt[c]);
                x = b.tilt[c] + tiltHigh_ * (x - b.tilt[c]);
                ch[c][i] = x * (1.0f + kGlueMix * (glueGains_[i] - 1.0f)) * trimGains_[i];
            }
        }
    }
}

} // namespace parh
