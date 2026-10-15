/**
 * @file EditorPerform.cpp
 * @brief The live pages (EditorPerform.h).
 * @note Copied from Totality `Plugin/EditorPerform.cpp` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 * @author Rene Weller
 */
#include "EditorPerform.h"
#include "parh/Presets.h"
#include <cmath>

using namespace parh;

namespace {

/** @brief the mutes' names, in the order of perform::MuteKick .. */
const char* const MuteNames[perform::Mutes] = { "Kick", "Bass", "Hats", "Perc", "Lead", "Synths", "Pads" };
const char* const KeyOf[perform::Mutes] = { "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3" };   ///< the key that toggles each mute

/**
 * @brief Gain @p g in dB, -120 for silence.
 * @param[in] g  the gain
 * @return the gain in dB, -120 for silence
 */
float toDb(float g) { return g > 1e-6f ? 20.0f * std::log10(g) : -120.0f; }

} // namespace

// ---------------------------------------------------------------------------------------------------

LearnButton::LearnButton(ParhelionProcessor& p, int id) : juce::TextButton("learn"), m_proc(p), m_id(id)
{
    setTooltip("MIDI learn: click, then move a controller (right click: forget it)");
    onClick = [this] { m_proc.learn(m_proc.learning() == m_id ? -1 : m_id); refresh(); };
}

void LearnButton::refresh()
{
    const int cc = m_proc.controllerFor(m_id);
    const juce::String t = m_proc.learning() == m_id ? juce::String("move ...") : cc >= 0 ? "CC " + juce::String(cc) : juce::String("learn");
    if (getButtonText() != t) setButtonText(t);
    setToggleState(m_proc.learning() == m_id, juce::dontSendNotification);
}

void LearnButton::mouseUp(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { m_proc.forget(m_id); refresh(); return; }
    juce::TextButton::mouseUp(e);
}

// ---------------------------------------------------------------------------------------------------

