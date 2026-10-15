/**
 * @file PluginEditor.h
 * @brief The plugin's panel (PLAN 10.1): the track or set on top, the instrument in tabs below.
 *
 * Top, the shared frame's header (Frame.h, 01.10.2026): style, key, scale, a single track or a DJ mix (a set) and its
 * length, compose, a new seed, play, mute; the ratings, the status and the rerolls, then undo, redo, help and the
 * settings; the arrange strip -- a set's tracks on their decks, a track's sections, the layer matrix (every element per 8-bar block, on or filtered) under the energy
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
 * @author Rene Weller
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
    /**
     * @brief The presets of instance @p instance of module @p m, chosen through @p p.
     * @param[in,out] p         the processor
     * @param[in]     m         the module
     * @param[in]     instance  its instance
     */
    PresetBar(ParhelionProcessor& p, parh::ModuleE m, int instance);
    /** @brief The step buttons either side of the menu, the composed preset under them. */
    void resized() override;
    /**
     * @brief Nothing of its own: its children draw.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint(juce::Graphics& g) override;

private:
    /** @brief Follows the composer's choice while no other preset was chosen by hand. */
    void timerCallback() override;
    /**
     * @brief Applies preset @p index (an undo step) and shows it.
     * @param[in] index  the preset's index
     */
    void choose(int index);
    void rebuildMenu();   ///< the sixteen groups, the user's presets, "Save as user preset..."
    void askName();       ///< the name of a user preset, then saves it
public:
    /**
     * @brief Shows menu item @p menuId as chosen (a factory preset's index + 1, or UserId + one of the player's).
     * @param[in] menuId  the menu id
     */
    void chosen(int menuId);
    static constexpr int UserId = 10001;   ///< menu ids: the first user preset
    static constexpr int SaveId = 20000;   ///< menu ids: the save item
private:
    ParhelionProcessor& m_proc;   ///< the processor: its presets and its undo
    parh::ModuleE m_module;   ///< the synth module
    int m_instance;   ///< its instance
    int m_shown = -2;   ///< the composer's preset the label shows
    juce::Array<juce::File> m_user;   ///< the user presets in the menu
    int m_loaded = -1;   ///< the menu id of the preset the knobs came from (-1: none known)
    std::vector<std::pair<int, float>> m_loadedKnobs;   ///< the knobs that preset put (store id, real value): the star compares
    /** @brief Opens the preset browser on this synth's presets (15.10.2026). */
    frame::IconButton m_browse{ frame::IconButton::IconE::SEARCH, "Browse the presets: by name, group, favourites, recently played; hold a row's triangle to listen" };
    /** @brief Saves the sound as a user preset. */
    frame::IconButton m_save{ frame::IconButton::IconE::SAVE, "Save this sound as a user preset (Documents/Parhelion/Presets)" };
    juce::Label m_star;   ///< "*" while the knobs have moved from the preset
    juce::ComboBox m_menu;   ///< the presets in their groups
    juce::TextButton m_prev{ "<" };   ///< the preset before
    juce::TextButton m_next{ ">" };   ///< the preset after
    juce::Label m_composed;   ///< the preset the composer chose
};

/**
 * @brief The parameters of module instances as knobs, menus and switches, in titled groups: every control with the
 *        frame's right-click menu (MIDI learn, forget, default), a double click to its default, and the live ring.
 */
