/**
 * @file Profile.h
 * @brief What each stage of the engine costs (PLAN 11): with the CMake option PARH_PROFILE, the stages of Deck::render and
 *        of the engine's mixer and master add their wall time to a slot each, and parh_render prints the table. Without
 *        it the scopes compile to nothing.
 * @note Copied from Totality `Core/include/tot/Profile.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#pragma once

namespace parh::prof {

/** @brief The stages timed. */
enum Slot : int { Kick, Sub, Kit, Bass, Poly, Buses, Rooms, TrackBus, Glue, Mixer, MasterTone, Clipper, Limiter, Count };
/** @brief Their names. */
inline const char* const kNames[Count] = { "kick", "sub", "kit (12 lanes)", "bass and 303", "poly (6 voices)", "buses, gates, pump",
                                           "rooms", "track bus", "glue and trim", "mixer", "master tone", "clipper (4x)", "limiter" };

} // namespace parh::prof

#ifdef PARH_PROFILE
#include <chrono>
namespace parh::prof {
/** @brief Nanoseconds spent per slot. */
inline double ns[Count] = {};
/** @brief Adds the time from its construction to its end to a slot. */
struct Scope {
    int slot;
    std::chrono::steady_clock::time_point t0;
    explicit Scope(int s) : slot(s), t0(std::chrono::steady_clock::now()) {}
    ~Scope() { ns[slot] += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count(); }
};
} // namespace parh::prof
#define PARH_PROF_CAT2(a, b) a##b
#define PARH_PROF_CAT(a, b) PARH_PROF_CAT2(a, b)
#define PARH_PROF(slot) const ::parh::prof::Scope PARH_PROF_CAT(parhProf, __LINE__)(::parh::prof::slot)
/** @brief A stage that is not a block of its own: its start, and its end in the same scope. */
#define PARH_PROF_BEGIN(slot) const auto parhProfT_##slot = std::chrono::steady_clock::now()
#define PARH_PROF_END(slot)     (::parh::prof::ns[::parh::prof::slot] += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - parhProfT_##slot).count())
#else
#define PARH_PROF(slot) ((void)0)
#define PARH_PROF_BEGIN(slot) ((void)0)
#define PARH_PROF_END(slot) ((void)0)
#endif
