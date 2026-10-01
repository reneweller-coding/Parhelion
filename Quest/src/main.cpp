/**
 * @file main.cpp
 * @brief Parhelion for Meta Quest: the whole generator on the headset, played with the hands (PLAN 9, Phase 7).
 *
 * No game engine -- `NativeActivity` + `android_native_app_glue`, EGL, GLES 3, the Khronos OpenXR loader,
 * `XR_EXT_hand_tracking`, Oboe for audio, and the unchanged core from `../Core`. The frame of the app -- the OpenXR
 * session, the swapchains, the point renderer, the font, the hands and the audio stream -- is Totality's Quest app (after
 * Ephemeris' and Phosphene's); the player with its two engines, the performer's "now", the panel and the parhelion in the
 * sky are Parhelion's.
 *
 * **Three threads.**
 *  - *Audio* (Oboe): `TrackPlayer::process` plays the front engine through the handover (Handover.h) and the play/stop
 *    fade. It never locks, never blocks and never allocates. It publishes the position and the level for the panel.
 *  - *Composer*: composes a whole track (or set) and loads it; "next" composes the next one into the second engine and
 *    the audio thread swaps it in behind a fade; "now" composes the track rewritten from its next 8-bar line into the
 *    second engine, pre-rolls it and hands over on a beat (no gap, the notes held go on). Then the loudness, measured
 *    while it plays (Leveler.h).
 *  - *Render* (the glue thread): OpenXR frame loop, hands, gestures, the picture.
 *
 * **The hands are the hands at the mixer.** The engines play live (Engine::setLive):
 *
 * | Gesture | Effect |
 * |---|---|
 * | left pinch | play / stop (a 15 ms fade, the music pauses where it is) |
 * | right pinch | kick out / kick in (perform.mute_kick) |
 * | right pinch held (0.6 s) | "Breakdown now" in a groove or drop, "Drop now" in a breakdown or build: the track rewritten from its next 8-bar line (planTrackRewritten), handed over without a gap -- a track, not a set |
 * | both hands pinched together | the next track (or set): composed from the next seed, swapped in behind a fade |
 * | left hand height | the master filter (perform.filter): low pass below mid height, high pass above, open at the middle |
 * | right hand height | the echo throw (perform.throw), from mid height up |
 *
 * A hand only moves its control while it is *not* pinching, and both are smoothed with a 0.15 s one-pole: a hand at mid
 * height plays exactly what the composer wrote, and nothing ever jumps.
 *
 * The grammar every generator's headset shares (01.10.2026): the desktop's frame (Plugin/Frame.h) reads the same hands
 * from the bridge with the same rules and the same numbers.
 *
 * **The parhelion in the sky** (the logo, Deploy/make_icon.py, at the size of a room): ahead and above, tilted towards the
 * player, the sun with its 22-degree halo and the two sun dogs on it. The sun swells after every kick; the halo is the
 * bar, once round, with every other part's notes as beads on rings about it and the playhead's hand sweeping them; the
 * sun dogs burn with the track's energy (the plan's curve, 0..10); the parhelic circle through all three is the track
 * itself, left to right, its sections marked, the playhead a light moving along it and a performer's "now" a mark
 * ahead. As the Kaleidoscope rules ask: nothing about the camera moves with the audio, and every brightness is a
 * continuous function of the position.
 *
 * **`parh.cfg`** in `<externalDataPath>` (`/sdcard/Android/data/com.reneweller.parhelion.quest/files`):
 * @code
 *   mute=1              start silent (the test rule; the engines still run)
 *   seed=2026           the first track's seed; the next takes the next seed
 *   minutes=7           length of a track
 *   set_minutes=60      a set of so many minutes instead of single tracks
 *   style=Uplifting     Uplifting, Progressive, Dream House, Acid or Deep
 *   osc_host=192.168.1.20   the score cues (Cue.h) to a visualiser such as Kaleidoscope; empty = off
 *   osc_port=9000
 *   bridge_host=192.168.1.20   the hands to the desktop's Parhelion (01.10.2026): played from here, the same gestures
 *   bridge_port=9104     its headset port (the desktop's settings, Headset)
 *   audio=0             no sound here, the desktop plays (the same as mute=1)
 *   quality=desktop     everything (default here: quest, PLAN 10: fewer unison oscillators, voices and players)
 *   knobs=compose.key=D;set.dramaturgy=Sunrise     any knobs, repeatable
 * @endcode
 */

#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <oboe/Oboe.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "parh/Cue.h"
#include "parh/Engine.h"
#include "parh/Handover.h"
#include "parh/Leveler.h"
#include "parh/Presets.h"
#include "parh/compose/Composer.h"
#include "parh/compose/Set.h"
#include "parh/compose/Style.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Parhelion", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Parhelion", __VA_ARGS__)

using namespace parh;

namespace {

// ---------------------------------------------------------------- small math

/** @brief Column-major 4x4 matrix, the layout GL wants. */
struct Mat4 { float m[16]; };
/** @brief A point or direction. */
struct Vec3 { float x, y, z; };

Mat4 identity() { Mat4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }

Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr)
            r.m[c * 4 + rr] = a.m[0 * 4 + rr] * b.m[c * 4 + 0] + a.m[1 * 4 + rr] * b.m[c * 4 + 1]
                            + a.m[2 * 4 + rr] * b.m[c * 4 + 2] + a.m[3 * 4 + rr] * b.m[c * 4 + 3];
    return r;
}

/** @brief Asymmetric projection from OpenXR's four half-angles. */
Mat4 projectionFromFov(const XrFovf& fov, float nearZ, float farZ)
{
    const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
    const float u = std::tan(fov.angleUp), d = std::tan(fov.angleDown);
    const float w = r - l, h = u - d;
    Mat4 p{};
    p.m[0] = 2.0f / w;  p.m[8] = (r + l) / w;
    p.m[5] = 2.0f / h;  p.m[9] = (u + d) / h;
    p.m[10] = -(farZ + nearZ) / (farZ - nearZ);
    p.m[11] = -1.0f;
    p.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    return p;
}

Mat4 rotationFromQuat(const XrQuaternionf& q)
{
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 r = identity();
    r.m[0] = 1 - 2 * (y * y + z * z); r.m[4] = 2 * (x * y - z * w);     r.m[8] = 2 * (x * z + y * w);
    r.m[1] = 2 * (x * y + z * w);     r.m[5] = 1 - 2 * (x * x + z * z); r.m[9] = 2 * (y * z - x * w);
    r.m[2] = 2 * (x * z - y * w);     r.m[6] = 2 * (y * z + x * w);     r.m[10] = 1 - 2 * (x * x + y * y);
    return r;
}

Vec3 rotate(const XrQuaternionf& q, Vec3 v)
{
    const Mat4 r = rotationFromQuat(q);
    return { r.m[0] * v.x + r.m[4] * v.y + r.m[8] * v.z,
             r.m[1] * v.x + r.m[5] * v.y + r.m[9] * v.z,
             r.m[2] * v.x + r.m[6] * v.y + r.m[10] * v.z };
}

/** @brief The view matrix of an eye pose: the inverse of the pose. */
Mat4 viewFromPose(const XrPosef& pose)
{
    const Mat4 r = rotationFromQuat(pose.orientation);
    Mat4 rt = identity();
    for (int c = 0; c < 3; ++c) for (int rr = 0; rr < 3; ++rr) rt.m[c * 4 + rr] = r.m[rr * 4 + c];
    Mat4 t = identity();
    t.m[12] = -pose.position.x; t.m[13] = -pose.position.y; t.m[14] = -pose.position.z;
    return multiply(rt, t);
}

float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// ---------------------------------------------------------------- 5x7 point font

/**
 * @brief One glyph as seven rows of five bits (bit 4 = left column), or the blank for the unknown.
 *
 * Text on the panel is drawn as points, like everything else: no texture, no atlas, one draw call
 * for the whole picture. (Table taken from Noctuary's Quest app, where it was measured legible at
 * arm's length; extended by '%', '#' and '*'.)
 */
const unsigned char* glyph(char c)
{
    static const unsigned char kFont[][7] = {
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // A B C
        {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // D E F
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // G H I
        {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}, {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // J K L
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // M N O
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // P Q R
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // S T U
        {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // V W X
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},                                        // Y Z
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 0 1 2
        {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 3 4 5
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 6 7 8
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},                                                                              // 9
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // space - .
        {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}, {0x01,0x02,0x04,0x08,0x10,0x00,0x00}, {0x02,0x04,0x08,0x04,0x02,0x00,0x00}, // : / <
        {0x08,0x04,0x02,0x04,0x08,0x00,0x00}, {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // > + =
        {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, {0x0A,0x1F,0x0A,0x0A,0x0A,0x1F,0x0A}, {0x00,0x15,0x0E,0x1F,0x0E,0x15,0x00}, // % # *
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00},                                                                              // unknown
    };
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return kFont[c - 'A'];
    if (c >= '0' && c <= '9') return kFont[26 + (c - '0')];
    switch (c) {
    case ' ': return kFont[36]; case '-': return kFont[37]; case '.': return kFont[38];
    case ':': return kFont[39]; case '/': return kFont[40]; case '<': return kFont[41];
    case '>': return kFont[42]; case '+': return kFont[43]; case '=': return kFont[44];
    case '%': return kFont[45]; case '#': return kFont[46]; case '*': return kFont[47];
    default: break;
    }
    return kFont[48];
}



// ---------------------------------------------------------------- config

/** @brief What `parh.cfg` can say. */
struct Config {
    bool mute = false;              ///< start silent (test rule); the engines still run
    uint64_t seed = 1;              ///< the first track's seed
    double minutes = 0.0;           ///< length of a track (0: compose.minutes)
    double setMinutes = 0.0;        ///< a set of so many minutes instead of single tracks
    std::string oscHost;            ///< cue target (Cue.h), empty = off
    int oscPort = 9000;             ///< cue port
    std::string bridgeHost;         ///< the bridge (01.10.2026): the hands to the desktop's Parhelion there; empty = off
    int bridgePort = 9104;          ///< its headset port (the desktop's settings, Headset)
    std::string knobs;              ///< knob assignments, "key=value" separated by ';' or newlines
    bool quest = true;              ///< the Quest's quality (Engine::Quality); `quality=desktop` plays all
};

/** @brief Reads `<dir>/parh.cfg`; every key is optional. */
Config readConfig(const char* dir)
{
    Config c;
    if (dir == nullptr) return c;
    const std::string path = std::string(dir) + "/parh.cfg";
    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) { LOGI("no config at %s", path.c_str()); return c; }
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = s.substr(0, eq), v = s.substr(eq + 1);
        if (k == "mute") c.mute = v != "0";
        else if (k == "seed") c.seed = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "minutes") c.minutes = std::max(3.0, std::atof(v.c_str()));
        else if (k == "set_minutes") c.setMinutes = std::clamp(std::atof(v.c_str()), 0.0, 240.0);
        else if (k == "osc_host") c.oscHost = v;
        else if (k == "osc_port") c.oscPort = std::atoi(v.c_str());
        else if (k == "bridge_host") c.bridgeHost = v;
        else if (k == "bridge_port") c.bridgePort = std::atoi(v.c_str());
        else if (k == "audio") { if (v == "0") c.mute = true; }   // no sound here: the desktop plays (the bridge)
        else if (k == "quality") c.quest = v != "desktop";
        else if (k == "style") { c.knobs += "compose.style=" + v; c.knobs += ";"; }
        else if (k == "knobs") { c.knobs += v; c.knobs += ";"; }
        else LOGE("parh.cfg: unknown key %s", k.c_str());
    }
    std::fclose(f);
    LOGI("config: mute %d, seed %llu, set %.0f min", c.mute ? 1 : 0, static_cast<unsigned long long>(c.seed), c.setMinutes);
    return c;
}