PerformPage::PerformPage(ParhelionProcessor& p) : m_proc(p)
{
    ParamStore& s = m_proc.store();
    for (int k = 0; k < perform::Mutes; ++k) {
        auto* b = m_mutes.add(new juce::TextButton(MuteNames[k]));
        b->setClickingTogglesState(true);
        b->setColour(juce::TextButton::buttonOnColourId, parhui::colour::Onset.withAlpha(0.6f));
        b->setTooltip(juce::String("Mute ") + MuteNames[k] + ": its notes stop, its tails ring out (MIDI key " + KeyOf[k] + ")");
        const int id = s.id(ModuleE::PERFORM, 0, perform::MUTE_KICK + k);
        m_muteAttach.push_back(std::make_unique<juce::ButtonParameterAttachment>(*m_proc.parameter(id), *b));
        addAndMakeVisible(b);
        addAndMakeVisible(m_learn.add(new LearnButton(m_proc, id)));
    }
    auto bigSlider = [&](juce::Slider& sl, int id, juce::Colour c) {
        sl.setSliderStyle(juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
        sl.setColour(juce::Slider::trackColourId, c);
        sl.setColour(juce::Slider::thumbColourId, c);
        sl.setDoubleClickReturnValue(true, 0.0);
        m_sliderAttach.push_back(std::make_unique<juce::SliderParameterAttachment>(*m_proc.parameter(id), sl));
        addAndMakeVisible(sl);
        addAndMakeVisible(m_learn.add(new LearnButton(m_proc, id)));
    };
    bigSlider(m_filter, s.id(ModuleE::PERFORM, 0, perform::FILTER), parhui::familyColour(parhui::Family::FILTER));
    m_filter.setTooltip("The master filter: left a low pass, right a high pass, the middle open (controller 74; double click: open)");
    bigSlider(m_throw, s.id(ModuleE::PERFORM, 0, perform::THROW), parhui::familyColour(parhui::Family::MOTION));
    m_throw.setTooltip("The echo throw: the whole mix into the mixer's tape echo (the expression pedal)");
    {   // The keyboard (01.10.2026): its choosers from the parameters' own names.
        auto choice = [&](std::unique_ptr<frame::Choice>& box, std::unique_ptr<juce::ComboBoxParameterAttachment>& attach, int id,
                          const char* tip) {
            const ParamDesc& d = s.desc(id);
            box = std::make_unique<frame::Choice>(nullptr, id);
            for (int c = 0; c <= static_cast<int>(d.m_maxValue); ++c) box->addItem(d.m_choices[c], c + 1);
            box->setTooltip(tip);
            attach = std::make_unique<juce::ComboBoxParameterAttachment>(*m_proc.parameter(id), *box);
            addAndMakeVisible(*box);
        };
        choice(m_keyPart, m_keyPartAttach, s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_PART),
               "What the keys of a MIDI keyboard play: the kit (C1 the kick, C#1 to C2 the lanes), the bass, the 303, a "
               "polyphonic voice, the piano, the strings, the choir, the brass, or by channel (1 kit, 2 bass, 3 303, 4 to 9 "
               "lead, counter, pluck, arp, pad, stab, 10 kit, 11 piano, 12 strings, 13 choir, 14 brass). Off: the keys from "
               "middle C toggle the mutes.");
        choice(m_keyMode, m_keyModeAttach, s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_MODE),
               "Replace: the voice the keyboard plays leaves the composer's notes out; Layer: it plays over them.");
        const int cid = s.id(ModuleE::PERFORM, 0, perform::COMPOSER);
        m_composer = std::make_unique<frame::Switch>(nullptr, cid);
        m_composer->setButtonText("Composer");
        m_composer->setTooltip("On: the composer's notes play. Off: only what the keyboard plays -- the mix, the filters and the "
                              "effects go on as composed.");
        m_composerAttach = std::make_unique<juce::ButtonParameterAttachment>(*m_proc.parameter(cid), *m_composer);
        addAndMakeVisible(*m_composer);
        // The options (02.10.2026), all off by default.
        choice(m_keyLower, m_keyLowerAttach, s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_LOWER),
               "A split: the keys below Split At play this voice, the keys from it the one above (Off: no split).");
        choice(m_keySplit, m_keySplitAttach, s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_SPLIT),
               "The lowest key the upper voice plays, when Lower Keys Play names a voice.");
        const int sid = s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_SCALE);
        m_keyScale = std::make_unique<frame::Switch>(nullptr, sid);
        m_keyScale->setButtonText("Scale Lock");
        m_keyScale->setTooltip("A played key goes to the nearest note of the track's key and scale (the kit's keys stay its "
                              "instruments).");
        m_keyScaleAttach = std::make_unique<juce::ButtonParameterAttachment>(*m_proc.parameter(sid), *m_keyScale);
        addAndMakeVisible(*m_keyScale);
        choice(m_keyVelocity, m_keyVelocityAttach, s.id(ModuleE::PERFORM, 0, perform::KEYBOARD_VELOCITY),
               "How a key's velocity reaches the voice: as played, soft (a light touch louder), hard (more effort for "
               "the loud end) or fixed (always 100).");
    }
    bigSlider(m_wheel, s.id(ModuleE::PERFORM, 0, perform::WHEEL), parhui::familyColour(parhui::Family::SOURCE));
    m_wheel.setTooltip("The mod wheel: a source of every voice's modulation matrix -- what it moves is the preset's (MIDI controller 1)");
    // Breakdown now, drop now: the planner rewrites the track from its next 8-bar line (PLAN 9).
    m_breakdownNow.setTooltip("From the next 8-bar line: a breakdown, its build and a drop after it (a track, not a set)");
    m_dropNow.setTooltip("From the next 8-bar line: the drop at once, after a beat of silence (a track, not a set)");
    m_breakdownNow.setColour(juce::TextButton::buttonColourId, parhui::familyColour(parhui::Family::SPACE).withAlpha(0.35f));
    m_dropNow.setColour(juce::TextButton::buttonColourId, parhui::colour::Sun.withAlpha(0.35f));
    auto now = [this](SectionKindE kind, const char* what) {
        const int bar = m_proc.performNow(kind);
        m_now.setText(bar < 0 ? juce::String("nothing to rewrite now (a set, or composing)")
                             : juce::String(what) + " from bar " + juce::String(bar + 1) + " of the track",
                     juce::dontSendNotification);
    };
    m_breakdownNow.onClick = [now] { now(SectionKindE::BREAKDOWN, "a breakdown"); };
    m_dropNow.onClick = [now] { now(SectionKindE::DROP, "the drop"); };
    m_now.setColour(juce::Label::textColourId, parhui::colour::Dim);
    addAndMakeVisible(m_breakdownNow);
    addAndMakeVisible(m_dropNow);
    addAndMakeVisible(m_now);
    for (int d = 0; d < Decks; ++d) {
        Strip& st = m_strips[d];
        static const char* const Band[3] = { "Low", "Mid", "High" };
        for (int b = 0; b < 3; ++b) {
            juce::TextButton& k = st.m_kill[b];
            k.setButtonText(juce::String("Kill ") + Band[b]);
            k.setColour(juce::TextButton::buttonOnColourId, parhui::deckColour(d).withAlpha(0.5f));
            const int id = s.id(ModuleE::DECK, d, deck::LOW + b);
            k.onClick = [this, id] { m_proc.setFromUi(id, m_proc.store().get(id) <= -59.9f ? 0.0f : -60.0f); };
            addAndMakeVisible(k);
            addAndMakeVisible(m_learn.add(new LearnButton(m_proc, id)));
        }
        st.m_fader.setSliderStyle(juce::Slider::LinearVertical);
        st.m_fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        st.m_fader.setColour(juce::Slider::trackColourId, parhui::deckColour(d));
        st.m_fader.setColour(juce::Slider::thumbColourId, parhui::deckColour(d));
        st.m_fader.setDoubleClickReturnValue(true, 0.0);
        const int fid = s.id(ModuleE::DECK, d, deck::FADER);
        m_sliderAttach.push_back(std::make_unique<juce::SliderParameterAttachment>(*m_proc.parameter(fid), st.m_fader));
        addAndMakeVisible(st.m_fader);
        addAndMakeVisible(m_learn.add(new LearnButton(m_proc, fid)));
    }
    startTimerHz(8);
}

