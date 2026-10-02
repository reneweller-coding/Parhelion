/**
 * @file Engine.cpp
 * @brief The engine: the decks, the DJ mixer, the master.
 * @note The raster and the event splitting follow Ephemeris `Core/src/Engine.cpp` at d047d79 (27.09.2026).
 * @note Copied from Totality `Core/src/Engine.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_; the stems are
 *       Parhelion's.
 */
#include "parh/Engine.h"
#include "parh/Profile.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace parh {

namespace {
constexpr float kClipCeiling = 0.97f;   ///< where the soft clipper's curve flattens (-0.26 dBFS)
constexpr float kSqrt2 = 1.41421356f;   ///< sqrt 2
}

const char* Engine::stemName(int s)
{
    static const char* const kNames[kStems] = { "kick", "sub", "bass", "acid", "hats", "perc", "lead", "counter", "pluck",
                                                "arp", "pad", "stab", "piano", "strings", "choir", "brass", "timpani", "room", "plate", "hall", "cloud", "fx", "djfx" };
    return s >= 0 && s < kStems ? kNames[s] : "";
}

void Engine::ThreeBand::set(float fs)
{
    for (Svf* f : { &split1, &low2, &high2 }) f->setK(200.0f, kSqrt2, fs);
    for (Svf* f : { &split2, &mid2, &top2, &ap }) f->setK(2500.0f, kSqrt2, fs);
}

void Engine::ThreeBand::reset()
{
    for (Svf* f : { &split1, &low2, &high2, &split2, &mid2, &top2, &ap }) f->reset();
}

void Engine::ThreeBand::process(float x, float& low, float& mid, float& top)
{
    float lp, bp, hp, l1, h1;
    split1.tick(x, l1, bp, h1);
    const float lo = low2.lp(l1);
    high2.tick(h1, lp, bp, hp);
    const float high = hp;
    split2.tick(high, l1, bp, h1);
    mid = mid2.lp(l1);
    top2.tick(h1, lp, bp, hp);
    top = hp;
    ap.tick(lo, lp, bp, hp);
    low = lo - 2.0f * kSqrt2 * bp;   // through the upper crossover's allpass, in phase with the others
}

void Engine::prepare(double sampleRate, int maxBlock)
{
    sampleRate_ = sampleRate;
    maxBlock_ = std::max(1, maxBlock);
    shown_.assign(static_cast<size_t>(params_.count()), std::numeric_limits<float>::quiet_NaN());
    for (int d = 0; d < kDecks; ++d) { decks_[d].prepare(&params_, sampleRate, d); decks_[d].setShown(shown_.data()); }
    const float fs = static_cast<float>(sampleRate);
    for (Channel& c : ch_) for (ThreeBand& b : c.bands) b.set(fs);
    for (auto& deck : stemBands_) for (auto& stem : deck) for (ThreeBand& b : stem) b.set(fs);
    deckStems_.assign(static_cast<size_t>(kDecks * Deck::kStems * 2 * kRaster), 0.0f);
    for (int d = 0; d < kDecks; ++d)
        for (int s = 0; s < Deck::kStems; ++s) {
            float* base = deckStems_.data() + static_cast<size_t>(((d * Deck::kStems + s) * 2) * kRaster);
            deckStemL_[d][s] = base;
            deckStemR_[d][s] = base + kRaster;
        }
    smooth_ = 1.0f - std::exp(-1.0f / (0.001f * fs));
    djEcho_.prepare(sampleRate, 3.0, 0x444A4543484Full);   // "DJECHO"
    djHall_.prepare(sampleRate);
    limiter_.prepare(sampleRate);
    for (BandLimit& b : cutLp_) b.prepare(sampleRate, 16000.0f);
    for (int c = 0; c < 2; ++c) { cutHp1_[c].setK(6000.0f, kSqrt2, fs); cutHp2_[c].setK(6000.0f, kSqrt2, fs); }
    cutAtt_ = 1.0f - std::exp(-1.0f / (0.001f * fs));
    cutRel_ = 1.0f - std::exp(-1.0f / (0.05f * fs));
    const size_t n = static_cast<size_t>(kRaster);
    for (int d = 0; d < kDecks; ++d) { deckL_[d].assign(n, 0.0f); deckR_[d].assign(n, 0.0f); }
    for (std::vector<float>* b : { &sendL_, &sendR_, &fxL_, &fxR_ }) b->assign(n, 0.0f);
    cellDirty_ = true;
}