class ParamPage final : public juce::Component, public frame::Revealer {
public:
    /**
     * @brief A page for the modules in @p groups.
     * @param[in,out] p          the processor
     * @param[in]     groups     module and instance pairs, shown one after the other
     * @param[in]     instances  how many instances the modules have; above one, a selector picks the instance
     * @param[in]     names      the instances' names for the selector (empty: 1, 2, 3 ...)
     */
    ParamPage(ParhelionProcessor& p, std::vector<std::pair<parh::ModuleE, int>> groups, int instances = 1,
              std::vector<juce::String> names = {});
    void resized() override;                  ///< lays the groups out, flowing across the page
    /**
     * the group boxes and their titles
     * @param[in,out] g  the graphics context to draw into
     */
    void paint(juce::Graphics& g) override;
    /**
     * @brief The height the page needs at @p width (for a page in a viewport).
     * @param[in] width  the page's width
     * @return the height it needs
     */
    int heightFor(int width) const;
    /**
     * @brief The height the page has in its window (the scrolling page around it says so before heightFor).
     * @param[in] h  the height in pixels
     */
    void setAvailableHeight(int h) { m_available = h; }
    /**
     * @brief The page's parameters in words, group by group (the help's topic for the tab).
     * @return the description
     */
    juce::String describe() const;
    /**
     * @brief The instance shown (the lane of the kit, the synth voice), 0 for a page of one.
     * @return the instance shown, 0 for a page of one
     */
    int instance() const { return m_instances > 1 ? m_selected : 0; }
    /**
     * @brief Shows the section holding @p part (the search found it there).
     * @param[in,out] part  the control to bring into view
     */
    void reveal(juce::Component& part) override;
    /**
     * @brief Shows the instance whose controls hold store id @p id (a lane, a voice); false when none of them does.
     * @param[in] id  the store id
     * @return false when no instance holds it
     */
    bool bringId(int id) override;

private:
    /** @brief Makes the controls of every group (once, in the constructor). */
    void build();
    ParhelionProcessor& m_proc;   ///< the processor: the parameters, the undo, MIDI learn
    frame::ControlActions m_actions;               ///< the controls' right-click menu
    frame::LiveRings m_live;                       ///< where each knob's value plays
    /** @brief The modulation at the knob (15.10.2026): the voice's matrix rows on its knobs, as arcs and in their menus. */
    std::unique_ptr<frame::ModulationAtKnob> m_modulation;
    std::vector<std::pair<parh::ModuleE, int>> m_groups;   ///< the module instances shown, in order
    int m_instances;   ///< how many instances the modules have
    /**
     * @brief The instances as a row of buttons (30.09.2026: behind a menu the six synth voices -- lead, counter, pluck,
     *        arp, pad, stab -- were easy to miss; the user took the strings for the only pads).
     */
    juce::OwnedArray<juce::TextButton> m_instanceButtons;
    int m_selected = 0;   ///< the instance shown
    juce::OwnedArray<juce::Component> m_controls;   ///< the knobs, menus, switches and preset bars
    juce::OwnedArray<juce::Label> m_labels;   ///< the names above them
    /** @brief A control of the page: its name above it, large or not (EditorTheme.h, layoutOf). */
    struct Cell {
        int m_control = -1;          ///< index into m_controls and m_labels
        bool m_big = false;          ///< a large encoder
        bool m_narrow = false;       ///< a narrow menu
        int m_kind = 0;              ///< 0 a knob, 1 a menu, 2 a switch, 4 a preset bar
        juce::Rectangle<int> m_bounds;   ///< where it sits on the page
    };
    /** @brief A titled group of cells, drawn as a box. */
    struct Box {
        juce::String m_title;   ///< the group's title
        juce::Colour m_colour;   ///< the module family's colour
        std::vector<Cell> m_cells;   ///< its controls
        juce::Rectangle<int> m_bounds;   ///< where the box sits on the page
    };
    std::vector<Box> m_boxes;   ///< the groups, in order
    /**
     * @brief A page taller than its window shows its groups in sections, one at a time (01.10.2026, frame::planSections):
     *        the sound and the modulation apart, each cut where the window ends.
     */
    frame::SectionSwitch m_section{ parhui::skin() };
    bool m_split = false;                          ///< more than one section
    std::vector<int> m_sectionOf;                  ///< per box its section
    const std::vector<int>* m_measuring = nullptr; ///< while planning: the boxes being measured
    int m_available = 0;   ///< the height of the window (setAvailableHeight)
    int m_plannedWidth = -1;   ///< the width the sections were planned for, -1 never
    int m_plannedAvailable = -1;   ///< the height the sections were planned for, -1 never
    /**
     * the box is in the section shown (or being measured)
     * @param[in] box  the box's index
     * @return true when it is in the section shown
     */
    bool shown(size_t box) const;
    /**
     * the sections of a page @p width wide in m_available
     * @param[in] width  the page's width
     */
    void plan(int width);
    void applySection();                          ///< the controls of the section shown, the others hidden
    void refit();                                 ///< laid out again, and the scrolling page around it measures again
    /**
     * height of the instance bar
     * @return the height of the instance bar
     */
    int top() const;
    /**
     * @brief Places the boxes and their cells in @p area (@p apply: move the components too); returns the height used.
     * @param[in] area   where the boxes go
     * @param[in] apply  true to move the components too
     * @return the height used
     */
    int layoutBoxes(juce::Rectangle<int> area, bool apply);
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> m_sliders;   ///< the knobs on their parameters
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> m_combos;   ///< the menus on their parameters
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> m_buttons;   ///< the switches on their parameters
};

