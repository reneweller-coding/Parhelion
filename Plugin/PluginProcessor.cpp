/**
 * @file PluginProcessor.cpp
 * @brief The plugin's processor.
 * @note Copied from Totality `Plugin/PluginProcessor.cpp` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 */
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "parh/Export.h"
#include "parh/Leveler.h"
#include "parh/Midi.h"
#include "parh/Presets.h"
#include "parh/WavWriter.h"
#include <algorithm>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

using namespace parh;

// ---------------------------------------------------------------------------------------------------
// StoreParameter (after Phosphene's Plugin/PluginProcessor.cpp at 9a2f615, as in Ephemeris)

StoreParameter::StoreParameter(ParamStore& store, int id, const juce::String& name)
    : juce::RangedAudioParameter(juce::ParameterID(juce::String(store.key(id)), 1), name,
                                 juce::AudioProcessorParameterWithIDAttributes().withLabel(store.desc(id).unit)),
      store_(store), id_(id), name_(name)
{
    const ParamDesc& d = store.desc(id);
    ParamStore* s = &store;
    range_ = juce::NormalisableRange<float>(
        d.minValue, d.maxValue,
        [s, id](float, float, float n) { return s->fromNormalised(id, n); },
        [s, id](float, float, float v) { return s->toNormalised(id, v); },
        [s, id](float lo, float hi, float v) {
            const ParamDesc& dd = s->desc(id);
            const float c = juce::jlimit(lo, hi, v);
            return (dd.curve == Curve::Linear || dd.curve == Curve::Log) ? c : std::round(c);
        });
}

float StoreParameter::getValue() const { return store_.toNormalised(id_, store_.get(id_)); }
void StoreParameter::setValue(float newValue) { store_.setNormalised(id_, newValue); }
float StoreParameter::getDefaultValue() const { return store_.toNormalised(id_, store_.defaultValue(id_)); }
// A length of 0 or less means no limit, as in JUCE's own parameters (the slider attachments ask with 0).
juce::String StoreParameter::getName(int maximumStringLength) const { return maximumStringLength > 0 ? name_.substring(0, maximumStringLength) : name_; }
juce::String StoreParameter::getLabel() const { return store_.desc(id_).unit; }

int StoreParameter::getNumSteps() const
{
    const ParamDesc& d = store_.desc(id_);
    if (d.curve == Curve::Toggle) return 2;
    if (d.curve == Curve::Choice || d.curve == Curve::Int) return static_cast<int>(std::lround(d.maxValue - d.minValue)) + 1;
    return juce::AudioProcessor::getDefaultNumParameterSteps();
}

bool StoreParameter::isDiscrete() const
{
    const Curve c = store_.desc(id_).curve;
    return c == Curve::Int || c == Curve::Choice || c == Curve::Toggle;
}

bool StoreParameter::isBoolean() const { return store_.desc(id_).curve == Curve::Toggle; }

juce::String StoreParameter::getText(float normalisedValue, int maximumStringLength) const
{
    const ParamDesc& d = store_.desc(id_);
    const float v = store_.fromNormalised(id_, normalisedValue);
    juce::String t;
    if (d.curve == Curve::Choice && d.choices != nullptr) t = d.choices[juce::jlimit(0, static_cast<int>(d.maxValue), static_cast<int>(std::lround(v)))];
    else if (d.curve == Curve::Toggle) t = v >= 0.5f ? "On" : "Off";
    else if (d.curve == Curve::Int) t = juce::String(static_cast<int>(std::lround(v)));
    // (juce::String(v, 0) would mean "as precise as it gets", not "no decimals": 159.126 ms)
    else t = std::fabs(v) >= 100.0f ? juce::String(juce::roundToInt(v)) : juce::String(v, std::fabs(v) >= 10.0f ? 1 : 2);
    return maximumStringLength > 0 ? t.substring(0, maximumStringLength) : t;
}

float StoreParameter::getValueForText(const juce::String& text) const
{
    const ParamDesc& d = store_.desc(id_);
    if (d.curve == Curve::Choice && d.choices != nullptr)
        for (int c = 0; c <= static_cast<int>(d.maxValue); ++c)
            if (text.equalsIgnoreCase(d.choices[c])) return store_.toNormalised(id_, static_cast<float>(c));
    if (d.curve == Curve::Toggle) return text.equalsIgnoreCase("on") ? 1.0f : 0.0f;
    return store_.toNormalised(id_, text.getFloatValue());
}

// ---------------------------------------------------------------------------------------------------
// The processor

namespace {

/** @brief An RBJ biquad (normalised by a0), for the K-weighting of the loudness meter. */
void designBiquad(double b0, double b1, double b2, double a0, double a1, double a2, double& ob0, double& ob1, double& ob2,
                  double& oa1, double& oa2)
{
    ob0 = b0 / a0; ob1 = b1 / a0; ob2 = b2 / a0; oa1 = a1 / a0; oa2 = a2 / a0;
}

} // namespace

ParhelionProcessor::ParhelionProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("Parhelion composer")
{
    ParamStore& s = store();
    for (int id = 0; id < s.count(); ++id) {
        auto* p = new StoreParameter(s, id, juce::String(s.key(id)).replaceCharacter('.', ' ') + " (" + s.desc(id).name + ")");
        params_.push_back(p);
        addParameter(p);
        p->addListener(this);   // undo: the panel's gestures (parameterGestureChanged)
    }
    engine_.setLive(true);   // the performer's controls act, the mixer is in a track's path (Engine.h)
    seed_ = static_cast<uint64_t>(juce::Time::currentTimeMillis() % 100000);
    if (const char* env = std::getenv("PARH_SEED")) seed_ = std::strtoull(env, nullptr, 10);
    autoPlay_ = std::getenv("PARH_PLAY") != nullptr;
    if (const char* set = std::getenv("PARH_SET")) s.set(s.id(Module::Set, 0, set::Minutes), static_cast<float>(std::atof(set)));   // a set of so many minutes
    // PARH_MUTE=1 (and the screenshot mode): silent from the first sample, never unmuted from inside. The house rule for
    // every automated run -- tests, screenshots, the manual -- is that nothing makes a sound.
    forceMute_ = std::getenv("PARH_MUTE") != nullptr || std::getenv("PARH_SHOT") != nullptr;
    mute_ = forceMute_;
    // The performer's controllers as a keyboard has them: the mod wheel the master filter, the expression pedal the throw.
    for (auto& c : ccMap_) c = -1;
    ccMap_[1] = s.id(Module::Perform, 0, perform::Wheel);     // the wheel: a source of every voice's matrix (Modulation.h)
    ccMap_[74] = s.id(Module::Perform, 0, perform::Filter);   // brightness: the master filter
    ccMap_[11] = s.id(Module::Perform, 0, perform::Throw);    // the expression pedal: the echo throw
    // Phase 17: the player's ratings, if any.
    ratings_ = loadRatings(ratingsFile().getFullPathName().toStdString());
    startTimerHz(10);
    headsetTick_.startTimerHz(30);
    compose();
}

ParhelionProcessor::~ParhelionProcessor()
{
    headsetTick_.stopTimer();
    stopTimer();
    newer_ = true;
    stopThread(10000);
    if (exporter_ && exporter_->joinable()) exporter_->join();
}

