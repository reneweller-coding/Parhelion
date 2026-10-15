/**
 * @file Preferences.h
 * @brief Phase 17: what the listener liked -- ratings of tracks and the weights the composer draws with from them.
 *
 * A rating is a track's form template (Planner.h), style, seed and the groups of the presets the composer chose for it,
 * with +1 or -1. The preferences are a factor per form and per preset group: 1 + 0.25 per like - 0.25 per dislike,
 * within 0.25 .. 3. With compose.use_ratings the composer multiplies the style's form weights (planTrack) and the
 * groups' fits (pickPreset) by them, so the liked kinds and sounds come more often and the disliked ones seldom; none is
 * ever ruled out or forced. (Totality rated its archetypes; Parhelion's kinds are its form templates.) The ratings live in a file of the player's (the plugin: Documents/Parhelion/ratings.tsv, one
 * rating a line); a saved set does not carry them, so it plays back the same only with use_ratings off.
 * @note Copied from Totality `Core/include/tot/Preferences.h` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 * @author Rene Weller
 */
#pragma once
#include <map>
#include <string>
#include <vector>

namespace parh
{

/** @brief One rating of a track. */
struct Rating
{
    int                      m_value = 1;   ///< +1 liked, -1 not
    int                      m_form = 0;    ///< its FormTemplate
    std::string              m_style;       ///< its profile's name
    unsigned long long       m_seed = 0;    ///< its seed
    std::vector<std::string> m_groups;      ///< the groups of its presets ("Anthem Supersaw", "Dream Haze" ...)
};

/** @brief The weights the ratings give. */
struct Preferences
{
    float m_form[5] = { 1, 1, 1, 1, 1 };     ///< a factor per FormTemplate (Anthem, Dream, Acid, Plateau, Drift)
    std::map<std::string, float> m_groups;   ///< a factor per preset group (1 where absent)
    /**
     * @brief The factor of preset group @p name.
     * @param[in] name  the preset group
     * @return its factor
     */
    float group( const std::string &name ) const
    {
        const auto it = m_groups.find( name );
        return it == m_groups.end() ? 1.0f : it->second;
    }
};

/**
 * @brief The preferences @p ratings give (Preferences.h).
 * @param[in] ratings  the ratings
 * @return the weights
 */
Preferences preferencesFrom( const std::vector<Rating> &ratings );

/**
 * @brief Reads the ratings from @p path (none if it does not exist).
 * @param[in] path  the ratings file
 * @return the ratings, empty when the file is missing
 */
std::vector<Rating> loadRatings( const std::string &path );

/**
 * @brief Appends @p r to the ratings in @p path.
 * @param[in] path  the ratings file
 * @param[in] r     the rating
 * @return false when the file cannot be written
 */
bool appendRating( const std::string &path, const Rating &r );

}   // namespace parh
