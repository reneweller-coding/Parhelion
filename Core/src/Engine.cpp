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
constexpr float ClipCeiling = 0.97f;   ///< where the soft clipper's curve flattens (-0.26 dBFS)
constexpr float Sqrt2 = 1.41421356f;   ///< sqrt 2
}

const char* Engine::stemName(int s)
{
    static const char* const Names[STEMS] = { "kick", "sub", "bass", "acid", "hats", "perc", "lead", "counter", "pluck",
                                                "arp", "pad", "stab", "piano", "strings", "choir", "brass", "timpani", "room", "plate", "hall", "cloud", "fx", "djfx" };
    return s >= 0 && s < STEMS ? Names[s] : "";
}

void Engine::ThreeBand::set(float fs)
{
    for (Svf* f : { &m_split1, &m_low2, &m_high2 }) f->setK(200.0f, Sqrt2, fs);
    for (Svf* f : { &m_split2, &m_mid2, &m_top2, &m_ap }) f->setK(2500.0f, Sqrt2, fs);
}

void Engine::ThreeBand::reset()
{
    for (Svf* f : { &m_split1, &m_low2, &m_high2, &m_split2, &m_mid2, &m_top2, &m_ap }) f->reset();
}

void Engine::ThreeBand::process(float x, float& low, float& mid, float& top)
{
    float lp, bp, hp, l1, h1;
    m_split1.tick(x, l1, bp, h1);
    const float lo = m_low2.lp(l1);
    m_high2.tick(h1, lp, bp, hp);
    const float high = hp;
    m_split2.tick(high, l1, bp, h1);
    mid = m_mid2.lp(l1);
    m_top2.tick(h1, lp, bp, hp);
    top = hp;
    m_ap.tick(lo, lp, bp, hp);
    low = lo - 2.0f * Sqrt2 * bp;   // through the upper crossover's allpass, in phase with the others
}

void Engine::prepare(double sampleRate, int maxBlock)
{
    m_sampleRate = sampleRate;
    m_maxBlock = std::max(1, maxBlock);
    m_shown.assign(static_cast<size_t>(m_params.count()), std::numeric_limits<float>::quiet_NaN());
    for (int d = 0; d < Decks; ++d) { m_decks[d].prepare(&m_params, sampleRate, d); m_decks[d].setShown(m_shown.data()); }
    const float fs = static_cast<float>(sampleRate);
    for (Channel& c : m_ch) for (ThreeBand& b : c.m_bands) b.set(fs);
    for (auto& deck : m_stemBands) for (auto& stem : deck) for (ThreeBand& b : stem) b.set(fs);
    m_deckStems.assign(static_cast<size_t>(Decks * Deck::STEMS * 2 * Raster), 0.0f);
    for (int d = 0; d < Decks; ++d)
        for (int s = 0; s < Deck::STEMS; ++s) {
            float* base = m_deckStems.data() + static_cast<size_t>(((d * Deck::STEMS + s) * 2) * Raster);
            m_deckStemL[d][s] = base;
            m_deckStemR[d][s] = base + Raster;
        }
    m_smooth = 1.0f - std::exp(-1.0f / (0.001f * fs));
    m_djEcho.prepare(sampleRate, 3.0, 0x444A4543484Full);   // "DJECHO"
    m_djHall.prepare(sampleRate);
    m_limiter.prepare(sampleRate);
    for (BandLimit& b : m_cutLp) b.prepare(sampleRate, 16000.0f);
    for (int c = 0; c < 2; ++c) { m_cutHp1[c].setK(6000.0f, Sqrt2, fs); m_cutHp2[c].setK(6000.0f, Sqrt2, fs); }
    m_cutAtt = 1.0f - std::exp(-1.0f / (0.001f * fs));
    m_cutRel = 1.0f - std::exp(-1.0f / (0.05f * fs));
    const size_t n = static_cast<size_t>(Raster);
    for (int d = 0; d < Decks; ++d) { m_deckL[d].assign(n, 0.0f); m_deckR[d].assign(n, 0.0f); }
    for (std::vector<float>* b : { &m_sendL, &m_sendR, &m_fxL, &m_fxR }) b->assign(n, 0.0f);
    m_cellDirty = true;
}

