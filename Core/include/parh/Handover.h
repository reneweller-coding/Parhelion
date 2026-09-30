/**
 * @file Handover.h
 * @brief A seamless change of what plays (Phase 7, 30.09.2026): a second engine takes over from the first at a sample,
 *        with a short crossfade, instead of the one engine loading anew.
 *
 * Loading allocates and a seek silences every voice (Engine.h): done on the engine that plays, a performer's "Breakdown
 * now" or "Drop now" (planTrackRewritten) costs a gap and the notes that were held. The rewritten score -- the old one up
 * to the rewrite bar -- goes into a second engine instead, off the audio thread: loaded, sought a few bars before where
 * the first one plays and rendered from there up to it and a little beyond (the pre-roll: its voices, rooms and echoes are
 * then the first one's as far as the notes since the seek decide them), until it is ahead of the audio thread by more than
 * a block, and on to the next beat. The handover is armed there; the audio thread plays the first engine up to it and
 * fades over to the second in kFade samples (a raised cosine; the two play the same notes, so equal gains). Only a note
 * held since before the seek is missing in the second and fades with the first. The two play the same notes, not the
 * same waves: a free-running oscillator's phase (the sub's sine, the saws) and the noise depend on all that was played
 * before, so the two are uncorrelated there and a fade between them can dip for a few milliseconds where they cancel.
 * On the beat the kick hides it and the pump has ducked the bass (measured, testHandover).
 *
 * @code
 *   worker:  next.load(rewritten); next.seek(a few bars back); handover.handOver(next, deadline, stop)
 *   audio:   if (handover.process(front, back, L, R, n)) swap(front, back);
 * @endcode
 *
 * **Threads.** Two atomics: the sample armed (the worker arms it and may take it back while the audio thread has not;
 * the audio thread takes it with a compare-and-exchange) and the audio thread's position after every block. The worker
 * touches the second engine only while nothing is armed, the audio thread only from the armed sample on. The audio
 * thread never waits and never allocates.
 */
#pragma once
#include "parh/Engine.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace parh {

/** @brief The handover from the engine that plays to a second one on the same timeline (the file comment). */
class Handover {
public:
    static constexpr int kFade = 1024;   ///< the crossfade, samples (21 ms at 48 kHz)

    /** @brief Allocates the scratch for blocks up to @p maxBlock (not on the audio thread). */
    void prepare(int maxBlock);

    /**
     * @brief Worker thread: renders @p next (loaded and sought by the caller, prepared like the engine that plays) until
     *        it is ahead of the audio thread by @p lead samples at least, arms the handover there, and waits until the
     *        audio thread has taken over. A handover the audio thread missed (it had passed the sample) is caught up
     *        and armed again.
     * @param deadline the playing engine's sample by which the handover must have begun (the rewrite bar less a fade);
     *                 past it the old engine has played what the new one changes, and the handover gives up
     * @param stop     asked between chunks: true breaks off (and takes back what is armed, if the audio thread has not)
     * @param align    the sample at or after the one given where a handover sounds best (the next beat); none: anywhere
     * @return true once the second engine plays (the fade has ended); false: broken off or too late, nothing changed
     */
    bool handOver(Engine& next, int64_t deadline, const std::function<bool()>& stop,
                  const std::function<int64_t(int64_t)>& align = {});

    /**
     * @brief Audio thread: renders @p n samples of @p front into @p L and @p R, and from an armed sample on the fade into
     *        @p back and @p back alone after it.
     * @return true in the block the fade ends: from the next block on @p back is the front (the caller swaps them)
     */
    bool process(Engine& front, Engine& back, float* L, float* R, int n);

    /** @brief Audio thread, when it does not render (paused, or the front just changed): publishes where @p front is. */
    void follow(const Engine& front) { playing_.store(front.samplePosition(), std::memory_order_release); }
    /** @brief Whether a handover is armed or fading (the second engine is the audio thread's). */
    bool busy() const { return at_.load(std::memory_order_acquire) != kNone; }
    /** @brief The playing engine's position after the last block (any thread). */
    int64_t playing() const { return playing_.load(std::memory_order_acquire); }
    /** @brief Where the last handover began (its fade's first sample), -1 before any (any thread). */
    int64_t lastTaken() const { return taken_.load(std::memory_order_acquire); }

private:
    static constexpr int64_t kNone = -1;       ///< nothing armed
    static constexpr int64_t kFading = -2;     ///< the audio thread took the armed sample and fades

    std::atomic<int64_t> at_{ kNone };         ///< the armed sample, kNone or kFading
    std::atomic<int64_t> playing_{ 0 };        ///< the audio thread's front engine's position after its last block
    std::atomic<int> outcome_{ 0 };            ///< 1 switched, -1 missed (the audio thread was past the sample)
    std::atomic<int64_t> taken_{ -1 };         ///< where the last fade began
    int fadePos_ = 0;                          ///< audio thread: samples of the fade done
    int maxBlock_ = 512;
    std::vector<float> fadeL_, fadeR_;         ///< audio thread: the second engine's samples during the fade
    std::vector<float> workL_, workR_;         ///< worker: where the pre-roll is rendered to
};

/**
 * @brief The first beat at or after @p from where the notes of @p a and @p b begin differently -- part by part, their
 *        onsets (beat, pitch, velocity; a length may differ, the note sounds the same until it ends) -- or the end of the
 *        longer score where they never do. A handover from @p a to @p b must begin before it: from there on the second
 *        engine plays other notes than the first, and at the fade its voices would hold others.
 */
double firstDifference(const Score& a, const Score& b, double from);

} // namespace parh