// ---------------------------------------------------------------- the track player

/** @brief What plays, for the panel: a set, or a track as deck A of a one-track set, with where its tracks lie. */
struct Now {
    SetScore set;
    bool isSet = false;
    uint64_t seed = 1;                   ///< the track's or the set's seed
    std::vector<Rewrite> rewrites;       ///< the performer's "now"s the track was rewritten by, in order
    std::vector<TrackInfo> tracks;
    std::vector<double> starts, swaps;   ///< set beats of each track's first bar and of its swap
    std::vector<int> decks;
    /** @brief The track that owns the low end at @p beat. */
    int trackAt(double beat) const
    {
        int t = 0;
        for (size_t i = 0; i < tracks.size(); ++i) if (beat >= (isSet && i > 0 ? swaps[i] : starts[i])) t = static_cast<int>(i);
        return t;
    }
};

/** @brief Where a performer's "now" stands, for the panel. */
enum class NowState : int { None = 0, Pending, TooLate, NotInSet, NoRoom };

/**
 * @brief Two engines and the composer, with a play/stop fade, a swap to the next track or set, and the performer's "now".
 *
 * **Two engines.** The audio thread plays the front one; the composer thread only ever loads the other, the back one,
 * and only while the audio thread has no use for it -- no handover armed or fading (Handover.h) and no swap pending. A
 * new track or set goes into the back engine and the audio thread swaps it in behind the play fade (the old one fades
 * out, the new one starts at its beginning); a rewritten track ("now") goes into the back engine, is pre-rolled there and
 * handed over on a beat, on the same timeline, without a gap. `front_` says which engine is the front; only the audio
 * thread changes it, and the composer waits for the change before it touches the other again. The knobs the hands play
 * are written into both engines, so whichever plays has them.
 *
 * **What the panel reads.** Never an engine: their scores and tempo maps change when a track is loaded. The audio thread
 * publishes beat, seconds and level as atomics after every block, and the composer publishes what plays as an
 * immutable shared copy under a mutex the render thread takes for a pointer copy.
 */
class TrackPlayer {
public:
    /** @brief Composes the first track and loads it. Composer thread, before the audio stream starts. */
    void prepare(int sampleRate, int block, const Config& cfg)
    {
        muted_ = cfg.mute;
        seed_ = cfg.seed;
        setMinutes_ = cfg.setMinutes;
        for (auto& e : engines_) e = std::make_unique<Engine>();
        ParamStore& p = engines_[0]->params();
        if (!cfg.knobs.empty()) {
            std::string err;
            if (!p.parseText(cfg.knobs, &err)) LOGE("parh.cfg knobs: %s", err.c_str());
        }
        if (cfg.minutes > 0.0) p.set(p.id(Module::Compose, 0, compose::Minutes), static_cast<float>(cfg.minutes));
        engines_[1]->params().copyValuesFrom(p);
        for (auto& e : engines_) {
            e->setLive(true);   // the perform module acts, the mixer is in a track's path (Engine.h)
            e->setQuality(cfg.quest ? Engine::Quality::Quest : Engine::Quality::Desktop);
            e->prepare(sampleRate, block);
        }
        handover_.prepare(block);
        sr_ = static_cast<double>(sampleRate);
        gainCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.015 * sampleRate)));   // 15 ms
        levelCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.3 * sampleRate)));    // 300 ms
        auto now = std::make_shared<Now>(composeFor(seed_));
        load(*engines_[0], *now);
        publish(now);
        levelDue_ = true;   // measured once the stream runs (pump)
        ready_.store(true, std::memory_order_release);
        LOGI("ready: seed %llu, %.0f s", static_cast<unsigned long long>(seed_), engines_[0]->lengthSeconds());
    }

    /** @brief Renders one block. Audio thread only. */
    void process(float* L, float* R, int n)
    {
        if (!ready_.load(std::memory_order_acquire)) {
            std::fill(L, L + n, 0.0f);
            std::fill(R, R + n, 0.0f);
            return;
        }
        int f = front_.load(std::memory_order_relaxed);
        // A new track or set waits in the back engine: the one that plays fades out, then the new one is the front.
        const bool swapping = swapReady_.load(std::memory_order_acquire);
        if (swapping && gain_ < 1.0e-4f) {
            f ^= 1;
            front_.store(f, std::memory_order_release);
            gen_.fetch_add(1, std::memory_order_acq_rel);
            swapReady_.store(false, std::memory_order_release);
            tap_.reset();
            gain_ = 0.0f;
        }
        Engine& front = *engines_[f];
        Engine& back = *engines_[f ^ 1];
        if (trimsReady_.load(std::memory_order_acquire)) {   // the loudness corrections, measured while it plays
            if (trimsGen_ == gen_.load(std::memory_order_relaxed))
                for (int d = 0; d < kDecks; ++d) {
                    if (!trimsIn_[d].empty()) front.setLevelTrims(d, trimsIn_[d]);
                    if (!balIn_[d].empty()) front.setLevelBalance(d, balIn_[d]);
                }
            trimsReady_.store(false, std::memory_order_release);
        }
        const float target = playing_.load(std::memory_order_relaxed) && !swapReady_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        if (target <= 0.0f && gain_ < 1.0e-4f) {
            // Faded out: the music waits where it is instead of running on silently.
            gain_ = 0.0f;
            std::fill(L, L + n, 0.0f);
            std::fill(R, R + n, 0.0f);
            handover_.follow(front);   // (the front may just have changed: "now" asks where it is)
            return;
        }
        const double from = front.beat();
        Engine* now = &front;
        if (handover_.process(front, back, L, R, n)) {
            // The rewritten track plays from here: the back engine is the front.
            front_.store(f ^ 1, std::memory_order_release);
            gen_.fetch_add(1, std::memory_order_acq_rel);
            tap_.reset();
            now = &back;
        }
        if (cues_ != nullptr && cues_->running()) {
            // The cues of this block (Cue.h), stamped with the moment the block is heard.
            const double blockSeconds = static_cast<double>(n) / sr_, to = now->beat();
            const float bpm = static_cast<float>(std::max(0.0, to - from) / blockSeconds * 60.0);
            tap_.scan(now->cueMarks(), from, to, bpm, CueSender::nowNanos(), static_cast<int64_t>(2.0 * blockSeconds * 1.0e9),
                      static_cast<int64_t>(blockSeconds * 1.0e9), cues_->ring());
        }
        const float out = muted_ ? 0.0f : 1.0f;
        float level = level_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) {
            gain_ += (target - gain_) * gainCoef_;   // one pole: no step, no click
            level += (0.5f * (L[i] * L[i] + R[i] * R[i]) * gain_ * gain_ - level) * levelCoef_;
            L[i] *= gain_ * out;
            R[i] *= gain_ * out;
        }
        level_.store(level, std::memory_order_relaxed);
        beat_.store(now->beat(), std::memory_order_relaxed);
        seconds_.store(now->seconds(), std::memory_order_relaxed);
        if (now->seconds() > now->lengthSeconds() + 8.0) nextRequest_.store(true, std::memory_order_relaxed);
    }

    /** @brief Binds the cue sender (Cue.h); null leaves the cues off. Call before the audio stream starts. */
    void setCueSender(CueSender* sender) { cues_ = sender; }
    /** @brief Play or stop (any thread; takes effect over the fade). */
    void setPlaying(bool on) { playing_.store(on, std::memory_order_relaxed); }
    /** @brief Whether play is on. */
    bool playing() const { return playing_.load(std::memory_order_relaxed); }
    /** @brief Asks for the next track or set (any thread). */
    void requestNext() { nextRequest_.store(true, std::memory_order_relaxed); }
    /** @brief Asks for "Breakdown now" or "Drop now" (any thread). */
    void requestNow(SectionKind kind) { nowRequest_.store(static_cast<int>(kind), std::memory_order_relaxed); }
    /** @brief One round of composer work: a next track, a "now", else the loudness of what plays. Composer thread. */
    void pump()
    {
        if (nextRequest_.exchange(false, std::memory_order_relaxed)) next();
        else if (const int k = nowRequest_.exchange(-1, std::memory_order_relaxed); k >= 0) rewrite(static_cast<SectionKind>(k));
        else if (levelDue_) { levelDue_ = false; level(); }
    }
    /** @brief Breaks off the composer's work (the app closes). */
    void quit() { quit_.store(true, std::memory_order_relaxed); }

    /** @brief Where the audio thread is, in beats and seconds (render thread). */
    double beat() const { return beat_.load(std::memory_order_relaxed); }
    double seconds() const { return seconds_.load(std::memory_order_relaxed); }   ///< @copydoc beat
    /** @brief The output's mean square, smoothed over 300 ms, as dBFS (render thread). */
    float levelDb() const { const float l = level_.load(std::memory_order_relaxed); return l > 1.0e-10f ? 10.0f * std::log10(l) : -100.0f; }
    /** @brief What plays (render thread): an immutable copy. */
    std::shared_ptr<const Now> now() const { std::lock_guard<std::mutex> lock(nowMutex_); return now_; }
    /** @brief Which track or set plays, from 1. */
    int number() const { return number_.load(std::memory_order_relaxed); }
    /** @brief The knobs of the engine that plays (read only; the ids are the same in both). */
    const ParamStore& params() const { return engines_[front_.load(std::memory_order_acquire)]->params(); }
    /** @brief Sets a knob in both engines (any thread): the hands' controls stay with whichever plays. */
    void setKnob(int id, float value) { for (auto& e : engines_) e->params().set(id, value); }
    /** @brief Whether prepare() has finished. */
    bool ready() const { return ready_.load(std::memory_order_acquire); }
    /** @brief Whether the output is muted by the config. */
    bool muted() const { return muted_; }
    /** @brief The performer's "now": its state, the bar it begins at and what it is (render thread). */
    NowState nowState() const { return static_cast<NowState>(nowState_.load(std::memory_order_relaxed)); }
    int nowBar() const { return nowBar_.load(std::memory_order_relaxed); }                                              ///< @copydoc nowState
    SectionKind nowKind() const { return static_cast<SectionKind>(nowKind_.load(std::memory_order_relaxed)); }          ///< @copydoc nowState
    /** @brief Seconds on the steady clock when the state last changed (the panel lets a message stand a while). */
    double nowSince() const { return nowSince_.load(std::memory_order_relaxed); }