void Engine::load(const Score& score)
{
    SetScore s;
    s.m_decks[0] = score;
    s.m_lengthBeats = score.m_lengthBeats;
    loadSet(s);
    m_isSet = m_live;   // a track alone: no isolator, no filter in its path -- unless it is played live
}

void Engine::setLive(bool on)
{
    m_live = on;
    for (Deck& d : m_decks) d.setLive(on);
}

void Engine::loadSet(const SetScore& set)
{
    for (int d = 0; d < Decks; ++d) {
        if (d == 0 || !set.m_decks[d].m_notes.empty() || !set.m_decks[d].m_gestures.empty()) m_decks[d].load(set.m_decks[d]);
        else m_decks[d].clear();
    }
    m_tempo = set.m_decks[0].m_tempo;
    m_lengthBeats = set.m_lengthBeats;
    m_isSet = true;
    m_steps.clear();
    for (const Deck& d : m_decks) m_steps.insert(m_steps.end(), d.mixerSteps().begin(), d.mixerSteps().end());
    std::sort(m_steps.begin(), m_steps.end());
    m_steps.erase(std::unique(m_steps.begin(), m_steps.end()), m_steps.end());
    // The set's own automation.
    m_tracks.clear();
    m_trackOf.assign(static_cast<size_t>(m_params.count()), -1);
    std::vector<Gesture> g = set.m_gestures;
    std::stable_sort(g.begin(), g.end(), [](const Gesture& a, const Gesture& b) { return a.m_beat < b.m_beat; });
    for (const Gesture& x : g) {
        if (x.m_param < 0 || x.m_param >= m_params.count()) continue;
        int& t = m_trackOf[static_cast<size_t>(x.m_param)];
        if (t < 0) { t = static_cast<int>(m_tracks.size()); m_tracks.push_back(Track{ x.m_param, {}, 0, 0.0f }); }
        m_tracks[static_cast<size_t>(t)].m_gestures.push_back(x);
    }
    // The cues: every deck's marks and the set's tracks.
    m_cueMarks.clear();
    for (int d = 0; d < Decks; ++d) {
        if (!m_decks[d].loaded()) continue;
        const std::vector<CueMark> m = cueMarksOf(set.m_decks[d], m_params);
        m_cueMarks.insert(m_cueMarks.end(), m.begin(), m.end());
    }
    {
        Score names;
        names.m_markers = set.m_markers;
        const std::vector<CueMark> m = cueMarksOf(names, m_params);
        m_cueMarks.insert(m_cueMarks.end(), m.begin(), m.end());
    }
    std::stable_sort(m_cueMarks.begin(), m_cueMarks.end(), [](const CueMark& a, const CueMark& b) { return a.m_beat < b.m_beat; });
    // The mixer's effects run where a deck sends into them (and always live: the throw).
    m_fxOn = m_live;
    for (int d = 0; d < Decks; ++d)
        for (const Gesture& x : set.m_decks[d].m_gestures)
            if (x.m_param == m_params.id(ModuleE::DECK, d, deck::FX_SEND)) m_fxOn = true;
    m_endSample = static_cast<int64_t>(std::ceil(lengthSeconds() * m_sampleRate));
    seek(0.0);
}