/** @brief A component in a viewport: it scrolls when it needs more height than the tab gives. */
class ScrollingPage final : public juce::Component {
public:
    /**
     * takes the page over
     * @param[in] page  the page, taken over
     */
    explicit ScrollingPage(std::unique_ptr<ParamPage> page);
    void resized() override;                                   ///< the page as wide as the view, as tall as it needs
    /**
     * @brief How tall the page is at @p width (a page that should not scroll takes that much).
     * @param[in] width  the width in pixels
     * @return the height in pixels
     */
    int contentHeight(int width) const { return m_page->heightFor(width); }
    /**
     * the page (the help describes it)
     * @return the page
     */
    const ParamPage& page() const { return *m_page; }
private:
    juce::Viewport m_view;   ///< scrolls the page
    std::unique_ptr<ParamPage> m_page;   ///< the page
};

/**
 * @brief What plays, across its length (PLAN 10.1, Arrange): a set's tracks as bars on their decks (their blends where
 *        two overlap), a track's blocks by their markers, the operations of the form as ticks, and a lane per group of
 *        layers (kick, hats, perc, ping, bass, pads) lit where it has notes; the playhead; a click jumps.
 */
class ArrangeView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    /**
     * @brief Shows @p p's score; @p detailed adds the names of the layers and a larger matrix (the Arrange tab).
     * @param[in,out] p         the processor
     * @param[in]     detailed  true for the Arrange tab's view
     */
    ArrangeView(ParhelionProcessor& p, bool detailed);
    /**
     * the tracks or sections, the matrix, the energy, the ruler, the playhead
     * @param[in,out] g  the graphics context to draw into
     */
    void paint(juce::Graphics& g) override;
    /**
     * jumps there (zoomed: on release, if it was no drag)
     * @param[in] e  the mouse event
     */
    void mouseDown(const juce::MouseEvent& e) override;
    /**
     * moves a zoomed view along
     * @param[in] e  the mouse event
     */
    void mouseDrag(const juce::MouseEvent& e) override;
    /**
     * the jump of a zoomed view
     * @param[in] e  the mouse event
     */
    void mouseUp(const juce::MouseEvent& e) override;
    /**
     * the whole length again
     * @param[in] e  the mouse event
     */
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    /**
     * @brief Zooms around the pointer; the wheel sideways, or with Shift, moves along.
     * @param[in] e      the mouse event
     * @param[in] wheel  the wheel's movement
     */
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    /**
     * a trackpad's pinch zooms
     * @param[in] e      the mouse event
     * @param[in] scale  the pinch's factor
     */
    void mouseMagnify(const juce::MouseEvent& e, float scale) override;
    /**
     * @brief The beats shown, @p from to @p to; false while the view shows the whole length.
     * @param[out] from  receives the first beat shown
     * @param[out] to    receives the last beat shown
     * @return false while the whole length is shown
     */
    bool zoomed(double& from, double& to) const;
    /**
     * @brief Shows the beats @p from to @p to (once a score is there; the screenshots' PARH_SHOT_ZOOM).
     * @param[in] from  the first beat to show
     * @param[in] to    the last beat to show
     */
    void zoomTo(double from, double to) { m_wanted = { from, to }; }
    /**
     * @brief Another view whose zoomed window this one marks (the strip on top marks the Arrange tab's).
     * @param[in] detail  the view whose window this one marks, or null
     */
    void setDetail(const ArrangeView* detail) { m_detail = detail; }