Playing ParhelionProcessor::composeNow(ParamStore& snapshot)
{
    snapshot.copyValuesFrom(store());
    Curation cur;
    uint64_t seed;
    {
        std::lock_guard<std::mutex> g(lock_);
        cur = curation_;
        seed = seed_;
    }
    // Phase 17: the ratings' weights while compose.use_ratings is on.
    Preferences prefs;
    const bool useRatings = snapshot.getBool(snapshot.id(Module::Compose, 0, compose::UseRatings));
    if (useRatings) {
        std::lock_guard<std::mutex> g(lock_);
        prefs = preferencesFrom(ratings_);
    }
    Playing out;
    const double setMinutes = snapshot.get(snapshot.id(Module::Set, 0, set::Minutes));
    if (setMinutes > 0.0) {
        out.isSet = true;
        // A newer score asked for breaks a long set off (its rest is thrown away, the timer composes the new one).
        out.set = composeSet(snapshot, seed, setMinutes, &cur, &out.setInfo, useRatings ? &prefs : nullptr, {},
                             [this] { return again_.load() || threadShouldExit(); });
        for (const SetTrack& t : out.setInfo.tracks) out.tracks.push_back({ t.start, t.swapIn, t.end, t.deck, t.info });
    } else {
        TrackInfo info;
        TrackRequest req;
        req.prefs = useRatings ? &prefs : nullptr;
        // The performer's "now": the same track rewritten from the bar asked for, loaded where it plays (performNow).
        if (const int bar = nowBar_.load(); bar >= 0) {
            std::lock_guard<std::mutex> g(lock_);
            req.rewriteBar = bar;
            req.rewriteKind = nowKind_;
            req.earlier = current_.rewrites;   // a second "now" keeps the first (Phase 7)
            out.rewrites = current_.rewrites;
            out.rewrites.push_back({ bar, nowKind_ });
            out.resume = true;
        }
        out.set.decks[0] = composeTrack(snapshot, seed, req, &cur, std::string(), &info);
        if (out.resume) {
            // Its level corrections as the track had them, until the Leveler has measured the new one.
            std::lock_guard<std::mutex> g(lock_);
            const std::vector<LevelMark>& was = current_.set.decks[0].levels;
            std::vector<LevelMark>& now = out.set.decks[0].levels;
            if (!was.empty() && !now.empty()) {
                now[0].trimDb = was[0].trimDb;
                now[0].balDb = was[0].balDb;
                now[0].breakDb = was[0].breakDb;
                now[0].buildDb = was[0].buildDb;
            }
        }
        out.set.lengthBeats = out.set.decks[0].lengthBeats;
        out.tracks.push_back({ 0.0, 0.0, out.set.lengthBeats, 0, info });
    }
    return out;
}

int ParhelionProcessor::performNow(SectionKind kind)
{
    if (composing_) return -1;
    double start = 0.0, bpm = 138.0;
    {
        std::lock_guard<std::mutex> g(lock_);
        if (current_.isSet || current_.tracks.empty()) return -1;
        start = current_.tracks[0].start;
        bpm = current_.set.decks[0].tempo.bpmAt(position_.load());
        nowKind_ = kind;
    }
    // The next 8-bar line of the track; the one after it where it is under six seconds away.
    const double beat = position_.load() - start;
    int bar = (static_cast<int>(std::floor(beat / 4.0)) / 8 + 1) * 8;
    if ((4.0 * bar - beat) * 60.0 / std::max(60.0, bpm) < 6.0) bar += 8;
    nowBar_ = bar;
    compose();
    return bar;
}

void ParhelionProcessor::compose()
{
    // One at a time: a press while composing is remembered and composed when the first is done.
    if (composing_.exchange(true)) { again_ = true; return; }
    newer_ = true;   // the last score's measuring, if it still runs, is broken off
    if (isThreadRunning()) waitForThreadToExit(-1);
    newer_ = false;
    startThread();
}

void ParhelionProcessor::newSeed()
{
    beginStep("New seed");
    {
        std::lock_guard<std::mutex> g(lock_);
        seed_ = static_cast<uint64_t>(juce::Random::getSystemRandom().nextInt64() & 0xFFFFFF);
        curation_ = Curation{};
    }
    endStep();
    compose();
}

void ParhelionProcessor::reroll(const juce::String& unit)
{
    beginStep("reroll " + unit);
    {
        std::lock_guard<std::mutex> g(lock_);
        curation_.reroll(unit.toStdString());
    }
    endStep();
    compose();
}

bool ParhelionProcessor::mixChosen() const { return store().get(store().id(Module::Set, 0, set::Minutes)) > 0.0f; }

void ParhelionProcessor::chooseMix(bool mix)
{
    const int id = store().id(Module::Set, 0, set::Minutes);
    const bool was = mixChosen();
    beginStep(mix ? "DJ mix" : "Track");
    if (was && !mix) mixMinutes_ = store().get(id);
    if (was != mix) setFromUi(id, mix ? std::max(10.0f, mixMinutes_) : 0.0f);
    endStep();
    if (was != mix || playingMix() != mix) compose();
}

juce::String ParhelionProcessor::curationText() const
{
    std::lock_guard<std::mutex> g(lock_);
    juce::String t;
    for (const auto& r : curation_.rerolls) t << r.first << " x" << r.second << "  ";
    return t.isEmpty() ? juce::String("nothing rerolled") : t.trimEnd();
}

int ParhelionProcessor::trackAt(double beat) const
{
    std::lock_guard<std::mutex> g(lock_);
    int found = -1;
    for (size_t i = 0; i < current_.tracks.size(); ++i) {
        const TrackPlace& t = current_.tracks[i];
        // In a set, a track owns the time from its swap to the next's; its intro before belongs to the one it follows.
        const double from = current_.isSet && i > 0 ? t.swapIn : t.start;
        if (beat >= from - 1e-9) found = static_cast<int>(i);
    }
    return found;
}

int ParhelionProcessor::composedPreset(Module m, int instance) const
{
    int lead = engine_.leadDeck();
    const double beat = position_.load();
    std::lock_guard<std::mutex> g(lock_);
    if (lead < 0 || lead >= kDecks) {
        // Before the engine has played a track's knob settings (stopped, a score just composed): the deck of the track
        // under the playhead, so the panel names the sounds before the first note.
        lead = 0;
        for (const TrackPlace& t : current_.tracks)
            if (beat >= t.start && beat < t.end) { lead = t.deck; break; }
    }
    int preset = -1;
    for (const SoundPick& k : current_.set.decks[lead].sounds)   // in beat order: the last one begun is the one that holds
        if (k.module == static_cast<int>(m) && k.instance == instance && k.beat <= beat + 1.0) preset = k.preset;
    return preset;
}

void ParhelionProcessor::applyPreset(Module m, int instance, int index)
{
    const std::vector<SoundPreset>& list = factoryPresets(m, instance);
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    beginStep(juce::String("preset ") + list[static_cast<size_t>(index)].name);
    const juce::ScopedValueSetter<bool> one(restoring_, true);   // the knobs' gestures join the step, not steps of their own
    for (const auto& [k, v] : presetKnobs(m, instance, list[static_cast<size_t>(index)])) {
        const int id = store().id(m, instance, k);
        StoreParameter* p = parameter(id);
        if (p == nullptr) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost(store().toNormalised(id, v));
        p->endChangeGesture();
    }
    restoring_ = false;
    endStep();
}

