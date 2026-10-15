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
    m_maxBlock = std::max(1, maxBlock);
    m_fadeL.assign(static_cast<size_t>(Fade), 0.0f);
    m_fadeR.assign(static_cast<size_t>(Fade), 0.0f);
    m_workL.assign(static_cast<size_t>(std::max(m_maxBlock, 1024)), 0.0f);
    m_workR.assign(m_workL.size(), 0.0f);
}

bool Handover::handOver(Engine& next, int64_t deadline, const std::function<bool()>& stop, const std::function<int64_t(int64_t)>& align)
{
    const double fs = next.sampleRate();
    // Ahead of the audio thread when armed: two of its blocks and 50 ms at least (its position is up to a block old);
    // each round of catching up renders a quarter second past where it was.
    const int64_t lead = std::max<int64_t>(2 * static_cast<int64_t>(m_maxBlock), static_cast<int64_t>(0.05 * fs));
    const int64_t reach = lead + static_cast<int64_t>(0.25 * fs);
    const auto stopped = [&] { return stop && stop(); };
    for (;;) {
        if (stopped()) return false;
        const int64_t p = m_playing.load(std::memory_order_acquire);
        if (p + lead > deadline) return false;   // too late: the old engine is about to play what the new one changes
        if (next.samplePosition() < p + lead) {
            const int64_t target = p + reach;
            while (next.samplePosition() < target) {
                const int k = static_cast<int>(std::min<int64_t>(static_cast<int64_t>(m_workL.size()), target - next.samplePosition()));
                next.process(m_workL.data(), m_workR.data(), k);
                if (stopped()) return false;
            }
            continue;   // measured again against where the audio thread is now
        }
        // On to where it sounds best (the next beat), if that is before the deadline; then measured again.
        if (align) {
            const int64_t want = align(next.samplePosition());
            if (want > next.samplePosition() && want <= deadline) {
                while (next.samplePosition() < want) {
                    const int k = static_cast<int>(std::min<int64_t>(static_cast<int64_t>(m_workL.size()), want - next.samplePosition()));
                    next.process(m_workL.data(), m_workR.data(), k);
                    if (stopped()) return false;
                }
                if (next.samplePosition() < m_playing.load(std::memory_order_acquire) + lead) continue;
            }
        }
        const int64_t at = next.samplePosition();
        if (at > deadline) return false;
        m_outcome.store(0, std::memory_order_relaxed);
        m_at.store(at, std::memory_order_release);
        for (;;) {
            const int o = m_outcome.load(std::memory_order_acquire);
            if (o == 1) return true;
            if (o == -1) break;   // missed: the audio thread was past it -- catch up and arm again
            if (stopped()) {
                int64_t expect = at;
                if (m_at.compare_exchange_strong(expect, None, std::memory_order_acq_rel)) return false;   // taken back in time
                // Taken by the audio thread: the fade runs, and ends.
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

bool Handover::process(Engine& front, Engine& back, float* L, float* R, int n)
{
    int done = 0;
    int64_t at = m_at.load(std::memory_order_acquire);
    if (at >= 0) {
        const int64_t s = front.samplePosition();
        int64_t expect = at;
        if (s > at) {
            // Armed where this thread had already been (the worker's view of it was too old): given back as missed.
            if (m_at.compare_exchange_strong(expect, None, std::memory_order_acq_rel)) m_outcome.store(-1, std::memory_order_release);
            at = None;
        } else if (s + n > at) {
            if (m_at.compare_exchange_strong(expect, Fading, std::memory_order_acq_rel)) {
                const int m = static_cast<int>(at - s);
                if (m > 0) front.process(L, R, m);
                done = m;
                m_fadePos = 0;
                m_taken.store(at, std::memory_order_release);
                at = Fading;
            } else {
                at = expect;   // the worker took it back
            }
        }
    }
    if (at == Fading) {
        while (done < n && m_fadePos < Fade) {
            const int k = std::min(n - done, Fade - m_fadePos);
            front.process(L + done, R + done, k);
            back.process(m_fadeL.data(), m_fadeR.data(), k);
            for (int i = 0; i < k; ++i) {
                // A raised cosine from the first engine to the second: both play the same notes, so equal gains.
                const float w = 0.5f - 0.5f * std::cos(3.14159265f * (static_cast<float>(m_fadePos + i) + 0.5f) / static_cast<float>(Fade));
                L[done + i] = L[done + i] * (1.0f - w) + m_fadeL[static_cast<size_t>(i)] * w;
                R[done + i] = R[done + i] * (1.0f - w) + m_fadeR[static_cast<size_t>(i)] * w;
            }
            done += k;
            m_fadePos += k;
        }
        if (m_fadePos >= Fade) {
            if (done < n) back.process(L + done, R + done, n - done);
            m_playing.store(back.samplePosition(), std::memory_order_release);
            m_at.store(None, std::memory_order_release);
            m_outcome.store(1, std::memory_order_release);
            return true;
        }
        m_playing.store(front.samplePosition(), std::memory_order_release);
        return false;
    }
    if (done < n) front.process(L + done, R + done, n - done);
    m_playing.store(front.samplePosition(), std::memory_order_release);
    return false;
}

double firstDifference(const Score& a, const Score& b, double from)
{
    struct Onset {
        double m_beat;
        int m_pitch;
        float m_velocity;
        bool operator==(const Onset& o) const { return m_beat == o.m_beat && m_pitch == o.m_pitch && m_velocity == o.m_velocity; }
        bool operator<(const Onset& o) const { return m_beat != o.m_beat ? m_beat < o.m_beat : m_pitch != o.m_pitch ? m_pitch < o.m_pitch : m_velocity < o.m_velocity; }
    };
    std::vector<Onset> x[NumParts], y[NumParts];
    auto gather = [from](const Score& s, std::vector<Onset>* out) {
        for (const NoteEvent& n : s.m_notes) {
            const int part = static_cast<int>(n.m_part);
            if (n.m_beat >= from && part >= 0 && part < NumParts) out[part].push_back({ n.m_beat, n.m_pitch, n.m_velocity });
        }
        for (int part = 0; part < NumParts; ++part) std::sort(out[part].begin(), out[part].end());
    };
    gather(a, x);
    gather(b, y);
    double first = std::max(a.m_lengthBeats, b.m_lengthBeats);
    for (int part = 0; part < NumParts; ++part) {
        const std::vector<Onset>& u = x[part];
        const std::vector<Onset>& v = y[part];
        size_t i = 0;
        while (i < u.size() && i < v.size() && u[i] == v[i]) ++i;
        if (i < u.size()) first = std::min(first, u[i].m_beat);
        if (i < v.size()) first = std::min(first, v[i].m_beat);
    }
    return first;
}

} // namespace parh
