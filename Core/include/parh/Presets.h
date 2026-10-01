/**
 * @file Presets.h
 * @brief The factory sound presets (Phase 5b, PLAN 12): 1024 for every sound module -- the kick, the sub, a lane of
 *        the kit, the bass, the 303, the six polyphonic voices, the piano, the strings, the choir, the brass, the
 *        timpani, the effects and the granular cloud -- eighteen banks, 18 432 presets.
 *
 * The user (29.09.2026): "Bitte achte auch wieder auf die Presets (wie bei den anderen Synth, möglichst 1024 pro
 * Modul) sowie genügend Modulationsmöglichkeiten in hinreichender Komplexität", and "Die Presets sollen beim Abspielen
 * natürlich auch entsprechend angezeigt werden".
 *
 * **How they are made** (the scheme of Ephemeris, Totality and Phosphene). A bank has sixteen groups, each an eight by
 * eight grid: the rows are the bank's eight adjectives from dark to bright, the columns the group's eight nouns, so every
 * name is two words, unique within its bank, and says where on the grid the sound lies. A group gives each knob it cares
 * about a range and an axis -- the row's ('A', mostly the brightness), the column's ('B', mostly the shape), a seeded
 * draw ('R') or a constant ('C') --, set in the knob's normalised space, so a logarithmic range spreads its eight steps
 * evenly to the ear; a knob can also draw from a list (a filter model, a family of risers). Its modulation recipes each
 * come with a chance and take the next free LFO and slot of the matrix (Modulation.h). The same index is always the
 * same sound. The source is `Tools/presets/bank_spec.py`; `Tools/presets/gen_bank.py` writes PresetBankData.inl.
 *
 * **What they leave alone.** A preset is the sound, not the mix: the level, the pan, the sends, the ducking, the trance
 * gate, the knobs the composer plays (the piano's pedal) and a lane's role and pattern stay where the mix and the
 * composer put them (presetLeaves). Each preset carries a measured trim (PresetTrims.inl: its loudness against the
 * module's default sound), the composer's offset on the level, so a change of sound does not jump in level.
 *
 * **The composer's choice** (unit `sounds`, compose.pick_sounds): a group by its weights in the five styles against the
 * track's profile, cubed (a style's own groups carry it), then a row around the styles' brightness and any column; a
 * lane of the kit only among groups made for its role. The choice goes into
 * the score as a SoundPick and as the preset's knob settings at the track's start (a program change): the knobs show
 * it while it plays, and so does the plugin (Engine::soundsVersion).
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/Params.h"
#include "parh/Preferences.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace parh {

/** @brief One preset: its group (a submenu), its name, and the values it sets (knob index in its module, value). */
struct SoundPreset {
    std::string group;   ///< its group (a submenu)
    std::string name;   ///< its name
    int groupIndex = -1;                                  ///< its group within the bank's sixteen
    float style[5] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };    ///< its group's fit to Uplifting, Progressive, Dream House, Acid, Deep
    uint32_t roles = 0;                                   ///< a kit lane's roles it is made for (bit PercRole), 0: any
    std::vector<std::pair<int, float>> values;            ///< every knob it sets (all but presetLeaves), by index in the module
    float trimDb = 0.0f;                                  ///< its loudness against the default sound, the composer's offset on the level
};

/** @brief The modules with a bank. */
bool hasPresets(Module module);
/** @brief The factory presets of a module (1024; Poly by its instance). Built once per bank, thread-safe; empty without. */
const std::vector<SoundPreset>& factoryPresets(Module module, int instance = 0);
/** @brief Whether a preset leaves knob @p k of @p module alone (the mix's, the composer's, the pattern's). */
bool presetLeaves(Module module, int k);
/**
 * @brief The values a preset gives every knob it does not leave: its own, the defaults for the rest -- the whole sound,
 *        so a preset sounds the same whatever was loaded before.
 */
std::vector<std::pair<int, float>> presetKnobs(Module module, int instance, const SoundPreset& preset);
/** @brief Applies @p preset to instance @p instance of @p module in @p params. */
void applyPreset(ParamStore& params, Module module, int instance, const SoundPreset& preset);
/** @brief The module's knobs that differ from their defaults, as `key=value` lines (a user preset); presetLeaves() knobs left out. */
std::string moduleText(const ParamStore& params, Module module, int instance = 0);
/**
 * @brief A user preset (the plugin's "Save as user preset"): every knob a preset sets, one `knob=value` a line with the
 *        key inside the module ("cutoff=2400"), so that it loads into any instance of the module -- a lane's sound into
 *        another lane. All of them, not only those off their defaults: the kit's lanes have defaults of their own. The
 *        values to nine digits: they read back exactly.
 */
std::string userPresetText(const ParamStore& params, Module module, int instance = 0);
/**
 * @brief The values user preset @p text gives every knob a preset sets, by index in the module: its own, the defaults
 *        for the rest -- the whole sound, as presetKnobs() for a factory preset. Keys the module does not have (another
 *        version's), keys a preset leaves alone and lines without '=' are skipped.
 */
std::vector<std::pair<int, float>> userPresetKnobs(const ParamStore& params, Module module, int instance, std::string_view text);
/**
 * @brief The composer's choice: a preset of @p module (instance @p instance) for a track of style weights @p style (the
 *        five styles, summing to 1), drawn from @p rng; for a kit lane only among presets made for @p role (-1: any).
 * @param prefs the player's ratings (compose.use_ratings): a factor on each group's weight, or null
 * @return its index in factoryPresets(), -1 if the module has none
 */
int pickPreset(Module module, int instance, const float* style, int role, Rng& rng, const Preferences* prefs = nullptr);
/** @brief The brightness a track of style weights @p style chooses its sounds around (0 dark .. 1 bright: the grid's rows). */
float styleBrightness(const float* style);
/** @brief How many keys the bank's table names that no module has (the tests); @p first gets the first. */
int bankUnknownKeys(std::string* first = nullptr);
/** @brief The bank's index of a module (the trims' table), -1 without a bank. */
int bankIndex(Module module, int instance);
/** @brief Banks in all. */
constexpr int kPresetBanks = 18;
/** @brief Presets per bank. */
constexpr int kPresetsPerBank = 1024;

} // namespace parh