juce::File ParhelionProcessor::userPresetFolder(Module m, int instance) const
{
    const std::string key = store().key(store().id(m, instance, 0));
    juce::String synth(key.substr(0, key.find('.')));
    if (m == Module::Perc) synth = synth.trimCharactersAtEnd("0123456789");   // every lane of the kit
    const char* dir = std::getenv("PARH_USER_DIR");
    const juce::File user = dir != nullptr ? juce::File(dir)
                                           : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Parhelion");
    return user.getChildFile("Presets").getChildFile(synth);
}

juce::Array<juce::File> ParhelionProcessor::userPresets(Module m, int instance) const
{
    juce::Array<juce::File> out = userPresetFolder(m, instance).findChildFiles(juce::File::findFiles, false, "*.txt");
    std::sort(out.begin(), out.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileNameWithoutExtension().compareNatural(b.getFileNameWithoutExtension()) < 0;
    });
    return out;
}

juce::File ParhelionProcessor::saveUserPreset(Module m, int instance, const juce::String& name)
{
    const juce::String clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty()) return {};
    const juce::String text(userPresetText(store(), m, instance));
    const juce::File dir = userPresetFolder(m, instance);
    dir.createDirectory();
    const juce::File f = dir.getChildFile(clean + ".txt");
    // "\n" line ends: JUCE's default is "\r\n", and the preset reads back as the text it was.
    return f.replaceWithText(text, false, false, "\n") ? f : juce::File();
}

bool ParhelionProcessor::applyUserPreset(Module m, int instance, const juce::File& file)
{
    if (!file.existsAsFile()) return false;
    beginStep("preset " + file.getFileNameWithoutExtension());
    {
        const juce::ScopedValueSetter<bool> one(restoring_, true);   // one step, not one per knob
        // The whole sound, as a factory preset has it: its knobs, the defaults for the rest it would set.
        for (const auto& [k, v] : userPresetKnobs(store(), m, instance, file.loadFileAsString().toStdString())) {
            const int id = store().id(m, instance, k);
            StoreParameter* p = parameter(id);
            if (p == nullptr) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost(store().toNormalised(id, v));
            p->endChangeGesture();
        }
    }
    endStep();
    return true;
}

void ParhelionProcessor::length(double& beats, double& seconds) const
{
    std::lock_guard<std::mutex> g(lock_);
    beats = current_.set.lengthBeats;
    seconds = current_.set.decks[0].tempo.secondsAt(beats);
}

void ParhelionProcessor::run()
{
    ParamStore snapshot;
    Playing p = composeNow(snapshot);
    uint64_t id = 0;
    {
        std::lock_guard<std::mutex> g(lock_);
        pending_ = std::make_unique<Playing>(p);
        id = pendingId_ = ++composed_;
    }
    composing_ = false;
    // Then its loudness (Leveler.h), while it already plays: 20 seconds of each track's loudest part rendered. The
    // corrections follow and glide in; a newer score asked for, or the plugin closing, breaks the measuring off.
    const auto stop = [this] { return newer_.load() || threadShouldExit(); };
    const bool done = p.isSet ? !levelSet(p.set, snapshot, 20.0, stop).empty() : !levelScore(p.set.decks[0], snapshot, 20.0, stop).empty();
    if (!done) return;
    std::lock_guard<std::mutex> g(lock_);
    for (int d = 0; d < kDecks; ++d) {
        trims_[d].clear();
        bal_[d].clear();
        for (const LevelMark& m : p.set.decks[d].levels) { trims_[d].push_back(m.trimDb); bal_[d].push_back(m.balDb); }
    }
    trimsFor_ = id;
}

void ParhelionProcessor::takeTrims()
{
    std::vector<float> trims[kDecks];
    std::vector<BalanceDb> bal[kDecks];
    {
        std::lock_guard<std::mutex> g(lock_);
        if (trimsFor_ == 0 || trimsFor_ > playingId_) return;   // (none, or for a score not yet loaded)
        const bool mine = trimsFor_ == playingId_;
        trimsFor_ = 0;
        if (!mine) return;
        for (int d = 0; d < kDecks; ++d) {
            trims[d].swap(trims_[d]);
            bal[d].swap(bal_[d]);
            for (size_t i = 0; i < current_.set.decks[d].levels.size() && i < trims[d].size(); ++i)
                current_.set.decks[d].levels[i].trimDb = trims[d][i];
            for (size_t i = 0; i < current_.set.decks[d].levels.size() && i < bal[d].size(); ++i)
                current_.set.decks[d].levels[i].balDb = bal[d][i];
        }
        levelled_ = true;
    }
    // A few numbers: the audio thread waits for them instead of losing a block (suspendProcessing would silence one).
    const juce::ScopedLock sl(getCallbackLock());
    for (int d = 0; d < kDecks; ++d) {
        if (!trims[d].empty()) engine_.setLevelTrims(d, trims[d]);
        if (!bal[d].empty()) engine_.setLevelBalance(d, bal[d]);
    }
}

SetScore ParhelionProcessor::forPlayback(const Playing& p) const
{
    SetScore out = p.set;
    const double bpm = hostBpm_.load();
    if (wrapperType != wrapperType_Standalone && bpm > 0.0)
        for (Score& s : out.decks) s.tempo.setConstant(bpm);
    return out;
}

void ParhelionProcessor::loadEngine(const Playing& p)
{
    const SetScore s = forPlayback(p);
    engine_.prepare(sampleRate_, blockSize_);
    engine_.setMeterSink(&meterSink_);   // the Mixer page's strips (a handful of sums per sample)
    if (p.isSet) engine_.loadSet(s);
    else engine_.load(s.decks[0]);
    std::lock_guard<std::mutex> g(lock_);
    if (levelled_)   // the corrections already found go with the score (a reload at a new host tempo)
        for (int d = 0; d < kDecks; ++d) {
            std::vector<float> t;
            std::vector<BalanceDb> b;
            for (const LevelMark& m : p.set.decks[d].levels) { t.push_back(m.trimDb); b.push_back(m.balDb); }
            if (!t.empty()) engine_.setLevelTrims(d, t);
            if (!b.empty()) engine_.setLevelBalance(d, b);
        }
}

