/**
 * @file Cue.cpp
 * @brief Score cues for a visualiser (Cue.h).
 * @note Copied from Ephemeris `Core/src/Cue.cpp` (namespace eph) at d047d79 (27.09.2026) by way of Totality; the marks are
 *       Parhelion's: the sections, what the layer matrix adds, filters or takes away, the keys.
 * @note Copied from Totality `Core/src/Cue.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 * @author Rene Weller
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

/**
 * @brief Copies @p src into @p dst, cut at 23 characters and terminated.
 * @param[out] dst  receives the text
 * @param[in]  src  the text
 */
void copyText(char (&dst)[24], const char* src)
{
    std::strncpy(dst, src, sizeof(dst) - 1);
    dst[sizeof(dst) - 1] = 0;
}

/**
 * @brief Appends @p v big-endian (OSC).
 * @param[in,out] out  the bytes, appended to
 * @param[in]     v    the value
 */
void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>(v >> s));
}

/**
 * @brief Appends @p s as an OSC string: terminated and padded to four bytes.
 * @param[in,out] out  the bytes, appended to
 * @param[in]     s    the string
 */
void putString(std::vector<uint8_t>& out, const char* s)
{
    const size_t n = std::strlen(s);
    out.insert(out.end(), s, s + n);
    for (size_t pad = 4 - n % 4; pad > 0; --pad) out.push_back(0);   // at least one zero, then to four bytes
}

} // namespace

/**
 * @brief The Camelot name of a minor key (Ionian: its major), the notation DJs mix by ("8A" is A minor).
 * @param[in] key    the key's root, a pitch class
 * @param[in] major  true for Ionian
 * @return the Camelot name
 */
std::string camelotOf(int key, bool major)
{
    static const int Minor[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };   // C minor 5A, C# minor 12A, ...
    static const int Major[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };   // C major 8B, C# major 3B, ...
    const int k = ((key % 12) + 12) % 12;
    return std::to_string(major ? Major[k] : Minor[k]) + (major ? "B" : "A");
}