void PerformPage::timerCallback()
{
    // The headset's group: shown while one sends (or always, as the settings say), its hands live.
    const bool hs = m_proc.headset().shown(frame::Settings::of("Parhelion").headset());
    if (hs != m_headset) { m_headset = hs; resized(); }
    if (m_headset) repaint(m_headsetArea);
    const ParamStore& s = m_proc.store();
    for (int d = 0; d < Decks; ++d)
        for (int b = 0; b < 3; ++b)
            m_strips[d].m_kill[b].setToggleState(s.get(s.id(ModuleE::DECK, d, deck::LOW + b)) <= -59.9f, juce::dontSendNotification);
    for (auto* l : m_learn) l->refresh();
    if (m_proc.pendingNow() < 0 && m_now.getText().contains(" from bar ")) m_now.setText("rewritten -- it plays on", juce::dontSendNotification);
}

void PerformPage::resized()
{
    auto r = getLocalBounds().reduced(16);
    // The mutes: a row of large pads, each with its learn button below.
    auto mutes = r.removeFromTop(110);
    const int w = mutes.getWidth() / perform::Mutes;
    int li = 0;
    for (int k = 0; k < perform::Mutes; ++k) {
        auto cell = mutes.removeFromLeft(w).reduced(5, 0);
        m_mutes[k]->setBounds(cell.removeFromTop(78));
        m_learn[li++]->setBounds(cell.removeFromTop(26).reduced(0, 3));
    }
    r.removeFromTop(28);
    auto line = [&](juce::Slider& sl) {
        auto row = r.removeFromTop(40);
        row.removeFromLeft(130);   // the name (paint)
        m_learn[li++]->setBounds(row.removeFromRight(80).reduced(4, 8));
        sl.setBounds(row);
        r.removeFromTop(8);
    };
    line(m_filter);
    line(m_throw);
    line(m_wheel);
    auto nowRow = r.removeFromTop(44);
    m_breakdownNow.setBounds(nowRow.removeFromLeft(180).reduced(4));
    m_dropNow.setBounds(nowRow.removeFromLeft(180).reduced(4));
    m_now.setBounds(nowRow.reduced(12, 4));
    r.removeFromTop(16);
    // The decks: three strips, each three kills and a fader.
    const int sw = std::min(260, r.getWidth() / Decks);
    for (int d = 0; d < Decks; ++d) {
        auto strip = r.removeFromLeft(sw).reduced(8, 0);
        strip.removeFromTop(22);   // the deck's name (paint)
        auto kills = strip.removeFromLeft(strip.getWidth() / 2);
        for (int b = 2; b >= 0; --b) {
            auto k = kills.removeFromTop(40);
            m_strips[d].m_kill[b].setBounds(k.removeFromLeft(k.getWidth() * 3 / 5).reduced(2));
            m_learn[li + b]->setBounds(k.reduced(2, 8));
        }
        li += 3;
        m_learn[li++]->setBounds(strip.removeFromBottom(26).reduced(10, 3));
        m_strips[d].m_fader.setBounds(strip.reduced(10, 0));
    }
    // Right of the decks: the keyboard, the headset under it.
    auto right = r.withTrimmedLeft(20);
    m_keyArea = right.removeFromTop(std::min(right.getHeight(), 150 + 4 * 36));   // three lines and the four options (02.10.2026)
    {
        auto k = m_keyArea.reduced(12, 8);
        k.removeFromTop(24);   // the title (paint)
        const int line = std::min(36, k.getHeight() / 7);   // seven lines, closer where the page is short (Parhelion)
        auto keyLine = [&](juce::Component& c) {
            auto row = k.removeFromTop(line);
            row.removeFromLeft(110);   // the name (paint)
            c.setBounds(row.removeFromLeft(std::min(220, row.getWidth())).reduced(0, 4));
        };
        keyLine(*m_keyPart);
        keyLine(*m_keyMode);
        keyLine(*m_composer);
        keyLine(*m_keyLower);
        keyLine(*m_keySplit);
        keyLine(*m_keyScale);
        keyLine(*m_keyVelocity);
    }
    right.removeFromTop(12);
    m_headsetArea = m_headset ? right : juce::Rectangle<int>();
}

