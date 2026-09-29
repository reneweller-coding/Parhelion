/**
 * @file Cue.h
 * @brief Score cues for a visualiser (PLAN 10.3): what the score knows, sent over OSC at the moment it is heard.
 *
 * Kaleidoscope guesses beat and section from the audio it is fed. Parhelion does not have to guess: it wrote the score.
 * So it says so, over UDP as OSC 1.0:
 * @code
 *   /parh/beat i f      beat number, tempo in BPM                         on every beat
 *   /parh/bar i         bar number                                         on every bar
 *   /parh/block s       the block's name ("Intro", "Block 3", "Reduction", "Return", "Outro", a set's track)   at its start
 *   /parh/op s          the form's operation ("add ride", "remove chord", "kick out")                         where it acts
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
enum class CueKind : uint8_t { Beat, Bar, Block, Op, Key };

/** @brief A mark in the score: a beat and what happens there (built at load, read by the audio thread). */
struct CueMark {
    double beat = 0.0;          ///< where
    CueKind kind = CueKind::Block;   ///< what
    int32_t a = 0;              ///< unused for marks (a beat's or a bar's number in a cue)
    float b = 0.0f;             ///< unused for marks
    char text[24] = {};         ///< Block: the section; Op: "add ride", "filter pluck"; Key: "8A"
};

/** @brief A cue with its instant: what the audio thread hands to the sender. */
struct Cue {
    int64_t dueNanos = 0;       ///< when the listener hears it (steady clock)
    CueKind kind = CueKind::Beat;   ///< what
    int32_t a = 0;              ///< Beat: the beat number; else as CueMark
    float b = 0.0f;             ///< Beat: the tempo; else as CueMark
    char text[24] = {};         ///< as CueMark
};

class ParamStore;
/**
 * @brief The marks of a score: its markers (blocks, reductions, returns, a set's tracks), its operations, and the key of
 *        every track (its LevelMark's beat; the key the score's automation sets there on compose.key, read against
 *        @p params' knob). In beat order.
 */
std::vector<CueMark> cueMarksOf(const Score& score, const ParamStore& params);

/** @brief Single-producer single-consumer ring of cues: the audio thread pushes, the sender pops. Wait-free. */
class CueRing {
public:
    /** @brief Adds a cue; false when full (the cue is dropped). */
    bool push(const Cue& c)
    {
        const uint32_t w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= kSize) return false;
        slots_[w % kSize] = c;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    /** @brief Takes the oldest cue; false when empty. */
    bool pop(Cue& c)
    {
        const uint32_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        c = slots_[r % kSize];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kSize = 512;
    std::array<Cue, kSize> slots_{};
    std::atomic<uint32_t> write_{ 0 }, read_{ 0 };
};

/** @brief Turns the beat range of a block into cues (audio thread: arithmetic and pushes only). */
class CueTap {
public:
    /**
     * @brief Emits the beats and the marks in [@p from, @p to).
     * @param marks      Engine::cueMarks() of the score that plays
     * @param from       beat at the block's first sample
     * @param to         beat just past its last sample
     * @param bpm        the tempo, for the beat message
     * @param nowNanos   the steady clock now
     * @param leadNanos  how much later the block's first sample is heard (output latency, lookahead)
     * @param blockNanos the block's duration
     * @param ring       where the cues go
     * @return the cues that did not fit
     */
    int scan(const std::vector<CueMark>& marks, double from, double to, float bpm, int64_t nowNanos,
             int64_t leadNanos, int64_t blockNanos, CueRing& ring);
    /** @brief Forgets the position in the marks (after a jump or a new score). */
    void reset() { cursor_ = 0; }

private:
    size_t cursor_ = 0;   ///< the first mark not yet sent
};

/**
 * @brief Encodes one OSC 1.0 message: the address, the type tags, the arguments, each padded to four bytes,
 *        numbers big-endian. @p tags are the argument types without the comma ("if", "s", "si").
 * @return the bytes
 */
std::vector<uint8_t> oscMessage(const char* address, const char* tags, const int32_t* ints, const float* floats, const char* const* strings);

/** @brief The bytes of a cue as its OSC message (CueKind decides the address and the arguments). */
std::vector<uint8_t> oscOf(const Cue& c);

/** @brief The UDP sender: a thread that pops cues, waits for their instant and sends them. */
class CueSender {
public:
    CueSender() = default;
    CueSender(const CueSender&) = delete;
    CueSender& operator=(const CueSender&) = delete;
    ~CueSender() { stop(); }
    /** @brief Opens the socket to @p host (an IPv4 address or a name) and @p port and starts the thread. */
    bool start(const std::string& host, int port);
    /** @brief Stops the thread and closes the socket. */
    void stop();
    /** @brief Whether the thread runs. */
    bool running() const { return running_.load(std::memory_order_acquire); }
    /** @brief The ring the audio thread fills. */
    CueRing& ring() { return ring_; }
    /** @brief Cues that could not be sent. */
    uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    /** @brief The steady clock in nanoseconds, the time base of the cues. */
    static int64_t nowNanos();

private:
    void loop();
    CueRing ring_;
    std::thread thread_;
    std::atomic<bool> running_{ false }, stop_{ false };
    std::atomic<uint64_t> dropped_{ 0 };
    intptr_t socket_ = -1;
    uint8_t address_[16] = {};   ///< a sockaddr_in, kept opaque so this header needs no socket headers
};

} // namespace parh