private:
    /** @brief A note as the zoomed matrix draws it: where it begins and ends (beats), how loud. */
    struct Hit {
        double m_from;      ///< where it begins, beats
        double m_to;        ///< where it ends, beats
        float m_velocity;   ///< how loud, 0..1
    };
    static constexpr double Narrowest = 16.0;   ///< the least the view shows, in beats (four bars)
    void timerCallback() override;               ///< pages on with the playhead, repaints while it is shown
    void rebuild();                              ///< a copy of a new score, its notes sorted onto the matrix's rows
    /**
     * the window, kept inside the length (all of it: not zoomed)
     * @param[in] from  the first beat
     * @param[in] span  how many beats
     */
    void show(double from, double span);
    /**
     * the window times @p factor, the beat under @p x staying put
     * @param[in] x       the pointer's position
     * @param[in] factor  the zoom factor
     */
    void zoomAround(float x, double factor);
    /**
     * the beats shown
     * @param[out] from  receives the first beat shown
     * @param[out] to    receives the last beat shown
     */
    void window(double& from, double& to) const;
    /**
     * the beat under @p x
     * @param[in] x  the position in pixels
     * @return the beat there
     */
    double beatAt(float x) const;
    ParhelionProcessor& m_proc;   ///< the processor: what plays, where it is
    bool m_detailed;   ///< the Arrange tab's view: the operations' names, a larger matrix
    int m_version = -1;                    ///< the score the copy was taken of
    Playing m_playing;                     ///< a copy of what plays (its decks carry the layer matrix and the sections)
    std::vector<Hit> m_hits[parh::Decks][parh::NumLayers];   ///< the notes per deck and row of the matrix, in the order they begin
    double m_longest = 0.0;                ///< the longest note (how far before a window its notes begin)
    double m_from = 0.0;   ///< the first beat of the window
    double m_span = 0.0;   ///< its length in beats (0: the whole length)
    double m_lastPos = -1.0;   ///< the playhead at the last tick
    double m_lastFrom = 0.0;   ///< the window's first beat at the last tick
    double m_lastTo = 0.0;   ///< the window's last beat at the last tick
    float m_downX = 0.0f;                  ///< where a press began
    double m_downFrom = 0.0;               ///< the window's beginning then
    bool m_dragged = false;   ///< the press has moved the view: no jump on release
    const ArrangeView* m_detail = nullptr;   ///< the view whose window this one marks, or null
    std::pair<double, double> m_wanted{ 0.0, 0.0 };   ///< zoomTo's window, until a score takes it
};

/**
 * @brief The Arrange tab: the arrangement large, and the rerolls of the track under the playhead -- each unit on its own
 *        stream (SetFile.h), so a reroll changes that and nothing else.
 */
