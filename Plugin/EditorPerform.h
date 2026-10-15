/**
 * @file EditorPerform.h
 * @brief The live pages (PLAN 10.1): Perform -- as at a mixing desk: the groups muted and unmuted, the isolator kills
 *        and faders of the decks, the master filter, the echo throw, each learnable from a MIDI controller -- and Mixer,
 *        the meters of the decks and the output beside the mix's and the master's knobs.
 * @note Copied from Totality `Plugin/EditorPerform.h` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 * @author Rene Weller
 */
#pragma once
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

/** @brief A small button that binds the next MIDI controller to a parameter (right click: forget it). */
class LearnButton final : public juce::TextButton
{
public:
    /**
     * @brief A learn button for store id @p id of @p p.
     * @param[in,out] p   the processor
     * @param[in]     id  the store id
     */
    LearnButton( ParhelionProcessor &p, int id );
    void refresh();   ///< shows the binding, or that it listens
    /**
     * right click forgets
     * @param[in] e  the mouse event
     */
    void mouseUp( const juce::MouseEvent &e ) override;

private:
    ParhelionProcessor &m_proc;   ///< the processor: its controller map
    int                 m_id;     ///< the store id it binds
};

/** @brief The Perform tab. */
class PerformPage final : public juce::Component, private juce::Timer
{
public:
    /**
     * @brief The tab for @p p.
     * @param[in,out] p  the processor
     */
    explicit PerformPage( ParhelionProcessor &p );
    /** @brief The mutes, the filter and the throw, the decks' strips, the learn buttons, the keyboard and the headset's group. */
    void resized() override;
    /**
     * @brief The groups' boxes and titles, the hands of a headset.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;

private:
    /** @brief Follows the headset and the learn buttons. */
    void timerCallback() override;
    ParhelionProcessor                                           &m_proc;         ///< the processor
    juce::OwnedArray<juce::TextButton>                            m_mutes;        ///< the mutes, one per group
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> m_muteAttach;   ///< the mutes on their parameters
    juce::Slider                                                  m_filter;   ///< the master filter (perform.filter)
    juce::Slider                                                  m_throw;    ///< the echo throw (perform.throw)
    juce::Slider     m_wheel;   ///< the mod wheel (perform.wheel): a source of every voice's modulation matrix
    juce::TextButton m_breakdownNow{ "Breakdown now" };   ///< a breakdown from the next 8-bar line (PLAN 9)
    juce::TextButton m_dropNow{ "Drop now" };             ///< a drop from the next 8-bar line
    juce::Label      m_now;                               ///< where the rewrite begins
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>>
        m_sliderAttach;   ///< the filter, the throw and the faders on their parameters
    /** @brief A deck's strip: three kills and the fader. */
    struct Strip
    {
        juce::TextButton m_kill[3];   ///< the kills: low, mid, high
        juce::Slider     m_fader;     ///< the deck's fader
    };
    Strip                             m_strips[parh::Decks];   ///< a strip per deck
    juce::OwnedArray<LearnButton>     m_learn;                 ///< the learn buttons beside the controls
    std::vector<juce::Rectangle<int>> m_learnFor;              ///< (layout) where each learn button sits
    bool                              m_headset = false;       ///< the headset's group is shown
    juce::Rectangle<int>              m_headsetArea;           ///< where it is drawn
    // The keyboard (01.10.2026, Engine::queueLive): what a MIDI keyboard plays, Replace or Layer, the composer on or off.
    std::unique_ptr<frame::Choice> m_keyPart;    ///< perform.keyboard_part: what the keys play
    std::unique_ptr<frame::Choice> m_keyMode;    ///< perform.keyboard_mode: Replace or Layer
    std::unique_ptr<frame::Switch> m_composer;   ///< perform.composer: the composer's notes on or off
    std::unique_ptr<juce::ComboBoxParameterAttachment> m_keyPartAttach;    ///< m_keyPart on its parameter
    std::unique_ptr<juce::ComboBoxParameterAttachment> m_keyModeAttach;    ///< m_keyMode on its parameter
    std::unique_ptr<juce::ButtonParameterAttachment>   m_composerAttach;   ///< m_composer on its parameter
    // The keyboard's options (02.10.2026): the lower keys' voice and the split, Scale Lock, the velocity curve.
    std::unique_ptr<frame::Choice> m_keyLower;      ///< perform.keyboard_lower: the voice under the split (Off: none)
    std::unique_ptr<frame::Choice> m_keySplit;      ///< perform.keyboard_split: the split key
    std::unique_ptr<frame::Switch> m_keyScale;      ///< perform.keyboard_scale: Scale Lock
    std::unique_ptr<frame::Choice> m_keyVelocity;   ///< perform.keyboard_velocity: the velocity curve
    std::unique_ptr<juce::ComboBoxParameterAttachment> m_keyLowerAttach;      ///< m_keyLower on its parameter
    std::unique_ptr<juce::ComboBoxParameterAttachment> m_keySplitAttach;      ///< m_keySplit on its parameter
    std::unique_ptr<juce::ButtonParameterAttachment>   m_keyScaleAttach;      ///< m_keyScale on its parameter
    std::unique_ptr<juce::ComboBoxParameterAttachment> m_keyVelocityAttach;   ///< m_keyVelocity on its parameter
    juce::Rectangle<int>                               m_keyArea;             ///< the Keyboard group's box
};

