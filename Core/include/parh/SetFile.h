/**
 * @file SetFile.h
 * @brief Curation and the `.parhset` file (PLAN 7.8, 9): a track or a set as seed, settings and rerolls.
 *
 * The composer writes every part of a track on its own stream (Composer.h): the form, the harmony, the drums, the bass,
 * the chords, the melody, the effects, the sounds, the mix; the set composer its order, tempo and blends (Set.h).
 * **Rerolling** one of them draws it again and leaves every other bit for bit as it was -- the user curates instead of
 * programming. A reroll is a counter per unit that moves the unit's stream (`form`, `harmony`, `drums`, `bass`, `chords`,
 * `melody`, `fx`, `sounds`, `mix`); in a set the same prefixed with the track (`track3.melody`) plus `set` for the
 * order, the styles and the keys. What is not rerolled is locked.
 *
 * The `.parhset` is text in the style of Ephemeris' `.ephset`:
 * @code
 *   # Parhelion set
 *   seed=11
 *   minutes=7
 *   set=120
 *   reroll track3.events=1
 *   param compose.style=Ostgut
 * @endcode
 * Only the parameters that differ from their defaults are written.
 *
 * @note Copied from Ephemeris `Core/include/eph/SetFile.h` (namespace eph, prefix EPH_) at d047d79 (27.09.2026).
 * @note Copied from Totality `Core/include/tot/SetFile.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace parh {

class ParamStore;

/** @brief The rerolls of a track or set: unit name -> how often it was drawn again. */
struct Curation {
    std::map<std::string, int> rerolls;   ///< unit name -> reroll count
    /** @brief The counter of @p unit (0 if never rerolled). */
    int count(const std::string& unit) const { const auto it = rerolls.find(unit); return it == rerolls.end() ? 0 : it->second; }
    /** @brief Draws @p unit once more. */
    void reroll(const std::string& unit) { ++rerolls[unit]; }
};

/** @brief Everything a set file holds. */
struct SetFile {
    uint64_t seed = 1;       ///< the seed of the track or set
    double minutes = 0.0;    ///< track length (0: compose.minutes)
    double set = 0.0;        ///< set length in minutes (0: one track)
    Curation curation;       ///< the rerolls
    std::string params;      ///< the parameters that differ from their defaults, as ParamStore text
};

/** @brief Writes a set; @p params' changed values are taken as they are now. */
bool saveSet(const char* path, const SetFile& set, const ParamStore& params);
/** @brief Reads a set and applies its parameters to @p params; @p error says why it failed. */
bool loadSet(const char* path, SetFile& set, ParamStore& params, std::string* error = nullptr);

} // namespace parh
