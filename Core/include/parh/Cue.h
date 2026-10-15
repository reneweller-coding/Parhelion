/**
 * @file Cue.h
 * @brief Score cues for a visualiser (PLAN 10.3): what the score knows, sent over OSC at the moment it is heard.
 *
 * Kaleidoscope guesses beat and section from the audio it is fed. Parhelion does not have to guess: it wrote the score.
 * So it says so, over UDP as OSC 1.0 -- the breakdown and the drop above all, the most rewarding moments for a visual
 * (PLAN 8):
 * @code
 *   /parh/beat i f      beat number, tempo in BPM                         on every beat
 *   /parh/bar i         bar number                                         on every bar
 *   /parh/block s       the section ("Intro", "Groove", "Breakdown", "Build", "Drop", "Outro", a set's track)   at its start
 *   /parh/op s          a change of the layer matrix ("add lead", "filter pluck")                             where it acts
 *   /parh/key s         the track's key as a Camelot label ("8A")                                            where a track begins
 * @endcode
 *
 * **Where a cue comes from** (after Phosphene's and Ephemeris' Cue.h): not from the composer but from the play position.
 * The engine's score becomes a list of marks when it is loaded (cueMarksOf); the audio thread gives CueTap::scan() the
 * beat range each block covers, and the tap stamps every mark in it with the instant the listener hears it.
 *
 * **The audio thread sends nothing.** scan() does arithmetic and one wait-free push per cue into a CueRing; the
 * CueSender's own thread pops, waits for the due instant and calls sendto(). **Failure is silence.**
 *
 * @note Copied from Ephemeris `Core/include/eph/Cue.h` (namespace eph, prefix EPH_) at d047d79 (27.09.2026); the
 *       messages are Totality's.
 * @note Copied from Totality `Core/include/tot/Cue.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 * @author Rene Weller
 */
#pragma once
#include "parh/Score.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace parh {

/** @brief What a mark or a cue says. */
enum class CueKindE : uint8_t { BEAT, BAR, BLOCK, OP, KEY };

/** @brief A mark in the score: a beat and what happens there (built at load, read by the audio thread). */
struct CueMark {
    double m_beat = 0.0;          ///< where
    CueKindE m_kind = CueKindE::BLOCK;   ///< what
    int32_t m_a = 0;              ///< unused for marks (a beat's or a bar's number in a cue)
    float m_b = 0.0f;             ///< unused for marks
    char m_text[24] = {};         ///< Block: the section; Op: "add ride", "filter pluck"; Key: "8A"
};

/** @brief A cue with its instant: what the audio thread hands to the sender. */
struct Cue {
    int64_t m_dueNanos = 0;       ///< when the listener hears it (steady clock)
    CueKindE m_kind = CueKindE::BEAT;   ///< what
    int32_t m_a = 0;              ///< Beat: the beat number; else as CueMark
    float m_b = 0.0f;             ///< Beat: the tempo; else as CueMark
    char m_text[24] = {};         ///< as CueMark
};

class ParamStore;
/**
 * @brief The marks of a score: its markers (blocks, reductions, returns, a set's tracks), its operations, and the key of
 *        every track (its LevelMark's beat; the key the score's automation sets there on compose.key, read against
 *        @p params' knob). In beat order.
 * @param[in] score   the score
 * @param[in] params  the knobs
 * @return the cue marks
 */
std::vector<CueMark> cueMarksOf(const Score& score, const ParamStore& params);