void ParhelionProcessor::timerCallback()
{
    // The cue sender follows its two parameters (message thread: the socket is opened and closed here).
    {
        const ParamStore& s = store();
        const int port = s.getBool(s.id(Module::Cue, 0, cue::Enabled)) ? s.getInt(s.id(Module::Cue, 0, cue::Port)) : 0;
        if (port != cuePort_) {
            cues_.stop();
            cuePort_ = port;
            if (port > 0) {
                const char* host = std::getenv("PARH_CUE_HOST");
                if (!cues_.start(host != nullptr ? host : "127.0.0.1", port)) cuePort_ = 0;
            }
        }
    }
    // A track began (or a jump landed in another): the engine wrote its sounds and mix on the knobs -- the host and the
    // pages are told (the values are in the store already).
    if (const uint32_t v = engine_.soundsVersion(); v != toldSounds_) {
        toldSounds_ = v;
        for (StoreParameter* p : params_) p->sendValueChangedMessageToListeners(p->getValue());
    }
    std::unique_ptr<Playing> next;
    uint64_t nextId = 0;
    {
        std::lock_guard<std::mutex> g(lock_);
        next = std::move(pending_);
        nextId = pendingId_;
    }
    // A score asked for again while it was composed is out of date: the next one comes.
    if (next && again_ && !composing_) {
        next.reset();
        again_ = false;
        compose();
        return;
    }
    if (!next) {
        // In a host the score plays at the host's tempo. A new tempo means new sample positions for every event, and
        // the engine allocates when it loads: so the score is loaded again here, and the engine goes on from its beat.
        const double bpm = hostBpm_.load();
        if (wrapperType != wrapperType_Standalone && bpm > 0.0 && std::fabs(bpm - playedBpm_.load()) > 1.0e-3) {
            Playing p;
            copyPlaying(p);
            suspendProcessing(true);
            const double beat = engine_.beat();
            loadEngine(p);
            engine_.seek(beat);
            playedBpm_ = bpm;
            suspendProcessing(false);
        }
        if (again_ && !composing_) { again_ = false; compose(); }
        takeTrims();
        return;
    }
    // The engine allocates when it loads: never on the audio thread.
    suspendProcessing(true);
    {
        std::lock_guard<std::mutex> g(lock_);
        levelled_ = false;
    }
    const bool resume = next->resume;
    const double at = engine_.beat();
    loadEngine(*next);
    playedBpm_ = wrapperType != wrapperType_Standalone ? hostBpm_.load() : 0.0;
    {
        std::lock_guard<std::mutex> g(lock_);
        current_ = std::move(*next);
        playingId_ = nextId;
    }
    ++scoreVersion_;
    if (resume) {
        // The performer's "now": the rewritten track goes on from where the old one was.
        engine_.seek(at);
        position_ = at;
        nowBar_ = -1;
    } else {
        position_ = 0.0;
    }
    cueTap_.reset();
    if (autoPlay_) { autoPlay_ = false; playing_ = true; }
    suspendProcessing(false);
    takeTrims();
}

void ParhelionProcessor::takeStripMeters(float* peak, float* rms)
{
    const double n = static_cast<double>(std::max<int64_t>(1, stripSamples_.exchange(0, std::memory_order_relaxed)));
    for (int s = 0; s < MeterSink::kStrips; ++s) {
        peak[s] = meterSink_.peak[s].exchange(0.0f, std::memory_order_relaxed);
        rms[s] = static_cast<float>(std::sqrt(meterSink_.sum[s].exchange(0.0, std::memory_order_relaxed) / n));
    }
}

void ParhelionProcessor::takeMeters(float* deckPeak, float* deckRms, float& outPeak, float& momentaryLufs)
{
    const int n = meterCount_.exchange(0, std::memory_order_acquire);
    for (int d = 0; d < kDecks; ++d) {
        const size_t k = static_cast<size_t>(d);
        deckPeak[d] = meterPeak_[k].exchange(0.0f, std::memory_order_relaxed);
        const double s = meterSum_[k].exchange(0.0, std::memory_order_relaxed);
        deckRms[d] = n > 0 ? static_cast<float>(std::sqrt(s / n)) : 0.0f;
    }
    outPeak = outPeak_.exchange(0.0f, std::memory_order_relaxed);
    momentaryLufs = lufs_.load(std::memory_order_relaxed);
}

void ParhelionProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    blockSize_ = std::max(1, samplesPerBlock);
    // The deck taps for the meters (reading only: the mix is the same to the bit).
    tapBuf_.assign(static_cast<size_t>(kDecks * 2 * blockSize_), 0.0f);
    for (int d = 0; d < kDecks; ++d) {
        tapL_[static_cast<size_t>(d)] = tapBuf_.data() + static_cast<size_t>(2 * d * blockSize_);
        tapR_[static_cast<size_t>(d)] = tapBuf_.data() + static_cast<size_t>((2 * d + 1) * blockSize_);
    }
    engine_.setDeckTaps(tapL_.data(), tapR_.data());
    engine_.setNoteTap(&noteTap_);   // MIDI out (02.10.2026)
    // The K-weighting of BS.1770 at this rate (the pre-filter's shelf and the RLB high pass, as pyloudnorm designs them).
    {
        const double fs = sampleRate;
        double K = std::tan(juce::MathConstants<double>::pi * 1681.974450955533 / fs);
        const double Vh = std::pow(10.0, 3.999843853973347 / 20.0), Vb = std::pow(Vh, 0.4996667741545416), Q = 0.7071752369554196;
        const double a0 = 1.0 + K / Q + K * K;
        for (Biquad& b : kShelf_) {
            designBiquad(Vh + Vb * K / Q + K * K, 2.0 * (K * K - Vh), Vh - Vb * K / Q + K * K, a0, 2.0 * (K * K - 1.0), 1.0 - K / Q + K * K,
                         b.b0, b.b1, b.b2, b.a1, b.a2);
            b.z1 = b.z2 = 0.0;
        }
        K = std::tan(juce::MathConstants<double>::pi * 38.13547087602444 / fs);
        const double Q2 = 0.5003270373238773, a02 = 1.0 + K / Q2 + K * K;
        for (Biquad& b : kHigh_) {
            designBiquad(1.0, -2.0, 1.0, a02, 2.0 * (K * K - 1.0), 1.0 - K / Q2 + K * K, b.b0, b.b1, b.b2, b.a1, b.a2);
            b.z1 = b.z2 = 0.0;
        }
        kCoef_ = 1.0 - std::exp(-1.0 / (0.4 * fs / 3.0));   // a 400 ms window as an exponential of a third of it
        kMs_ = 0.0;
    }
    if (const char* secs = std::getenv("PARH_PLAY"); secs != nullptr && std::getenv("PARH_RECORD") != nullptr) {
        recordTarget_ = static_cast<size_t>(std::atof(secs) * sampleRate) * 2;
        record_.assign(recordTarget_, 0.0f);
        recordPos_ = 0;
    }
    Playing p;
    copyPlaying(p);
    loadEngine(p);
    playedBpm_ = wrapperType != wrapperType_Standalone ? hostBpm_.load() : 0.0;
}