void PerformPage::paint(juce::Graphics& g)
{
    g.setColour(parhui::colour::Dim);
    g.setFont(juce::FontOptions(13.0f));
    auto r = getLocalBounds().reduced(16);
    g.drawText("MUTE -- the keys C3 to F#3 toggle them (while the keyboard plays nothing); a muted group's notes stop, its tails ring out",
               r.getX(), r.getY() + 112, r.getWidth(), 20,
               juce::Justification::left);
    g.drawText("Master Filter", r.getX(), m_filter.getY(), 120, m_filter.getHeight(), juce::Justification::centredLeft);
    g.drawText("Echo Throw", r.getX(), m_throw.getY(), 120, m_throw.getHeight(), juce::Justification::centredLeft);
    g.drawText("Mod Wheel", r.getX(), m_wheel.getY(), 120, m_wheel.getHeight(), juce::Justification::centredLeft);
    for (int d = 0; d < Decks; ++d) {
        const auto b = m_strips[d].m_kill[2].getBounds();
        g.setColour(parhui::deckColour(d));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(juce::String("DECK ") + juce::String::charToString(static_cast<juce::juce_wchar>('A' + d)) + (d == 2 ? " (loops)" : ""),
                   b.getX(), b.getY() - 22, 200, 18, juce::Justification::left);
    }
    if (!m_keyArea.isEmpty()) {   // the keyboard's box, as the frame draws a group
        const frame::Skin& sk = parhui::skin();
        const auto box = m_keyArea.toFloat();
        g.setColour(sk.m_group);
        g.fillRoundedRectangle(box, sk.m_radius);
        g.setColour(sk.m_edge);
        g.drawRoundedRectangle(box.reduced(0.5f), sk.m_radius, 1.0f);
        g.setColour(sk.family(frame::FamilyE::SOURCE));
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText("KEYBOARD", m_keyArea.getX() + 12, m_keyArea.getY() + 8, 200, 16, juce::Justification::centredLeft);
        g.setColour(parhui::colour::Dim);
        g.setFont(juce::FontOptions(13.0f));
        for (auto [c, name] : { std::pair<juce::Component*, const char*>{ m_keyPart.get(), "Plays" }, { m_keyMode.get(), "Mode" },
                                { m_composer.get(), "Composer" }, { m_keyLower.get(), "Lower Keys" },
                                { m_keySplit.get(), "Split At" }, { m_keyScale.get(), "Scale" },
                                { m_keyVelocity.get(), "Velocity" } })
            g.drawText(name, m_keyArea.getX() + 12, c->getY(), 96, c->getHeight(), juce::Justification::centredLeft);
    }
    if (m_headset && !m_headsetArea.isEmpty())   // the headset (the frame): what the hands do, and how they stand now
        frame::drawHeadsetBox(g, m_headsetArea, parhui::skin(), m_proc.headset(), "kick out / in", "breakdown / drop now");
}