void Engine::load(const Score& score)
{
    SetScore s;
    s.decks[0] = score;
    s.lengthBeats = score.lengthBeats;
    loadSet(s);
    isSet_ = live_;   // a track alone: no isolator, no filter in its path -- unless it is played live
}

void Engine::setLive(bool on)
{
    live_ = on;
    for (Deck& d : decks_) d.setLive(on);
}

void Engine::loadSet(const SetScore& set)
{
    for (int d = 0; d < kDecks; ++d) {
        if (d == 0 || !set.decks[d].notes.empty() || !set.decks[d].gestures.empty()) decks_[d].load(set.decks[d]);
        else decks_[d].clear();
    }
    tempo_ = set.decks[0].tempo;
    lengthBeats_ = set.lengthBeats;
    isSet_ = true;
    steps_.clear();
    for (const Deck& d : decks_) steps_.insert(steps_.end(), d.mixerSteps().begin(), d.mixerSteps().end());
    std::sort(steps_.begin(), steps_.end());
    steps_.erase(std::unique(steps_.begin(), steps_.end()), steps_.end());
    // The set's own automation.
    tracks_.clear();
    trackOf_.assign(static_cast<size_t>(params_.count()), -1);
    std::vector<Gesture> g = set.gestures;
    std::stable_sort(g.begin(), g.end(), [](const Gesture& a, const Gesture& b) { return a.beat < b.beat; });
    for (const Gesture& x : g) {
        if (x.param < 0 || x.param >= params_.count()) continue;
        int& t = trackOf_[static_cast<size_t>(x.param)];
        if (t < 0) { t = static_cast<int>(tracks_.size()); tracks_.push_back(Track{ x.param, {}, 0, 0.0f }); }
        tracks_[static_cast<size_t>(t)].gestures.push_back(x);
    }
    // The cues: every deck's marks and the set's tracks.
    cueMarks_.clear();
    for (int d = 0; d < kDecks; ++d) {
        if (!decks_[d].loaded()) continue;
        const std::vector<CueMark> m = cueMarksOf(set.decks[d], params_);
        cueMarks_.insert(cueMarks_.end(), m.begin(), m.end());
    }
    {
        Score names;
        names.markers = set.markers;
        const std::vector<CueMark> m = cueMarksOf(names, params_);
        cueMarks_.insert(cueMarks_.end(), m.begin(), m.end());
    }
    std::stable_sort(cueMarks_.begin(), cueMarks_.end(), [](const CueMark& a, const CueMark& b) { return a.beat < b.beat; });
    // The mixer's effects run where a deck sends into them (and always live: the throw).
    fxOn_ = live_;
    for (int d = 0; d < kDecks; ++d)
        for (const Gesture& x : set.decks[d].gestures)
            if (x.param == params_.id(Module::Deck, d, deck::FxSend)) fxOn_ = true;
    endSample_ = static_cast<int64_t>(std::ceil(lengthSeconds() * sampleRate_));
    seek(0.0);
}

void Engine::seek(double beat)
{
    const double x = std::max(0.0, tempo_.secondsAt(beat)) * sampleRate_;
    sample_ = static_cast<int64_t>(std::llround(x));
    for (Deck& d : decks_) if (d.loaded()) d.seek(sample_);
    for (bool& p : playing_) p = false;
    stepCursor_ = 0;
    while (stepCursor_ < steps_.size() && steps_[stepCursor_] < sample_) ++stepCursor_;
    for (Track& t : tracks_) { t.cursor = t.gestures.size(); t.offset = 0.0f; }
    for (auto& deck : stemBands_) for (auto& stem : deck) for (ThreeBand& b : stem) b.reset();
    for (auto& deck : stemFilt_) for (auto& stem : deck) for (Svf& f : stem) f.reset();
    for (Channel& c : ch_) {
        for (ThreeBand& b : c.bands) b.reset();
        for (Svf& f : c.filt) f.reset();
        for (int k = 0; k < 3; ++k) c.g[k] = c.target[k] = 1.0f;
        c.gFader = c.fader = 1.0f;
    }
    djEcho_.reset();
    djHall_.reset();
    limiter_.reset();
    for (Oversampler4& o : clipOs_) o.reset();
    for (BandLimit& b : cutLp_) b.reset();
    for (int c = 0; c < 2; ++c) { cutHp1_[c].reset(); cutHp2_[c].reset(); }
    cutEnv_ = 0.0f;
    sideHp1_.reset();
    sideHp2_.reset();
    for (Svf& f : perfFilt_) f.reset();
    lead_ = -1;
    leadGroup_ = -1.0;
    cellDirty_ = true;
}