bool ParhelionProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ParhelionProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    perform(midi);
    midi.clear();
    const int n = buffer.getNumSamples();
    bool play = playing_.load();
    // In a host, the host's playhead is the clock.
    if (wrapperType != wrapperType_Standalone) {
        play = false;
        if (auto* ph = getPlayHead()) {
            if (const auto pos = ph->getPosition()) {
                play = pos->getIsPlaying();
                if (const auto bpm = pos->getBpm(); bpm && *bpm > 0.0) hostBpm_ = *bpm;
                // Until the message thread has loaded the score at a new host tempo, the positions drift apart by
                // design: only a real jump of the host's playhead is followed then.
                const bool tempoPending = std::fabs(hostBpm_.load() - playedBpm_.load()) > 1.0e-3;
                if (play)
                    if (const auto ppq = pos->getPpqPosition())
                        if (std::fabs(*ppq - engine_.beat()) > (tempoPending ? 4.0 : 0.05)) engine_.seek(*ppq);
            }
        }
    }
    const double seek = seekRequest_.exchange(-1.0);
    if (seek >= 0.0) engine_.seek(seek);
    {   // Stopped, or another keyboard target: every played key is released (01.10.2026).
        const int target = store().getInt(store().id(Module::Perform, 0, perform::KeyboardPart));
        if (!play || target != keyboardSeen_) engine_.liveAllOff();
        keyboardSeen_ = target;
    }
    if (!play || buffer.getNumChannels() < 2 || n > blockSize_) {
        silenceMidi(midi);
        buffer.clear();
        return;
    }
    const double before = engine_.beat();
    const int64_t start = engine_.samplePosition();
    if (start != midiExpect_) silenceMidi(midi);   // a jump: whatever sounded is released
    noteTap_.clear();
    engine_.process(buffer.getWritePointer(0), buffer.getWritePointer(1), n);
    emitMidi(midi, start, n);
    midiExpect_ = engine_.samplePosition();
    stripSamples_.fetch_add(n, std::memory_order_relaxed);
    position_ = engine_.beat();
    if (cues_.running()) {
        // The cues of this block, stamped with the moment it is heard (Cue.h); a jump starts the marks again.
        if (std::fabs(before - lastBeat_) > 1.0e-6) cueTap_.reset();
        const double blockSeconds = static_cast<double>(n) / sampleRate_;
        const double bpm = 60.0 * (engine_.beat() - before) / std::max(1e-9, blockSeconds);
        cueTap_.scan(engine_.cueMarks(), before, engine_.beat(), static_cast<float>(bpm), CueSender::nowNanos(),
                     static_cast<int64_t>(blockSeconds * 1.0e9), static_cast<int64_t>(blockSeconds * 1.0e9), cues_.ring());
    }
    lastBeat_ = engine_.beat();
    {
        // The meters: raised here, taken by the editor (takeMeters). One writer, so a load and a store will do.
        for (int d = 0; d < kDecks; ++d) {
            const size_t k = static_cast<size_t>(d);
            float pk = 0.0f;
            double ss = 0.0;
            for (int i = 0; i < n; ++i) {
                const float l = tapL_[k][i], r = tapR_[k][i];
                pk = std::max(pk, std::max(std::fabs(l), std::fabs(r)));
                ss += 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r);
            }
            if (pk > meterPeak_[k].load(std::memory_order_relaxed)) meterPeak_[k].store(pk, std::memory_order_relaxed);
            meterSum_[k].store(meterSum_[k].load(std::memory_order_relaxed) + ss, std::memory_order_relaxed);
        }
        meterCount_.fetch_add(n, std::memory_order_release);
        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getReadPointer(1);
        float pk = outPeak_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            pk = std::max(pk, std::max(std::fabs(l[i]), std::fabs(r[i])));
            const double kl = kHigh_[0].run(kShelf_[0].run(l[i])), kr = kHigh_[1].run(kShelf_[1].run(r[i]));
            kMs_ += kCoef_ * (kl * kl + kr * kr - kMs_);
        }
        outPeak_.store(pk, std::memory_order_relaxed);
        lufs_.store(kMs_ > 1e-10 ? static_cast<float>(-0.691 + 10.0 * std::log10(kMs_)) : -70.0f, std::memory_order_relaxed);
    }
    if (recordTarget_ > 0) {
        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getReadPointer(1);
        size_t pos = recordPos_.load();
        for (int i = 0; i < n && pos + 1 < recordTarget_; ++i) { record_[pos++] = l[i]; record_[pos++] = r[i]; }
        recordPos_ = pos + 1 >= recordTarget_ ? recordTarget_ : pos;
    }
    if (mute_.load(std::memory_order_relaxed)) buffer.clear();   // after the meters and the recording
    // The standalone stops at the end, with the rooms rung out.
    if (wrapperType == wrapperType_Standalone && engine_.seconds() > engine_.lengthSeconds() + 8.0) playing_ = false;
}

void ParhelionProcessor::silenceMidi(juce::MidiBuffer& midi)
{
    pendingOffs_ = 0;
    midiExpect_ = -1;
    if (!midiSounding_) return;
    midiSounding_ = false;
    for (int ch = 1; ch <= 16; ++ch) midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
}

void ParhelionProcessor::emitMidi(juce::MidiBuffer& midi, int64_t start, int n)
{
    const int64_t end = start + n;
    auto at = [&](int64_t s) { return static_cast<int>(std::clamp<int64_t>(s - start, 0, n - 1)); };
    // The one-shots' offs that are due (an off and an on at one sample: the off first).
    for (int i = 0; i < pendingOffs_;) {
        const PendingOff& p = pendingOff_[static_cast<size_t>(i)];
        if (p.sample < end) {
            midi.addEvent(juce::MidiMessage::noteOff(p.channel, p.pitch), at(p.sample));
            pendingOff_[static_cast<size_t>(i)] = pendingOff_[static_cast<size_t>(--pendingOffs_)];
        } else {
            ++i;
        }
    }
    for (int i = 0; i < noteTap_.count; ++i) {
        const parh::NoteTap::Note& nt = noteTap_.notes[i];
        const int channel = parh::midiChannelOf(static_cast<parh::Part>(nt.part)) + 1;
        if (nt.velocity == 0) {
            midi.addEvent(juce::MidiMessage::noteOff(channel, nt.pitch), at(nt.sample));
            continue;
        }
        midi.addEvent(juce::MidiMessage::noteOn(channel, nt.pitch, static_cast<juce::uint8>(nt.velocity)), at(nt.sample));
        midiSounding_ = true;
        if (nt.offSample >= 0 && pendingOffs_ < static_cast<int>(pendingOff_.size()))
            pendingOff_[static_cast<size_t>(pendingOffs_++)] = PendingOff{ std::max(nt.offSample, nt.sample + 1), static_cast<uint8_t>(channel), nt.pitch };
    }
}

bool ParhelionProcessor::saveSet(const juce::File& file)
{
    SetFile sf;
    {
        std::lock_guard<std::mutex> g(lock_);
        sf.seed = seed_;
        sf.curation = curation_;
    }
    sf.minutes = store().get(store().id(Module::Compose, 0, compose::Minutes));
    sf.set = store().get(store().id(Module::Set, 0, set::Minutes));
    return parh::saveSet(file.getFullPathName().toRawUTF8(), sf, store());
}

bool ParhelionProcessor::loadSet(const juce::File& file)
{
    SetFile sf;
    std::string err;
    beginStep("load " + file.getFileName());
    if (!parh::loadSet(file.getFullPathName().toRawUTF8(), sf, store(), &err)) { endStep(); return false; }
    {
        std::lock_guard<std::mutex> g(lock_);
        seed_ = sf.seed;
        curation_ = sf.curation;
    }
    if (sf.minutes > 0.0) store().set(store().id(Module::Compose, 0, compose::Minutes), static_cast<float>(sf.minutes));
    store().set(store().id(Module::Set, 0, set::Minutes), static_cast<float>(sf.set));
    for (int id = 0; id < store().count(); ++id)
        if (StoreParameter* p = parameter(id)) p->sendValueChangedMessageToListeners(p->getValue());
    endStep();
    compose();
    return true;
}