void Engine::seek(double beat)
{
    const double x = std::max(0.0, m_tempo.secondsAt(beat)) * m_sampleRate;
    m_sample = static_cast<int64_t>(std::llround(x));
    for (Deck& d : m_decks) if (d.loaded()) d.seek(m_sample);
    for (bool& p : m_playing) p = false;
    m_stepCursor = 0;
    while (m_stepCursor < m_steps.size() && m_steps[m_stepCursor] < m_sample) ++m_stepCursor;
    for (Track& t : m_tracks) { t.m_cursor = t.m_gestures.size(); t.m_offset = 0.0f; }
    for (auto& deck : m_stemBands) for (auto& stem : deck) for (ThreeBand& b : stem) b.reset();
    for (auto& deck : m_stemFilt) for (auto& stem : deck) for (Svf& f : stem) f.reset();
    for (Channel& c : m_ch) {
        for (ThreeBand& b : c.m_bands) b.reset();
        for (Svf& f : c.m_filt) f.reset();
        for (int k = 0; k < 3; ++k) c.m_g[k] = c.m_target[k] = 1.0f;
        c.m_gFader = c.m_fader = 1.0f;
    }
    m_djEcho.reset();
    m_djHall.reset();
    m_limiter.reset();
    for (Oversampler4& o : m_clipOs) o.reset();
    for (BandLimit& b : m_cutLp) b.reset();
    for (int c = 0; c < 2; ++c) { m_cutHp1[c].reset(); m_cutHp2[c].reset(); }
    m_cutEnv = 0.0f;
    m_sideHp1.reset();
    m_sideHp2.reset();
    for (Svf& f : m_perfFilt) f.reset();
    m_lead = -1;
    m_leadGroup = -1.0;
    m_cellDirty = true;
}

void Engine::showKnobs(int d)
{
    for (int id = 0; id < m_params.count(); ++id) {
        const float b = m_decks[d].baseOf(id);
        if (!(b == b)) continue;
        m_params.set(id, b);
        m_shown[static_cast<size_t>(id)] = m_params.get(id);
    }
    m_lead = d;
    m_leadGroup = m_decks[d].knobGroup();
    m_soundsVersion.fetch_add(1, std::memory_order_relaxed);
}

float Engine::setPlayed(int id) const
{
    const float knob = m_params.get(id);
    if (id < 0 || id >= static_cast<int>(m_trackOf.size()) || m_trackOf[static_cast<size_t>(id)] < 0) return knob;
    const float off = m_tracks[static_cast<size_t>(m_trackOf[static_cast<size_t>(id)])].m_offset;
    return off == 0.0f ? knob : m_params.fromNormalised(id, m_params.toNormalised(id, knob) + off);
}

