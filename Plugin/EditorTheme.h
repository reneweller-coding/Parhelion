/**
 * @file EditorTheme.h
 * @brief The editor's look: Parhelion's skin of the shared frame (Frame.h), the function families, and how every
 *        module's controls fall into groups.
 *
 * **The skin** (01.10.2026, the GUIs unified): the sky of a parhelion -- a night blue, the ice crystals' silver for the
 * text, the title and the accent, the low sun's gold for the energy and the drops, soft corners and a soft glow, the
 * name in a light face spaced wide, and behind the panel a 22-degree halo with its sun dogs over a frozen land
 * (Resources/backdrop.jpg). The families of function are the same on every page and in every generator, as the
 * colour-coded panels of the machines the styles were made on: gold for the sources (oscillators, engines, levels of a
 * voice), copper for the filters, sage for the envelopes, teal for everything that moves by itself (sweeps, the motion,
 * the echo's time), steel blue for the room and the mix (sends, returns, halls).
 *
 * **The groups.** A module's page is not a grid of every knob of its table but the panel of an instrument: its controls
 * in titled groups, the ones a hand goes to first as large encoders. layoutOf() says which; a parameter it does not name
 * lands in a group "More" at the end, so nothing added to a table is ever lost from the editor.
 *
 * @note After Ephemeris' Plugin/EditorTheme.h at d047d79 (27.09.2026); the look and feel is the frame's since
 *       01.10.2026, the palette and the groups are Parhelion's.
 * @note Copied from Totality `Plugin/EditorTheme.h` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 * @author Rene Weller
 */
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Frame.h"
#include "parh/Params.h"
#include <vector>

namespace parhui {

/** @brief The palette. */
namespace colour {
const juce::Colour Bg        { 0xff070b16 };   ///< the window: the night sky
const juce::Colour Panel     { 0xa00d1222 };   ///< a page (the halo shows faintly through it)
const juce::Colour Group     { 0xff131a2c };   ///< a group box
const juce::Colour Raised    { 0xff1c2439 };   ///< buttons, menus, the knobs' bodies
const juce::Colour Edge      { 0xff2a3452 };   ///< hairlines
const juce::Colour Ink       { 0xffe9eff8 };   ///< text: the ice crystals' light
const juce::Colour Dim       { 0xff8e98b0 };   ///< names, secondary text
const juce::Colour Faint     { 0xff434c68 };   ///< tracks, axes, the off state
const juce::Colour Accent    { 0xffcfe3f4 };   ///< the halo's silver: the title, the tab in front
const juce::Colour Sun       { 0xfff2cf7e };   ///< the low sun: the energy, the drops
const juce::Colour Onset     { 0xffd6553f };   ///< the onsets' red: the playhead, a mute
const juce::Colour Green     { 0xff7fd49a };   ///< a meter in range
const juce::Colour Red       { 0xffe06a5f };   ///< a meter over
} // namespace colour

using Family = frame::FamilyE;   ///< The module families (the frame's).
/**
 * @brief The colour of a family.
 * @param[in] f  the family
 * @return its colour
 */
juce::Colour familyColour(Family f);
/**
 * @brief The colour of deck @p d (A, B, C).
 * @param[in] d  the deck
 * @return its colour
 */
juce::Colour deckColour(int d);
/**
 * @brief Parhelion's skin of the frame: the palette above, the halo behind it, the logo.
 * @return the skin
 */
const frame::Skin& skin();

/**
 * @brief One group of a module's panel: its title, its family and its parameters by key ("*cutoff": a large one,
 *        "~wave": a narrow menu).
 */
struct GroupSpec {
    const char* m_title;   ///< the group's title
    Family m_family;   ///< its family: the colour
    std::vector<const char*> m_keys;   ///< its parameters by key
};
/**
 * @brief The panel of a module: its groups, in order (empty: one group of everything).
 * @param[in] m  the module
 * @return its page's groups
 */
const std::vector<GroupSpec>& layoutOf(parh::ModuleE m);

} // namespace parhui
