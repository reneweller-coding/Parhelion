/**
 * @file EditorTheme.h
 * @brief The editor's look: one palette, the function families, the knob, and how every module's controls fall into
 *        groups.
 *
 * **The palette** is the logo's (Deploy/make_icon.py): the night-blue sky of a parhelion, the sun's warm light for the
 * text and the accent, and the onsets' red. The families of function are the same on every page, as the colour-coded panels of the machines the style
 * was made on: the corona's gold for the sources (oscillators, engines, levels of a voice), copper for the filters, sage
 * for the envelopes, teal for everything that moves by itself (sweeps, the motion, the echo's time), steel blue for the
 * room and the mix (sends, returns, halls).
 *
 * **The groups.** A module's page is not a grid of every knob of its table but the panel of an instrument: its controls
 * in titled groups, the ones a hand goes to first as large encoders. layoutOf() says which; a parameter it does not name
 * lands in a group "More" at the end, so nothing added to a table is ever lost from the editor.
 *
 * @note After Ephemeris' Plugin/EditorTheme.h at d047d79 (27.09.2026); the look and feel is its, the palette and the
 *       groups are Parhelion's.
 * @note Copied from Totality `Plugin/EditorTheme.h` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 */
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "parh/Params.h"
#include <vector>

namespace parhui {

/** @brief The palette. */
namespace colour {
const juce::Colour bg        { 0xff0a0c16 };   ///< the window: the night sky
const juce::Colour panel     { 0xff10131f };   ///< a page
const juce::Colour group     { 0xff161a29 };   ///< a group box
const juce::Colour raised    { 0xff1f2334 };   ///< buttons, menus, the knobs' bodies
const juce::Colour edge      { 0xff2b3044 };   ///< hairlines
const juce::Colour ink       { 0xffeee4d0 };   ///< text: the sun's light
const juce::Colour dim       { 0xff8f8f98 };   ///< names, secondary text
const juce::Colour faint     { 0xff464853 };   ///< tracks, axes, the off state
const juce::Colour amber     { 0xffeed6a8 };   ///< the sun: the accent
const juce::Colour onset     { 0xffd6553f };   ///< the onsets' red: the playhead, a mute
const juce::Colour green     { 0xff7fd49a };   ///< a meter in range
const juce::Colour red       { 0xffe06a5f };   ///< a meter over
} // namespace colour

/** @brief The families of function. */
enum class Family { Source, Filter, Envelope, Motion, Space };
/** @brief The colour of a family. */
juce::Colour familyColour(Family f);
/** @brief The colour of deck @p d (A, B, C). */
juce::Colour deckColour(int d);

/**
 * @brief One group of a module's panel: its title, its family and its parameters by key ("*cutoff": a large one,
 *        "~wave": a narrow menu).
 */
struct GroupSpec {
    const char* title;
    Family family;
    std::vector<const char*> keys;
};
/** @brief The panel of a module: its groups, in order (empty: one group of everything). */
const std::vector<GroupSpec>& layoutOf(parh::Module m);

/** @brief The editor's look and feel: the knob, the switches, the menus, the tabs, the buttons. */
class LookAndFeel final : public juce::LookAndFeel_V4 {
public:
    LookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float pos, float startAngle, float endAngle,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;
    juce::Font getTabButtonFont(juce::TabBarButton&, float height) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
};

} // namespace parhui