void Engine::showKnobs(int d)
{
    for (int id = 0; id < params_.count(); ++id) {
        const float b = decks_[d].baseOf(id);
        if (!(b == b)) continue;
        params_.set(id, b);
        shown_[static_cast<size_t>(id)] = params_.get(id);
    }
    lead_ = d;
    leadGroup_ = decks_[d].knobGroup();
    soundsVersion_.fetch_add(1, std::memory_order_relaxed);
}

float Engine::setPlayed(int id) const
{
    const float knob = params_.get(id);
    if (id < 0 || id >= static_cast<int>(trackOf_.size()) || trackOf_[static_cast<size_t>(id)] < 0) return knob;
    const float off = tracks_[static_cast<size_t>(trackOf_[static_cast<size_t>(id)])].offset;
    return off == 0.0f ? knob : params_.fromNormalised(id, params_.toNormalised(id, knob) + off);
}

void Engine::updateCell()
{
    cellDirty_ = false;
    const double beat = tempo_.beatAt(seconds());
    for (Track& t : tracks_) {
        const size_t none = t.gestures.size();
        size_t c = t.cursor, next = c == none ? 0 : c + 1;
        while (next < t.gestures.size() && t.gestures[next].beat <= beat) { c = next; ++next; }
        t.cursor = c;
        t.offset = c == none ? 0.0f : gestureValue(t.gestures[c], beat);
    }
    const float fs = static_cast<float>(sampleRate_);
    // The channels: each deck reads its own channel as its score moves it.
    for (int d = 0; d < kDecks; ++d) {
        Channel& c = ch_[d];
        if (!decks_[d].loaded()) continue;
        float v[16];
        decks_[d].readPlayed(Module::Deck, d, v);
        const auto gain = [](float db) { return db <= -59.9f ? 0.0f : dbToGain(db); };
        c.fader = gain(v[deck::Fader]);
        c.target[0] = gain(v[deck::Low]);
        c.target[1] = gain(v[deck::Mid]);
        c.target[2] = gain(v[deck::High]);
        c.send = v[deck::FxSend];
        c.filter = v[deck::Filter];
        if (std::fabs(c.filter) >= 0.01f) {
            // Below 0 a low pass from 20 kHz down to 20 Hz, above 0 a high pass from 20 Hz up to 20 kHz.
            const float f = c.filter < 0.0f ? 20000.0f * std::exp2(10.0f * c.filter) : 20.0f * std::exp2(10.0f * c.filter);
            for (Svf& s : c.filt) s.setQ(std::min(f, 0.45f * fs), 0.9f, fs);
        } else {
            for (Svf& s : c.filt) s.reset();
            for (auto& stem : stemFilt_[d]) for (Svf& s : stem) s.reset();
        }
    }
    // The performer (live only): the master filter, the echo throw.
    if (live_) {
        perfFilter_ = params_.get(params_.id(Module::Perform, 0, perform::Filter));
        perfThrow_ = params_.get(params_.id(Module::Perform, 0, perform::Throw));
        if (std::fabs(perfFilter_) >= 0.01f) {
            const float f = perfFilter_ < 0.0f ? 20000.0f * std::exp2(10.0f * perfFilter_) : 20.0f * std::exp2(10.0f * perfFilter_);
            for (Svf& s : perfFilt_) s.setQ(std::min(f, 0.45f * fs), 0.9f, fs);
        } else {
            for (Svf& s : perfFilt_) s.reset();
        }
    }
    if (fxOn_) {
        float v[16];
        for (int k = 0; k < djfx::Count; ++k) v[k] = setPlayed(params_.id(Module::DjFx, 0, k));
        static const double kEchoBeats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
        EchoSettings e;
        e.delaySeconds = kEchoBeats[std::clamp(static_cast<int>(std::lround(v[djfx::EchoTime])), 0, 5)] * 60.0 / std::max(20.0, tempo_.bpmAt(beat));
        e.feedback = v[djfx::Feedback];
        e.toneHz = 5000.0f;
        e.lowCutHz = 250.0f;
        djEcho_.set(e);
        djHall_.set(1.2f, v[djfx::HallDecay], 0.4f, 0.02f * fs, 300.0f, 8000.0f);
        echoReturn_ = v[djfx::EchoReturn] <= -59.9f ? 0.0f : dbToGain(v[djfx::EchoReturn]);
        hallReturn_ = v[djfx::HallReturn] <= -59.9f ? 0.0f : dbToGain(v[djfx::HallReturn]);
    }
    // The master: the knobs.
    masterGain_ = dbToGain(params_.get(params_.id(Module::Master, 0, master::Level)));
    clipDrive_ = dbToGain(params_.get(params_.id(Module::Master, 0, master::Clip)));
    limiter_.set(params_.get(params_.id(Module::Master, 0, master::Ceiling)), 60.0f);
    cut_ = false;   // (Totality's vinyl cut profile is not Parhelion's)
    const float monoKnob = params_.get(params_.id(Module::Master, 0, master::MonoBelow));
    const float mono = cut_ ? std::max(monoKnob, 150.0f) : monoKnob;
    sideHp1_.setK(mono, 1.8477590f, fs);   // fourth-order Butterworth
    sideHp2_.setK(mono, 0.7653669f, fs);
}

