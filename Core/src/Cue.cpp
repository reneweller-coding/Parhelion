/**
 * @file Cue.cpp
 * @brief Score cues for a visualiser (Cue.h).
 * @note Copied from Ephemeris `Core/src/Cue.cpp` (namespace eph) at d047d79 (27.09.2026) by way of Totality; the marks are
 *       Parhelion's: the sections, what the layer matrix adds, filters or takes away, the keys.
 * @note Copied from Totality `Core/src/Cue.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#include "parh/Cue.h"
#include "parh/Dsp.h"
#include "parh/Params.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

namespace parh {

namespace {

/** @brief Copies @p src into @p dst, cut at 23 characters and terminated. */
void copyText(char (&dst)[24], const char* src)
{
    std::strncpy(dst, src, sizeof(dst) - 1);
    dst[sizeof(dst) - 1] = 0;
}

/** @brief Appends @p v big-endian (OSC). */
void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
}

/** @brief Appends @p s as an OSC string: terminated and padded to four bytes. */
void putString(std::vector<uint8_t>& out, const char* s)
{
    const size_t n = std::strlen(s);
    out.insert(out.end(), s, s + n);
    for (size_t pad = 4 - n % 4; pad > 0; --pad) out.push_back(0);   // at least one zero, then to four bytes
}

} // namespace

/** @brief The Camelot name of a minor key (Ionian: its major), the notation DJs mix by ("8A" is A minor). */
std::string camelotOf(int key, bool major)
{
    static const int kMinor[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };   // C minor 5A, C# minor 12A, ...
    static const int kMajor[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };   // C major 8B, C# major 3B, ...
    const int k = ((key % 12) + 12) % 12;
    return std::to_string(major ? kMajor[k] : kMinor[k]) + (major ? "B" : "A");
}

std::vector<CueMark> cueMarksOf(const Score& score, const ParamStore& params)
{
    std::vector<CueMark> marks;
    for (const Marker& m : score.markers) {
        CueMark c;
        c.beat = m.beat;
        c.kind = CueKind::Block;
        copyText(c.text, m.text.c_str());
        marks.push_back(c);
    }
    for (const Section& sec : score.sections) {
        CueMark c;
        c.beat = sec.beat;
        c.kind = CueKind::Block;
        copyText(c.text, kSectionNames[static_cast<int>(sec.kind)]);
        marks.push_back(c);
    }
    // The layer matrix: what each 8-bar block changes against the one before ("add lead", "filter pluck").
    for (size_t b = 0; b < score.layers.size(); ++b) {
        for (int l = 0; l < kNumLayers; ++l) {
            const LayerState now = score.layers[b].state[static_cast<size_t>(l)];
            const LayerState before = b == 0 ? LayerState::Off : score.layers[b - 1].state[static_cast<size_t>(l)];
            if (now == before) continue;
            CueMark c;
            c.beat = score.layers[b].beat;
            c.kind = CueKind::Op;
            const char* verb = now == LayerState::On ? "add" : now == LayerState::Filtered ? "filter" : "remove";
            copyText(c.text, (std::string(verb) + " " + kLayerNames[l]).c_str());
            marks.push_back(c);
        }
    }
    const int keyId = params.id(Module::Compose, 0, compose::Key);
    for (const LevelMark& l : score.levels) {
        const float set = score.knobAt(keyId, l.beat + 1e-6);
        const float knob = set == set ? set : params.get(keyId);
        const int key = static_cast<int>(std::lround(params.fromNormalised(keyId, params.toNormalised(keyId, knob) + score.gestureOffset(keyId, l.beat + 1e-6))));
        CueMark c;
        c.beat = l.beat;
        c.kind = CueKind::Key;
        copyText(c.text, camelotOf(key, score.scale == static_cast<int>(Scale::Ionian)).c_str());
        marks.push_back(c);
    }
    std::stable_sort(marks.begin(), marks.end(), [](const CueMark& x, const CueMark& y) { return x.beat < y.beat; });
    return marks;
}

