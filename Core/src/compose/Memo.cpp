/**
 * @file Memo.cpp
 * @brief The memorisation gate (Memo.h); the hash is the one of Tools/corpus/memorisation.py (FNV-1a 64 over the bytes
 *        onset, interval + 128).
 */
#include "parh/compose/Memo.h"
#include <algorithm>

namespace parh {

bool memoWindowHash(const std::vector<int>& top, int start, uint64_t& hash)
{
    int notes = 0, distinct = 0;
    int seen[kMemoMinPitches] = { -1, -1, -1 };
    const int end = std::min(start + 32, static_cast<int>(top.size()));
    for (int k = std::max(0, start); k < end; ++k) {
        if (top[static_cast<size_t>(k)] < 0) continue;
        ++notes;
        if (distinct < kMemoMinPitches && std::find(seen, seen + distinct, top[static_cast<size_t>(k)]) == seen + distinct)
            seen[distinct++] = top[static_cast<size_t>(k)];
    }
    if (notes < kMemoMinNotes || distinct < kMemoMinPitches) return false;
    uint64_t h = 0xCBF29CE484222325ull;
    auto mix = [&](int byte) { h ^= static_cast<uint64_t>(byte & 0xFF); h *= 0x100000001B3ull; };
    int prev = -1;
    for (int k = std::max(0, start); k < end; ++k) {
        const int p = top[static_cast<size_t>(k)];
        if (p < 0) continue;
        const int iv = prev < 0 ? 0 : std::clamp(p - prev, -127, 127);
        mix(k - start);
        mix(iv + 128);
        prev = p;
    }
    hash = h;
    return true;
}

bool memoContains(uint64_t hash)
{
    const uint64_t h1 = hash & 0xFFFFFFFFull, h2 = (hash >> 32) | 1ull;
    const uint64_t mask = static_cast<uint64_t>(kMemoBloomBits) - 1;
    for (int i = 0; i < kMemoBloomK; ++i) {
        const uint64_t b = (h1 + static_cast<uint64_t>(i) * h2) & mask;
        if (!(kMemoBloom[b >> 6] >> (b & 63) & 1ull)) return false;
    }
    return true;
}

bool memoHitsAnyBar(const std::vector<int>& top)
{
    for (int start = 0; start < static_cast<int>(top.size()); start += 16) {
        uint64_t h = 0;
        if (memoWindowHash(top, start, h) && memoContains(h)) return true;
    }
    return false;
}

} // namespace parh