void Engine::updateCell()
{
    m_cellDirty = false;
    const double beat = m_tempo.beatAt(seconds());
    for (Track& t : m_tracks) {
        const size_t none = t.m_gestures.size();
        size_t c = t.m_cursor, next = c == none ? 0 : c + 1;
        while (next < t.m_gestures.size() && t.m_gestures[next].m_beat <= beat) { c = next; ++next; }
        t.m_cursor = c;
        t.m_offset = c == none ? 0.0f : gestureValue(t.m_gestures[c], beat);
    }
    const float fs = static_cast<float>(m_sampleRate);
    // The channels: each deck reads its own channel as its score moves it.
    for (int d = 0; d < Decks; ++d) {
        Channel& c = m_ch[d];
        if (!m_decks[d].loaded()) continue;
        float v[16];
        m_decks[d].readPlayed(ModuleE::DECK, d, v);
        const auto gain = [](float db) { return db <= -59.9f ? 0.0f : dbToGain(db); };
        c.m_fader = gain(v[deck::FADER]);
        c.m_target[0] = gain(v[deck::LOW]);
        c.m_target[1] = gain(v[deck::MID]);
        c.m_target[2] = gain(v[deck::HIGH]);
        c.m_send = v[deck::FX_SEND];
        c.m_filter = v[deck::FILTER];
        if (std::fabs(c.m_filter) >= 0.01f) {
            // Below 0 a low pass from 20 kHz down to 20 Hz, above 0 a high pass from 20 Hz up to 20 kHz.
            const float f = c.m_filter < 0.0f ? 20000.0f * std::exp2(10.0f * c.m_filter) : 20.0f * std::exp2(10.0f * c.m_filter);
            for (Svf& s : c.m_filt) s.setQ(std::min(f, 0.45f * fs), 0.9f, fs);
        } else {
            for (Svf& s : c.m_filt) s.reset();
            for (auto& stem : m_stemFilt[d]) for (Svf& s : stem) s.reset();
        }
    }
    // The performer (live only): the master filter, the echo throw.
    if (m_live) {
        m_perfFilter = m_params.get(m_params.id(ModuleE::PERFORM, 0, perform::FILTER));
        // The family jam (02.10.2026): the leader's energy as a low pass -- open at full energy, about 3.5 kHz at 0.3 --,
        // gliding over half a second, and only while the knob rests (the performer's hand comes first).
        const float jamTarget = m_jamEnergy >= 0.0f ? (std::clamp(m_jamEnergy, 0.0f, 1.0f) - 1.0f) * 0.35f : 0.0f;
        m_jamFilter += (jamTarget - m_jamFilter) * (1.0f - std::exp(-static_cast<float>(Raster) / (0.5f * fs)));
        if (std::fabs(m_perfFilter) < 0.01f && std::fabs(m_jamFilter) >= 0.01f) m_perfFilter = m_jamFilter;
        m_perfThrow = m_params.get(m_params.id(ModuleE::PERFORM, 0, perform::THROW));
        if (std::fabs(m_perfFilter) >= 0.01f) {
            const float f = m_perfFilter < 0.0f ? 20000.0f * std::exp2(10.0f * m_perfFilter) : 20.0f * std::exp2(10.0f * m_perfFilter);
            for (Svf& s : m_perfFilt) s.setQ(std::min(f, 0.45f * fs), 0.9f, fs);
        } else {
            for (Svf& s : m_perfFilt) s.reset();
        }
    }
    if (m_fxOn) {
        float v[16];
        for (int k = 0; k < djfx::COUNT; ++k) v[k] = setPlayed(m_params.id(ModuleE::DJ_FX, 0, k));
        static const double EchoBeats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
        EchoSettings e;
        e.m_delaySeconds = EchoBeats[std::clamp(static_cast<int>(std::lround(v[djfx::ECHO_TIME])), 0, 5)] * 60.0 / std::max(20.0, m_tempo.bpmAt(beat));
        e.m_feedback = v[djfx::FEEDBACK];
        e.m_toneHz = 5000.0f;
        e.m_lowCutHz = 250.0f;
        m_djEcho.set(e);
        m_djHall.set(1.2f, v[djfx::HALL_DECAY], 0.4f, 0.02f * fs, 300.0f, 8000.0f);
        m_echoReturn = v[djfx::ECHO_RETURN] <= -59.9f ? 0.0f : dbToGain(v[djfx::ECHO_RETURN]);
        m_hallReturn = v[djfx::HALL_RETURN] <= -59.9f ? 0.0f : dbToGain(v[djfx::HALL_RETURN]);
    }
    // The master: the knobs.
    m_masterGain = dbToGain(m_params.get(m_params.id(ModuleE::MASTER, 0, master::LEVEL)));
    m_clipDrive = dbToGain(m_params.get(m_params.id(ModuleE::MASTER, 0, master::CLIP)));
    m_limiter.set(m_params.get(m_params.id(ModuleE::MASTER, 0, master::CEILING)), 60.0f);
    m_cut = false;   // (Totality's vinyl cut profile is not Parhelion's)
    const float monoKnob = m_params.get(m_params.id(ModuleE::MASTER, 0, master::MONO_BELOW));
    const float mono = m_cut ? std::max(monoKnob, 150.0f) : monoKnob;
    m_sideHp1.setK(mono, 1.8477590f, fs);   // fourth-order Butterworth
    m_sideHp2.setK(mono, 0.7653669f, fs);
}