class ArrangePage final : public juce::Component, private juce::Timer {
public:
    /**
     * @brief The tab for @p p.
     * @param[in,out] p  the processor
     */
    explicit ArrangePage(ParhelionProcessor& p);
    /** @brief The view large, the rerolls under it. */
    void resized() override;
    /**
     * @brief The background and the headings.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint(juce::Graphics& g) override;
    /**
     * the large view (the strip on top marks its window)
     * @return the large view
     */
    ArrangeView& view() { return m_view; }

private:
    /** @brief Shows the track under the playhead and its rerolls. */
    void timerCallback() override;
    ParhelionProcessor& m_proc;   ///< the processor: what plays, the rerolls
    ArrangeView m_view;   ///< the arrangement, large
    juce::Label m_which;   ///< which track is under the playhead
    juce::Label m_sounds;   ///< its sounds (the presets and the kit)
    juce::OwnedArray<juce::TextButton> m_rerolls;   ///< a reroll per unit of the track
    juce::TextButton m_track{ "reroll the whole track" };   ///< rerolls the whole track
    juce::TextButton m_set{ "reroll the mix's plan" };   ///< rerolls the mix's plan
    juce::String m_prefix;   ///< "track3." in a set, empty for a track
};

/** @brief The Export tab (PLAN 10.1): the files, what each holds, the OSC cues' settings. */
class ExportPage final : public juce::Component, private juce::Timer {
public:
    /**
     * @brief The tab for @p p.
     * @param[in,out] p  the processor
     */
    explicit ExportPage(ParhelionProcessor& p);
    /** @brief The buttons, the status, the cue settings under them. */
    void resized() override;
    /**
     * @brief The background and what each file holds.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint(juce::Graphics& g) override;

private:
public:
    /**
     * asks for a file, then exports (Ctrl+E: plain)
     * @param[in] extras  what goes with the audio (0: plain)
     */
    void exportWith(int extras);
    void save();                   ///< asks for a file, then saves the set (Ctrl+S)
    void load();                   ///< asks for a set, then loads it (Ctrl+O)

private:
    /** @brief Shows the export's progress and result. */
    void timerCallback() override;
    ParhelionProcessor& m_proc;   ///< the processor: the export, the set file
    juce::TextButton m_wav{ "WAV + MIDI + cues" };   ///< exports the WAV, the MIDI and the cues
    juce::TextButton m_stems{ "... with stems" };   ///< ... and the stems
    juce::TextButton m_loops{ "... with DJ loops" };   ///< ... and the DJ loops
    juce::TextButton m_all{ "... with both" };   ///< ... and both
    frame::IconButton m_save{ frame::IconButton::IconE::SAVE, "Save the set (Ctrl+S)" };   ///< saves the set (Ctrl+S): a disk
    frame::IconButton m_load{ frame::IconButton::IconE::OPEN, "Load a set (Ctrl+O)" };   ///< loads a set (Ctrl+O): a folder
    juce::Label m_status;   ///< the export's progress and result
    std::unique_ptr<ParamPage> m_cue;   ///< the OSC cues' settings
    std::unique_ptr<juce::FileChooser> m_chooser;   ///< the file dialog while it is open
};

/**
 * @brief The editor's body: everything, drawn at the design size and scaled to the window (as Phosphene's and
 *        Ephemeris'), so a maximised or full-screen window shows the panel larger rather than emptier.
 */
class EditorBody final : public juce::Component {
public:
    std::function<void(juce::Graphics&)> m_painter;   ///< draws the background and the logo
    std::function<void()> m_onResize;                 ///< lays the controls out
    /**
     * @brief Draws through painter.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint(juce::Graphics& g) override { if (m_painter) m_painter(g); }
    /** @brief Lays out through onResize. */
    void resized() override { if (m_onResize) m_onResize(); }
};

/**
 * @brief The logo (Deploy/make_icon.py, drawn as vectors): the sun, its 22-degree halo and the two sun dogs on it.
 * @param[in,out] g  the graphics context to draw into
 * @param[in]     r  where the logo goes
 */
void drawLogo(juce::Graphics& g, juce::Rectangle<float> r);