void ParhelionProcessor::exportTo(const juce::File& wav, int extras)
{
    if (exporting_.exchange(true)) return;
    if (exporter_ && exporter_->joinable()) exporter_->join();
    Playing p;
    bool levelled = true;
    uint64_t seed = 0;
    {
        std::lock_guard<std::mutex> g(lock_);
        p = current_;
        levelled = levelled_;
        seed = seed_;
    }
    auto params = std::make_shared<ParamStore>();
    params->copyValuesFrom(store());
    exporter_ = std::make_unique<std::thread>([this, p, params, wav, extras, levelled, seed]() mutable {
        if (!levelled) {   // exported before its measuring was done: measured here
            if (p.isSet) levelSet(p.set, *params);
            else levelScore(p.set.decks[0], *params);
        }
        constexpr double kRate = 48000.0;
        Engine e;
        e.params().copyValuesFrom(*params);
        e.prepare(kRate, 512);
        if (p.isSet) e.loadSet(p.set);
        else e.load(p.set.decks[0]);
        // The cues (PLAN 9): in the WAV and as JSON beside it; the tags.
        std::vector<CueAt> cues;
        std::string title;
        const TempoMap& tm = p.set.decks[0].tempo;
        if (p.isSet) {
            cues = setCues(p.setInfo, tm);
            title = std::string("Parhelion set ") + kDramaturgyNames[static_cast<int>(p.setInfo.dramaturgy)] + " seed " + std::to_string(seed);
        } else if (!p.tracks.empty()) {
            const TrackInfo& info = p.tracks[0].info;
            trackCues(info, 0.0, tm, std::string(), cues);
            title = "Parhelion " + info.style + " " + kFormTemplateNames[static_cast<int>(info.form)] + " " + kKeyNames[info.key] + " " + info.camelot
                  + " seed " + std::to_string(seed);
        }
        WavWriter w;
        bool ok = w.open(wav.getFullPathName().toRawUTF8(), static_cast<int>(kRate), 2, WavFormat::Pcm24);
        for (const CueAt& c : cues) w.addCue(static_cast<uint64_t>(std::llround(c.seconds * kRate)), c.label);
        w.setInfo("INAM", title);
        w.setInfo("ISFT", std::string("Parhelion ") + PARH_VERSION);
        w.setInfo("IGNR", "Trance");
        ok = ok && writeCuesJson(wav.getFullPathName().toStdString() + ".cues.json", cues, kRate);
        // Phase 16: the WAV as a rekordbox collection (its grid and its cues) beside it.
        {
            const int key = p.isSet ? (p.setInfo.tracks.empty() ? -1 : p.setInfo.tracks[0].info.key) : (p.tracks.empty() ? -1 : p.tracks[0].info.key);
            const std::string wavPath = wav.getFullPathName().toStdString();
            ok = ok && writeRekordboxXml(wavPath + ".rekordbox.xml", wavPath, title, key >= 0 ? std::string(kKeyNames[key]) + "m" : std::string(),
                                         cues, tm, p.set.lengthBeats, kRate);
        }
        // The stems: a WAV per element, their sum the mix before the master (Engine.h).
        const bool stems = (extras & kStems) != 0;
        std::vector<std::vector<float>> stemBuf(stems ? 2 * Engine::kStems : 0, std::vector<float>(512));
        std::vector<float*> stemL(Engine::kStems), stemR(Engine::kStems);
        std::vector<WavWriter> stemWav(stems ? Engine::kStems : 0);
        if (stems && ok) {
            const juce::File dir = wav.getParentDirectory().getChildFile(wav.getFileNameWithoutExtension() + "_stems");
            dir.createDirectory();
            for (int s = 0; s < Engine::kStems; ++s) {
                stemL[static_cast<size_t>(s)] = stemBuf[static_cast<size_t>(2 * s)].data();
                stemR[static_cast<size_t>(s)] = stemBuf[static_cast<size_t>(2 * s + 1)].data();
                const juce::String name = juce::String(s + 1).paddedLeft('0', 2) + "_" + Engine::stemName(s) + ".wav";
                ok = ok && stemWav[static_cast<size_t>(s)].open(dir.getChildFile(name).getFullPathName().toRawUTF8(), static_cast<int>(kRate), 2, WavFormat::Float32);
            }
            e.setStems(stemL.data(), stemR.data());
        }
        const int64_t total = static_cast<int64_t>((e.lengthSeconds() + 2.0) * kRate);
        std::vector<float> L(512), R(512);
        for (int64_t done = 0; ok && done < total; done += 512) {
            const int n = static_cast<int>(std::min<int64_t>(512, total - done));
            e.process(L.data(), R.data(), n);
            w.write(L.data(), R.data(), n);
            for (size_t s = 0; s < stemWav.size(); ++s) stemWav[s].write(stemL[s], stemR[s], n);
        }
        w.close();
        for (WavWriter& sw : stemWav) sw.close();
        const juce::File mid = wav.withFileExtension(".mid");
        ok = ok && writeMidiFile(p.isSet ? flattenSet(p.set) : p.set.decks[0], mid.getFullPathName().toRawUTF8(), "Parhelion", params.get());
        // The DJ loops of a track.
        if (ok && (extras & kLoops) != 0 && !p.isSet && !p.tracks.empty()) {
            const juce::File dir = wav.getParentDirectory().getChildFile(wav.getFileNameWithoutExtension() + "_loops");
            ok = renderLoops(*params, p.set.decks[0], p.tracks[0].info, dir.getFullPathName().toStdString(), kRate);
        }
        {
            std::lock_guard<std::mutex> g(lock_);
            lastExport_ = ok ? "exported " + wav.getFileName() + ", its MIDI and cues" + (stems ? juce::String(", stems") : juce::String())
                                   + ((extras & kLoops) != 0 && !p.isSet ? juce::String(", loops") : juce::String())
                             : "could not write " + wav.getFileName();
        }
        exporting_ = false;
    });
}

void ParhelionProcessor::writeRecording()
{
    const char* path = std::getenv("PARH_RECORD");
    if (path == nullptr || record_.empty()) return;
    WavWriter w;
    if (!w.open(path, static_cast<int>(sampleRate_), 2, WavFormat::Float32)) return;
    std::vector<float> L(record_.size() / 2), R(record_.size() / 2);
    for (size_t i = 0; i < L.size(); ++i) { L[i] = record_[2 * i]; R[i] = record_[2 * i + 1]; }
    w.write(L.data(), R.data(), static_cast<int>(L.size()));
    w.close();
    recordTarget_ = 0;
}

juce::File ParhelionProcessor::ratingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Parhelion").getChildFile("ratings.tsv");
}

