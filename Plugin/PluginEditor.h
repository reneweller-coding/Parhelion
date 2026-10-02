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
    /** @brief The presets of instance @p instance of module @p m, chosen through @p p. */
    PresetBar(ParhelionProcessor& p, parh::Module m, int instance);
    /** @brief The step buttons either side of the menu, the composed preset under them. */
    void resized() override;
    /** @brief Nothing of its own: its children draw. */
    void paint(juce::Graphics& g) override;

private:
    /** @brief Follows the composer's choice while no other preset was chosen by hand. */
    void timerCallback() override;
    /** @brief Applies preset @p index (an undo step) and shows it. */
    void choose(int index);
    void rebuildMenu();   ///< the sixteen groups, the user's presets, "Save as user preset..."
    void askName();       ///< the name of a user preset, then saves it
    static constexpr int kUserId = 10001;   ///< menu ids: the first user preset
    static constexpr int kSaveId = 20000;   ///< menu ids: the save item
    ParhelionProcessor& proc_;   ///< the processor: its presets and its undo
    parh::Module module_;   ///< the synth module
    int instance_;   ///< its instance
    int shown_ = -2;   ///< the composer's preset the label shows
    juce::Array<juce::File> user_;   ///< the user presets in the menu
    juce::ComboBox menu_;   ///< the presets in their groups
    juce::TextButton prev_{ "<" };   ///< the preset before
    juce::TextButton next_{ ">" };   ///< the preset after
    juce::Label composed_;   ///< the preset the composer chose
};

/**
 * @brief The parameters of module instances as knobs, menus and switches, in titled groups: every control with the
 *        frame's right-click menu (MIDI learn, forget, default), a double click to its default, and the live ring.
 */
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
    /** @brief The height the page has in its window (the scrolling page around it says so before heightFor). */
    void setAvailableHeight(int h) { available_ = h; }
    /** @brief The page's parameters in words, group by group (the help's topic for the tab). */
    juce::String describe() const;

private:
    /** @brief Makes the controls of every group (once, in the constructor). */
    void build();
    ParhelionProcessor& proc_;   ///< the processor: the parameters, the undo, MIDI learn
    frame::ControlActions actions_;               ///< the controls' right-click menu
    frame::LiveRings live_;                       ///< where each knob's value plays
    std::vector<std::pair<parh::Module, int>> groups_;   ///< the module instances shown, in order
    int instances_;   ///< how many instances the modules have
    /**
     * @brief The instances as a row of buttons (30.09.2026: behind a menu the six synth voices -- lead, counter, pluck,
     *        arp, pad, stab -- were easy to miss; the user took the strings for the only pads).
     */
    juce::OwnedArray<juce::TextButton> instanceButtons_;
    int selected_ = 0;   ///< the instance shown
    juce::OwnedArray<juce::Component> controls_;   ///< the knobs, menus, switches and preset bars
    juce::OwnedArray<juce::Label> labels_;   ///< the names above them
    /** @brief A control of the page: its name above it, large or not (EditorTheme.h, layoutOf). */
    struct Cell {
        int control = -1;          ///< index into controls_ and labels_
        bool big = false;          ///< a large encoder
        bool narrow = false;       ///< a narrow menu
        int kind = 0;              ///< 0 a knob, 1 a menu, 2 a switch, 4 a preset bar
        juce::Rectangle<int> bounds;   ///< where it sits on the page
    };
    /** @brief A titled group of cells, drawn as a box. */
    struct Box {
        juce::String title;   ///< the group's title
        juce::Colour colour;   ///< the module family's colour
        std::vector<Cell> cells;   ///< its controls
        juce::Rectangle<int> bounds;   ///< where the box sits on the page
    };
    std::vector<Box> boxes_;   ///< the groups, in order
    /**
     * @brief A page taller than its window shows its groups in sections, one at a time (01.10.2026, frame::planSections):
     *        the sound and the modulation apart, each cut where the window ends.
     */
    frame::SectionSwitch section_{ parhui::skin() };
    bool split_ = false;                          ///< more than one section
    std::vector<int> sectionOf_;                  ///< per box its section
    const std::vector<int>* measuring_ = nullptr; ///< while planning: the boxes being measured
    int available_ = 0;   ///< the height of the window (setAvailableHeight)
    int plannedWidth_ = -1;   ///< the width the sections were planned for, -1 never
    int plannedAvailable_ = -1;   ///< the height the sections were planned for, -1 never
    bool shown(size_t box) const;                 ///< the box is in the section shown (or being measured)
    void plan(int width);                         ///< the sections of a page @p width wide in available_
    void applySection();                          ///< the controls of the section shown, the others hidden
    void refit();                                 ///< laid out again, and the scrolling page around it measures again
    int top() const;   ///< height of the instance bar
    /** @brief Places the boxes and their cells in @p area (@p apply: move the components too); returns the height used. */
    int layoutBoxes(juce::Rectangle<int> area, bool apply);
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliders_;   ///< the knobs on their parameters
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> combos_;   ///< the menus on their parameters
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttons_;   ///< the switches on their parameters
};

