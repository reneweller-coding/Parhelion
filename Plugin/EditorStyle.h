/**
 * @file EditorStyle.h
 * @brief The Style tab (PLAN 9): the profile the knobs describe (compose.style, morphed towards compose.morph_to by
 *        compose.morph) on the six axes that separate the sub-genres (Dok. 2: tempo, drum density, the lead's
 *        archetype, the breakdown's share, the reverb's length, the palette), the five styles marked on each for
 *        comparison; and the references' medians per style (PLAN 13.4, Tools/ref_stats.json), the corridor the profiles
 *        are calibrated against (Tools/calibrate.py).
 * @note The frame (a page that follows the knobs on a timer) after Totality `Plugin/EditorStyle.h` at 4d3c0d2
 *       (29.09.2026); the axes and the table are Parhelion's.
 */
#pragma once
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

/** @brief The Style tab. */
class StylePage final : public juce::Component, private juce::Timer {
public:
    /** @brief The tab for @p p. */
    explicit StylePage(ParhelionProcessor& p);
    /** @brief The profile's numbers and the button on top, the custom style's knobs under them. */
    void resized() override;
    /** @brief The style's profile beside the references' medians. */
    void paint(juce::Graphics& g) override;

private:
    /** @brief Follows the style chosen. */
    void timerCallback() override;
    ParhelionProcessor& proc_;   ///< the processor
    ScrollingPage knobs_;   ///< compose.style, morph_to, morph (the Compose module's page)
    int knobsH_ = 150;      ///< its height: as tall as it needs, where the chart keeps its room
    juce::String shown_;    ///< the profile's numbers as last drawn
};