private:
    /** @brief The rewrite line at least this far ahead: time for the composer and the pre-roll on the headset's cores. */
    static constexpr double kNowAhead = 10.0;
    /** @brief The pre-roll: the second engine starts this many bars before where the first one plays. */
    static constexpr int kPrerollBars = 4;

    static double clockSeconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void setNow(NowState s, int bar, SectionKind kind)
    {
        nowBar_.store(bar, std::memory_order_relaxed);
        nowKind_.store(static_cast<int>(kind), std::memory_order_relaxed);
        nowState_.store(static_cast<int>(s), std::memory_order_relaxed);
        nowSince_.store(clockSeconds(), std::memory_order_relaxed);
    }
    /** @brief Waits until the audio thread has made engine @p f the front (after a swap or a handover). */
    void waitFront(int f)
    {
        while (front_.load(std::memory_order_acquire) != f && !quit_.load(std::memory_order_relaxed))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    /** @brief Composes the track or set after this one into the back engine; the audio thread swaps it in. */
    void next()
    {
        const int n = number_.load(std::memory_order_relaxed) + 1;
        const uint64_t seed = seed_ + static_cast<uint64_t>(n - 1);
        auto now = std::make_shared<Now>(composeFor(seed));
        const int back = front_.load(std::memory_order_acquire) ^ 1;
        load(*engines_[back], *now);   // allocates: here, in the engine the audio thread does not touch
        swapReady_.store(true, std::memory_order_release);
        waitFront(back);
        number_.store(n, std::memory_order_relaxed);
        publish(now);
        setNow(NowState::None, -1, SectionKind::Breakdown);
        levelDue_ = true;
        LOGI("%s %d: seed %llu", now->isSet ? "set" : "track", n, static_cast<unsigned long long>(seed));
    }

    /**
     * @brief "Breakdown now" or "Drop now" (the plugin's performNow, PluginProcessor.h): the track that plays, rewritten
     *        from its next 8-bar line -- the one after it where that is under kNowAhead away -- into a breakdown (its
     *        build, a drop) or a drop at once, in the back engine, and handed over on a beat before the two tracks play
     *        a note differently. Its level corrections go with it until the Leveler has measured the new one. A set is
     *        not rewritten (its next blend would have to move with it).
     */
    void rewrite(SectionKind kind)
    {
        const std::shared_ptr<const Now> cur = now();
        if (!cur) return;
        if (cur->isSet) { setNow(NowState::NotInSet, -1, kind); return; }
        const Score& old = cur->set.decks[0];
        const double beat = old.tempo.beatAt(static_cast<double>(handover_.playing()) / sr_);
        const double bpm = old.tempo.bpmAt(beat);
        int bar = (static_cast<int>(std::floor(beat / 4.0)) / 8 + 1) * 8;
        if ((4.0 * bar - beat) * 60.0 / std::max(60.0, bpm) < kNowAhead) bar += 8;
        if (4.0 * bar + 4.0 * 32 > old.lengthBeats) { setNow(NowState::NoRoom, bar, kind); return; }
        setNow(NowState::Pending, bar, kind);
        TrackRequest req;
        req.rewriteBar = bar;
        req.rewriteKind = kind;
        req.earlier = cur->rewrites;   // a second "now" keeps the first
        TrackInfo info;
        Score re = composeTrack(params(), cur->seed, req, nullptr, std::string(), &info);
        if (!old.levels.empty() && !re.levels.empty()) {
            re.levels[0].trimDb = old.levels[0].trimDb;
            re.levels[0].balDb = old.levels[0].balDb;
            re.levels[0].breakDb = old.levels[0].breakDb;
            re.levels[0].buildDb = old.levels[0].buildDb;
        }
        const int back = front_.load(std::memory_order_acquire) ^ 1;
        Engine& e = *engines_[back];
        e.load(re);
        const double seekBeat = std::max(0.0, 4.0 * (std::floor(beat / 4.0) - kPrerollBars));
        e.seek(seekBeat);
        const double differ = std::min(firstDifference(old, re, seekBeat), 4.0 * bar);
        const int64_t deadline = static_cast<int64_t>(std::llround(old.tempo.secondsAt(differ) * sr_)) - Handover::kFade;
        const auto onBeat = [&old, this](int64_t s) {
            const double b = std::ceil(old.tempo.beatAt(static_cast<double>(s) / sr_) - 1e-9);
            return static_cast<int64_t>(std::llround(old.tempo.secondsAt(b) * sr_));
        };
        // A next track, a newer "now" (this one then gives way to it) or the app closing break it off.
        const auto stop = [this] {
            return nextRequest_.load(std::memory_order_relaxed) || nowRequest_.load(std::memory_order_relaxed) >= 0 || quit_.load(std::memory_order_relaxed);
        };
        if (!handover_.handOver(e, deadline, stop, onBeat)) {
            if (!stop()) setNow(NowState::TooLate, bar, kind);
            LOGI("now at bar %d: not handed over", bar);
            return;
        }
        waitFront(back);
        auto n = std::make_shared<Now>(*cur);
        n->set.decks[0] = std::move(re);
        n->set.lengthBeats = n->set.decks[0].lengthBeats;
        n->tracks[0] = info;
        n->rewrites.push_back({ bar, kind });
        publish(n);
        levelDue_ = true;
        LOGI("now: %s from bar %d", kSectionNames[static_cast<int>(kind)], bar);
    }

    /**
     * @brief The loudness of what plays (Leveler.h), measured while it plays -- on the headset's cores a good many
     *        seconds: the corrections go to the audio thread, which glides them in (Engine::setLevelTrims) if the engine
     *        they were measured for still plays. A request for the next track or a "now" breaks it off.
     */
    void level()
    {
        const std::shared_ptr<const Now> playing = now();
        if (!playing) return;
        const uint32_t gen = gen_.load(std::memory_order_acquire);
        SetScore s = playing->set;
        auto stop = [this] {
            return nextRequest_.load(std::memory_order_relaxed) || nowRequest_.load(std::memory_order_relaxed) >= 0 || quit_.load(std::memory_order_relaxed);
        };
        const bool done = playing->isSet ? !levelSet(s, params(), 20.0, stop).empty() : !levelScore(s.decks[0], params(), 20.0, stop).empty();
        if (!done || trimsReady_.load(std::memory_order_acquire)) return;
        for (int d = 0; d < kDecks; ++d) {
            trimsIn_[d].clear();
            balIn_[d].clear();
            for (const LevelMark& m : s.decks[d].levels) { trimsIn_[d].push_back(m.trimDb); balIn_[d].push_back(m.balDb); }
        }
        trimsGen_ = gen;
        trimsReady_.store(true, std::memory_order_release);
        LOGI("%d: loudness corrected", number());
    }

    void publish(std::shared_ptr<const Now> s) { std::lock_guard<std::mutex> lock(nowMutex_); now_ = std::move(s); }

    static void load(Engine& e, const Now& now)
    {
        if (now.isSet) e.loadSet(now.set);
        else e.load(now.set.decks[0]);
    }

    /** @brief A track, or with set_minutes a set. Composer thread. */
    Now composeFor(uint64_t seed)
    {
        const ParamStore& p = params();
        Now out;
        out.seed = seed;
        if (setMinutes_ > 0.0) {
            SetInfo info;
            out.isSet = true;
            out.set = composeSet(p, seed, setMinutes_, nullptr, &info);
            for (const SetTrack& t : info.tracks) {
                out.tracks.push_back(t.info);
                out.starts.push_back(t.start);
                out.swaps.push_back(t.swapIn);
                out.decks.push_back(t.deck);
            }
        } else {
            TrackInfo info;
            out.set.decks[0] = composeTrack(p, seed, TrackRequest{}, nullptr, std::string(), &info);
            out.set.lengthBeats = out.set.decks[0].lengthBeats;
            out.tracks.push_back(info);
            out.starts.push_back(0.0);
            out.swaps.push_back(0.0);
            out.decks.push_back(0);
        }
        return out;
    }

    std::unique_ptr<Engine> engines_[2];
    std::atomic<int> front_{ 0 };                ///< which engine plays (the audio thread changes it)
    std::atomic<uint32_t> gen_{ 0 };             ///< counts the fronts: a correction measured for an older one is dropped
    Handover handover_;
    uint64_t seed_ = 1;
    double setMinutes_ = 0.0;
    std::atomic<bool> ready_{ false }, playing_{ false }, nextRequest_{ false }, swapReady_{ false }, quit_{ false };
    std::atomic<int> nowRequest_{ -1 };          ///< a SectionKind asked for, -1 none
    std::atomic<int> nowState_{ 0 }, nowBar_{ -1 }, nowKind_{ 0 };
    std::atomic<double> nowSince_{ 0.0 };
    bool levelDue_ = false;                      ///< what plays is still to be measured (composer thread)
    std::vector<float> trimsIn_[kDecks];         ///< its corrections, for the audio thread once trimsReady_ says so
    std::vector<BalanceDb> balIn_[kDecks];       ///< the parts' corrections found with them
    uint32_t trimsGen_ = 0;                      ///< the front they were measured for
    std::atomic<bool> trimsReady_{ false };
    std::atomic<int> number_{ 1 };
    std::atomic<double> beat_{ 0.0 }, seconds_{ 0.0 };
    std::atomic<float> level_{ 0.0f };
    bool muted_ = false;
    double sr_ = 48000.0;
    CueSender* cues_ = nullptr;   ///< the cue bridge, owned by the app; null = off
    CueTap tap_;                  ///< audio thread: beat range -> cues
    float gain_ = 0.0f, gainCoef_ = 0.002f, levelCoef_ = 0.0001f;
    mutable std::mutex nowMutex_;
    std::shared_ptr<const Now> now_;
};

// ---------------------------------------------------------------- audio

/** @brief Oboe output stream: a low-latency float stream straight into TrackPlayer::process. */
class Audio : public oboe::AudioStreamDataCallback {
public:
    explicit Audio(TrackPlayer& p) : player_(p) {}

    bool open()
    {
        oboe::AudioStreamBuilder b;
        b.setDirection(oboe::Direction::Output)
         ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
         ->setSharingMode(oboe::SharingMode::Exclusive)
         ->setFormat(oboe::AudioFormat::Float)
         ->setChannelCount(2)
         ->setSampleRate(48000)
         ->setDataCallback(this);
        if (b.openStream(stream_) != oboe::Result::OK) { LOGE("Oboe: cannot open stream"); return false; }
        sampleRate_ = stream_->getSampleRate();
        burst_ = stream_->getFramesPerBurst();
        stream_->setBufferSizeInFrames(burst_ * 2);
        bufL_.assign(4096, 0.0f);
        bufR_.assign(4096, 0.0f);
        LOGI("Oboe: %d Hz, burst %d", sampleRate_, burst_);
        return true;
    }
    bool start() { return stream_ && stream_->requestStart() == oboe::Result::OK; }
    void stop() { if (stream_) { stream_->requestStop(); stream_->close(); stream_.reset(); } }
    int sampleRate() const { return sampleRate_; }
    int burst() const { return burst_; }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream*, void* data, int32_t frames) override
    {
        float* out = static_cast<float*>(data);
        int done = 0;
        while (done < frames) {
            const int n = std::min(frames - done, static_cast<int32_t>(bufL_.size()));
            player_.process(bufL_.data(), bufR_.data(), n);
            for (int i = 0; i < n; ++i) {
                out[(done + i) * 2] = bufL_[static_cast<size_t>(i)];
                out[(done + i) * 2 + 1] = bufR_[static_cast<size_t>(i)];
            }
            done += n;
        }
        return oboe::DataCallbackResult::Continue;
    }

private:
    TrackPlayer& player_;
    std::shared_ptr<oboe::AudioStream> stream_;
    std::vector<float> bufL_, bufR_;
    int sampleRate_ = 48000, burst_ = 256;
};

// ---------------------------------------------------------------- GL scene

const char* kVertexShader = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
layout(location = 2) in float aSize;
uniform mat4 uVP;
out vec4 vCol;
void main() {
    vec4 p = uVP * vec4(aPos, 1.0);
    gl_Position = p;
    gl_PointSize = aSize / max(p.w, 0.2);
    vCol = aCol;
})";

const char* kFragmentShader = R"(#version 300 es
precision mediump float;
in vec4 vCol;
out vec4 o;
void main() {
    vec2 d = gl_PointCoord - vec2(0.5);
    float r = length(d) * 2.0;
    float a = smoothstep(1.0, 0.15, r);
    o = vec4(vCol.rgb * a * vCol.a, 1.0);
})";

/** @brief One soft round point: the only primitive the scene has. */
struct Point { float x, y, z; float r, g, b, a; float size; };