void Engine::mix(float* L, float* R, int n)
{
    PARH_PROF_BEGIN(Mixer);
    for (int i = 0; i < n; ++i) { L[i] = 0.0f; R[i] = 0.0f; sendL_[static_cast<size_t>(i)] = 0.0f; sendR_[static_cast<size_t>(i)] = 0.0f; }
    for (int d = 0; d < kDecks; ++d) {
        if (!playing_[d]) continue;
        Channel& c = ch_[d];
        float* dl = deckL_[d].data();
        float* dr = deckR_[d].data();
        for (int i = 0; i < n; ++i) {
            float l = dl[i], r = dr[i];
            if (isSet_) {
                for (int k = 0; k < 3; ++k) c.g[k] += (c.target[k] - c.g[k]) * smooth_;
                c.gFader += (c.fader - c.gFader) * smooth_;
                float lo, mi, hi;
                c.bands[0].process(l, lo, mi, hi);
                l = (lo * c.g[0] + mi * c.g[1] + hi * c.g[2]) * c.gFader;
                c.bands[1].process(r, lo, mi, hi);
                r = (lo * c.g[0] + mi * c.g[1] + hi * c.g[2]) * c.gFader;
                if (std::fabs(c.filter) >= 0.01f) {
                    float lp, bp, hp;
                    c.filt[0].tick(l, lp, bp, hp);
                    l = c.filter < 0.0f ? lp : hp;
                    c.filt[1].tick(r, lp, bp, hp);
                    r = c.filter < 0.0f ? lp : hp;
                }
            }
            L[i] += l;
            R[i] += r;
            if (tapL_ != nullptr) { tapL_[d][tapOffset_ + i] = l; tapR_[d][tapOffset_ + i] = r; }
            if (fxOn_) { sendL_[static_cast<size_t>(i)] += l * c.send; sendR_[static_cast<size_t>(i)] += r * c.send; }
            if (stemL_ != nullptr) { chGains_[0][i] = c.g[0]; chGains_[1][i] = c.g[1]; chGains_[2][i] = c.g[2]; chGains_[3][i] = c.gFader; }
        }
        if (stemL_ == nullptr) continue;
        // The deck's stems through their own copies of its channel (the same gains, the filters' own states).
        const bool filtered = std::fabs(c.filter) >= 0.01f;
        for (int s = 0; s < Deck::kStems; ++s) {
            const float* in[2] = { deckStemL_[d][s], deckStemR_[d][s] };
            float* out[2] = { stemL_[s] + tapOffset_, stemR_[s] + tapOffset_ };
            for (int ch = 0; ch < 2; ++ch) {
                ThreeBand& b = stemBands_[d][s][ch];
                Svf& f = stemFilt_[d][s][ch];
                f.copyCoefficients(c.filt[ch]);
                for (int i = 0; i < n; ++i) {
                    float x = in[ch][i];
                    if (isSet_) {
                        float lo, mi, hi;
                        b.process(x, lo, mi, hi);
                        x = (lo * chGains_[0][i] + mi * chGains_[1][i] + hi * chGains_[2][i]) * chGains_[3][i];
                        if (filtered) {
                            float lp, bp, hp;
                            f.tick(x, lp, bp, hp);
                            x = c.filter < 0.0f ? lp : hp;
                        }
                    }
                    out[ch][i] += x;
                }
            }
        }
    }
    if (live_) {
        // The performer's master filter over the decks, then the throw of what is heard into the echo.
        const bool filtered = std::fabs(perfFilter_) >= 0.01f;
        for (int i = 0; i < n; ++i) {
            if (filtered) {
                float lp, bp, hp;
                perfFilt_[0].tick(L[i], lp, bp, hp);
                L[i] = perfFilter_ < 0.0f ? lp : hp;
                perfFilt_[1].tick(R[i], lp, bp, hp);
                R[i] = perfFilter_ < 0.0f ? lp : hp;
            }
            sendL_[static_cast<size_t>(i)] += L[i] * perfThrow_;
            sendR_[static_cast<size_t>(i)] += R[i] * perfThrow_;
        }
    }
    if (fxOn_) {
        for (int i = 0; i < n; ++i) { fxL_[static_cast<size_t>(i)] = 0.0f; fxR_[static_cast<size_t>(i)] = 0.0f; }
        djEcho_.process(sendL_.data(), sendR_.data(), fxL_.data(), fxR_.data(), n);
        for (int i = 0; i < n; ++i) {
            L[i] += fxL_[static_cast<size_t>(i)] * echoReturn_;
            R[i] += fxR_[static_cast<size_t>(i)] * echoReturn_;
        }
        if (stemL_ != nullptr)
            for (int i = 0; i < n; ++i) {
                stemL_[kStemMixFx][tapOffset_ + i] = fxL_[static_cast<size_t>(i)] * echoReturn_;
                stemR_[kStemMixFx][tapOffset_ + i] = fxR_[static_cast<size_t>(i)] * echoReturn_;
            }
        djHall_.process(sendL_.data(), sendR_.data(), fxL_.data(), fxR_.data(), n);
        for (int i = 0; i < n; ++i) {
            L[i] += fxL_[static_cast<size_t>(i)] * hallReturn_;
            R[i] += fxR_[static_cast<size_t>(i)] * hallReturn_;
        }
        if (stemL_ != nullptr)
            for (int i = 0; i < n; ++i) {
                stemL_[kStemMixFx][tapOffset_ + i] += fxL_[static_cast<size_t>(i)] * hallReturn_;
                stemR_[kStemMixFx][tapOffset_ + i] += fxR_[static_cast<size_t>(i)] * hallReturn_;
            }
    }
    if (preL_ != nullptr)
        for (int i = 0; i < n; ++i) { preL_[tapOffset_ + i] = L[i]; preR_[tapOffset_ + i] = R[i]; }
    PARH_PROF_END(Mixer);
    PARH_PROF_BEGIN(MasterTone);
    // The master: the side under Mono Below goes (PLAN 8.2), the level, the vinyl cut, the clipper, the limiter.
    for (int i = 0; i < n; ++i) {
        const float mid = 0.5f * (L[i] + R[i]);
        float side = 0.5f * (L[i] - R[i]), lp, bp, hp;
        sideHp1_.tick(side, lp, bp, hp);
        sideHp2_.tick(hp, lp, bp, hp);
        side = hp;
        L[i] = (mid + side) * masterGain_;
        R[i] = (mid - side) * masterGain_;
    }
    if (cut_) {
        // The vinyl cut (Erg. 3): the band over 6 kHz limited dynamically (a cutter head's protection), then 16 kHz.
        for (int i = 0; i < n; ++i) {
            float h[2];
            float* chn[2] = { &L[i], &R[i] };
            float peak = 0.0f;
            for (int c = 0; c < 2; ++c) {
                float lp, bp, hp;
                cutHp1_[c].tick(*chn[c], lp, bp, hp);
                cutHp2_[c].tick(hp, lp, bp, h[c]);
                peak = std::max(peak, std::fabs(h[c]));
            }
            cutEnv_ += (peak - cutEnv_) * (peak > cutEnv_ ? cutAtt_ : cutRel_);
            const float thr = 0.1f;   // -20 dBFS in the band
            const float gr = cutEnv_ > thr ? std::pow(thr / cutEnv_, 0.75f) : 1.0f;   // ratio 4
            for (int c = 0; c < 2; ++c) *chn[c] = cutLp_[c].process(*chn[c] - (1.0f - gr) * h[c]);
        }
    }
    PARH_PROF_END(MasterTone);
    PARH_PROF_BEGIN(Clipper);
    const float gd = clipDrive_, t = kClipCeiling;
    auto curve = [gd, t](float x) { return t * std::tanh(gd * x / t); };
    for (int i = 0; i < n; ++i) {
        L[i] = clipOs_[0].process(L[i], curve);
        R[i] = clipOs_[1].process(R[i], curve);
    }
    PARH_PROF_END(Clipper);
    { PARH_PROF(Limiter); limiter_.process(L, R, n); }
}