void Engine::mix(float* L, float* R, int n)
{
    PARH_PROF_BEGIN(Mixer);
    for (int i = 0; i < n; ++i) { L[i] = 0.0f; R[i] = 0.0f; m_sendL[static_cast<size_t>(i)] = 0.0f; m_sendR[static_cast<size_t>(i)] = 0.0f; }
    for (int d = 0; d < Decks; ++d) {
        if (!m_playing[d]) continue;
        Channel& c = m_ch[d];
        float* dl = m_deckL[d].data();
        float* dr = m_deckR[d].data();
        for (int i = 0; i < n; ++i) {
            float l = dl[i], r = dr[i];
            if (m_isSet) {
                for (int k = 0; k < 3; ++k) c.m_g[k] += (c.m_target[k] - c.m_g[k]) * m_smooth;
                c.m_gFader += (c.m_fader - c.m_gFader) * m_smooth;
                float lo, mi, hi;
                c.m_bands[0].process(l, lo, mi, hi);
                l = (lo * c.m_g[0] + mi * c.m_g[1] + hi * c.m_g[2]) * c.m_gFader;
                c.m_bands[1].process(r, lo, mi, hi);
                r = (lo * c.m_g[0] + mi * c.m_g[1] + hi * c.m_g[2]) * c.m_gFader;
                if (std::fabs(c.m_filter) >= 0.01f) {
                    float lp, bp, hp;
                    c.m_filt[0].tick(l, lp, bp, hp);
                    l = c.m_filter < 0.0f ? lp : hp;
                    c.m_filt[1].tick(r, lp, bp, hp);
                    r = c.m_filter < 0.0f ? lp : hp;
                }
            }
            L[i] += l;
            R[i] += r;
            if (m_tapL != nullptr) { m_tapL[d][m_tapOffset + i] = l; m_tapR[d][m_tapOffset + i] = r; }
            if (m_fxOn) { m_sendL[static_cast<size_t>(i)] += l * c.m_send; m_sendR[static_cast<size_t>(i)] += r * c.m_send; }
            if (m_stemL != nullptr) { m_chGains[0][i] = c.m_g[0]; m_chGains[1][i] = c.m_g[1]; m_chGains[2][i] = c.m_g[2]; m_chGains[3][i] = c.m_gFader; }
        }
        if (m_stemL == nullptr) continue;
        // The deck's stems through their own copies of its channel (the same gains, the filters' own states).
        const bool filtered = std::fabs(c.m_filter) >= 0.01f;
        for (int s = 0; s < Deck::STEMS; ++s) {
            const float* in[2] = { m_deckStemL[d][s], m_deckStemR[d][s] };
            float* out[2] = { m_stemL[s] + m_tapOffset, m_stemR[s] + m_tapOffset };
            for (int ch = 0; ch < 2; ++ch) {
                ThreeBand& b = m_stemBands[d][s][ch];
                Svf& f = m_stemFilt[d][s][ch];
                f.copyCoefficients(c.m_filt[ch]);
                for (int i = 0; i < n; ++i) {
                    float x = in[ch][i];
                    if (m_isSet) {
                        float lo, mi, hi;
                        b.process(x, lo, mi, hi);
                        x = (lo * m_chGains[0][i] + mi * m_chGains[1][i] + hi * m_chGains[2][i]) * m_chGains[3][i];
                        if (filtered) {
                            float lp, bp, hp;
                            f.tick(x, lp, bp, hp);
                            x = c.m_filter < 0.0f ? lp : hp;
                        }
                    }
                    out[ch][i] += x;
                }
            }
        }
    }
    if (m_live) {
        // The performer's master filter over the decks, then the throw of what is heard into the echo.
        const bool filtered = std::fabs(m_perfFilter) >= 0.01f;
        for (int i = 0; i < n; ++i) {
            if (filtered) {
                float lp, bp, hp;
                m_perfFilt[0].tick(L[i], lp, bp, hp);
                L[i] = m_perfFilter < 0.0f ? lp : hp;
                m_perfFilt[1].tick(R[i], lp, bp, hp);
                R[i] = m_perfFilter < 0.0f ? lp : hp;
            }
            m_sendL[static_cast<size_t>(i)] += L[i] * m_perfThrow;
            m_sendR[static_cast<size_t>(i)] += R[i] * m_perfThrow;
        }
    }
    if (m_fxOn) {
        for (int i = 0; i < n; ++i) { m_fxL[static_cast<size_t>(i)] = 0.0f; m_fxR[static_cast<size_t>(i)] = 0.0f; }
        m_djEcho.process(m_sendL.data(), m_sendR.data(), m_fxL.data(), m_fxR.data(), n);
        for (int i = 0; i < n; ++i) {
            L[i] += m_fxL[static_cast<size_t>(i)] * m_echoReturn;
            R[i] += m_fxR[static_cast<size_t>(i)] * m_echoReturn;
        }
        if (m_stemL != nullptr)
            for (int i = 0; i < n; ++i) {
                m_stemL[STEM_MIX_FX][m_tapOffset + i] = m_fxL[static_cast<size_t>(i)] * m_echoReturn;
                m_stemR[STEM_MIX_FX][m_tapOffset + i] = m_fxR[static_cast<size_t>(i)] * m_echoReturn;
            }
        m_djHall.process(m_sendL.data(), m_sendR.data(), m_fxL.data(), m_fxR.data(), n);
        for (int i = 0; i < n; ++i) {
            L[i] += m_fxL[static_cast<size_t>(i)] * m_hallReturn;
            R[i] += m_fxR[static_cast<size_t>(i)] * m_hallReturn;
        }
        if (m_stemL != nullptr)
            for (int i = 0; i < n; ++i) {
                m_stemL[STEM_MIX_FX][m_tapOffset + i] += m_fxL[static_cast<size_t>(i)] * m_hallReturn;
                m_stemR[STEM_MIX_FX][m_tapOffset + i] += m_fxR[static_cast<size_t>(i)] * m_hallReturn;
            }
    }
    if (m_preL != nullptr)
        for (int i = 0; i < n; ++i) { m_preL[m_tapOffset + i] = L[i]; m_preR[m_tapOffset + i] = R[i]; }
    PARH_PROF_END(MIXER);
    PARH_PROF_BEGIN(MasterTone);
    // The master: the side under Mono Below goes (PLAN 8.2), the level, the vinyl cut, the clipper, the limiter.
    for (int i = 0; i < n; ++i) {
        const float mid = 0.5f * (L[i] + R[i]);
        float side = 0.5f * (L[i] - R[i]), lp, bp, hp;
        m_sideHp1.tick(side, lp, bp, hp);
        m_sideHp2.tick(hp, lp, bp, hp);
        side = hp;
        L[i] = (mid + side) * m_masterGain;
        R[i] = (mid - side) * m_masterGain;
    }
    if (m_cut) {
        // The vinyl cut (Erg. 3): the band over 6 kHz limited dynamically (a cutter head's protection), then 16 kHz.
        for (int i = 0; i < n; ++i) {
            float h[2];
            float* chn[2] = { &L[i], &R[i] };
            float peak = 0.0f;
            for (int c = 0; c < 2; ++c) {
                float lp, bp, hp;
                m_cutHp1[c].tick(*chn[c], lp, bp, hp);
                m_cutHp2[c].tick(hp, lp, bp, h[c]);
                peak = std::max(peak, std::fabs(h[c]));
            }
            m_cutEnv += (peak - m_cutEnv) * (peak > m_cutEnv ? m_cutAtt : m_cutRel);
            const float thr = 0.1f;   // -20 dBFS in the band
            const float gr = m_cutEnv > thr ? std::pow(thr / m_cutEnv, 0.75f) : 1.0f;   // ratio 4
            for (int c = 0; c < 2; ++c) *chn[c] = m_cutLp[c].process(*chn[c] - (1.0f - gr) * h[c]);
        }
    }
    PARH_PROF_END(MASTER_TONE);
    PARH_PROF_BEGIN(Clipper);
    const float gd = m_clipDrive, t = ClipCeiling;
    auto curve = [gd, t](float x) { return t * std::tanh(gd * x / t); };
    for (int i = 0; i < n; ++i) {
        L[i] = m_clipOs[0].process(L[i], curve);
        R[i] = m_clipOs[1].process(R[i], curve);
    }
    PARH_PROF_END(CLIPPER);
    { PARH_PROF(LIMITER); m_limiter.process(L, R, n); }
}