/** @brief The Decks sub-page: the decks' meters beside their channels' knobs. */
class DecksPage final : public juce::Component
{
public:
    /**
     * @brief The sub-page for @p p.
     * @param[in,out] p  the processor
     */
    explicit DecksPage( ParhelionProcessor &p );
    /** @brief The meters left, the knobs right. */
    void resized() override;
    /**
     * @brief The meters with their held peaks, the output and its loudness.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    /**
     * @brief A new reading of the decks and the output.
     * @param[in] peak  each deck's peak
     * @param[in] rms   each deck's RMS
     * @param[in] out   the output's peak
     * @param[in] lufs  the output's loudness in LUFS
     */
    void meter( const float *peak, const float *rms, float out, float lufs );

private:
    ScrollingPage m_knobs;                        ///< the decks' channel knobs
    float         m_peak[parh::Decks] = {};       ///< per deck: its peak
    float         m_rms[parh::Decks] = {};        ///< per deck: its RMS
    float         m_out = 0.0f;                   ///< the output's peak
    float         m_lufs = -70.0f;                ///< the output's momentary loudness, LUFS
    float         m_hold[parh::Decks + 1] = {};   ///< the held peaks: per deck, then the output
};

/**
 * @brief The Mixer tab (01.10.2026, the frame): Console -- a strip per source with its meter, fader, pan, sends and
 *        mute, the sends' returns and the output, scrolling sideways where they do not fit --, Buses and Master, Decks.
 */
class MixerPage final : public juce::Component, private juce::Timer
{
public:
    /**
     * @brief The tab for @p p.
     * @param[in,out] p  the processor
     */
    explicit MixerPage( ParhelionProcessor &p );
    /** @brief The sub-tabs over the whole page. */
    void resized() override;
    /**
     * @brief The background.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;

private:
    /** @brief Reads the meters into the console's strips and the Decks page, and the sounds the strips name. */
    void timerCallback() override;
    ParhelionProcessor   &m_proc;                ///< the processor
    frame::ControlActions m_actions;             ///< the controls' right-click menu
    frame::LiveRings      m_live;                ///< where each knob's value plays
    frame::SubTabs        m_tabs;                ///< Console, Buses and Master, Decks
    frame::Console       *m_console = nullptr;   ///< (owned by m_tabs)
    DecksPage            *m_decks = nullptr;     ///< (owned by m_tabs, made when first shown)
    std::vector<std::pair<int, std::pair<parh::ModuleE, int>>>
                                       m_sounds;   ///< per strip: its meter, and the synth it names
    juce::OwnedArray<juce::TextButton> m_mutes;    ///< the strips' mutes
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> m_muteLinks;   ///< the mutes on their parameters
    float  m_lufs = -70.0f;    ///< the output's momentary loudness, LUFS
    double m_lastPoll = 0.0;   ///< when the strip meters were last taken (ms)
    int    m_tick = 0;         ///< counts the timer ticks (some readings every few)
};