// ---------------------------------------------------------------------------------------------------

DecksPage::DecksPage(ParhelionProcessor& p)
    : m_knobs(std::make_unique<ParamPage>(p, std::vector<std::pair<ModuleE, int>>{ { ModuleE::DECK, 0 }, { ModuleE::DECK, 1 }, { ModuleE::DECK, 2 } }))
{
    addAndMakeVisible(m_knobs);
}

void DecksPage::meter(const float* pk, const float* rms, float out, float lufs)
{
    for (int d = 0; d < Decks; ++d) {
        m_peak[d] = std::max(pk[d], m_peak[d] * 0.85f);
        m_rms[d] = rms[d];
        m_hold[d] = std::max(toDb(pk[d]), m_hold[d] - 0.6f);
    }
    m_out = std::max(out, m_out * 0.85f);
    m_hold[Decks] = std::max(toDb(out), m_hold[Decks] - 0.6f);
    m_lufs = lufs;
    repaint(getLocalBounds().withWidth(220));
}

void DecksPage::resized()
{
    m_knobs.setBounds(getLocalBounds().withTrimmedLeft(220));
}

void DecksPage::paint(juce::Graphics& g)
{
    using namespace parhui::colour;

    auto r = getLocalBounds().withWidth(220).reduced(12);
    g.setColour(Ink);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText(m_lufs > -69.0f ? juce::String(m_lufs, 1) + " LUFS" : juce::String("-- LUFS"), r.removeFromTop(28), juce::Justification::centredLeft);
    g.setColour(Dim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawFittedText("momentary (400 ms, K-weighted); the styles aim at -8 to -10 LUFS over their loudest 20 seconds", r.removeFromTop(30),
                     juce::Justification::topLeft, 3);
    r.removeFromTop(8);
    // Meters: the three decks after their channels, and the output; -48 .. +6 dB.
    const auto yOf = [&](float db, juce::Rectangle<int> m) {
        const float t = juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 54.0f);
        return static_cast<float>(m.getBottom()) - t * static_cast<float>(m.getHeight());
    };
    const int mw = r.getWidth() / (Decks + 1);
    for (int i = 0; i <= Decks; ++i) {
        auto m = r.withX(r.getX() + i * mw).withWidth(mw).reduced(8, 0).withTrimmedBottom(22);
        g.setColour(Group);
        g.fillRect(m);
        const float pdb = toDb(i < Decks ? m_peak[i] : m_out), rdb = i < Decks ? toDb(m_rms[i]) : pdb;
        const juce::Colour c = i < Decks ? parhui::deckColour(i) : Accent;
        g.setColour(c.withAlpha(0.35f));
        g.fillRect(juce::Rectangle<float>(static_cast<float>(m.getX()), yOf(pdb, m), static_cast<float>(m.getWidth()), static_cast<float>(m.getBottom()) - yOf(pdb, m)));
        if (i < Decks) {
            g.setColour(c.withAlpha(0.85f));
            g.fillRect(juce::Rectangle<float>(static_cast<float>(m.getX()) + 3.0f, yOf(rdb, m), static_cast<float>(m.getWidth()) - 6.0f, static_cast<float>(m.getBottom()) - yOf(rdb, m)));
        }
        g.setColour(m_hold[i] > -0.1f ? Red : Ink);
        g.fillRect(static_cast<float>(m.getX()), yOf(m_hold[i], m) - 1.0f, static_cast<float>(m.getWidth()), 2.0f);
        g.setColour(Dim);
        g.drawText(i < Decks ? juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)) : juce::String("Out"), m.getX() - 6, m.getBottom() + 2,
                   m.getWidth() + 12, 18, juce::Justification::centred);
    }
    g.setColour(Faint);
    for (int db : { 0, -6, -12, -24, -36 }) {
        const float y = yOf(static_cast<float>(db), r.withTrimmedBottom(22));
        g.fillRect(static_cast<float>(r.getX()), y, static_cast<float>(r.getWidth()), 1.0f);
    }
}