/** @brief A head-locked plane to lay text out on: origin plus a right and an up axis. */
struct Panel { Vec3 origin, right, up; };

GLuint compile(GLenum type, const char* src)
{
    const GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); LOGE("shader: %s", log); }
    return s;
}

/**
 * @brief Everything visible, as additive points.
 *
 * The Kaleidoscope rules apply (docs/PLAN.md 8.2): nothing about the camera moves with the audio,
 * and every brightness is a continuous function of the bar position -- no step, no flash.
 */
class Scene {
public:
    /** @brief Display pixels per radian on the Quest 2's render target (about 1830 px over 90 deg). */
    static constexpr float kPixelsPerRadian = 1150.0f;

    bool init()
    {
        program_ = glCreateProgram();
        glAttachShader(program_, compile(GL_VERTEX_SHADER, kVertexShader));
        glAttachShader(program_, compile(GL_FRAGMENT_SHADER, kFragmentShader));
        glLinkProgram(program_);
        GLint ok = 0;
        glGetProgramiv(program_, GL_LINK_STATUS, &ok);
        if (!ok) { LOGE("program link failed"); return false; }
        uVP_ = glGetUniformLocation(program_, "uVP");
        glGenBuffers(1, &vbo_);
        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Point), reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Point), reinterpret_cast<void*>(12));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Point), reinterpret_cast<void*>(28));
        glBindVertexArray(0);
        return true;
    }

    void begin() { points_.clear(); }

    /** @brief One point in world space. */
    void add(float x, float y, float z, float r, float g, float b, float a, float size)
    {
        points_.push_back({ x, y, z, r, g, b, a, size });
    }

    /** @brief One point on a panel, at panel coordinates @p u (right) and @p v (up). */
    void addOn(const Panel& p, float u, float v, float r, float g, float b, float a, float size)
    {
        add(p.origin.x + p.right.x * u + p.up.x * v,
            p.origin.y + p.right.y * u + p.up.y * v,
            p.origin.z + p.right.z * u + p.up.z * v, r, g, b, a, size);
    }

    /**
     * @brief Text on a panel, one point per lit pixel of the 5x7 font; @p cell is the pixel pitch.
     *
     * The point size is `cell * kPixelsPerRadian / w`: a cell of `cell` metres at `w` metres
     * subtends `cell / w` radians, and the Quest 2 renders about 1150 pixels per radian (roughly
     * 1830 pixels over 90 degrees per eye), so the dots of a glyph just touch at any distance.
     */
    void addText(const Panel& p, float u, float v, float cell, const char* text,
                 float r, float g, float b, float a)
    {
        for (const char* c = text; *c; ++c) {
            const unsigned char* gl = glyph(*c);
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (gl[row] & (0x10 >> col))
                        addOn(p, u + static_cast<float>(col) * cell, v - static_cast<float>(row) * cell,
                              r, g, b, a, cell * kPixelsPerRadian);
            u += 6.0f * cell;
        }
    }

    void upload()
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(points_.size() * sizeof(Point)), points_.data(), GL_DYNAMIC_DRAW);
    }

    void draw(const Mat4& vp, int width, int height)
    {
        glViewport(0, 0, width, height);
        glClearColor(0.008f, 0.008f, 0.010f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);            // additive: points add light, they never occlude
        glUseProgram(program_);
        glUniformMatrix4fv(uVP_, 1, GL_FALSE, vp.m);
        glBindVertexArray(vao_);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(points_.size()));
        glBindVertexArray(0);
    }

private:
    GLuint program_ = 0, vbo_ = 0, vao_ = 0;
    GLint uVP_ = -1;
    std::vector<Point> points_;
};

// ---------------------------------------------------------------- hands

/**
 * @brief Hand state and the mapping to the two macros and the two buttons.
 *
 * Height is measured against the head, not the floor: `(palm.y - (head.y - 1.0)) / 0.8`, so the
 * mapping is the same whether the runtime gave us a STAGE space (floor at y = 0) or a LOCAL one
 * (origin wherever the session started), and it is the same for a tall and a short player.
 *
 * The pinch is the thumb tip to index tip distance with a Schmitt trigger (closed under 22 mm, open
 * over 38 mm), so a hand held near the threshold does not rattle between the two states.
 */
class Hands {
public:
    /** @brief One hand's measurement of this frame. */
    struct Hand {
        bool valid = false;
        XrVector3f palm{};
        float pinch = 0.0f;     ///< 0 open .. 1 closed
        bool closed = false;    ///< after the Schmitt trigger
        float height = 0.5f;    ///< 0..1 against the head
        float macro = 0.5f;     ///< the smoothed value the macro follows
        bool everSeen = false;  ///< this hand has been tracked at least once
    };

    /** @brief Feeds one hand; @p headY is the head's height in the same space. */
    void setHand(int h, const XrVector3f& palm, float thumbToIndex, float headY)
    {
        Hand& s = hand_[h];
        s.valid = true;
        s.everSeen = true;
        s.palm = palm;
        s.pinch = clamp01(1.0f - (thumbToIndex - 0.015f) / 0.035f);
        if (thumbToIndex < 0.022f) s.closed = true;
        else if (thumbToIndex > 0.038f) s.closed = false;
        s.height = clamp01((palm.y - (headY - 1.0f)) / 0.8f);
    }
    /** @brief Marks a hand as not tracked this frame (its macro then holds its value). */
    void lost(int h) { hand_[h].valid = false; hand_[h].closed = false; }

    /**
     * @brief Advances the smoothing and reports the two rising pinch edges.
     * @param dt         seconds since the last frame
     * @param leftPinch  receives true on the frame the left hand closes
     * @param rightPinch receives true on the frame the right hand closes
     *
     * A macro follows its hand only while that hand is open: the pinch that starts a track must not
     * also drag the gain with it.
     */
    void update(double dt, bool& leftPinch, bool& rightPinch)
    {
        // 0.15 s one pole. dt is capped at 0.1 s so that a long frame -- the first one after the
        // session resumes, say -- cannot make the coefficient 1 and snap the macro to the hand.
        const float k = 1.0f - std::exp(-static_cast<float>(std::min(dt, 0.1)) / 0.15f);
        for (int h = 0; h < 2; ++h) {
            Hand& s = hand_[h];
            if (s.valid && !s.closed) s.macro += (s.height - s.macro) * k;
        }
        leftPinch = hand_[0].closed && !wasClosed_[0];
        rightPinch = hand_[1].closed && !wasClosed_[1];
        wasClosed_[0] = hand_[0].closed;
        wasClosed_[1] = hand_[1].closed;
    }

    const Hand& hand(int h) const { return hand_[h]; }

private:
    Hand hand_[2];
    bool wasClosed_[2] = { false, false };
};

// ---------------------------------------------------------------- the bridge

/**
 * @brief The bridge mode (01.10.2026): the hands to the desktop's Parhelion over OSC (UDP), which then plays as if they were
 *        its own -- the same gestures, read by its frame (Plugin/Frame.h, Headset) with the same rules as here.
 *
 * "/hands" with six floats: left height, right height, left pinch, right pinch, left tracked, right tracked (the heights
 * 0..1 against the head and unsmoothed, the desktop smooths them; the others 0 or 1), about 30 times a second. Fire and
 * forget: a desktop that is not there changes nothing. After Noctuary's bridge (AmbientSynth's Quest/src/main.cpp, OscOut).
 */
class HandBridge {
public:
    HandBridge() = default;
    HandBridge(const HandBridge&) = delete;
    HandBridge& operator=(const HandBridge&) = delete;
    ~HandBridge() { close(); }
    /** @brief Opens the UDP socket to @p host (a dotted IPv4 address, no names) and @p port; false if it cannot. */
    bool open(const std::string& host, int port)
    {
        close();
        sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ < 0) return false;
        std::memset(&to_, 0, sizeof(to_));
        to_.sin_family = AF_INET;
        to_.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, host.c_str(), &to_.sin_addr) != 1) { close(); return false; }
        return true;
    }
    /** @brief Closes the socket; send() then does nothing. */
    void close() { if (sock_ >= 0) ::close(sock_); sock_ = -1; }
    /** @brief Whether the bridge is on. */
    bool ok() const { return sock_ >= 0; }
    /** @brief Sends the hands, at most 30 times a second; @p dt the seconds since the last frame (render thread). */
    void send(const Hands& hands, double dt)
    {
        if (sock_ < 0) return;
        since_ += dt;
        if (since_ < 1.0 / 30.0) return;
        since_ = 0.0;
        float a[6];
        for (int h = 0; h < 2; ++h) {
            const Hands::Hand& s = hands.hand(h);
            a[h] = s.height;
            a[2 + h] = s.valid && s.closed ? 1.0f : 0.0f;
            a[4 + h] = s.valid ? 1.0f : 0.0f;
        }
        char buf[64];
        size_t pos = 0;
        auto putStr = [&](const char* t) {
            const size_t l = std::strlen(t) + 1;
            std::memcpy(buf + pos, t, l);
            pos += l;
            while (pos & 3) buf[pos++] = 0;
        };
        putStr("/hands");
        putStr(",ffffff");
        for (const float f : a) {   // big-endian 32-bit words
            uint32_t u;
            std::memcpy(&u, &f, 4);
            buf[pos++] = static_cast<char>(u >> 24);
            buf[pos++] = static_cast<char>(u >> 16);
            buf[pos++] = static_cast<char>(u >> 8);
            buf[pos++] = static_cast<char>(u);
        }
        ::sendto(sock_, buf, pos, 0, reinterpret_cast<const sockaddr*>(&to_), sizeof(to_));
    }

private:
    int sock_ = -1;        ///< the UDP socket, or -1 while closed
    sockaddr_in to_{};     ///< the desktop's address and port
    double since_ = 0.0;   ///< seconds since the last datagram
};

// ---------------------------------------------------------------- the app

/** @brief One eye's swapchain and the framebuffer it is rendered through. */
struct SwapchainTarget {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    int width = 0, height = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    GLuint fbo = 0, depth = 0;
};


class App {
public:
    explicit App(android_app* app) : app_(app) {}

    bool init()
    {
        dataDir_ = app_->activity->externalDataPath ? app_->activity->externalDataPath : "";
        config_ = readConfig(dataDir_.c_str());
        // The cue bridge, off unless parh.cfg names a host: a visualiser that is not there changes nothing.
        if (!config_.oscHost.empty()) {
            if (cues_.start(config_.oscHost, config_.oscPort)) {
                LOGI("cues: OSC to %s:%d", config_.oscHost.c_str(), config_.oscPort);
                player_.setCueSender(&cues_);
            } else LOGE("cues: cannot reach %s:%d", config_.oscHost.c_str(), config_.oscPort);
        }
        // The bridge (01.10.2026): the hands to the desktop's Parhelion, off unless parh.cfg names a host.
        if (!config_.bridgeHost.empty()) {
            if (bridge_.open(config_.bridgeHost, config_.bridgePort))
                LOGI("bridge: the hands to %s:%d", config_.bridgeHost.c_str(), config_.bridgePort);
            else LOGE("bridge: bad host %s (a dotted IPv4 address)", config_.bridgeHost.c_str());
        }
        if (!initLoader()) return false;
        if (!initInstance()) return false;
        if (!initEgl()) return false;
        if (!initSession()) return false;
        if (!initHands()) LOGE("hand tracking unavailable: the hands will not play");
        if (!scene_.init()) return false;
        // The stream is opened first so the engines are prepared for the rate the device really gives.
        if (!audio_.open()) return false;
        composerThread_ = std::thread([this] { composerLoop(); });
        return true;
    }