/** @brief A component in a viewport: it scrolls when it needs more height than the tab gives. */
class ScrollingPage final : public juce::Component {
public:
    explicit ScrollingPage(std::unique_ptr<ParamPage> page);   ///< takes the page over
    void resized() override;                                   ///< the page as wide as the view, as tall as it needs
    /** @brief How tall the page is at @p width (a page that should not scroll takes that much). */
    int contentHeight(int width) const { return page_->heightFor(width); }
    const ParamPage& page() const { return *page_; }           ///< the page (the help describes it)
private:
    juce::Viewport view_;   ///< scrolls the page
    std::unique_ptr<ParamPage> page_;   ///< the page
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
    struct Hit {
        double from;      ///< where it begins, beats
        double to;        ///< where it ends, beats
        float velocity;   ///< how loud, 0..1
    };
    static constexpr double kNarrowest = 16.0;   ///< the least the view shows, in beats (four bars)
    void timerCallback() override;               ///< pages on with the playhead, repaints while it is shown
    void rebuild();                              ///< a copy of a new score, its notes sorted onto the matrix's rows
    void show(double from, double span);         ///< the window, kept inside the length (all of it: not zoomed)
    void zoomAround(float x, double factor);     ///< the window times @p factor, the beat under @p x staying put
    void window(double& from, double& to) const; ///< the beats shown
    double beatAt(float x) const;                ///< the beat under @p x
    ParhelionProcessor& proc_;   ///< the processor: what plays, where it is
    bool detailed_;   ///< the Arrange tab's view: the operations' names, a larger matrix
    int version_ = -1;                    ///< the score the copy was taken of
    Playing playing_;                     ///< a copy of what plays (its decks carry the layer matrix and the sections)
    std::vector<Hit> hits_[parh::kDecks][parh::kNumLayers];   ///< the notes per deck and row of the matrix, in the order they begin
    double longest_ = 0.0;                ///< the longest note (how far before a window its notes begin)
    double from_ = 0.0;   ///< the first beat of the window
    double span_ = 0.0;   ///< its length in beats (0: the whole length)
    double lastPos_ = -1.0;   ///< the playhead at the last tick
    double lastFrom_ = 0.0;   ///< the window's first beat at the last tick
    double lastTo_ = 0.0;   ///< the window's last beat at the last tick
    float downX_ = 0.0f;                  ///< where a press began
    double downFrom_ = 0.0;               ///< the window's beginning then
    bool dragged_ = false;   ///< the press has moved the view: no jump on release
    const ArrangeView* detail_ = nullptr;   ///< the view whose window this one marks, or null
    std::pair<double, double> wanted_{ 0.0, 0.0 };   ///< zoomTo's window, until a score takes it
};

/**
 * @brief The Arrange tab: the arrangement large, and the rerolls of the track under the playhead -- each unit on its own
 *        stream (SetFile.h), so a reroll changes that and nothing else.
 */