// ---------------------------------------------------------------------------------------------------

namespace {
/** @brief A strip of the console: its name, its meter (a stem), its synth, its fader, pan and sends, its mute. */
struct StripSpec {
    std::string m_name;   ///< the strip's name
    int m_meter;   ///< the strip it reads (MeterSink::Strip)
    ModuleE m_synth;   ///< the synth whose preset it names (Module::Count: none)
    int m_instance;   ///< the synth's instance
    std::string m_fader;   ///< its fader's key
    std::vector<std::pair<std::string, std::string>> m_knobs;   ///< key, label; "Pan" is not a send
    int m_mute;                                                   ///< perform::Mute* offset from MuteKick, -1 none
};
} // namespace

MixerPage::MixerPage(ParhelionProcessor& p)
    : m_proc(p), m_live([this](int id) { return m_proc.playedNormalised(id); }), m_tabs(parhui::skin())
{
    m_actions.m_learn = [this](int id) { m_proc.learn(id); };
    m_actions.m_controllerFor = [this](int id) { return m_proc.controllerFor(id); };
    m_actions.m_forget = [this](int id) { m_proc.forget(id); };
    m_actions.m_reset = [this](int id) { m_proc.resetToDefault(id); };
    m_actions.m_describe = [this](int id) { return juce::String(m_proc.store().desc(id).m_name) + "  (" + m_proc.store().key(id) + ")"; };
    using F = parhui::Family;
    m_tabs.add("Console", [this] {
        auto console = std::make_unique<frame::Console>(parhui::skin());
        m_console = console.get();
        ParamStore& s = m_proc.store();
        using DT = Deck;
        using Keys = std::vector<std::pair<std::string, std::string>>;
        const Keys poly = { { "pan", "Pan" }, { "room_send", "Room" }, { "plate_send", "Plate" }, { "hall_send", "Hall" }, { "delay_send", "Delay" } };
        const Keys orch = { { "room_send", "Room" }, { "plate_send", "Plate" }, { "hall_send", "Hall" } };
        auto with = [](const std::string& prefix, const Keys& keys) {
            Keys out;
            for (const auto& [k, l] : keys) out.emplace_back(prefix + "." + k, l);
            return out;
        };
        std::vector<StripSpec> specs = {
            { "Kick", DT::STEM_KICK, ModuleE::KICK, 0, "kick.level", {}, 0 },
            { "Sub", DT::STEM_SUB, ModuleE::SUB, 0, "sub.level", {}, 1 },
            { "Bass", DT::STEM_BASS, ModuleE::BASS, 0, "bass.level", { { "bass.pan", "Pan" }, { "bass.room_send", "Room" }, { "bass.plate_send", "Plate" } }, 1 },
            { "303", DT::STEM_ACID, ModuleE::ACID, 0, "acid.level", { { "acid.pan", "Pan" }, { "acid.room_send", "Room" }, { "acid.plate_send", "Plate" } }, 1 },
            { "Hats", DT::STEM_HATS, ModuleE::COUNT, 0, "mix.hats_level", { { "sends.hats_send", "Room" } }, 2 },
            { "Perc", DT::STEM_PERC, ModuleE::COUNT, 0, "mix.perc_level", { { "sends.perc_send", "Plate" } }, 3 },
        };
        static const int PolyMute[PolyInstances] = { 4, 4, 5, 5, 6, 6 };
        for (int v = 0; v < PolyInstances; ++v) {
            const std::string n = PolyInstanceNames[v];
            specs.push_back({ std::string(1, static_cast<char>(std::toupper(n[0]))) + n.substr(1), DT::STEM_LEAD + v, ModuleE::POLY, v, n + ".level",
                              with(n, poly), PolyMute[v] });
        }
        specs.push_back({ "Piano", DT::STEM_PIANO, ModuleE::PIANO, 0, "piano.level", with("piano", orch), -1 });
        specs.push_back({ "Strings", DT::STEM_STRINGS, ModuleE::STRINGS, 0, "strings.level", with("strings", orch), -1 });
        specs.push_back({ "Choir", DT::STEM_CHOIR, ModuleE::CHOIR, 0, "choir.level", with("choir", orch), -1 });
        specs.push_back({ "Brass", DT::STEM_BRASS, ModuleE::BRASS, 0, "brass.level", with("brass", orch), -1 });
        specs.push_back({ "Timpani", DT::STEM_TIMPANI, ModuleE::TIMPANI, 0, "timpani.level", with("timpani", orch), -1 });
        specs.push_back({ "FX", DT::STEM_FX, ModuleE::SFX, 0, "sfx.level", with("sfx", orch), -1 });
        specs.push_back({ "Room", DT::STEM_ROOM, ModuleE::COUNT, 0, "sends.room_return", {}, -1 });
        specs.push_back({ "Plate", DT::STEM_PLATE, ModuleE::COUNT, 0, "sends.plate_return", {}, -1 });
        specs.push_back({ "Hall", DT::STEM_HALL, ModuleE::COUNT, 0, "sends.hall_return", {}, -1 });
        specs.push_back({ "Cloud", DT::STEM_CLOUD, ModuleE::COUNT, 0, "cloud.level", { { "cloud.plate_send", "Plate" } }, -1 });
        auto control = [&](const std::string& key, const juce::String& label, bool send) {
            frame::ChannelStrip::Control c;
            c.m_id = s.find(key);
            c.m_param = c.m_id >= 0 ? m_proc.parameter(c.m_id) : nullptr;
            c.m_label = label;
            c.m_send = send;
            return c;
        };
        m_sounds.clear();
        for (const StripSpec& sp : specs) {
            std::vector<frame::ChannelStrip::Control> knobs;
            for (const auto& [key, label] : sp.m_knobs) knobs.push_back(control(key, label, label != "Pan"));
            const F family = sp.m_meter >= DT::STEM_ROOM && sp.m_meter <= DT::STEM_CLOUD ? F::SPACE
                           : sp.m_meter <= DT::STEM_PERC ? F::SOURCE : sp.m_meter <= DT::STEM_STAB ? F::FILTER : F::ENVELOPE;
            auto& strip = console->add(std::make_unique<frame::ChannelStrip>(parhui::skin(), sp.m_name, parhui::familyColour(family),
                                                                              control(sp.m_fader, juce::String(sp.m_name) + " level", false), knobs, &m_actions, &m_live));
            m_sounds.push_back({ sp.m_meter, { sp.m_synth, sp.m_instance } });
            if (sp.m_mute >= 0) {
                auto* m = m_mutes.add(new juce::TextButton("M"));
                m->setClickingTogglesState(true);
                m->setColour(juce::TextButton::buttonOnColourId, parhui::colour::Onset.withAlpha(0.6f));
                m->setTooltip("Mute the group (as on the Perform page)");
                m->setWantsKeyboardFocus(false);
                m_muteLinks.push_back(std::make_unique<juce::ButtonParameterAttachment>(*m_proc.parameter(s.id(ModuleE::PERFORM, 0, perform::MUTE_KICK + sp.m_mute)), *m));
                strip.setHeadButton(m);
            }
        }
        // The output: the master's level, and the meter of what leaves.
        console->add(std::make_unique<frame::ChannelStrip>(parhui::skin(), "Out", parhui::colour::Accent, control("master.level", "Master level", false),
                                                           std::vector<frame::ChannelStrip::Control>{}, &m_actions, &m_live));
        return std::unique_ptr<juce::Component>(std::move(console));
    });
    m_tabs.add("Buses and Master", [this] {
        return std::unique_ptr<juce::Component>(std::make_unique<ScrollingPage>(
            std::make_unique<ParamPage>(m_proc, std::vector<std::pair<ModuleE, int>>{ { ModuleE::MIX, 0 }, { ModuleE::MASTER, 0 }, { ModuleE::PUMP, 0 } })));
    });
    m_tabs.add("Decks", [this] {
        auto d = std::make_unique<DecksPage>(m_proc);
        m_decks = d.get();
        return std::unique_ptr<juce::Component>(std::move(d));
    });
    addAndMakeVisible(m_tabs);
    startTimerHz(30);
}