int CueTap::scan(const std::vector<CueMark>& marks, double from, double to, float bpm, int64_t nowNanos,
                 int64_t leadNanos, int64_t blockNanos, CueRing& ring)
{
    if (!(to > from)) return 0;
    int lost = 0;
    auto at = [&](double beat) {
        return nowNanos + leadNanos + static_cast<int64_t>(static_cast<double>(blockNanos) * (beat - from) / (to - from));
    };
    // A jump backwards (a seek): start again from the first mark at or after the new position.
    if (cursor_ > 0 && cursor_ <= marks.size() && marks[cursor_ - 1].beat >= from) cursor_ = 0;
    while (cursor_ < marks.size() && marks[cursor_].beat < from) ++cursor_;
    // The beats: every whole beat in the range; every fourth a bar as well.
    for (double b = std::ceil(from); b < to; b += 1.0) {
        Cue c;
        c.dueNanos = at(b);
        c.kind = CueKind::Beat;
        c.a = static_cast<int32_t>(b);
        c.b = bpm;
        if (!ring.push(c)) ++lost;
        if (static_cast<int64_t>(b) % 4 == 0) {
            Cue bar = c;
            bar.kind = CueKind::Bar;
            bar.a = static_cast<int32_t>(b / 4.0);
            if (!ring.push(bar)) ++lost;
        }
    }
    for (; cursor_ < marks.size() && marks[cursor_].beat < to; ++cursor_) {
        const CueMark& m = marks[cursor_];
        Cue c;
        c.dueNanos = at(m.beat);
        c.kind = m.kind;
        c.a = m.a;
        c.b = m.b;
        std::memcpy(c.text, m.text, sizeof(c.text));
        if (!ring.push(c)) ++lost;
    }
    return lost;
}

std::vector<uint8_t> oscMessage(const char* address, const char* tags, const int32_t* ints, const float* floats, const char* const* strings)
{
    std::vector<uint8_t> out;
    putString(out, address);
    const std::string t = std::string(",") + tags;
    putString(out, t.c_str());
    int ni = 0, nf = 0, ns = 0;
    for (const char* p = tags; *p != 0; ++p) {
        if (*p == 'i') put32(out, static_cast<uint32_t>(ints[ni++]));
        else if (*p == 'f') { uint32_t u; std::memcpy(&u, &floats[nf++], 4); put32(out, u); }
        else if (*p == 's') putString(out, strings[ns++]);
    }
    return out;
}

std::vector<uint8_t> oscOf(const Cue& c)
{
    const char* text = c.text;
    switch (c.kind) {
    case CueKind::Beat:  return oscMessage("/parh/beat", "if", &c.a, &c.b, nullptr);
    case CueKind::Bar:   return oscMessage("/parh/bar", "i", &c.a, nullptr, nullptr);
    case CueKind::Block: return oscMessage("/parh/block", "s", nullptr, nullptr, &text);
    case CueKind::Op:    return oscMessage("/parh/op", "s", nullptr, nullptr, &text);
    case CueKind::Key:   return oscMessage("/parh/key", "s", nullptr, nullptr, &text);
    }
    return {};
}

int64_t CueSender::nowNanos()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool CueSender::start(const std::string& host, int port)
{
    stop();
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) return false;
        addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    static_assert(sizeof(address_) >= sizeof(sockaddr_in), "room for the address");
    std::memcpy(address_, &addr, sizeof(addr));
    const auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#if defined(_WIN32)
    if (s == INVALID_SOCKET) return false;
#else
    if (s < 0) return false;
#endif
    socket_ = static_cast<intptr_t>(s);
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { loop(); });
    return true;
}

void CueSender::stop()
{
    if (thread_.joinable()) {
        stop_ = true;
        thread_.join();
    }
    running_ = false;
    if (socket_ >= 0) {
#if defined(_WIN32)
        closesocket(static_cast<SOCKET>(socket_));
        WSACleanup();
#else
        close(static_cast<int>(socket_));
#endif
        socket_ = -1;
    }
}

void CueSender::loop()
{
    Cue c;
    while (!stop_.load(std::memory_order_acquire)) {
        if (!ring_.pop(c)) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
        // Hold it back until the listener hears it (a cue that is late already goes at once).
        while (!stop_.load(std::memory_order_acquire)) {
            const int64_t wait = c.dueNanos - nowNanos();
            if (wait <= 0) break;
            std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<int64_t>(wait, 2000000)));
        }
        const std::vector<uint8_t> msg = oscOf(c);
        const auto sent = sendto(static_cast<decltype(socket(0, 0, 0))>(socket_), reinterpret_cast<const char*>(msg.data()),
                                 static_cast<int>(msg.size()), 0, reinterpret_cast<const sockaddr*>(address_), sizeof(sockaddr_in));
        if (sent < 0) dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace parh
