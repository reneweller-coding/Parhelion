/**
 * @file PluginEditor.h
 * @brief The plugin's panel (PLAN 10.1): the track or set on top, the instrument in tabs below.
 *
 * Top: style, key, scale, a single track or a DJ mix (a set) and its length, compose, a new seed, play, mute; a row of
 * rerolls (one per unit of the track under the playhead, SetFile.h) and the files; the arrange strip -- a set's tracks on
 * their decks, a track's sections, the layer matrix (every element per 8-bar block, on or filtered) under the energy
 * curve, the playhead, click to jump, the wheel to zoom. Below, the tabs of PLAN 9: Set, Arrange, Low End, Drums, Synths, Keys, Orchestra, Effects, Mixer,
 * Perform, Export, Style; every synth's page with its factory presets (the one the composer chose for the track that
 * plays shown and followed) and its modulation block (Modulation.h). The
 * parameter pages are generated from the parameter tables (EditorTheme.h, layoutOf), so a parameter that exists is on
 * the panel without anyone writing it there.
 *
 * `PARH_SHOT` (a PNG file) and `PARH_TAB` (a tab index) render the panel into a picture after the first score is composed
 * and quit the standalone -- how the layout is checked without a person looking. `PARH_SHOT_SIZE` ("1600x1000") the
 * window's size, `PARH_SHOT_AT` (a beat) jumps there first; with `PARH_PLAY` set, the meters then show that place.
 * `PARH_SHOT_ZOOM` ("from:to", beats) zooms the Arrange tab's view to that window.
 *
 * @note After Ephemeris' Plugin/PluginEditor.h at d047d79 (27.09.2026).
 * @note Copied from Totality `Plugin/PluginEditor.h` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 */
#pragma once
#include "PluginProcessor.h"
#include "EditorTheme.h"
#include "UpdateCheck.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

/**
 * @brief A synth's factory presets (parh/Presets.h): a menu of the 1024 in their sixteen groups, a step back and forth, and
 *        the preset the composer chose for the track that plays -- the menu follows it while you have not chosen another.
 */
class PresetBar final : public juce::Component, private juce::Timer {
public:
    PresetBar(ParhelionProcessor& p, parh::Module m, int instance);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;
    void choose(int index);
    void rebuildMenu();   ///< the sixteen groups, the user's presets, "Save as user preset..."
    void askName();       ///< the name of a user preset, then saves it
    static constexpr int kUserId = 10001, kSaveId = 20000;   ///< menu ids: a user preset, the save item
    ParhelionProcessor& proc_;
    parh::Module module_;
    int instance_;
    int shown_ = -2;   ///< the composer's preset the label shows
    juce::Array<juce::File> user_;   ///< the user presets in the menu
    juce::ComboBox menu_;
    juce::TextButton prev_{ "<" }, next_{ ">" };
    juce::Label composed_;
};

/** @brief The parameters of module instances as knobs, menus and switches, in titled groups. */
class ParamPage final : public juce::Component {
public:
    /**
     * @brief A page for the modules in @p groups.
     * @param p         the processor
     * @param groups    module and instance pairs, shown one after the other
     * @param instances how many instances the modules have; above one, a selector picks the instance
     * @param names     the instances' names for the selector (empty: 1, 2, 3 ...)
     */
    ParamPage(ParhelionProcessor& p, std::vector<std::pair<parh::Module, int>> groups, int instances = 1,
              std::vector<juce::String> names = {});
    void resized() override;                  ///< lays the groups out, flowing across the page
    void paint(juce::Graphics& g) override;   ///< the group boxes and their titles
    /** @brief The height the page needs at @p width (for a page in a viewport). */
    int heightFor(int width) const;

private:
    void build();
    ParhelionProcessor& proc_;
    std::vector<std::pair<parh::Module, int>> groups_;
    int instances_;
    /**
     * @brief The instances as a row of buttons (30.09.2026: behind a menu the six synth voices -- lead, counter, pluck,
     *        arp, pad, stab -- were easy to miss; the user took the strings for the only pads).
     */
    juce::OwnedArray<juce::TextButton> instanceButtons_;
    int selected_ = 0;   ///< the instance shown
    juce::OwnedArray<juce::Component> controls_;
    juce::OwnedArray<juce::Label> labels_;
    /** @brief A control of the page: its name above it, large or not (EditorTheme.h, layoutOf). */
    struct Cell {
        int control = -1;          ///< index into controls_ and labels_
        bool big = false;          ///< a large encoder
        bool narrow = false;       ///< a narrow menu
        int kind = 0;              ///< 0 a knob, 1 a menu, 2 a switch, 4 a preset bar
        juce::Rectangle<int> bounds;
    };
    /** @brief A titled group of cells, drawn as a box. */
    struct Box {
        juce::String title;
        juce::Colour colour;
        std::vector<Cell> cells;
        juce::Rectangle<int> bounds;
    };
    std::vector<Box> boxes_;
    int top() const;   ///< height of the instance bar
    /** @brief Places the boxes and their cells in @p area (@p apply: move the components too); returns the height used. */
    int layoutBoxes(juce::Rectangle<int> area, bool apply);
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliders_;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> combos_;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttons_;
};