/** @brief The editor: the frame's header and keys, the arrange strip, the tabs, the help over them. */
class ParhelionEditor final : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener {
public:
    /**
     * builds the panel for @p p
     * @param[in,out] p  the processor
     */
    explicit ParhelionEditor(ParhelionProcessor& p);
    ~ParhelionEditor() override;                           ///< stops the refresh timer
    /**
     * the background
     * @param[in,out] g  the graphics context
     */
    void paint(juce::Graphics& g) override;
    void resized() override;                           ///< scales the body to the window
    void parentHierarchyChanged() override;            ///< the standalone's title bar gets a maximise button, the window its place
    /**
     * the frame's keys (frame::handleKey)
     * @param[in] key  the key
     * @return true when the key was used
     */
    bool keyPressed(const juce::KeyPress& key) override;
    /**
     * @brief Opens the preset browser on instance @p instance of synth @p m; @p chosen hears the menu id taken (a factory
     *        preset's index + 1, or PresetBar::UserId + one of the player's).
     * @param[in] m         the synth
     * @param[in] instance  its instance
     * @param[in] chosen    called with the menu id taken
     */
    void browsePresets(parh::ModuleE m, int instance, std::function<void(int)> chosen);
    /**
     * @brief Shows a line in the info line (an Alt-drag's depth).
     * @param[in] text  the line
     */
    void say(const juce::String& text) { if (m_info != nullptr) m_info->showText(text); }

private:
    /** @brief Follows the processor: the status, the play button, the rerolls, the update link; the screenshots. */
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;   ///< the settings changed (the backdrop)
    void layoutBody();                                 ///< the top bar, the arrange strip, the tabs, at the design scale
    void toggleFullScreen();                           ///< the standalone's window full screen and back
    /**
     * whether it is
     * @return true in full screen
     */
    bool fullScreen() const;
    /**
     * the editor sits in the standalone's window
     * @return true in the standalone's window
     */
    bool standalone() const;
    /**
     * the length slider for a track's minutes or a mix's
     * @param[in] mix  true for a mix's minutes, false for a track's
     */
    void showLength(bool mix);
    /**
     * the help over the tabs (F1)
     * @param[in] on  true to show it
     */
    void showHelp(bool on);
    void showSettings();                               ///< the settings menu
    /**
     * the Export tab's page
     * @return the page
     */
    ExportPage* exportPage() const;
    void openPalette();                                ///< the command palette (Ctrl+K)
    /**
     * what the palette can do: actions, pages, controls, presets
     * @return the palette's commands
     */
    std::vector<frame::CommandPalette::Command> paletteCommands();
    void startTour();                                  ///< the first steps, from the first
    /**
     * @brief The info line's words for store id @p id: its name, its value, its range, what moves it, its key.
     * @param[in]  id    the store id
     * @param[out] name  the control's name
     * @param[out] text  its value, range, mover and key
     * @return false when no page has the control
     */
    bool describeControl(int id, juce::String& name, juce::String& text);
    /**
     * @brief Shows the control of store id @p id (its tab, sub-tab, section, voice) and lights it; false when no page
     *        has it.
     * @param[in] id  the store id
     * @return false when no page has the control
     */
    bool revealId(int id);
    ParhelionProcessor& m_proc;   ///< the processor
    frame::LookAndFeel m_lnf{ parhui::skin() };          ///< first, so it outlives every component that uses it
    juce::TooltipWindow m_tooltips{ nullptr, 700 };   ///< the tooltips, after 700 ms
    EditorBody m_body;   ///< everything, at the design size
    frame::Backdrop m_backdrop;   ///< the picture behind the panel
    int m_headerBottom = 0;                             ///< where the header ends (the backdrop is strong above)
    juce::SharedResourcePointer<UpdateCheck> m_updates;   ///< the update check, shared by every editor
    juce::HyperlinkButton m_update;   ///< the link to a newer release
    juce::Label m_status;   ///< what plays, the status line
    juce::Label m_rerolls;   ///< what was rerolled or locked
    juce::Component m_title;                            ///< the name's place (drawn by the body)
    juce::Rectangle<float> m_logo;   ///< where the logo is drawn
    frame::IconButton m_undo{ frame::IconButton::IconE::UNDO, "Undo (Ctrl+Z)" };   ///< undo (Ctrl+Z)
    frame::IconButton m_redo{ frame::IconButton::IconE::REDO, "Redo (Ctrl+Y)" };   ///< redo (Ctrl+Y)
    frame::IconButton m_help{ frame::IconButton::IconE::HELP, "Help (F1)" };   ///< the help (F1)
    frame::IconButton m_settings{ frame::IconButton::IconE::SETTINGS, "Settings" };   ///< the settings menu
    /** @brief shown while a headset sends its hands */
    frame::IconButton m_headsetIcon{ frame::IconButton::IconE::HEADSET, "A headset sends its hands: the Perform page shows them" };
    std::unique_ptr<frame::HelpView> m_helpView;   ///< the help, while it is open
    juce::ComboBox m_style;   ///< compose.style
    juce::ComboBox m_key;   ///< compose.key
    juce::ComboBox m_scale;   ///< compose.scale
    /** One track or a DJ mix (set.minutes 0 or not, ParhelionProcessor::chooseMix), and the length of what is chosen. */
    juce::TextButton m_trackMode{ "Track" }, m_mixMode{ "DJ mix" };   ///< a DJ mix (set.minutes)
    juce::Slider m_length;   ///< the length: a track's minutes or a mix's
    juce::Label m_lengthLabel;   ///< the length's name
    bool m_lengthOfMix = false;                         ///< the slider shows set.minutes (else compose.minutes)
    bool m_syncing = false;                             ///< the slider is set from its parameter, not by a hand
    juce::TextButton m_compose{ "Compose track" };   ///< composes a track or a mix
    frame::IconButton m_seed{ frame::IconButton::IconE::DICE, "New seed" };   ///< a new seed, then composes: a die
    frame::IconButton m_play{ frame::IconButton::IconE::PLAY, "Play (Space)" };   ///< play and stop: a triangle, a square
    frame::IconButton m_mute{ frame::IconButton::IconE::SPEAKER, "Silence the output" };   ///< mutes the output: a speaker, crossed out when muted
    /** Phase 17: the ratings, as thumbs since the frame (01.10.2026). */
    frame::IconButton m_like{ frame::IconButton::IconE::THUMB_UP, "I like this track: its kind and its sounds come more often (with Favor Ratings)" };
    /** @brief the thumb down */
    frame::IconButton m_dislike{ frame::IconButton::IconE::THUMB_DOWN, "Not this one: its kind and its sounds come less often (with Favor Ratings)" };
    juce::String m_rated;                             ///< what was rated last, shown a while
    int m_ratedTicks = 0;   ///< ticks m_rated is still shown
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> m_combos;   ///< the header's menus on their parameters
    ArrangeView m_arrange;   ///< the arrange strip on top
    juce::TabbedComponent m_tabs{ juce::TabbedButtonBar::TabsAtTop };   ///< the pages
    juce::String m_shotPath;   ///< PARH_SHOT: where the screenshot goes, empty none
    int m_shotTicks = 0;   ///< ticks until the screenshot is taken
    bool m_shotHands = false;                           ///< PARH_SHOT_HEADSET: the headset's hands kept alive
    /** @brief The command palette's button (Ctrl+K). */
    frame::IconButton m_search{ frame::IconButton::IconE::SEARCH, "Search (Ctrl+K): any control, preset or action by a few letters" };
    std::unique_ptr<frame::InfoLine> m_info;            ///< the info line at the bottom: what the control under the mouse is
    frame::CommandPalette m_palette{ parhui::skin() };  ///< the command palette, over everything while open
    frame::PresetBrowser m_browser{ parhui::skin(), "Parhelion" };   ///< the preset browser, over everything while open
    frame::Tour m_tour{ parhui::skin() };               ///< the first steps
    frame::WindowKeeper m_window;                       ///< the standalone's window back where it was left
    int m_tourTicks = 0;                                ///< ticks until the first start's tour (-1: considered)
};