juce::String ParhelionProcessor::rate(int value)
{
    const int t = trackAt(positionBeats());
    Rating r;
    juce::String what;
    {
        std::lock_guard<std::mutex> g(lock_);
        if (t < 0 || t >= static_cast<int>(current_.tracks.size())) return {};
        const TrackInfo& info = current_.tracks[static_cast<size_t>(t)].info;
        r.value = value > 0 ? 1 : -1;
        r.form = static_cast<int>(info.form);
        r.style = info.style;
        r.seed = current_.isSet ? current_.setInfo.tracks[static_cast<size_t>(t)].seed : seed_;
        // The groups of the presets it plays (its SoundPicks where it begins, on its deck).
        const TrackPlace& place = current_.tracks[static_cast<size_t>(t)];
        for (const SoundPick& s : current_.set.decks[place.deck].sounds) {
            if (s.beat != place.start) continue;
            const std::vector<SoundPreset>& ps = factoryPresets(static_cast<Module>(s.module), s.instance);
            if (s.preset < 0 || static_cast<size_t>(s.preset) >= ps.size()) continue;
            const std::string& grp = ps[static_cast<size_t>(s.preset)].group;
            if (std::find(r.groups.begin(), r.groups.end(), grp) == r.groups.end()) r.groups.push_back(grp);
        }
        ratings_.push_back(r);
        what << (r.value > 0 ? "liked: " : "not liked: ") << (current_.isSet ? "T" + juce::String(t + 1) + " " : juce::String())
             << juce::String(info.style) << " " << kFormTemplateNames[static_cast<int>(info.form)] << " (" << static_cast<int>(ratings_.size()) << " ratings)";
    }
    ratingsFile().getParentDirectory().createDirectory();
    appendRating(ratingsFile().getFullPathName().toStdString(), r);
    return what;
}

juce::String ParhelionProcessor::status() const
{
    if (composing_) return "composing ...";
    if (exporting_) return "exporting ...";
    std::lock_guard<std::mutex> g(lock_);
    juce::String t;
    const double secs = current_.set.decks[0].tempo.secondsAt(current_.set.lengthBeats);
    t << "seed " << juce::String(static_cast<juce::int64>(seed_)) << "   ";
    if (current_.isSet) t << "DJ mix of " << static_cast<int>(current_.tracks.size()) << " tracks, " << kDramaturgyNames[static_cast<int>(current_.setInfo.dramaturgy)];
    else if (!current_.tracks.empty()) {
        const TrackInfo& i = current_.tracks[0].info;
        t << "single track, " << juce::String(i.style) << " " << kFormTemplateNames[static_cast<int>(i.form)] << ", " << juce::String(i.bpm, 1) << " BPM, " << kKeyNames[i.key] << " "
          << kScaleNames[i.scale] << " (" << juce::String(i.camelot) << ")";
    }
    t << ", " << juce::String(secs / 60.0, 1) << " min";
    if (!levelled_ && !current_.tracks.empty()) t << ", levelling";
    if (lastExport_.isNotEmpty()) t << "   " << lastExport_;
    return t;
}

int ParhelionProcessor::controllerFor(int id) const
{
    for (int c = 0; c < 128; ++c) if (ccMap_[static_cast<size_t>(c)].load() == id) return c;
    return -1;
}

void ParhelionProcessor::forget(int id)
{
    for (auto& c : ccMap_) if (c.load() == id) c = -1;
}

void ParhelionProcessor::setFromMidi(int id, float value)
{
    StoreParameter* p = parameter(id);
    if (p == nullptr) return;
    const float norm = store().toNormalised(id, value);
    if (std::fabs(p->getValue() - norm) < 1.0e-6f) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost(norm);
    p->endChangeGesture();
}

void ParhelionProcessor::perform(const juce::MidiBuffer& midi)
{
    const ParamStore& s = store();
    // A keyboard that plays (perform.keyboard_part, 01.10.2026): its keys go to the engine, on their samples; else the
    // keys from middle C toggle the mutes.
    const bool keys = s.getInt(s.id(Module::Perform, 0, perform::KeyboardPart)) != perform::keys::Off;
    for (const auto meta : midi) {
        const juce::MidiMessage m = meta.getMessage();
        if (m.isAllNotesOff() || m.isAllSoundOff()) {
            engine_.liveAllOff();
        } else if (keys && (m.isNoteOn() || m.isNoteOff())) {
            engine_.queueLive(meta.samplePosition, m.getNoteNumber(), m.getVelocity(), m.getChannel() - 1, m.isNoteOn());
        } else if (m.isNoteOn()) {
            // The keys from middle C: C kick, C# bass, D hats, D# perc, E lead, F synths, F# pads -- each press toggles.
            const int k = m.getNoteNumber() - 60;
            if (k >= 0 && k < perform::kMutes) {
                const int id = s.id(Module::Perform, 0, perform::MuteKick + k);
                setFromMidi(id, s.getBool(id) ? 0.0f : 1.0f);
            }
        } else if (m.isChannelPressure()) {
            // The pressure: a source of every voice's matrix (perform.pressure).
            setFromMidi(s.id(Module::Perform, 0, perform::Pressure), static_cast<float>(m.getChannelPressureValue()) / 127.0f);
        } else if (m.isController()) {
            const int cc = m.getControllerNumber();
            if (cc < 0 || cc > 127) continue;
            const int learning = learn_.exchange(-1);
            if (learning >= 0) {
                for (auto& c : ccMap_) if (c.load() == learning) c = -1;   // one controller per control
                ccMap_[static_cast<size_t>(cc)] = learning;
            }
            const int id = ccMap_[static_cast<size_t>(cc)].load();
            if (id < 0) continue;
            const ParamDesc& d = s.desc(id);
            const float u = static_cast<float>(m.getControllerValue()) / 127.0f;
            float v = d.minValue + u * (d.maxValue - d.minValue);
            // A bipolar control has its middle on the controller's middle (64), exactly; a switch turns at the middle.
            if (d.minValue < 0.0f && d.maxValue > 0.0f && m.getControllerValue() == 64) v = 0.0f;
            if (d.curve == Curve::Toggle) v = m.getControllerValue() >= 64 ? 1.0f : 0.0f;
            setFromMidi(id, v);
        }
    }
}

void ParhelionProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("Parhelion");
    {
        std::lock_guard<std::mutex> g(lock_);
        xml.setAttribute("seed", juce::String(static_cast<juce::int64>(seed_)));
        juce::String rerolls;
        for (const auto& r : curation_.rerolls) rerolls << r.first << "=" << r.second << ";";
        xml.setAttribute("rerolls", rerolls);
    }
    xml.setAttribute("params", juce::String(store().toText(true)));
    juce::String cc;
    for (int c = 0; c < 128; ++c)
        if (const int id = ccMap_[static_cast<size_t>(c)].load(); id >= 0) cc << c << "=" << juce::String(store().key(id)) << ";";
    xml.setAttribute("controllers", cc);
    xml.setAttribute("mixMinutes", static_cast<double>(mixMinutes_));
    copyXmlToBinary(xml, destData);
}

void ParhelionProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName("Parhelion")) return;
    store().resetDefaults();
    store().parseText(xml->getStringAttribute("params").toStdString());
    if (xml->hasAttribute("mixMinutes")) mixMinutes_ = static_cast<float>(xml->getDoubleAttribute("mixMinutes", 60.0));
    if (xml->hasAttribute("controllers")) {
        for (auto& c : ccMap_) c = -1;
        for (const auto& item : juce::StringArray::fromTokens(xml->getStringAttribute("controllers"), ";", "")) {
            const int c = item.upToFirstOccurrenceOf("=", false, false).getIntValue();
            const int id = store().find(item.fromFirstOccurrenceOf("=", false, false).toStdString());
            if (c >= 0 && c < 128 && id >= 0) ccMap_[static_cast<size_t>(c)] = id;
        }
    }
    {
        std::lock_guard<std::mutex> g(lock_);
        seed_ = static_cast<uint64_t>(xml->getStringAttribute("seed").getLargeIntValue());
        curation_ = Curation{};
        for (const auto& item : juce::StringArray::fromTokens(xml->getStringAttribute("rerolls"), ";", ""))
            if (item.contains("=")) curation_.rerolls[item.upToFirstOccurrenceOf("=", false, false).toStdString()] = item.fromFirstOccurrenceOf("=", false, false).getIntValue();
    }
    history_.clear();   // a host's state is a new beginning
    compose();
}

// ---------------------------------------------------------------------------------------------------------------- undo

std::vector<float> ParhelionProcessor::values() const
{
    const ParamStore& s = engine_.params();
    std::vector<float> v(static_cast<size_t>(s.count()));
    for (int id = 0; id < s.count(); ++id) v[static_cast<size_t>(id)] = s.get(id);
    return v;
}

juce::String ParhelionProcessor::extraState() const
{
    std::lock_guard<std::mutex> g(lock_);
    juce::String t;
    t << "seed=" << juce::String(static_cast<juce::int64>(seed_)) << "\nmix=" << juce::String(mixMinutes_) << "\nrerolls=";
    for (const auto& r : curation_.rerolls) t << r.first << ":" << r.second << ";";
    return t;
}

void ParhelionProcessor::applyExtra(const juce::String& text)
{
    if (text == extraState()) return;
    bool again = false;
    {
        std::lock_guard<std::mutex> g(lock_);
        for (const juce::String& line : juce::StringArray::fromLines(text)) {
            const juce::String k = line.upToFirstOccurrenceOf("=", false, false), v = line.fromFirstOccurrenceOf("=", false, false);
            if (k == "seed") {
                const uint64_t seed = static_cast<uint64_t>(v.getLargeIntValue());
                again = again || seed != seed_;
                seed_ = seed;
            } else if (k == "mix") {
                mixMinutes_ = v.getFloatValue();
            } else if (k == "rerolls") {
                Curation c;
                for (const juce::String& item : juce::StringArray::fromTokens(v, ";", ""))
                    if (item.contains(":")) c.rerolls[item.upToLastOccurrenceOf(":", false, false).toStdString()] = item.fromLastOccurrenceOf(":", false, false).getIntValue();
                again = again || c.rerolls != curation_.rerolls;
                curation_ = c;
            }
        }
    }
    if (again) compose();
}

void ParhelionProcessor::beginStep(const juce::String& what) { history_.begin(what, values(), extraState(), true); }

void ParhelionProcessor::endStep() { history_.end(values(), extraState()); }

void ParhelionProcessor::parameterGestureChanged(int parameterIndex, bool gestureIsStarting)
{
    // Only the panel's gestures, which come on the message thread: a controller's (perform(), the audio thread) and an
    // undo's own are not steps.
    if (restoring_ || !juce::MessageManager::existsAndIsCurrentThread()) return;
    if (gestureIsStarting) {
        const ParamStore& s = store();
        history_.begin(parameterIndex >= 0 && parameterIndex < s.count() ? juce::String(s.desc(parameterIndex).name) : juce::String("knob"),
                       values(), extraState(), false);
        history_.touch(parameterIndex);
    } else {
        history_.end(values(), extraState());
    }
}

void ParhelionProcessor::applyStep(const frame::UndoStep& step, bool after)
{
    const juce::ScopedValueSetter<bool> quiet(restoring_, true);
    for (const auto& [id, before, now] : step.values) setFromUi(id, after ? now : before);
    applyExtra(after ? step.extraAfter : step.extraBefore);
}

bool ParhelionProcessor::undo()
{
    if (const frame::UndoStep* s = history_.undo()) { applyStep(*s, false); return true; }
    return false;
}

bool ParhelionProcessor::redo()
{
    if (const frame::UndoStep* s = history_.redo()) { applyStep(*s, true); return true; }
    return false;
}

void ParhelionProcessor::resetToDefault(int id)
{
    if (id < 0 || id >= store().count()) return;
    beginStep(juce::String(store().desc(id).name) + " to its default");
    setFromUi(id, store().desc(id).defValue);
    endStep();
}

float ParhelionProcessor::playedNormalised(int id) const
{
    const ParamStore& s = engine_.params();
    if (id < 0 || id >= s.count()) return std::numeric_limits<float>::quiet_NaN();
    const int lead = std::clamp(engine_.leadDeck(), 0, kDecks - 1);
    const float v = engine_.deck(lead).played(id), k = s.get(id);
    if (std::fabs(s.toNormalised(id, v) - s.toNormalised(id, k)) < 1.0e-4f) return std::numeric_limits<float>::quiet_NaN();
    return s.toNormalised(id, v);
}

// ------------------------------------------------------------------------------------------------------------- headset

void ParhelionProcessor::pollHeadset()
{
    frame::Settings& st = frame::Settings::of("Parhelion");
    headset_.listen(st.headset() == frame::Settings::HeadsetMode::Off ? 0 : st.headsetPort());
    const frame::HeadsetEvents e = headset_.poll();
    const ParamStore& s = store();
    {
        // The hands' moves are performing, not editing: no steps of their own.
        const juce::ScopedValueSetter<bool> quiet(restoring_, true);
        if (e.playStop && wrapperType == wrapperType_Standalone) setPlaying(!isPlaying());
        if (e.action) {
            const int id = s.id(Module::Perform, 0, perform::MuteKick);
            setFromUi(id, s.getBool(id) ? 0.0f : 1.0f);
        }
        if (e.filterMoved) setFromUi(s.id(Module::Perform, 0, perform::Filter), e.filter);
        if (e.throwMoved) setFromUi(s.id(Module::Perform, 0, perform::Throw), e.throwAmount);
    }
    if (e.hold) {
        // The performer's "now", as the Quest app has it: a drop in a breakdown, a break or a build, else a breakdown.
        SectionKind kind = SectionKind::Breakdown;
        const double beat = positionBeats();
        const int t = trackAt(beat);
        {
            std::lock_guard<std::mutex> g(lock_);
            if (t >= 0 && t < static_cast<int>(current_.tracks.size())) {
                const Score& deck = current_.set.decks[static_cast<size_t>(current_.tracks[static_cast<size_t>(t)].deck)];
                const Section* sec = nullptr;
                for (const Section& x : deck.sections) { if (x.beat > beat) break; sec = &x; }
                if (sec != nullptr && (sec->kind == SectionKind::Breakdown || sec->kind == SectionKind::Break || sec->kind == SectionKind::Build))
                    kind = SectionKind::Drop;
            }
        }
        performNow(kind);
    }
    if (e.next) newSeed();
}

juce::AudioProcessorEditor* ParhelionProcessor::createEditor() { return new ParhelionEditor(*this); }

/** @brief The plugin's factory, called by the JUCE wrappers. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ParhelionProcessor(); }