    void run()
    {
        while (!app_->destroyRequested) {
            int events;
            android_poll_source* source;
            const int timeout = (sessionRunning_ || app_->window == nullptr) ? 0 : -1;
            while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
                if (source) source->process(app_, source);
                if (app_->destroyRequested) break;
            }
            pollXrEvents();
            if (quit_) break;
            if (sessionRunning_) frame();
        }
    }

    void shutdown()
    {
        stopComposer_.store(true);
        player_.quit();
        if (composerThread_.joinable()) composerThread_.join();
        audio_.stop();
        cues_.stop();   // after the stream: the sender's thread reads the ring the audio thread fills
        for (SwapchainTarget& t : targets_) if (t.swapchain != XR_NULL_HANDLE) xrDestroySwapchain(t.swapchain);
        for (int h = 0; h < 2; ++h)
            if (handTracker_[h] != XR_NULL_HANDLE && pfnDestroyHandTracker_) pfnDestroyHandTracker_(handTracker_[h]);
        if (viewSpace_ != XR_NULL_HANDLE) xrDestroySpace(viewSpace_);
        if (stageSpace_ != XR_NULL_HANDLE) xrDestroySpace(stageSpace_);
        if (session_ != XR_NULL_HANDLE) xrDestroySession(session_);
        if (instance_ != XR_NULL_HANDLE) xrDestroyInstance(instance_);
    }