void MixerPage::timerCallback()
{
    if (!isShowing()) return;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = m_lastPoll > 0.0 ? juce::jlimit(0.0, 0.5, now - m_lastPoll) : 1.0 / 30.0;
    m_lastPoll = now;
    float pk[Decks], rms[Decks], out = 0.0f, lufs = -70.0f;
    m_proc.takeMeters(pk, rms, out, lufs);
    float spk[MeterSink::Strips], srms[MeterSink::Strips];
    m_proc.takeStripMeters(spk, srms);
    if (m_decks != nullptr && m_decks->isShowing()) m_decks->meter(pk, rms, out, lufs);
    m_lufs = lufs;
    if (m_console != nullptr && m_console->isShowing()) {
        for (int i = 0; i < m_console->size() && i < static_cast<int>(m_sounds.size()); ++i) {
            const int m = m_sounds[static_cast<size_t>(i)].first;
            m_console->strip(i).meter(spk[m], srms[m], dt);
        }
        if (m_console->size() > static_cast<int>(m_sounds.size())) m_console->strip(m_console->size() - 1).meter(out, out * 0.7071f, dt);
        // Now and then: the sound each synth's strip plays.
        if (++m_tick % 15 == 0)
            for (size_t i = 0; i < m_sounds.size(); ++i) {
                const auto [m, inst] = m_sounds[i].second;
                juce::String name;
                if (m != ModuleE::COUNT)
                    if (const int index = m_proc.composedPreset(m, inst); index >= 0) name = factoryPresets(m, inst)[static_cast<size_t>(index)].m_name;
                m_console->strip(static_cast<int>(i)).setSound(name);
            }
        repaint(getLocalBounds().removeFromTop(36));
    }
}

void MixerPage::resized() { m_tabs.setBounds(getLocalBounds()); }

void MixerPage::paint(juce::Graphics& g)
{
    // The loudness at the console's top right.
    if (m_tabs.current() != 0) return;
    g.setColour(parhui::colour::Ink);
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText(m_lufs > -69.0f ? juce::String(m_lufs, 1) + " LUFS" : juce::String("-- LUFS"), getLocalBounds().removeFromTop(36).reduced(14, 0),
               juce::Justification::centredRight);
}