/** @brief A component in a viewport: it scrolls when it needs more height than the tab gives. */
class ScrollingPage final : public juce::Component {
public:
    explicit ScrollingPage(std::unique_ptr<ParamPage> page);   ///< takes the page over
    void resized() override;                                   ///< the page as wide as the view, as tall as it needs
    /** @brief How tall the page is at @p width (a page that should not scroll takes that much). */
    int contentHeight(int width) const { return page_->heightFor(width); }
private:
    juce::Viewport view_;
    std::unique_ptr<ParamPage> page_;
};

/**
 * @brief What plays, across its length (PLAN 10.1, Arrange): a set's tracks as bars on their decks (their blends where
 *        two overlap), a track's blocks by their markers, the operations of the form as ticks, and a lane per group of
 *        layers (kick, hats, perc, ping, bass, pads) lit where it has notes; the playhead; a click jumps.
 */
class ArrangeView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    /** @brief Shows @p p's score; @p detailed adds the names of the layers and a larger matrix (the Arrange tab). */
    ArrangeView(ParhelionProcessor& p, bool detailed);
    void paint(juce::Graphics& g) override;                     ///< the tracks or sections, the matrix, the energy, the ruler, the playhead
    void mouseDown(const juce::MouseEvent& e) override;         ///< jumps there (zoomed: on release, if it was no drag)
    void mouseDrag(const juce::MouseEvent& e) override;         ///< moves a zoomed view along
    void mouseUp(const juce::MouseEvent& e) override;           ///< the jump of a zoomed view
    void mouseDoubleClick(const juce::MouseEvent& e) override;  ///< the whole length again
    /** @brief Zooms around the pointer; the wheel sideways, or with Shift, moves along. */
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scale) override;   ///< a trackpad's pinch zooms
    /** @brief The beats shown, @p from to @p to; false while the view shows the whole length. */
    bool zoomed(double& from, double& to) const;
    /** @brief Shows the beats @p from to @p to (once a score is there; the screenshots' PARH_SHOT_ZOOM). */
    void zoomTo(double from, double to) { wanted_ = { from, to }; }
    /** @brief Another view whose zoomed window this one marks (the strip on top marks the Arrange tab's). */
    void setDetail(const ArrangeView* detail) { detail_ = detail; }

private:
    /** @brief A note as the zoomed matrix draws it: where it begins and ends (beats), how loud. */
    struct Hit { double from, to; float velocity; };
    static constexpr double kNarrowest = 16.0;   ///< the least the view shows, in beats (four bars)
    void timerCallback() override;               ///< pages on with the playhead, repaints while it is shown
    void rebuild();                              ///< a copy of a new score, its notes sorted onto the matrix's rows
    void show(double from, double span);         ///< the window, kept inside the length (all of it: not zoomed)
    void zoomAround(float x, double factor);     ///< the window times @p factor, the beat under @p x staying put
    void window(double& from, double& to) const; ///< the beats shown
    double beatAt(float x) const;                ///< the beat under @p x
    ParhelionProcessor& proc_;
    bool detailed_;
    int version_ = -1;                    ///< the score the copy was taken of
    Playing playing_;                     ///< a copy of what plays (its decks carry the layer matrix and the sections)
    std::vector<Hit> hits_[parh::kDecks][parh::kNumLayers];   ///< the notes per deck and row of the matrix, in the order they begin
    double longest_ = 0.0;                ///< the longest note (how far before a window its notes begin)
    double from_ = 0.0, span_ = 0.0;      ///< the window (span 0: the whole length)
    double lastPos_ = -1.0, lastFrom_ = 0.0, lastTo_ = 0.0;   ///< the playhead and the window at the last tick
    float downX_ = 0.0f;                  ///< where a press began
    double downFrom_ = 0.0;               ///< the window's beginning then
    bool dragged_ = false;
    const ArrangeView* detail_ = nullptr;
    std::pair<double, double> wanted_{ 0.0, 0.0 };   ///< zoomTo's window, until a score takes it
};