bool Engine::process(float* L, float* R, int n)
{
    DenormalGuard guard;
    int done = 0;
    while (done < n) {
        const bool atRaster = sample_ % kRaster == 0;
        bool atStep = false;
        while (stepCursor_ < steps_.size() && steps_[stepCursor_] <= sample_) { atStep = atStep || steps_[stepCursor_] == sample_; ++stepCursor_; }
        const bool cell = cellDirty_ || atRaster || atStep;
        for (int d = 0; d < kDecks; ++d) {
            const bool plays = decks_[d].loaded() && decks_[d].playsAt(sample_);
            if (plays && (cell || !playing_[d])) { PARH_PROF(Cells); decks_[d].updateCell(sample_); }
            playing_[d] = plays;
        }
        // The knobs show the knob settings of the track that began last on deck A or B (the loops' deck C only while
        // they have none): written when that changes. The sound does not wait for it -- every deck plays from its own.
        {
            int lead = -1;
            double at = -1.0;
            for (int d = 0; d < 2; ++d) if (decks_[d].knobGroup() >= 0.0 && decks_[d].knobGroup() > at) { at = decks_[d].knobGroup(); lead = d; }
            if (lead < 0 && decks_[2].knobGroup() >= 0.0) lead = 2;
            for (Deck& d : decks_) d.takeNewGroup();
            if (lead >= 0 && (lead != lead_ || decks_[lead].knobGroup() != leadGroup_)) showKnobs(lead);
        }
        if (cell) { PARH_PROF(Cells); updateCell(); }
        { PARH_PROF(Events); for (int d = 0; d < kDecks; ++d) if (playing_[d]) decks_[d].dispatchUntil(sample_); }
        // The keys played on a MIDI keyboard, on their samples (queueLive, 01.10.2026).
        while (liveCursor_ < liveCount_ && liveQueue_[liveCursor_].at <= sample_) playLive(liveQueue_[liveCursor_++]);
        int64_t end = std::min<int64_t>(sample_ + (n - done), (sample_ / kRaster + 1) * kRaster);
        for (const Deck& d : decks_) {
            if (!d.loaded()) continue;
            const int64_t e = d.nextEvent(), r = d.nextRestChange(sample_);
            if (e > sample_ && e < end) end = e;
            if (r < end) end = r;
        }
        if (stepCursor_ < steps_.size() && steps_[stepCursor_] < end) end = steps_[stepCursor_];
        if (liveCursor_ < liveCount_ && liveQueue_[liveCursor_].at < end) end = liveQueue_[liveCursor_].at;
        const int len = static_cast<int>(end - sample_);
        if (stemL_ != nullptr)
            for (int s = 0; s < kStems; ++s)
                for (int i = 0; i < len; ++i) { stemL_[s][done + i] = 0.0f; stemR_[s][done + i] = 0.0f; }
        for (int d = 0; d < kDecks; ++d)
            if (playing_[d])
                decks_[d].render(sample_, deckL_[d].data(), deckR_[d].data(), len, stemL_ != nullptr ? deckStemL_[d] : nullptr,
                                 stemL_ != nullptr ? deckStemR_[d] : nullptr);
        if (tapL_ != nullptr)
            for (int d = 0; d < kDecks; ++d)
                for (int i = 0; i < len; ++i) { tapL_[d][done + i] = 0.0f; tapR_[d][done + i] = 0.0f; }
        tapOffset_ = done;
        mix(L + done, R + done, len);
        done += len;
        sample_ += len;
    }
    while (liveCursor_ < liveCount_) playLive(liveQueue_[liveCursor_++]);   // a key past the block's end: now
    liveCount_ = liveCursor_ = 0;
    return sample_ < endSample_;
}

