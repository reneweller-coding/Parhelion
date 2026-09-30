/**
 * @file Handover.cpp
 * @brief The handover from the engine that plays to a second one (Handover.h).
 */
#include "parh/Handover.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace parh {

void Handover::prepare(int maxBlock)
{
    maxBlock_ = std::max(1, maxBlock);
    fadeL_.assign(static_cast<size_t>(kFade), 0.0f);
    fadeR_.assign(static_cast<size_t>(kFade), 0.0f);
    workL_.assign(static_cast<size_t>(std::max(maxBlock_, 1024)), 0.0f);
    workR_.assign(workL_.size(), 0.0f);
}

bool Handover::handOver(Engine& next, int64_t deadline, const std::function<bool()>& stop, const std::function<int64_t(int64_t)>& align)
{
    const double fs = next.sampleRate();
    // Ahead of the audio thread when armed: two of its blocks and 50 ms at least (its position is up to a block old);
    // each round of catching up renders a quarter second past where it was.
    const int64_t lead = std::max<int64_t>(2 * static_cast<int64_t>(maxBlock_), static_cast<int64_t>(0.05 * fs));
    const int64_t reach = lead + static_cast<int64_t>(0.25 * fs);
    const auto stopped = [&] { return stop && stop(); };
    for (;;) {
        if (stopped()) return false;
        const int64_t p = playing_.load(std::memory_order_acquire);
        if (p + lead > deadline) return false;   // too late: the old engine is about to play what the new one changes
        if (next.samplePosition() < p + lead) {
            const int64_t target = p + reach;
            while (next.samplePosition() < target) {
                const int k = static_cast<int>(std::min<int64_t>(static_cast<int64_t>(workL_.size()), target - next.samplePosition()));
                next.process(workL_.data(), workR_.data(), k);
                if (stopped()) return false;
            }
            continue;   // measured again against where the audio thread is now
        }
        // On to where it sounds best (the next beat), if that is before the deadline; then measured again.
        if (align) {
            const int64_t want = align(next.samplePosition());
            if (want > next.samplePosition() && want <= deadline) {
                while (next.samplePosition() < want) {
                    const int k = static_cast<int>(std::min<int64_t>(static_cast<int64_t>(workL_.size()), want - next.samplePosition()));
                    next.process(workL_.data(), workR_.data(), k);
                    if (stopped()) return false;
                }
                if (next.samplePosition() < playing_.load(std::memory_order_acquire) + lead) continue;
            }
        }
        const int64_t at = next.samplePosition();
        if (at > deadline) return false;
        outcome_.store(0, std::memory_order_relaxed);
        at_.store(at, std::memory_order_release);
        for (;;) {
            const int o = outcome_.load(std::memory_order_acquire);
            if (o == 1) return true;
            if (o == -1) break;   // missed: the audio thread was past it -- catch up and arm again
            if (stopped()) {
                int64_t expect = at;
                if (at_.compare_exchange_strong(expect, kNone, std::memory_order_acq_rel)) return false;   // taken back in time
                // Taken by the audio thread: the fade runs, and ends.
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

bool Handover::process(Engine& front, Engine& back, float* L, float* R, int n)
{
    int done = 0;
    int64_t at = at_.load(std::memory_order_acquire);
    if (at >= 0) {
        const int64_t s = front.samplePosition();
        int64_t expect = at;
        if (s > at) {
            // Armed where this thread had already been (the worker's view of it was too old): given back as missed.
            if (at_.compare_exchange_strong(expect, kNone, std::memory_order_acq_rel)) outcome_.store(-1, std::memory_order_release);
            at = kNone;
        } else if (s + n > at) {
            if (at_.compare_exchange_strong(expect, kFading, std::memory_order_acq_rel)) {
                const int m = static_cast<int>(at - s);
                if (m > 0) front.process(L, R, m);
                done = m;
                fadePos_ = 0;
                taken_.store(at, std::memory_order_release);
                at = kFading;
            } else {
                at = expect;   // the worker took it back
            }
        }
    }
    if (at == kFading) {
        while (done < n && fadePos_ < kFade) {
            const int k = std::min(n - done, kFade - fadePos_);
            front.process(L + done, R + done, k);
            back.process(fadeL_.data(), fadeR_.data(), k);
            for (int i = 0; i < k; ++i) {
                // A raised cosine from the first engine to the second: both play the same notes, so equal gains.
                const float w = 0.5f - 0.5f * std::cos(3.14159265f * (static_cast<float>(fadePos_ + i) + 0.5f) / static_cast<float>(kFade));
                L[done + i] = L[done + i] * (1.0f - w) + fadeL_[static_cast<size_t>(i)] * w;
                R[done + i] = R[done + i] * (1.0f - w) + fadeR_[static_cast<size_t>(i)] * w;
            }
            done += k;
            fadePos_ += k;
        }
        if (fadePos_ >= kFade) {
            if (done < n) back.process(L + done, R + done, n - done);
            playing_.store(back.samplePosition(), std::memory_order_release);
            at_.store(kNone, std::memory_order_release);
            outcome_.store(1, std::memory_order_release);
            return true;
        }
        playing_.store(front.samplePosition(), std::memory_order_release);
        return false;
    }
    if (done < n) front.process(L + done, R + done, n - done);
    playing_.store(front.samplePosition(), std::memory_order_release);
    return false;
}

double firstDifference(const Score& a, const Score& b, double from)
{
    struct Onset {
        double beat;
        int pitch;
        float velocity;
        bool operator==(const Onset& o) const { return beat == o.beat && pitch == o.pitch && velocity == o.velocity; }
        bool operator<(const Onset& o) const { return beat != o.beat ? beat < o.beat : pitch != o.pitch ? pitch < o.pitch : velocity < o.velocity; }
    };
    std::vector<Onset> x[kNumParts], y[kNumParts];
    auto gather = [from](const Score& s, std::vector<Onset>* out) {
        for (const NoteEvent& n : s.notes) {
            const int part = static_cast<int>(n.part);
            if (n.beat >= from && part >= 0 && part < kNumParts) out[part].push_back({ n.beat, n.pitch, n.velocity });
        }
        for (int part = 0; part < kNumParts; ++part) std::sort(out[part].begin(), out[part].end());
    };
    gather(a, x);
    gather(b, y);
    double first = std::max(a.lengthBeats, b.lengthBeats);
    for (int part = 0; part < kNumParts; ++part) {
        const std::vector<Onset>& u = x[part];
        const std::vector<Onset>& v = y[part];
        size_t i = 0;
        while (i < u.size() && i < v.size() && u[i] == v[i]) ++i;
        if (i < u.size()) first = std::min(first, u[i].beat);
        if (i < v.size()) first = std::min(first, v[i].beat);
    }
    return first;
}

} // namespace parh