/**
 * @brief The Arrange tab: the arrangement large, and the rerolls of the track under the playhead -- each unit on its own
 *        stream (SetFile.h), so a reroll changes that and nothing else.
 */
class ArrangePage final : public juce::Component, private juce::Timer {
public:
    explicit ArrangePage(ParhelionProcessor& p);
    void resized() override;
    void paint(juce::Graphics& g) override;
    ArrangeView& view() { return view_; }   ///< the large view (the strip on top marks its window)

private:
    void timerCallback() override;
    ParhelionProcessor& proc_;
    ArrangeView view_;
    juce::Label which_, sounds_;
    juce::OwnedArray<juce::TextButton> rerolls_;
    juce::TextButton track_{ "reroll the whole track" }, set_{ "reroll the mix's plan" };
    juce::String prefix_;   ///< "track3." in a set, empty for a track
};

/** @brief The Export tab (PLAN 10.1): the files, what each holds, the OSC cues' settings. */
class ExportPage final : public juce::Component, private juce::Timer {
public:
    explicit ExportPage(ParhelionProcessor& p);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;
    void exportWith(int extras);
    ParhelionProcessor& proc_;
    juce::TextButton wav_{ "WAV + MIDI + cues" }, stems_{ "... with stems" }, loops_{ "... with DJ loops" }, all_{ "... with both" };
    juce::TextButton save_{ "Save .parhset" }, load_{ "Load .parhset" };
    juce::Label status_;
    std::unique_ptr<ParamPage> cue_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

/**
 * @brief The editor's body: everything, drawn at the design size and scaled to the window (as Phosphene's and
 *        Ephemeris'), so a maximised or full-screen window shows the panel larger rather than emptier.
 */
class EditorBody final : public juce::Component {
public:
    std::function<void(juce::Graphics&)> painter;   ///< draws the background and the logo
    std::function<void()> onResize;                 ///< lays the controls out
    void paint(juce::Graphics& g) override { if (painter) painter(g); }
    void resized() override { if (onResize) onResize(); }
};

/** @brief The logo (Deploy/make_icon.py, drawn as vectors): the moon's dark disc, its rim, the corona in streamers. */
void drawLogo(juce::Graphics& g, juce::Rectangle<float> r);

/** @brief The editor. */
class ParhelionEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ParhelionEditor(ParhelionProcessor& p);           ///< builds the panel for @p p
    ~ParhelionEditor() override;                           ///< stops the refresh timer
    void paint(juce::Graphics& g) override;            ///< the background
    void resized() override;                           ///< scales the body to the window
    void parentHierarchyChanged() override;            ///< the standalone's title bar gets a maximise button
    bool keyPressed(const juce::KeyPress& key) override;   ///< F11: full screen (the standalone), Esc leaves it

private:
    void timerCallback() override;
    void layoutBody();                                 ///< the top bar, the arrange strip, the tabs, at the design scale
    void toggleFullScreen();                           ///< the standalone's window full screen and back
    void showLength(bool mix);                         ///< the length slider for a track's minutes or a mix's
    ParhelionProcessor& proc_;
    parhui::LookAndFeel lnf_;                           ///< first, so it outlives every component that uses it
    juce::TooltipWindow tooltips_{ nullptr, 700 };
    EditorBody body_;
    juce::TextButton full_{ "Full screen" };
    juce::SharedResourcePointer<UpdateCheck> updates_;
    juce::HyperlinkButton update_;
    juce::ToggleButton checkUpdates_{ "Update check" };
    juce::Label title_, status_, rerolls_;
    juce::Rectangle<float> logo_;
    juce::ComboBox style_, key_, scale_;
    /** One track or a DJ mix (set.minutes 0 or not, ParhelionProcessor::chooseMix), and the length of what is chosen. */
    juce::TextButton trackMode_{ "Track" }, mixMode_{ "DJ mix" };
    juce::Slider length_;
    juce::Label lengthLabel_;
    bool lengthOfMix_ = false;                         ///< the slider shows set.minutes (else compose.minutes)
    bool syncing_ = false;                             ///< the slider is set from its parameter, not by a hand
    juce::TextButton compose_{ "Compose track" }, seed_{ "New seed" }, play_{ "Play" }, mute_{ "Mute" };
    juce::TextButton like_{ "+" }, dislike_{ "-" };   ///< Phase 17: the ratings
    juce::String rated_;                             ///< what was rated last, shown a while
    int ratedTicks_ = 0;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> combos_;
    ArrangeView arrange_;
    juce::TabbedComponent tabs_{ juce::TabbedButtonBar::TabsAtTop };
    juce::String shotPath_;
    int shotTicks_ = 0;
};