int Engine::keyboardTarget(int channel) const
{
    const int part = params_.getInt(params_.id(Module::Perform, 0, perform::KeyboardPart));
    if (part != perform::keys::ByChannel) return std::clamp(part, 0, static_cast<int>(perform::keys::ByChannel) - 1);
    // By channel: 1 kit, 2 bass, 3 303, 4 .. 9 the polyphonic voices, 10 kit (General MIDI's drums), 11 .. 14 the piano,
    // the strings, the choir, the brass.
    static const int kChannel[14] = { perform::keys::Kit, perform::keys::Bass, perform::keys::Acid, perform::keys::Lead,
                                      perform::keys::Counter, perform::keys::Pluck, perform::keys::Arp, perform::keys::Pad,
                                      perform::keys::Stab, perform::keys::Kit, perform::keys::Piano, perform::keys::Strings,
                                      perform::keys::Choir, perform::keys::Brass };
    return channel >= 0 && channel < 14 ? kChannel[channel] : perform::keys::Off;
}

void Engine::queueLive(int offset, int pitch, int velocity, int channel, bool on, int target)
{
    if (!live_ || pitch < 0 || pitch > 127 || liveCount_ >= kLiveQueue) return;
    LiveKey k;
    k.at = sample_ + std::max(0, offset);
    k.pitch = pitch;
    k.velocity = static_cast<float>(std::clamp(velocity, 1, 127)) / 127.0f;
    k.target = on ? (target > perform::keys::Off ? target : keyboardTarget(channel)) : liveTarget_[pitch] - 1;   // the split names one
    k.on = on;
    if (k.target <= perform::keys::Off) return;
    liveQueue_[liveCount_++] = k;
}

void Engine::playLive(const LiveKey& k)
{
    if (!k.on) { releaseLive(k.pitch); return; }
    if (liveTarget_[k.pitch] != 0) releaseLive(k.pitch);
    const int d = std::max(0, lead_.load(std::memory_order_relaxed));
    if (!decks_[d].loaded()) return;
    decks_[d].liveNote(sample_, k.target, k.pitch, k.velocity, true);
    liveTarget_[k.pitch] = static_cast<uint8_t>(k.target + 1);
    liveDeck_[k.pitch] = static_cast<uint8_t>(d);
}

void Engine::releaseLive(int pitch)
{
    if (liveTarget_[pitch] == 0) return;
    decks_[liveDeck_[pitch]].liveNote(sample_, liveTarget_[pitch] - 1, pitch, 0.0f, false);
    liveTarget_[pitch] = 0;
}

void Engine::liveAllOff()
{
    liveCount_ = liveCursor_ = 0;
    for (int k = 0; k < 128; ++k) releaseLive(k);
    for (Deck& d : decks_) d.liveAllOff();
}

} // namespace parh