std::vector<CueMark> cueMarksOf(const Score& score, const ParamStore& params)
{
    std::vector<CueMark> marks;
    for (const Marker& m : score.m_markers) {
        CueMark c;
        c.m_beat = m.m_beat;
        c.m_kind = CueKindE::BLOCK;
        copyText(c.m_text, m.m_text.c_str());
        marks.push_back(c);
    }
    for (const Section& sec : score.m_sections) {
        CueMark c;
        c.m_beat = sec.m_beat;
        c.m_kind = CueKindE::BLOCK;
        copyText(c.m_text, SectionNames[static_cast<int>(sec.m_kind)]);
        marks.push_back(c);
    }
    // The layer matrix: what each 8-bar block changes against the one before ("add lead", "filter pluck").
    for (size_t b = 0; b < score.m_layers.size(); ++b) {
        for (int l = 0; l < NumLayers; ++l) {
            const LayerStateE now = score.m_layers[b].m_state[static_cast<size_t>(l)];
            const LayerStateE before = b == 0 ? LayerStateE::OFF : score.m_layers[b - 1].m_state[static_cast<size_t>(l)];
            if (now == before) continue;
            CueMark c;
            c.m_beat = score.m_layers[b].m_beat;
            c.m_kind = CueKindE::OP;
            const char* verb = now == LayerStateE::ON ? "add" : now == LayerStateE::FILTERED ? "filter" : "remove";
            copyText(c.m_text, (std::string(verb) + " " + LayerNames[l]).c_str());
            marks.push_back(c);
        }
    }
    const int keyId = params.id(ModuleE::COMPOSE, 0, compose::KEY);
    for (const LevelMark& l : score.m_levels) {
        const float set = score.knobAt(keyId, l.m_beat + 1e-6);
        const float knob = set == set ? set : params.get(keyId);
        const int key = static_cast<int>(std::lround(params.fromNormalised(keyId, params.toNormalised(keyId, knob) + score.gestureOffset(keyId, l.m_beat + 1e-6))));
        CueMark c;
        c.m_beat = l.m_beat;
        c.m_kind = CueKindE::KEY;
        copyText(c.m_text, camelotOf(key, score.m_scale == static_cast<int>(ScaleE::IONIAN)).c_str());
        marks.push_back(c);
    }
    std::stable_sort(marks.begin(), marks.end(), [](const CueMark& x, const CueMark& y) { return x.m_beat < y.m_beat; });
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
    if (m_cursor > 0 && m_cursor <= marks.size() && marks[m_cursor - 1].m_beat >= from) m_cursor = 0;
    while (m_cursor < marks.size() && marks[m_cursor].m_beat < from) ++m_cursor;
    // The beats: every whole beat in the range; every fourth a bar as well.
    for (double b = std::ceil(from); b < to; b += 1.0) {
        Cue c;
        c.m_dueNanos = at(b);
        c.m_kind = CueKindE::BEAT;
        c.m_a = static_cast<int32_t>(b);
        c.m_b = bpm;
        if (!ring.push(c)) ++lost;
        if (static_cast<int64_t>(b) % 4 == 0) {
            Cue bar = c;
            bar.m_kind = CueKindE::BAR;
            bar.m_a = static_cast<int32_t>(b / 4.0);
            if (!ring.push(bar)) ++lost;
        }
    }
    for (; m_cursor < marks.size() && marks[m_cursor].m_beat < to; ++m_cursor) {
        const CueMark& m = marks[m_cursor];
        Cue c;
        c.m_dueNanos = at(m.m_beat);
        c.m_kind = m.m_kind;
        c.m_a = m.m_a;
        c.m_b = m.m_b;
        std::memcpy(c.m_text, m.m_text, sizeof(c.m_text));
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
    const char* text = c.m_text;
    switch (c.m_kind) {
    case CueKindE::BEAT:  return oscMessage("/parh/beat", "if", &c.m_a, &c.m_b, nullptr);
    case CueKindE::BAR:   return oscMessage("/parh/bar", "i", &c.m_a, nullptr, nullptr);
    case CueKindE::BLOCK: return oscMessage("/parh/block", "s", nullptr, nullptr, &text);
    case CueKindE::OP:    return oscMessage("/parh/op", "s", nullptr, nullptr, &text);
    case CueKindE::KEY:   return oscMessage("/parh/key", "s", nullptr, nullptr, &text);
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
    static_assert(sizeof(m_address) >= sizeof(sockaddr_in), "room for the address");
    std::memcpy(m_address, &addr, sizeof(addr));
    const auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#if defined(_WIN32)
    if (s == INVALID_SOCKET) return false;
#else
    if (s < 0) return false;
#endif
    m_socket = static_cast<intptr_t>(s);
    m_stop = false;
    m_running = true;
    m_thread = std::thread([this] { loop(); });
    return true;
}

void CueSender::stop()
{
    if (m_thread.joinable()) {
        m_stop = true;
        m_thread.join();
    }
    m_running = false;
    if (m_socket >= 0) {
#if defined(_WIN32)
        closesocket(static_cast<SOCKET>(m_socket));
        WSACleanup();
#else
        close(static_cast<int>(m_socket));
#endif
        m_socket = -1;
    }
}

void CueSender::loop()
{
    Cue c;
    while (!m_stop.load(std::memory_order_acquire)) {
        if (!m_ring.pop(c)) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
        // Hold it back until the listener hears it (a cue that is late already goes at once).
        while (!m_stop.load(std::memory_order_acquire)) {
            const int64_t wait = c.m_dueNanos - nowNanos();
            if (wait <= 0) break;
            std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<int64_t>(wait, 2000000)));
        }
        const std::vector<uint8_t> msg = oscOf(c);
        const auto sent = sendto(static_cast<decltype(socket(0, 0, 0))>(m_socket), reinterpret_cast<const char*>(msg.data()),
                                 static_cast<int>(msg.size()), 0, reinterpret_cast<const sockaddr*>(m_address), sizeof(sockaddr_in));
        if (sent < 0) m_dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace parh
