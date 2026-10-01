/**
 * @file EditorPerform.h
 * @brief The live pages (PLAN 10.1): Perform -- as at a mixing desk: the groups muted and unmuted, the isolator kills
 *        and faders of the decks, the master filter, the echo throw, each learnable from a MIDI controller -- and Mixer,
 *        the meters of the decks and the output beside the mix's and the master's knobs.
 * @note Copied from Totality `Plugin/EditorPerform.h` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 */
#pragma once
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

/** @brief A small button that binds the next MIDI controller to a parameter (right click: forget it). */
class LearnButton final : public juce::TextButton {
public:
    LearnButton(ParhelionProcessor& p, int id);
    void refresh();                                         ///< shows the binding, or that it listens
    void mouseUp(const juce::MouseEvent& e) override;       ///< right click forgets

private:
    ParhelionProcessor& proc_;
    int id_;
};

/** @brief The Perform tab. */
class PerformPage final : public juce::Component, private juce::Timer {
public:
    explicit PerformPage(ParhelionProcessor& p);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;
    ParhelionProcessor& proc_;
    juce::OwnedArray<juce::TextButton> mutes_;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> muteAttach_;
    juce::Slider filter_, throw_, wheel_;
    juce::TextButton breakdownNow_{ "Breakdown now" }, dropNow_{ "Drop now" };   ///< PLAN 9: the planner rewrites from the next 8-bar line
    juce::Label now_;                                                            ///< where the rewrite begins
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttach_;
    /** @brief A deck's strip: three kills and the fader. */
    struct Strip {
        juce::TextButton kill[3];
        juce::Slider fader;
    };
    Strip strips_[parh::kDecks];
    juce::OwnedArray<LearnButton> learn_;
    std::vector<juce::Rectangle<int>> learnFor_;   ///< (layout) where each learn button sits
    bool headset_ = false;                         ///< the headset's group is shown
    juce::Rectangle<int> headsetArea_;             ///< where it is drawn
};

/** @brief The Decks sub-page: the decks' meters beside their channels' knobs. */
class DecksPage final : public juce::Component {
public:
    explicit DecksPage(ParhelionProcessor& p);
    void resized() override;
    void paint(juce::Graphics& g) override;
    /** @brief A new reading of the decks and the output. */
    void meter(const float* peak, const float* rms, float out, float lufs);

private:
    ScrollingPage knobs_;
    float peak_[parh::kDecks] = {}, rms_[parh::kDecks] = {}, out_ = 0.0f, lufs_ = -70.0f;
    float hold_[parh::kDecks + 1] = {};
};

/**
 * @brief The Mixer tab (01.10.2026, the frame): Console -- a strip per source with its meter, fader, pan, sends and
 *        mute, the sends' returns and the output, scrolling sideways where they do not fit --, Buses and Master, Decks.
 */
class MixerPage final : public juce::Component, private juce::Timer {
public:
    explicit MixerPage(ParhelionProcessor& p);
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;
    ParhelionProcessor& proc_;
    frame::ControlActions actions_;
    frame::LiveRings live_;
    frame::SubTabs tabs_;
    frame::Console* console_ = nullptr;            ///< (owned by tabs_)
    DecksPage* decks_ = nullptr;                   ///< (owned by tabs_, made when first shown)
    std::vector<std::pair<int, std::pair<parh::Module, int>>> sounds_;   ///< per strip: its meter, and the synth it names
    juce::OwnedArray<juce::TextButton> mutes_;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> muteLinks_;
    float lufs_ = -70.0f;
    double lastPoll_ = 0.0;
    int tick_ = 0;
};