private:
    // ------------------------------------------------ the composer thread

    /**
     * @brief Composes and loads the first track (or set), starts the stream, then serves "next", "now" and the loudness.
     *
     * A track is composed whole before it plays (Composer.h): a few seconds on the headset's small cores, during which
     * the panel says COMPOSING and the audio stream is not yet started.
     */
    void composerLoop()
    {
        player_.prepare(audio_.sampleRate(), audio_.burst() * 2, config_);
        const ParamStore& p = player_.params();
        filterId_ = p.id(Module::Perform, 0, perform::Filter);
        throwId_ = p.id(Module::Perform, 0, perform::Throw);
        muteKickId_ = p.id(Module::Perform, 0, perform::MuteKick);
        if (!audio_.start()) LOGE("Oboe: cannot start");
        else LOGI("audio started%s", config_.mute ? " (muted)" : "");
        while (!stopComposer_.load()) {
            player_.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    // ------------------------------------------------ OpenXR setup

    bool initLoader()
    {
        PFN_xrInitializeLoaderKHR initLoader = nullptr;
        xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", reinterpret_cast<PFN_xrVoidFunction*>(&initLoader));
        if (initLoader == nullptr) { LOGE("no xrInitializeLoaderKHR"); return false; }
        XrLoaderInitInfoAndroidKHR li{ XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
        li.applicationVM = app_->activity->vm;
        li.applicationContext = app_->activity->clazz;
        return XR_SUCCEEDED(initLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&li)));
    }

    bool initInstance()
    {
        const char* exts[] = { XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
                               XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
                               XR_EXT_HAND_TRACKING_EXTENSION_NAME };
        XrInstanceCreateInfoAndroidKHR android{ XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
        android.applicationVM = app_->activity->vm;
        android.applicationActivity = app_->activity->clazz;
        XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO, &android };
        std::strncpy(ci.applicationInfo.applicationName, "Parhelion", XR_MAX_APPLICATION_NAME_SIZE - 1);
        ci.applicationInfo.applicationVersion = 1;
        std::strncpy(ci.applicationInfo.engineName, "ParhelionCore", XR_MAX_ENGINE_NAME_SIZE - 1);
        ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        ci.enabledExtensionCount = 3;
        ci.enabledExtensionNames = exts;
        if (XR_FAILED(xrCreateInstance(&ci, &instance_))) { LOGE("xrCreateInstance failed"); return false; }

        XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
        sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (XR_FAILED(xrGetSystem(instance_, &sgi, &system_))) { LOGE("xrGetSystem failed"); return false; }
        XrSystemHandTrackingPropertiesEXT ht{ XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
        XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES, &ht };
        xrGetSystemProperties(instance_, system_, &sp);
        handsSupported_ = ht.supportsHandTracking == XR_TRUE;
        LOGI("system: %s, hand tracking %d", sp.systemName, handsSupported_ ? 1 : 0);
        return true;
    }

    bool initEgl()
    {
        PFN_xrGetOpenGLESGraphicsRequirementsKHR getReq = nullptr;
        xrGetInstanceProcAddr(instance_, "xrGetOpenGLESGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&getReq));
        XrGraphicsRequirementsOpenGLESKHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
        if (getReq == nullptr || XR_FAILED(getReq(instance_, system_, &req))) { LOGE("GLES requirements failed"); return false; }

        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0;
        if (!eglInitialize(display_, &major, &minor)) { LOGE("eglInitialize failed"); return false; }
        const EGLint attribs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                   EGL_DEPTH_SIZE, 0, EGL_NONE };
        EGLint count = 0;
        if (!eglChooseConfig(display_, attribs, &eglConfig_, 1, &count) || count == 0) { LOGE("eglChooseConfig failed"); return false; }
        const EGLint pbuf[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
        surface_ = eglCreatePbufferSurface(display_, eglConfig_, pbuf);
        const EGLint ctx[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        context_ = eglCreateContext(display_, eglConfig_, EGL_NO_CONTEXT, ctx);
        if (context_ == EGL_NO_CONTEXT || !eglMakeCurrent(display_, surface_, surface_, context_)) { LOGE("EGL context failed"); return false; }
        LOGI("EGL %d.%d, GL %s", major, minor, glGetString(GL_VERSION));
        return true;
    }

    bool initSession()
    {
        XrGraphicsBindingOpenGLESAndroidKHR gb{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
        gb.display = display_; gb.config = eglConfig_; gb.context = context_;
        XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO, &gb };
        sci.systemId = system_;
        if (XR_FAILED(xrCreateSession(instance_, &sci, &session_))) { LOGE("xrCreateSession failed"); return false; }

        XrReferenceSpaceCreateInfo rs{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        rs.poseInReferenceSpace.orientation.w = 1.0f;
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        if (XR_FAILED(xrCreateReferenceSpace(session_, &rs, &stageSpace_))) {
            rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            if (XR_FAILED(xrCreateReferenceSpace(session_, &rs, &stageSpace_))) { LOGE("no reference space"); return false; }
        }
        rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        xrCreateReferenceSpace(session_, &rs, &viewSpace_);

        uint32_t viewCount = 0;
        xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
        std::vector<XrViewConfigurationView> cfg(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
        xrEnumerateViewConfigurationViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, cfg.data());
        views_.assign(viewCount, { XR_TYPE_VIEW });

        uint32_t fmtCount = 0;
        xrEnumerateSwapchainFormats(session_, 0, &fmtCount, nullptr);
        std::vector<int64_t> formats(fmtCount);
        xrEnumerateSwapchainFormats(session_, fmtCount, &fmtCount, formats.data());
        int64_t format = formats.empty() ? GL_RGBA8 : formats[0];
        for (int64_t f : formats) if (f == GL_SRGB8_ALPHA8) { format = f; break; }

        targets_.resize(viewCount);
        for (uint32_t v = 0; v < viewCount; ++v) {
            SwapchainTarget& t = targets_[v];
            XrSwapchainCreateInfo sc{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
            sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
            sc.format = format;
            sc.sampleCount = 1;
            sc.width = cfg[v].recommendedImageRectWidth;
            sc.height = cfg[v].recommendedImageRectHeight;
            sc.faceCount = 1; sc.arraySize = 1; sc.mipCount = 1;
            if (XR_FAILED(xrCreateSwapchain(session_, &sc, &t.swapchain))) { LOGE("xrCreateSwapchain failed"); return false; }
            t.width = static_cast<int>(sc.width);
            t.height = static_cast<int>(sc.height);
            uint32_t imgCount = 0;
            xrEnumerateSwapchainImages(t.swapchain, 0, &imgCount, nullptr);
            t.images.assign(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
            xrEnumerateSwapchainImages(t.swapchain, imgCount, &imgCount, reinterpret_cast<XrSwapchainImageBaseHeader*>(t.images.data()));
            glGenFramebuffers(1, &t.fbo);
            glGenRenderbuffers(1, &t.depth);
            glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, t.width, t.height);
            LOGI("view %u: %dx%d, %u images", v, t.width, t.height, imgCount);
        }
        return true;
    }

    bool initHands()
    {
        if (!handsSupported_) return false;
        xrGetInstanceProcAddr(instance_, "xrCreateHandTrackerEXT", reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateHandTracker_));
        xrGetInstanceProcAddr(instance_, "xrLocateHandJointsEXT", reinterpret_cast<PFN_xrVoidFunction*>(&pfnLocateHandJoints_));
        xrGetInstanceProcAddr(instance_, "xrDestroyHandTrackerEXT", reinterpret_cast<PFN_xrVoidFunction*>(&pfnDestroyHandTracker_));
        if (!pfnCreateHandTracker_ || !pfnLocateHandJoints_) return false;
        for (int h = 0; h < 2; ++h) {
            XrHandTrackerCreateInfoEXT hci{ XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
            hci.hand = (h == 0) ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
            hci.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
            if (XR_FAILED(pfnCreateHandTracker_(session_, &hci, &handTracker_[h]))) return false;
        }
        return true;
    }

    void pollXrEvents()
    {
        XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
        while (xrPollEvent(instance_, &ev) == XR_SUCCESS) {
            if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                const auto* sc = reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev);
                sessionState_ = sc->state;
                switch (sessionState_) {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                    bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if (XR_SUCCEEDED(xrBeginSession(session_, &bi))) sessionRunning_ = true;
                    break;
                }
                case XR_SESSION_STATE_STOPPING:
                    xrEndSession(session_);
                    sessionRunning_ = false;
                    break;
                case XR_SESSION_STATE_EXITING:
                case XR_SESSION_STATE_LOSS_PENDING:
                    quit_ = true;
                    break;
                default: break;
                }
            } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
                quit_ = true;
            }
            ev = { XR_TYPE_EVENT_DATA_BUFFER };
        }
    }

    // ------------------------------------------------ per frame

    void updateHead(XrTime time)
    {
        headValid_ = false;
        if (viewSpace_ == XR_NULL_HANDLE) return;
        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        if (XR_FAILED(xrLocateSpace(viewSpace_, stageSpace_, time, &loc))) return;
        const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if ((loc.locationFlags & need) != need) return;
        headPose_ = loc.pose;
        headValid_ = true;
    }

    void updateHands(XrTime time)
    {
        for (int h = 0; h < 2; ++h) {
            if (handTracker_[h] == XR_NULL_HANDLE) { hands_.lost(h); continue; }
            XrHandJointLocationEXT joints[XR_HAND_JOINT_COUNT_EXT];
            XrHandJointLocationsEXT locs{ XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
            locs.jointCount = XR_HAND_JOINT_COUNT_EXT;
            locs.jointLocations = joints;
            XrHandJointsLocateInfoEXT li{ XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
            li.baseSpace = stageSpace_;
            li.time = time;
            if (XR_FAILED(pfnLocateHandJoints_(handTracker_[h], &li, &locs)) || !locs.isActive) { hands_.lost(h); continue; }
            const XrHandJointLocationEXT& palm = joints[XR_HAND_JOINT_PALM_EXT];
            const XrHandJointLocationEXT& thumb = joints[XR_HAND_JOINT_THUMB_TIP_EXT];
            const XrHandJointLocationEXT& index = joints[XR_HAND_JOINT_INDEX_TIP_EXT];
            const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            if ((palm.locationFlags & need) != need) { hands_.lost(h); continue; }
            const float dx = thumb.pose.position.x - index.pose.position.x;
            const float dy = thumb.pose.position.y - index.pose.position.y;
            const float dz = thumb.pose.position.z - index.pose.position.z;
            hands_.setHand(h, palm.pose.position, std::sqrt(dx * dx + dy * dy + dz * dz),
                           headValid_ ? headPose_.position.y : 1.6f);
        }
    }

    /**
     * @brief Turns the hands' heights into the perform controls (Params.h, perform), in both engines.
     *
     * Left height is the master filter over its whole range: mid height open (the composed sound), lower a low pass,
     * higher a high pass, as a DJ mixer's filter knob. Right height is the echo throw: nothing below mid height, all of
     * it with the hand raised.
     */
    void applyMacros()
    {
        if (!player_.ready() || filterId_ < 0) return;
        // A hand that has never been tracked writes nothing: without hand tracking the controls keep what parh.cfg set.
        const Hands::Hand& left = hands_.hand(0);
        const Hands::Hand& right = hands_.hand(1);
        if (left.everSeen) {
            // A dead zone round the middle, so a resting hand leaves the filter open.
            const float x = 2.0f * (left.macro - 0.5f);
            const float y = std::fabs(x) < 0.1f ? 0.0f : (x - (x > 0.0f ? 0.1f : -0.1f)) / 0.9f;
            player_.setKnob(filterId_, std::clamp(y, -1.0f, 1.0f));
        }
        if (right.everSeen) player_.setKnob(throwId_, clamp01(2.0f * (right.macro - 0.5f)));
    }

    /**
     * @brief Geometry of the panel.
     *
     * A glyph is 7 cells high and a character 6 cells wide, so at kCell and kPanelDistance a line of 20 characters
     * spans 0.50 m -- about 29 degrees -- and a glyph stands 1.7 degrees tall, roughly print at reading distance. Every
     * line drawn below stays inside 20 characters. These are design values (Totality's) and untuned: nobody has put
     * the headset on yet.
     */
    static constexpr float kCell = 0.0042f;         ///< pitch of one font pixel, metres
    static constexpr float kRow = 0.040f;           ///< distance between text rows, metres
    static constexpr float kPanelDistance = 1.0f;   ///< how far in front of the eyes the panel sits
    static constexpr int kLineChars = 20;           ///< the longest line
    static constexpr double kLongPinch = 0.6;       ///< seconds the right pinch is held for "now"

    /** @brief The head-locked panel: in front of the eyes, following the yaw only. */
    Panel headPanel() const
    {
        Panel p;
        const Vec3 fwd = rotate(headPose_.orientation, { 0.0f, 0.0f, -1.0f });
        const float len = std::fmax(std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z), 1.0e-3f);
        const Vec3 f{ fwd.x / len, 0.0f, fwd.z / len };
        p.right = { -f.z, 0.0f, f.x };
        p.up = { 0.0f, 1.0f, 0.0f };
        // Shifted half a 16-character line to the left (the lines are left aligned and 13 to 20 characters long), so
        // the block of text sits roughly centred in front of the eyes.
        const float half = 0.5f * 16.0f * 6.0f * kCell;
        p.origin = { headPose_.position.x + f.x * kPanelDistance - p.right.x * half,
                     headPose_.position.y + 0.12f,
                     headPose_.position.z + f.z * kPanelDistance - p.right.z * half };
        return p;
    }

    /** @brief The section of @p s at @p beat, or null. */
    static const Section* sectionAt(const Score& s, double beat)
    {
        const Section* out = nullptr;
        for (const Section& x : s.sections) { if (x.beat > beat) break; out = &x; }
        return out;
    }

    /** @brief The plan's energy at @p beat, 0..1: the section's, from its start to its end (a continuous curve). */
    static float energyAt(const Score& s, double beat)
    {
        const Section* x = sectionAt(s, beat);
        if (x == nullptr) return 0.0f;
        const float t = static_cast<float>(std::clamp((beat - x->beat) / std::max(1.0, x->length), 0.0, 1.0));
        return clamp01((x->energyFrom + (x->energyTo - x->energyFrom) * t) / 10.0f);
    }

    /** @brief One part's onsets in a bar: position in the bar 0..1 and velocity. */
    struct Beads { int part = 0; std::vector<std::pair<float, float>> onsets; };

    /** @brief The onsets of bar @p bar of @p s, per part (the kick apart, the ghost left out), in part order. */
    static void gatherBar(const Score& s, int bar, std::vector<Beads>& rings, std::vector<float>& kick)
    {
        rings.clear();
        kick.clear();
        if (bar < 0) return;
        const double b0 = 4.0 * bar, b1 = b0 + 4.0;
        auto it = std::lower_bound(s.notes.begin(), s.notes.end(), b0 - 0.1, [](const NoteEvent& n, double b) { return n.beat < b; });
        int index[kNumParts];
        for (int& i : index) i = -1;
        for (; it != s.notes.end() && it->beat < b1 - 0.1; ++it) {
            if (it->velocity <= 0.0f || it->beat < b0) continue;
            const float pos = static_cast<float>(std::clamp((it->beat - b0) / 4.0, 0.0, 0.9999));
            if (it->part == Part::Kick) { kick.push_back(pos); continue; }
            if (it->part == Part::Ghost) continue;
            const int p = static_cast<int>(it->part);
            if (p < 0 || p >= kNumParts) continue;
            if (index[p] < 0) { index[p] = static_cast<int>(rings.size()); rings.push_back({ p, {} }); }
            rings[static_cast<size_t>(index[p])].onsets.push_back({ pos, it->velocity });
        }
        std::sort(rings.begin(), rings.end(), [](const Beads& a, const Beads& b) { return a.part < b.part; });
    }

    /** @brief A part's colour in the sky: the drums the sun's white, the low end blue, the voices the halo's colours. */
    static void partColour(int part, float& r, float& g, float& b)
    {
        switch (static_cast<Part>(part)) {
        case Part::Sub: case Part::Bass: r = 0.40f; g = 0.52f; b = 0.86f; return;
        case Part::Acid: r = 0.95f; g = 0.42f; b = 0.28f; return;
        case Part::Lead: case Part::Counter: r = 1.00f; g = 0.80f; b = 0.50f; return;
        case Part::Pluck: case Part::Arp: r = 0.55f; g = 0.85f; b = 0.95f; return;
        case Part::Pad: r = 0.72f; g = 0.60f; b = 0.95f; return;
        case Part::Stab: r = 1.00f; g = 0.62f; b = 0.36f; return;
        case Part::Piano: r = 0.88f; g = 0.92f; b = 1.00f; return;
        case Part::Strings: case Part::Choir: case Part::Brass: case Part::Timpani: r = 0.66f; g = 0.86f; b = 0.66f; return;
        case Part::Fx: r = 0.60f; g = 0.60f; b = 0.66f; return;
        default: r = 1.00f; g = 0.94f; b = 0.84f; return;
        }
    }

    /** @brief A section's colour on the parhelic circle: the drops warm, the breakdowns blue, the rest dim. */
    static void sectionColour(SectionKind k, float& r, float& g, float& b)
    {
        switch (k) {
        case SectionKind::Drop: r = 1.00f; g = 0.78f; b = 0.45f; return;
        case SectionKind::Build: r = 1.00f; g = 0.55f; b = 0.35f; return;
        case SectionKind::Breakdown: case SectionKind::Break: r = 0.50f; g = 0.62f; b = 1.00f; return;
        case SectionKind::Groove: r = 0.85f; g = 0.72f; b = 0.55f; return;
        default: r = 0.55f; g = 0.58f; b = 0.70f; return;
        }
    }

    /** @brief A glow of points: a disc of radius @p r at (@p u, @p v) on @p p, brighter to the middle. */
    void addGlow(const Panel& p, float u, float v, float r, float cr, float cg, float cb, float a, float size)
    {
        for (int ring = 0; ring < 4; ++ring) {
            const float rr = r * static_cast<float>(ring) / 3.0f;
            const int dots = ring == 0 ? 1 : 6 * ring;
            for (int i = 0; i < dots; ++i) {
                const float t = 6.2831853f * (static_cast<float>(i) + 0.5f * static_cast<float>(ring)) / static_cast<float>(dots);
                scene_.addOn(p, u + rr * std::cos(t), v + rr * std::sin(t), cr, cg, cb, a * (1.0f - 0.2f * static_cast<float>(ring)), size);
            }
        }
    }

    /**
     * @brief The logo (Deploy/make_icon.py) in points of light: the sun, the halo round it, the two sun dogs with their
     *        tails outwards and the parhelic circle through all three; centred at (@p u, @p v), @p size across.
     */
    void addLogo(const Panel& p, float u, float v, float size)
    {
        const float R = 0.3f * size;
        for (int i = 0; i < 48; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / 48.0f;
            scene_.addOn(p, u + R * std::cos(a), v + R * std::sin(a), 1.00f, 0.86f, 0.70f, 0.55f, 14.0f);
        }
        for (int i = -14; i <= 14; ++i) {
            const float x = 1.45f * R * static_cast<float>(i) / 14.0f;
            scene_.addOn(p, u + x, v, 1.00f, 0.96f, 0.90f, 0.18f, 10.0f);
        }
        for (int side = -1; side <= 1; side += 2) {
            addGlow(p, u + static_cast<float>(side) * R, v, 0.05f * size, 1.00f, 0.80f, 0.55f, 0.8f, 16.0f);
            for (int k = 1; k <= 6; ++k)
                scene_.addOn(p, u + static_cast<float>(side) * (R + 0.02f * size * static_cast<float>(k)), v, 1.00f, 0.92f, 0.82f,
                             0.5f * (1.0f - static_cast<float>(k) / 7.0f), 12.0f);
        }
        addGlow(p, u, v, 0.09f * size, 1.00f, 0.84f, 0.60f, 0.9f, 22.0f);
        scene_.addOn(p, u, v, 1.00f, 0.99f, 0.95f, 1.0f, 30.0f);
    }

    /**
     * @brief The parhelion in the sky (the file comment): ahead of the player and above, tilted towards them, anchored
     *        where the head was when the session began -- it never moves with the head or the audio.
     */
    void addSky(const Now& now, double beat)
    {
        if (!skyAnchored_) return;
        const int t = now.trackAt(beat);
        const Score& s = now.set.decks[now.decks.empty() ? 0 : now.decks[static_cast<size_t>(t)]];
        const int bar = static_cast<int>(std::floor(beat / 4.0));
        const float phase = static_cast<float>(beat / 4.0 - std::floor(beat / 4.0));
        // The bar that plays and the one before, crossfaded over the first eighth of the bar: no bead pops in.
        std::vector<Beads> rings[2];
        std::vector<float> kicks[2];
        gatherBar(s, bar, rings[0], kicks[0]);
        gatherBar(s, bar - 1, rings[1], kicks[1]);
        const float fadeIn = std::min(1.0f, phase / 0.125f);
        const float weight[2] = { 0.5f - 0.5f * std::cos(3.14159265f * fadeIn), 0.5f + 0.5f * std::cos(3.14159265f * fadeIn) };
        const Panel& e = sky_;
        const float R = 0.9f, sun = 0.14f, inner = 0.50f, outer = 1.30f;
        // The sun swells after every kick of the bar: a decaying raised cosine of the distance to it, continuous.
        float swell = 0.0f;
        for (float k : kicks[0]) {
            const float d = phase - k;
            if (d >= 0.0f && d < 0.2f) swell = std::max(swell, 0.5f + 0.5f * std::cos(3.14159265f * d / 0.2f));
        }
        addGlow(e, 0.0f, 0.0f, sun * (1.0f + 0.25f * swell), 1.00f, 0.86f, 0.62f, 0.55f + 0.35f * swell, 70.0f);
        addGlow(e, 0.0f, 0.0f, 0.45f * sun, 1.00f, 0.98f, 0.92f, 0.9f, 60.0f);
        // The halo: reddish inside, warm, bluish-white outside (the ice crystals' refraction).
        const float haloCol[3][3] = { { 1.00f, 0.58f, 0.35f }, { 1.00f, 0.86f, 0.66f }, { 0.82f, 0.90f, 1.00f } };
        for (int k = 0; k < 3; ++k) {
            const float r = R + 0.018f * static_cast<float>(k - 1);
            for (int i = 0; i < 160; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / 160.0f;
                scene_.addOn(e, r * std::cos(a), r * std::sin(a), haloCol[k][0], haloCol[k][1], haloCol[k][2], 0.20f, 30.0f);
            }
        }
        const auto angleOf = [](float pos) { return 1.5707963f - 6.2831853f * pos; };   // bar start at the top, clockwise
        // The parts' rings about the halo and their beads.
        for (int w = 0; w < 2; ++w) {
            const int n = static_cast<int>(rings[w].size());
            for (int i = 0; i < n; ++i) {
                const Beads& r = rings[w][static_cast<size_t>(i)];
                const float rad = inner + (outer - inner) * (n <= 1 ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1));
                float cr, cg, cb;
                partColour(r.part, cr, cg, cb);
                if (w == 0)
                    for (int d = 0; d < 72; ++d) {
                        const float a = 6.2831853f * static_cast<float>(d) / 72.0f;
                        scene_.addOn(e, rad * std::cos(a), rad * std::sin(a), cr, cg, cb, 0.05f, 16.0f);
                    }
                for (const auto& o : r.onsets) {
                    const float a = angleOf(o.first);
                    const float d = w == 0 ? phase - o.first : 1.0f;
                    const float lit = d >= 0.0f && d < 0.1f ? 0.5f + 0.5f * std::cos(3.14159265f * d / 0.1f) : 0.0f;
                    scene_.addOn(e, rad * std::cos(a), rad * std::sin(a), cr, cg, cb, weight[w] * (0.30f + 0.3f * o.second + 0.35f * lit),
                                 50.0f + 40.0f * o.second + 60.0f * lit);
                }
            }
        }
        // The playhead's hand, from the sun's rim outwards.
        const float ha = angleOf(phase);
        for (int i = 0; i < 24; ++i) {
            const float r = sun * 1.6f + (outer - sun * 1.6f) * static_cast<float>(i) / 23.0f;
            scene_.addOn(e, r * std::cos(ha), r * std::sin(ha), 1.00f, 0.62f, 0.40f, 0.30f, 24.0f);
        }
        // The sun dogs on the halo, burning with the plan's energy; their tails point outwards.
        const float energy = energyAt(s, beat);
        for (int side = -1; side <= 1; side += 2) {
            const float x = static_cast<float>(side) * R;
            addGlow(e, x, 0.0f, 0.05f + 0.06f * energy, 1.00f, 0.72f, 0.45f, 0.25f + 0.65f * energy, 60.0f);
            addGlow(e, x, 0.0f, 0.02f, 1.00f, 0.97f, 0.90f, 0.3f + 0.7f * energy, 44.0f);
            const int tail = 4 + static_cast<int>(16.0f * energy);
            for (int k = 1; k <= tail; ++k) {
                const float d = 0.018f * static_cast<float>(k);
                scene_.addOn(e, x + static_cast<float>(side) * (0.03f + d), 0.0f, 1.00f, 0.93f, 0.85f,
                             (0.2f + 0.5f * energy) * (1.0f - static_cast<float>(k) / static_cast<float>(tail + 1)), 34.0f);
            }
        }
        addTimeline(now, s, t, beat);
    }

    /**
     * @brief The parhelic circle through the sun and the sun dogs: the track from left to right (a set's current track),
     *        its sections as ticks in their colours, the part played brighter, the playhead a light on it, and a
     *        performer's "now" a mark at its bar.
     */
    void addTimeline(const Now& now, const Score& s, int t, double beat)
    {
        const Panel& e = sky_;
        const float x0 = -1.75f, x1 = 1.75f;
        const double from = now.starts[static_cast<size_t>(t)];
        const double to = static_cast<size_t>(t) + 1 < now.tracks.size() ? now.starts[static_cast<size_t>(t) + 1]
                                                                         : std::max(from + 4.0, s.lengthBeats);
        const auto xOf = [&](double b) { return x0 + (x1 - x0) * static_cast<float>(std::clamp((b - from) / (to - from), 0.0, 1.0)); };
        const float xp = xOf(beat);
        for (int i = 0; i <= 175; ++i) {
            const float x = x0 + (x1 - x0) * static_cast<float>(i) / 175.0f;
            if (std::fabs(x) < 0.2f) continue;   // (the sun)
            scene_.addOn(e, x, 0.0f, 1.00f, 0.95f, 0.88f, x <= xp ? 0.16f : 0.07f, 16.0f);
        }
        for (const Section& x : s.sections) {
            if (x.beat < from - 1e-6 || x.beat >= to) continue;
            float cr, cg, cb;
            sectionColour(x.kind, cr, cg, cb);
            const float xs = xOf(x.beat);
            for (int k = -2; k <= 2; ++k) scene_.addOn(e, xs, 0.012f * static_cast<float>(k), cr, cg, cb, 0.35f, 18.0f);
        }
        addGlow(e, xp, 0.0f, 0.018f, 1.00f, 0.95f, 0.85f, 0.9f, 30.0f);
        if (player_.nowState() == NowState::Pending && player_.nowBar() >= 0) {
            float cr, cg, cb;
            sectionColour(player_.nowKind(), cr, cg, cb);
            // It breathes with the bar's phase: a continuous function of the position, no flash.
            const float br = 0.5f + 0.4f * std::cos(6.2831853f * static_cast<float>(beat / 4.0 - std::floor(beat / 4.0)));
            const float xn = xOf(4.0 * player_.nowBar());
            for (int k = -4; k <= 4; ++k) scene_.addOn(e, xn, 0.014f * static_cast<float>(k), cr, cg, cb, br, 24.0f);
        }
    }

    /** @brief Anchors the parhelion at the first head pose: 3 m ahead, 1.3 m above the eyes, tilted 40 degrees down. */
    void anchorSky()
    {
        if (skyAnchored_ || !headValid_) return;
        const Vec3 fwd = rotate(headPose_.orientation, { 0.0f, 0.0f, -1.0f });
        const float len = std::fmax(std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z), 1.0e-3f);
        const Vec3 f{ fwd.x / len, 0.0f, fwd.z / len };
        const float tilt = 0.70f;   // radians the plane leans towards the player
        sky_.right = { -f.z, 0.0f, f.x };
        // "Up" on the plane: up, leaning back away from the player, so its face looks down at them.
        sky_.up = { f.x * std::sin(tilt), std::cos(tilt), f.z * std::sin(tilt) };
        sky_.origin = { headPose_.position.x + f.x * 3.0f, headPose_.position.y + 1.3f, headPose_.position.z + f.z * 3.0f };
        skyAnchored_ = true;
    }

    /** @brief A line of the panel, cut to kLineChars and upper case (the font's). */
    static void fit(char* line)
    {
        line[kLineChars] = 0;
        for (char* c = line; *c; ++c) if (*c >= 'a' && *c <= 'z') *c = static_cast<char>(*c - 'a' + 'A');
    }

    /** @brief The performer panel: the track, its section and key, two presets, the time, the level, the hands' controls. */
    void buildScene()
    {
        // The floor ring: a fixed horizon, never moved by the audio.
        for (int i = 0; i < 72; ++i) {
            const float a = static_cast<float>(i) / 72.0f * 6.2831853f;
            scene_.add(2.5f * std::sin(a), 0.02f, -2.5f * std::cos(a), 0.30f, 0.27f, 0.36f, 0.4f, 40.0f);
        }
        for (int h = 0; h < 2; ++h) {
            const Hands::Hand& s = hands_.hand(h);
            if (!s.valid) continue;
            const float t = s.pinch;
            // The right hand held towards "now" warms up to its colour while the pinch is held.
            const float hold = h == 1 ? clamp01(static_cast<float>(held_[1] / kLongPinch)) : 0.0f;
            scene_.add(s.palm.x, s.palm.y, s.palm.z, 1.00f, 0.86f - 0.4f * t - 0.2f * hold, 0.66f - 0.4f * t - 0.3f * hold, 1.0f, 130.0f);
        }
        if (!headValid_) return;
        anchorSky();

        const Panel p = headPanel();
        char line[96];
        if (!player_.ready()) {
            addLogo(p, 0.200f, 0.10f, 0.14f);   // centred over the name, 0.40 m wide at 1.8 cells
            scene_.addText(p, 0.0f, 0.0f, kCell * 1.8f, "PARHELION", 1.00f, 0.86f, 0.66f, 1.0f);
            scene_.addText(p, 0.0f, -0.08f, kCell, "COMPOSING", 0.6f, 0.7f, 0.9f, 0.8f);
            return;
        }
        const std::shared_ptr<const Now> np = player_.now();
        if (!np) return;
        const Now& now = *np;
        const double beat = player_.beat();
        const ParamStore& par = player_.params();
        addSky(now, beat);

        // The title line above the rest: the logo and the name.
        addLogo(p, 0.012f, kRow + 0.012f, 0.05f);
        scene_.addText(p, 0.036f, kRow, kCell, "PARHELION", 1.00f, 0.86f, 0.66f, 0.85f);
        float row = 0.0f;
        auto text = [&](char* t, float r, float g, float b, float a) {
            fit(t);
            scene_.addText(p, 0.0f, row, kCell, t, r, g, b, a);
            row -= kRow;
        };
        const int t = now.trackAt(beat);
        const TrackInfo& info = now.tracks[static_cast<size_t>(t)];
        if (now.isSet) std::snprintf(line, sizeof(line), "SET %d  T%d/%d", player_.number(), t + 1, static_cast<int>(now.tracks.size()));
        else std::snprintf(line, sizeof(line), "TRACK %d", player_.number());
        text(line, 1.00f, 0.86f, 0.66f, 1.0f);
        std::snprintf(line, sizeof(line), "%s %s", info.style.c_str(), kFormTemplateNames[static_cast<int>(info.form)]);
        text(line, 1.00f, 0.86f, 0.66f, 0.9f);
        const Score& deck = now.set.decks[now.decks[static_cast<size_t>(t)]];
        if (const Section* sec = sectionAt(deck, beat)) {
            float cr, cg, cb;
            sectionColour(sec->kind, cr, cg, cb);
            std::snprintf(line, sizeof(line), "%s  BAR %d", kSectionNames[static_cast<int>(sec->kind)],
                          static_cast<int>(std::floor((beat - now.starts[static_cast<size_t>(t)]) / 4.0)) + 1);
            text(line, cr, cg, cb, 0.95f);
        }
        std::snprintf(line, sizeof(line), "%s %s %s", kKeyNames[info.key], kScaleNames[info.scale], info.camelot.c_str());
        text(line, 0.70f, 0.85f, 1.00f, 0.9f);
        // The composer's presets (Presets.h) of the lead and the pad, as the plugin's pages name them.
        for (const auto& [inst, label] : { std::pair<int, const char*>{ static_cast<int>(PolyInstance::Lead), "LEAD" },
                                           std::pair<int, const char*>{ static_cast<int>(PolyInstance::Pad), "PAD" } }) {
            int index = -1;
            for (const SoundPick& k : deck.sounds)
                if (k.module == static_cast<int>(Module::Poly) && k.instance == inst && k.beat <= beat + 1.0) index = k.preset;
            if (index < 0) continue;
            std::snprintf(line, sizeof(line), "%s %s", label, factoryPresets(Module::Poly, inst)[static_cast<size_t>(index)].name.c_str());
            text(line, 0.72f, 0.60f, 0.95f, 0.8f);
        }
        const TempoMap& tm = now.set.decks[0].tempo;
        const double sec = player_.seconds(), total = tm.secondsAt(now.set.lengthBeats);
        std::snprintf(line, sizeof(line), "%02d:%02d/%02d:%02d %.0f BPM", static_cast<int>(sec) / 60, static_cast<int>(sec) % 60,
                      static_cast<int>(total) / 60, static_cast<int>(total) % 60, tm.bpmAt(beat));
        text(line, 1.00f, 0.86f, 0.60f, 0.9f);
        const float db = player_.levelDb();
        std::snprintf(line, sizeof(line), "%+.1f DB  %s", static_cast<double>(std::max(db, -99.0f)),
                      player_.muted() ? "MUTED" : (player_.playing() ? "PLAY" : "STOP"));
        text(line, 1.00f, 0.86f, 0.60f, 0.9f);
        const float f = par.get(filterId_);
        if (std::fabs(f) < 0.01f) std::snprintf(line, sizeof(line), "FILTER OPEN");
        else std::snprintf(line, sizeof(line), "FILTER %s %d%%", f < 0.0f ? "LP" : "HP", static_cast<int>(std::lround(std::fabs(f) * 100.0f)));
        text(line, 0.60f, 0.75f, 0.95f, 0.75f);
        std::snprintf(line, sizeof(line), "THROW %d%%", static_cast<int>(std::lround(par.get(throwId_) * 100.0f)));
        text(line, 0.60f, 0.75f, 0.95f, 0.75f);
        if (par.getBool(muteKickId_)) { std::snprintf(line, sizeof(line), "KICK OUT"); text(line, 1.00f, 0.45f, 0.30f, 1.0f); }
        // The performer's "now": where it lands while it is being composed and handed over, a word when it cannot.
        const NowState ns = player_.nowState();
        const double age = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count() - player_.nowSince();
        const char* what = player_.nowKind() == SectionKind::Drop ? "DROP" : "BREAKDOWN";
        if (ns == NowState::Pending) { std::snprintf(line, sizeof(line), "%s > BAR %d", what, player_.nowBar() + 1); text(line, 1.00f, 0.78f, 0.45f, 1.0f); }
        else if (age < 3.0 && ns == NowState::TooLate) { std::snprintf(line, sizeof(line), "%s: TOO LATE", what); text(line, 1.00f, 0.45f, 0.30f, 0.9f); }
        else if (age < 3.0 && ns == NowState::NotInSet) { std::snprintf(line, sizeof(line), "NOT IN A SET"); text(line, 1.00f, 0.45f, 0.30f, 0.9f); }
        else if (age < 3.0 && ns == NowState::NoRoom) { std::snprintf(line, sizeof(line), "%s: TRACK ENDS", what); text(line, 1.00f, 0.45f, 0.30f, 0.9f); }

        row -= kRow * 0.3f;
        // Four beat lamps: a raised cosine of the distance to the beat, a continuous pulse.
        const double inBar = beat / 4.0 - std::floor(beat / 4.0);
        const float phase = static_cast<float>(inBar) * 4.0f;
        for (int b = 0; b < 4; ++b) {
            float dist = phase - static_cast<float>(b);
            if (dist < 0.0f) dist += 4.0f;
            const float x = dist < 1.0f ? 0.5f + 0.5f * std::cos(3.14159265f * dist) : 0.0f;
            const float level = player_.playing() ? 0.22f + 0.78f * x : 0.18f;
            scene_.addOn(p, 0.026f * static_cast<float>(b), row, 1.0f, 0.62f + 0.3f * x, 0.30f, level, 150.0f);
        }
    }

    /**
     * @brief The pinches: a short one acts when it opens again, so both hands closed together can mean something else
     *        -- the next track, once, while neither then counts as a single pinch. Left alone: play / stop; right alone:
     *        kick out / kick in; right held kLongPinch: "now" -- a breakdown from a groove or a drop, a drop from a
     *        breakdown, a break or a build -- and its release does nothing more.
     */
    void gestures(double dt)
    {
        const bool closed[2] = { hands_.hand(0).closed, hands_.hand(1).closed };
        if (closed[0] && closed[1]) {
            spoiled_[0] = spoiled_[1] = true;
            if (!bothFired_) { player_.requestNext(); bothFired_ = true; }
        }
        for (int h = 0; h < 2; ++h) {
            if (closed[h] && !handWas_[h] && !closed[1 - h]) { spoiled_[h] = false; held_[h] = 0.0; }
            if (closed[h]) held_[h] += std::min(dt, 0.1);
            else held_[h] = 0.0;
            if (h == 1 && closed[1] && !spoiled_[1] && held_[1] >= kLongPinch) {
                spoiled_[1] = true;
                SectionKind kind = SectionKind::Breakdown;
                if (const std::shared_ptr<const Now> np = player_.now()) {
                    const Now& now = *np;
                    const double beat = player_.beat();
                    const Score& deck = now.set.decks[now.decks[static_cast<size_t>(now.trackAt(beat))]];
                    if (const Section* sec = sectionAt(deck, beat))
                        if (sec->kind == SectionKind::Breakdown || sec->kind == SectionKind::Break || sec->kind == SectionKind::Build)
                            kind = SectionKind::Drop;
                }
                player_.requestNow(kind);
            }
            if (!closed[h] && handWas_[h] && !spoiled_[h]) {
                if (h == 0) player_.setPlaying(!player_.playing());
                else player_.setKnob(muteKickId_, player_.params().getBool(muteKickId_) ? 0.0f : 1.0f);
            }
            handWas_[h] = closed[h];
        }
        if (!closed[0] && !closed[1]) bothFired_ = false;
    }

    void frame()
    {
        XrFrameWaitInfo wi{ XR_TYPE_FRAME_WAIT_INFO };
        XrFrameState fs{ XR_TYPE_FRAME_STATE };
        if (XR_FAILED(xrWaitFrame(session_, &wi, &fs))) return;
        XrFrameBeginInfo bi{ XR_TYPE_FRAME_BEGIN_INFO };
        xrBeginFrame(session_, &bi);

        const double dt = lastTime_ == 0 ? 1.0 / 72.0 : static_cast<double>(fs.predictedDisplayTime - lastTime_) * 1.0e-9;
        lastTime_ = fs.predictedDisplayTime;
        updateHead(fs.predictedDisplayTime);
        updateHands(fs.predictedDisplayTime);
        bool leftPinch = false, rightPinch = false;
        hands_.update(dt, leftPinch, rightPinch);
        bridge_.send(hands_, dt);   // the bridge: the hands to the desktop (01.10.2026)
        if (player_.ready()) {
            gestures(dt);
            applyMacros();
        }

        std::vector<XrCompositionLayerProjectionView> projViews;
        XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        const XrCompositionLayerBaseHeader* layers[1] = { reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer) };
        uint32_t layerCount = 0;

        if (fs.shouldRender) {
            XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
            vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            vli.displayTime = fs.predictedDisplayTime;
            vli.space = stageSpace_;
            XrViewState vs{ XR_TYPE_VIEW_STATE };
            uint32_t viewCount = 0;
            xrLocateViews(session_, &vli, &vs, static_cast<uint32_t>(views_.size()), &viewCount, views_.data());
            if ((vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) && (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
                scene_.begin();
                buildScene();
                scene_.upload();
                projViews.resize(viewCount, { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW });
                for (uint32_t v = 0; v < viewCount; ++v) {
                    SwapchainTarget& t = targets_[v];
                    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                    uint32_t index = 0;
                    xrAcquireSwapchainImage(t.swapchain, &ai, &index);
                    XrSwapchainImageWaitInfo swi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                    swi.timeout = XR_INFINITE_DURATION;
                    xrWaitSwapchainImage(t.swapchain, &swi);

                    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.images[index].image, 0);
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
                    scene_.draw(multiply(projectionFromFov(views_[v].fov, 0.05f, 100.0f), viewFromPose(views_[v].pose)), t.width, t.height);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);

                    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                    xrReleaseSwapchainImage(t.swapchain, &ri);

                    projViews[v].pose = views_[v].pose;
                    projViews[v].fov = views_[v].fov;
                    projViews[v].subImage.swapchain = t.swapchain;
                    projViews[v].subImage.imageRect = { { 0, 0 }, { t.width, t.height } };
                    projViews[v].subImage.imageArrayIndex = 0;
                }
                layer.space = stageSpace_;
                layer.viewCount = viewCount;
                layer.views = projViews.data();
                layerCount = 1;
            }
        }

        XrFrameEndInfo ei{ XR_TYPE_FRAME_END_INFO };
        ei.displayTime = fs.predictedDisplayTime;
        ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        ei.layerCount = layerCount;
        ei.layers = layers;
        xrEndFrame(session_, &ei);
    }

    android_app* app_;
    std::string dataDir_;
    Config config_;
    TrackPlayer player_;
    Audio audio_{ player_ };
    std::thread composerThread_;
    std::atomic<bool> stopComposer_{ false };
    Scene scene_;
    Hands hands_;
    HandBridge bridge_;                  ///< the bridge mode: the hands to the desktop's Parhelion (01.10.2026)
    int filterId_ = -1, throwId_ = -1, muteKickId_ = -1;   ///< perform.filter, .throw and .mute_kick: the hands' controls
    bool handWas_[2] = {}, spoiled_[2] = {}, bothFired_ = false;   ///< gestures(): the pinches as they were
    double held_[2] = {};                ///< how long each hand has pinched, seconds
    Panel sky_{};                        ///< the parhelion's plane in the room (anchorSky)
    bool skyAnchored_ = false;
    CueSender cues_;                     ///< the cue bridge's socket and thread (Cue.h)

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig eglConfig_ = nullptr;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId system_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace stageSpace_ = XR_NULL_HANDLE, viewSpace_ = XR_NULL_HANDLE;
    XrSessionState sessionState_ = XR_SESSION_STATE_UNKNOWN;
    bool sessionRunning_ = false, quit_ = false, handsSupported_ = false, headValid_ = false;
    std::vector<XrView> views_;
    std::vector<SwapchainTarget> targets_;
    XrPosef headPose_{ { 0, 0, 0, 1 }, { 0, 0, 0 } };
    XrTime lastTime_ = 0;

    PFN_xrCreateHandTrackerEXT pfnCreateHandTracker_ = nullptr;
    PFN_xrLocateHandJointsEXT pfnLocateHandJoints_ = nullptr;
    PFN_xrDestroyHandTrackerEXT pfnDestroyHandTracker_ = nullptr;
    XrHandTrackerEXT handTracker_[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
};

void handleCmd(android_app*, int32_t) {}

} // namespace

/** @brief NativeActivity entry point (android_native_app_glue). */
void android_main(android_app* app)
{
    app->onAppCmd = handleCmd;
    JNIEnv* env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);
    {
        // On the heap: the engine and the composer's plans are far more than the glue thread's stack.
        auto a = std::make_unique<App>(app);
        if (a->init()) a->run();
        else LOGE("init failed");
        a->shutdown();
    }
    app->activity->vm->DetachCurrentThread();
}