/** @brief Single-producer single-consumer ring of cues: the audio thread pushes, the sender pops. Wait-free. */
class CueRing {
public:
    /**
     * @brief Adds a cue; false when full (the cue is dropped).
     * @param[in] c  the cue
     * @return false when the ring is full and the cue is dropped
     */
    bool push(const Cue& c)
    {
        const uint32_t w = m_write.load(std::memory_order_relaxed);
        if (w - m_read.load(std::memory_order_acquire) >= Size) return false;
        m_slots[w % Size] = c;
        m_write.store(w + 1, std::memory_order_release);
        return true;
    }
    /**
     * @brief Takes the oldest cue; false when empty.
     * @param[out] c  receives the oldest cue
     * @return false when the ring is empty
     */
    bool pop(Cue& c)
    {
        const uint32_t r = m_read.load(std::memory_order_relaxed);
        if (r == m_write.load(std::memory_order_acquire)) return false;
        c = m_slots[r % Size];
        m_read.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t Size = 512;   ///< slots in the ring
    std::array<Cue, Size> m_slots{};   ///< the ring
    std::atomic<uint32_t> m_write{ 0 };   ///< the next slot to write (audio thread)
    std::atomic<uint32_t> m_read{ 0 };   ///< the next slot to read (sender thread)
};

/** @brief Turns the beat range of a block into cues (audio thread: arithmetic and pushes only). */
class CueTap {
public:
    /**
     * @brief Emits the beats and the marks in [@p from, @p to).
     * @param[in]  marks       Engine::cueMarks() of the score that plays
     * @param[in]  from        beat at the block's first sample
     * @param[in]  to          beat just past its last sample
     * @param[in]  bpm         the tempo, for the beat message
     * @param[in]  nowNanos    the steady clock now
     * @param[in]  leadNanos   how much later the block's first sample is heard (output latency, lookahead)
     * @param[in]  blockNanos  the block's duration
     * @param[out] ring        where the cues go
     * @return the cues that did not fit
     */
    int scan(const std::vector<CueMark>& marks, double from, double to, float bpm, int64_t nowNanos,
             int64_t leadNanos, int64_t blockNanos, CueRing& ring);
    /** @brief Forgets the position in the marks (after a jump or a new score). */
    void reset() { m_cursor = 0; }

private:
    size_t m_cursor = 0;   ///< the first mark not yet sent
};

/**
 * @brief Encodes one OSC 1.0 message: the address, the type tags, the arguments, each padded to four bytes,
 *        numbers big-endian. @p tags are the argument types without the comma ("if", "s", "si").
 * @param[in] address  the OSC address
 * @param[in] tags     the type tags
 * @param[in] ints     the int arguments
 * @param[in] floats   the float arguments
 * @param[in] strings  the string arguments
 * @return the bytes
 */
std::vector<uint8_t> oscMessage(const char* address, const char* tags, const int32_t* ints, const float* floats, const char* const* strings);

/**
 * @brief The bytes of a cue as its OSC message (CueKind decides the address and the arguments).
 * @param[in] c  the cue
 * @return its OSC message
 */
std::vector<uint8_t> oscOf(const Cue& c);

/** @brief The UDP sender: a thread that pops cues, waits for their instant and sends them. */
class CueSender {
public:
    CueSender() = default;
    CueSender(const CueSender&) = delete;
    CueSender& operator=(const CueSender&) = delete;
    ~CueSender() { stop(); }
    /**
     * @brief Opens the socket to @p host (an IPv4 address or a name) and @p port and starts the thread.
     * @param[in] host  an IPv4 address or a host name
     * @param[in] port  the UDP port
     * @return false when the socket cannot be opened
     */
    bool start(const std::string& host, int port);
    /** @brief Stops the thread and closes the socket. */
    void stop();
    /**
     * @brief Whether the thread runs.
     * @return true while the sending thread runs
     */
    bool running() const { return m_running.load(std::memory_order_acquire); }
    /**
     * @brief The ring the audio thread fills.
     * @return the ring the audio thread fills
     */
    CueRing& ring() { return m_ring; }
    /**
     * @brief Cues that could not be sent.
     * @return how many cues could not be sent
     */
    uint64_t dropped() const { return m_dropped.load(std::memory_order_relaxed); }
    /**
     * @brief The steady clock in nanoseconds, the time base of the cues.
     * @return the steady clock in nanoseconds
     */
    static int64_t nowNanos();

private:
    /** @brief The thread: takes the cues off the ring and sends each as an OSC message when its time comes. */
    void loop();
    CueRing m_ring;   ///< the ring the audio thread fills
    std::thread m_thread;   ///< the sender thread
    std::atomic<bool> m_running{ false };   ///< the thread runs
    std::atomic<bool> m_stop{ false };   ///< the thread is asked to end
    std::atomic<uint64_t> m_dropped{ 0 };   ///< cues that could not be sent
    intptr_t m_socket = -1;   ///< the UDP socket, -1 closed
    uint8_t m_address[16] = {};   ///< a sockaddr_in, kept opaque so this header needs no socket headers
};

} // namespace parh