bool Engine::process(float* L, float* R, int n)
{
    DenormalGuard guard;
    int done = 0;
    while (done < n) {
        const bool atRaster = m_sample % Raster == 0;
        bool atStep = false;
        while (m_stepCursor < m_steps.size() && m_steps[m_stepCursor] <= m_sample) { atStep = atStep || m_steps[m_stepCursor] == m_sample; ++m_stepCursor; }
        const bool cell = m_cellDirty || atRaster || atStep;
        for (int d = 0; d < Decks; ++d) {
            const bool plays = m_decks[d].loaded() && m_decks[d].playsAt(m_sample);
            if (plays && (cell || !m_playing[d])) { PARH_PROF(CELLS); m_decks[d].updateCell(m_sample); }
            m_playing[d] = plays;
        }
        // The knobs show the knob settings of the track that began last on deck A or B (the loops' deck C only while
        // they have none): written when that changes. The sound does not wait for it -- every deck plays from its own.
        {
            int lead = -1;
            double at = -1.0;
            for (int d = 0; d < 2; ++d) if (m_decks[d].knobGroup() >= 0.0 && m_decks[d].knobGroup() > at) { at = m_decks[d].knobGroup(); lead = d; }
            if (lead < 0 && m_decks[2].knobGroup() >= 0.0) lead = 2;
            for (Deck& d : m_decks) d.takeNewGroup();
            if (lead >= 0 && (lead != m_lead || m_decks[lead].knobGroup() != m_leadGroup)) showKnobs(lead);
        }
        if (cell) { PARH_PROF(CELLS); updateCell(); }
        { PARH_PROF(EVENTS); for (int d = 0; d < Decks; ++d) if (m_playing[d]) m_decks[d].dispatchUntil(m_sample); }
        // The keys played on a MIDI keyboard, on their samples (queueLive, 01.10.2026).
        while (m_liveCursor < m_liveCount && m_liveQueue[m_liveCursor].m_at <= m_sample) playLive(m_liveQueue[m_liveCursor++]);
        int64_t end = std::min<int64_t>(m_sample + (n - done), (m_sample / Raster + 1) * Raster);
        for (const Deck& d : m_decks) {
            if (!d.loaded()) continue;
            const int64_t e = d.nextEvent(), r = d.nextRestChange(m_sample);
            if (e > m_sample && e < end) end = e;
            if (r < end) end = r;
        }
        if (m_stepCursor < m_steps.size() && m_steps[m_stepCursor] < end) end = m_steps[m_stepCursor];
        if (m_liveCursor < m_liveCount && m_liveQueue[m_liveCursor].m_at < end) end = m_liveQueue[m_liveCursor].m_at;
        const int len = static_cast<int>(end - m_sample);
        if (m_stemL != nullptr)
            for (int s = 0; s < STEMS; ++s)
                for (int i = 0; i < len; ++i) { m_stemL[s][done + i] = 0.0f; m_stemR[s][done + i] = 0.0f; }
        for (int d = 0; d < Decks; ++d)
            if (m_playing[d])
                m_decks[d].render(m_sample, m_deckL[d].data(), m_deckR[d].data(), len, m_stemL != nullptr ? m_deckStemL[d] : nullptr,
                                 m_stemL != nullptr ? m_deckStemR[d] : nullptr);
        if (m_tapL != nullptr)
            for (int d = 0; d < Decks; ++d)
                for (int i = 0; i < len; ++i) { m_tapL[d][done + i] = 0.0f; m_tapR[d][done + i] = 0.0f; }
        m_tapOffset = done;
        mix(L + done, R + done, len);
        done += len;
        m_sample += len;
    }
    while (m_liveCursor < m_liveCount) playLive(m_liveQueue[m_liveCursor++]);   // a key past the block's end: now
    m_liveCount = m_liveCursor = 0;
    return m_sample < m_endSample;
}