class ArrangePage final : public juce::Component, private juce::Timer {
public:
    /** @brief The tab for @p p. */
    explicit ArrangePage(ParhelionProcessor& p);
    /** @brief The view large, the rerolls under it. */
    void resized() override;
    /** @brief The background and the headings. */
    void paint(juce::Graphics& g) override;
    ArrangeView& view() { return view_; }   ///< the large view (the strip on top marks its window)

private:
    /** @brief Shows the track under the playhead and its rerolls. */
    void timerCallback() override;
    ParhelionProcessor& proc_;   ///< the processor: what plays, the rerolls
    ArrangeView view_;   ///< the arrangement, large
    juce::Label which_;   ///< which track is under the playhead
    juce::Label sounds_;   ///< its sounds (the presets and the kit)
    juce::OwnedArray<juce::TextButton> rerolls_;   ///< a reroll per unit of the track
    juce::TextButton track_{ "reroll the whole track" };   ///< rerolls the whole track
    juce::TextButton set_{ "reroll the mix's plan" };   ///< rerolls the mix's plan
    juce::String prefix_;   ///< "track3." in a set, empty for a track
};

/** @brief The Export tab (PLAN 10.1): the files, what each holds, the OSC cues' settings. */
class ExportPage final : public juce::Component, private juce::Timer {
public:
    /** @brief The tab for @p p. */
    explicit ExportPage(ParhelionProcessor& p);
    /** @brief The buttons, the status, the cue settings under them. */
    void resized() override;
    /** @brief The background and what each file holds. */
    void paint(juce::Graphics& g) override;

private:
public:
    void exportWith(int extras);   ///< asks for a file, then exports (Ctrl+E: plain)
    void save();                   ///< asks for a file, then saves the set (Ctrl+S)
    void load();                   ///< asks for a set, then loads it (Ctrl+O)

private:
    /** @brief Shows the export's progress and result. */
    void timerCallback() override;
    ParhelionProcessor& proc_;   ///< the processor: the export, the set file
    juce::TextButton wav_{ "WAV + MIDI + cues" };   ///< exports the WAV, the MIDI and the cues
    juce::TextButton stems_{ "... with stems" };   ///< ... and the stems
    juce::TextButton loops_{ "... with DJ loops" };   ///< ... and the DJ loops
    juce::TextButton all_{ "... with both" };   ///< ... and both
    frame::IconButton save_{ frame::IconButton::Icon::Save, "Save the set (Ctrl+S)" };   ///< saves the set (Ctrl+S): a disk
    frame::IconButton load_{ frame::IconButton::Icon::Open, "Load a set (Ctrl+O)" };   ///< loads a set (Ctrl+O): a folder
    juce::Label status_;   ///< the export's progress and result
    std::unique_ptr<ParamPage> cue_;   ///< the OSC cues' settings
    std::unique_ptr<juce::FileChooser> chooser_;   ///< the file dialog while it is open
};

/**
 * @brief The editor's body: everything, drawn at the design size and scaled to the window (as Phosphene's and
 *        Ephemeris'), so a maximised or full-screen window shows the panel larger rather than emptier.
 */
class EditorBody final : public juce::Component {
public:
    std::function<void(juce::Graphics&)> painter;   ///< draws the background and the logo
    std::function<void()> onResize;                 ///< lays the controls out
    /** @brief Draws through painter. */
    void paint(juce::Graphics& g) override { if (painter) painter(g); }
    /** @brief Lays out through onResize. */
    void resized() override { if (onResize) onResize(); }
};

/** @brief The logo (Deploy/make_icon.py, drawn as vectors): the moon's dark disc, its rim, the corona in streamers. */
void drawLogo(juce::Graphics& g, juce::Rectangle<float> r);

/** @brief The editor: the frame's header and keys, the arrange strip, the tabs, the help over them. */
class ParhelionEditor final : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener {
public:
    explicit ParhelionEditor(ParhelionProcessor& p);           ///< builds the panel for @p p
    ~ParhelionEditor() override;                           ///< stops the refresh timer
    void paint(juce::Graphics& g) override;            ///< the background
    void resized() override;                           ///< scales the body to the window
    void parentHierarchyChanged() override;            ///< the standalone's title bar gets a maximise button
    bool keyPressed(const juce::KeyPress& key) override;   ///< the frame's keys (frame::handleKey)

private:
    /** @brief Follows the processor: the status, the play button, the rerolls, the update link; the screenshots. */
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;   ///< the settings changed (the backdrop)
    void layoutBody();                                 ///< the top bar, the arrange strip, the tabs, at the design scale
    void toggleFullScreen();                           ///< the standalone's window full screen and back
    bool fullScreen() const;                           ///< whether it is
    bool standalone() const;                           ///< the editor sits in the standalone's window
    void showLength(bool mix);                         ///< the length slider for a track's minutes or a mix's
    void showHelp(bool on);                            ///< the help over the tabs (F1)
    void showSettings();                               ///< the settings menu
    ExportPage* exportPage() const;                    ///< the Export tab's page
    ParhelionProcessor& proc_;   ///< the processor
    frame::LookAndFeel lnf_{ parhui::skin() };          ///< first, so it outlives every component that uses it
    juce::TooltipWindow tooltips_{ nullptr, 700 };   ///< the tooltips, after 700 ms
    EditorBody body_;   ///< everything, at the design size
    frame::Backdrop backdrop_;   ///< the picture behind the panel
    int headerBottom_ = 0;                             ///< where the header ends (the backdrop is strong above)
    juce::SharedResourcePointer<UpdateCheck> updates_;   ///< the update check, shared by every editor
    juce::HyperlinkButton update_;   ///< the link to a newer release
    juce::Label status_;   ///< what plays, the status line
    juce::Label rerolls_;   ///< what was rerolled or locked
    juce::Component title_;                            ///< the name's place (drawn by the body)
    juce::Rectangle<float> logo_;   ///< where the logo is drawn
    frame::IconButton undo_{ frame::IconButton::Icon::Undo, "Undo (Ctrl+Z)" };   ///< undo (Ctrl+Z)
    frame::IconButton redo_{ frame::IconButton::Icon::Redo, "Redo (Ctrl+Y)" };   ///< redo (Ctrl+Y)
    frame::IconButton help_{ frame::IconButton::Icon::Help, "Help (F1)" };   ///< the help (F1)
    frame::IconButton settings_{ frame::IconButton::Icon::Settings, "Settings" };   ///< the settings menu
    /** @brief shown while a headset sends its hands */
    frame::IconButton headsetIcon_{ frame::IconButton::Icon::Headset, "A headset sends its hands: the Perform page shows them" };
    std::unique_ptr<frame::HelpView> helpView_;   ///< the help, while it is open
    juce::ComboBox style_;   ///< compose.style
    juce::ComboBox key_;   ///< compose.key
    juce::ComboBox scale_;   ///< compose.scale
    /** One track or a DJ mix (set.minutes 0 or not, ParhelionProcessor::chooseMix), and the length of what is chosen. */
    juce::TextButton trackMode_{ "Track" }, mixMode_{ "DJ mix" };   ///< a DJ mix (set.minutes)
    juce::Slider length_;   ///< the length: a track's minutes or a mix's
    juce::Label lengthLabel_;   ///< the length's name
    bool lengthOfMix_ = false;                         ///< the slider shows set.minutes (else compose.minutes)
    bool syncing_ = false;                             ///< the slider is set from its parameter, not by a hand
    juce::TextButton compose_{ "Compose track" };   ///< composes a track or a mix
    frame::IconButton seed_{ frame::IconButton::Icon::Dice, "New seed" };   ///< a new seed, then composes: a die
    frame::IconButton play_{ frame::IconButton::Icon::Play, "Play (Space)" };   ///< play and stop: a triangle, a square
    frame::IconButton mute_{ frame::IconButton::Icon::Speaker, "Silence the output" };   ///< mutes the output: a speaker, crossed out when muted
    /** Phase 17: the ratings, as thumbs since the frame (01.10.2026). */
    frame::IconButton like_{ frame::IconButton::Icon::ThumbUp, "I like this track: its kind and its sounds come more often (with Favor Ratings)" };
    /** @brief the thumb down */
    frame::IconButton dislike_{ frame::IconButton::Icon::ThumbDown, "Not this one: its kind and its sounds come less often (with Favor Ratings)" };
    juce::String rated_;                             ///< what was rated last, shown a while
    int ratedTicks_ = 0;   ///< ticks rated_ is still shown
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> combos_;   ///< the header's menus on their parameters
    ArrangeView arrange_;   ///< the arrange strip on top
    juce::TabbedComponent tabs_{ juce::TabbedButtonBar::TabsAtTop };   ///< the pages
    juce::String shotPath_;   ///< TOT_SHOT: where the screenshot goes, empty none
    int shotTicks_ = 0;   ///< ticks until the screenshot is taken
    bool shotHands_ = false;                           ///< PARH_SHOT_HEADSET: the headset's hands kept alive
};