int Engine::keyboardTarget(int channel) const
{
    const int part = m_params.getInt(m_params.id(ModuleE::PERFORM, 0, perform::KEYBOARD_PART));
    if (part != perform::keys::BY_CHANNEL) return std::clamp(part, 0, static_cast<int>(perform::keys::BY_CHANNEL) - 1);
    // By channel: 1 kit, 2 bass, 3 303, 4 .. 9 the polyphonic voices, 10 kit (General MIDI's drums), 11 .. 14 the piano,
    // the strings, the choir, the brass.
    static const int KeysOfChannel[14] = { perform::keys::KIT, perform::keys::BASS, perform::keys::ACID, perform::keys::LEAD,
                                      perform::keys::COUNTER, perform::keys::PLUCK, perform::keys::ARP, perform::keys::PAD,
                                      perform::keys::STAB, perform::keys::KIT, perform::keys::PIANO, perform::keys::STRINGS,
                                      perform::keys::CHOIR, perform::keys::BRASS };
    return channel >= 0 && channel < 14 ? KeysOfChannel[channel] : perform::keys::OFF;
}

void Engine::queueLive(int offset, int pitch, int velocity, int channel, bool on, int target)
{
    if (!m_live || pitch < 0 || pitch > 127 || m_liveCount >= LiveQueue) return;
    LiveKey k;
    k.m_at = m_sample + std::max(0, offset);
    k.m_pitch = pitch;
    k.m_velocity = static_cast<float>(std::clamp(velocity, 1, 127)) / 127.0f;
    k.m_target = on ? (target > perform::keys::OFF ? target : keyboardTarget(channel)) : m_liveTarget[pitch] - 1;   // the split names one
    k.m_on = on;
    if (k.m_target <= perform::keys::OFF) return;
    m_liveQueue[m_liveCount++] = k;
}

void Engine::playLive(const LiveKey& k)
{
    if (!k.m_on) { releaseLive(k.m_pitch); return; }
    if (m_liveTarget[k.m_pitch] != 0) releaseLive(k.m_pitch);
    const int d = std::max(0, m_lead.load(std::memory_order_relaxed));
    if (!m_decks[d].loaded()) return;
    m_decks[d].liveNote(m_sample, k.m_target, k.m_pitch, k.m_velocity, true);
    m_liveTarget[k.m_pitch] = static_cast<uint8_t>(k.m_target + 1);
    m_liveDeck[k.m_pitch] = static_cast<uint8_t>(d);
}

void Engine::releaseLive(int pitch)
{
    if (m_liveTarget[pitch] == 0) return;
    m_decks[m_liveDeck[pitch]].liveNote(m_sample, m_liveTarget[pitch] - 1, pitch, 0.0f, false);
    m_liveTarget[pitch] = 0;
}

void Engine::liveAllOff()
{
    m_liveCount = m_liveCursor = 0;
    for (int k = 0; k < 128; ++k) releaseLive(k);
    for (Deck& d : m_decks) d.liveAllOff();
}

} // namespace parh
