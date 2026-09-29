/**
 * @file EditorPerform.cpp
 * @brief The live pages (EditorPerform.h).
 * @note Copied from Totality `Plugin/EditorPerform.cpp` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 */
#include "EditorPerform.h"
#include <cmath>

using namespace parh;

namespace {

const char* const kMuteNames[perform::kMutes] = { "Kick", "Bass", "Hats", "Perc", "Lead", "Synths", "Pads" };
const char* const kKeyOf[perform::kMutes] = { "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3" };

float toDb(float g) { return g > 1e-6f ? 20.0f * std::log10(g) : -120.0f; }

} // namespace

// ---------------------------------------------------------------------------------------------------

LearnButton::LearnButton(ParhelionProcessor& p, int id) : juce::TextButton("learn"), proc_(p), id_(id)
{
    setTooltip("MIDI learn: click, then move a controller (right click: forget it)");
    onClick = [this] { proc_.learn(proc_.learning() == id_ ? -1 : id_); refresh(); };
}

void LearnButton::refresh()
{
    const int cc = proc_.controllerFor(id_);
    const juce::String t = proc_.learning() == id_ ? juce::String("move ...") : cc >= 0 ? "CC " + juce::String(cc) : juce::String("learn");
    if (getButtonText() != t) setButtonText(t);
    setToggleState(proc_.learning() == id_, juce::dontSendNotification);
}

void LearnButton::mouseUp(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { proc_.forget(id_); refresh(); return; }
    juce::TextButton::mouseUp(e);
}

// ---------------------------------------------------------------------------------------------------

PerformPage::PerformPage(ParhelionProcessor& p) : proc_(p)
{
    ParamStore& s = proc_.store();
    for (int k = 0; k < perform::kMutes; ++k) {
        auto* b = mutes_.add(new juce::TextButton(kMuteNames[k]));
        b->setClickingTogglesState(true);
        b->setColour(juce::TextButton::buttonOnColourId, parhui::colour::onset.withAlpha(0.6f));
        b->setTooltip(juce::String("Mute ") + kMuteNames[k] + ": its notes stop, its tails ring out (MIDI key " + kKeyOf[k] + ")");
        const int id = s.id(Module::Perform, 0, perform::MuteKick + k);
        muteAttach_.push_back(std::make_unique<juce::ButtonParameterAttachment>(*proc_.parameter(id), *b));
        addAndMakeVisible(b);
        addAndMakeVisible(learn_.add(new LearnButton(proc_, id)));
    }
    auto bigSlider = [&](juce::Slider& sl, int id, juce::Colour c) {
        sl.setSliderStyle(juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 22);
        sl.setColour(juce::Slider::trackColourId, c);
        sl.setColour(juce::Slider::thumbColourId, c);
        sl.setDoubleClickReturnValue(true, 0.0);
        sliderAttach_.push_back(std::make_unique<juce::SliderParameterAttachment>(*proc_.parameter(id), sl));
        addAndMakeVisible(sl);
        addAndMakeVisible(learn_.add(new LearnButton(proc_, id)));
    };
    bigSlider(filter_, s.id(Module::Perform, 0, perform::Filter), parhui::familyColour(parhui::Family::Filter));
    filter_.setTooltip("The master filter: left a low pass, right a high pass, the middle open (controller 74; double click: open)");
    bigSlider(throw_, s.id(Module::Perform, 0, perform::Throw), parhui::familyColour(parhui::Family::Motion));
    throw_.setTooltip("The echo throw: the whole mix into the mixer's tape echo (the expression pedal)");
    bigSlider(wheel_, s.id(Module::Perform, 0, perform::Wheel), parhui::familyColour(parhui::Family::Source));
    wheel_.setTooltip("The mod wheel: a source of every voice's modulation matrix -- what it moves is the preset's (MIDI controller 1)");
    // Breakdown now, drop now: the planner rewrites the track from its next 8-bar line (PLAN 9).
    breakdownNow_.setTooltip("From the next 8-bar line: a breakdown, its build and a drop after it (a track, not a set)");
    dropNow_.setTooltip("From the next 8-bar line: the drop at once, after a beat of silence (a track, not a set)");
    breakdownNow_.setColour(juce::TextButton::buttonColourId, parhui::familyColour(parhui::Family::Space).withAlpha(0.35f));
    dropNow_.setColour(juce::TextButton::buttonColourId, parhui::colour::amber.withAlpha(0.35f));
    auto now = [this](SectionKind kind, const char* what) {
        const int bar = proc_.performNow(kind);
        now_.setText(bar < 0 ? juce::String("nothing to rewrite now (a set, or composing)")
                             : juce::String(what) + " from bar " + juce::String(bar + 1) + " of the track",
                     juce::dontSendNotification);
    };
    breakdownNow_.onClick = [now] { now(SectionKind::Breakdown, "a breakdown"); };
    dropNow_.onClick = [now] { now(SectionKind::Drop, "the drop"); };
    now_.setColour(juce::Label::textColourId, parhui::colour::dim);
    addAndMakeVisible(breakdownNow_);
    addAndMakeVisible(dropNow_);
    addAndMakeVisible(now_);
    for (int d = 0; d < kDecks; ++d) {
        Strip& st = strips_[d];
        static const char* const kBand[3] = { "Low", "Mid", "High" };
        for (int b = 0; b < 3; ++b) {
            juce::TextButton& k = st.kill[b];
            k.setButtonText(juce::String("Kill ") + kBand[b]);
            k.setColour(juce::TextButton::buttonOnColourId, parhui::deckColour(d).withAlpha(0.5f));
            const int id = s.id(Module::Deck, d, deck::Low + b);
            k.onClick = [this, id] { proc_.setFromUi(id, proc_.store().get(id) <= -59.9f ? 0.0f : -60.0f); };
            addAndMakeVisible(k);
            addAndMakeVisible(learn_.add(new LearnButton(proc_, id)));
        }
        st.fader.setSliderStyle(juce::Slider::LinearVertical);
        st.fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        st.fader.setColour(juce::Slider::trackColourId, parhui::deckColour(d));
        st.fader.setColour(juce::Slider::thumbColourId, parhui::deckColour(d));
        st.fader.setDoubleClickReturnValue(true, 0.0);
        const int fid = s.id(Module::Deck, d, deck::Fader);
        sliderAttach_.push_back(std::make_unique<juce::SliderParameterAttachment>(*proc_.parameter(fid), st.fader));
        addAndMakeVisible(st.fader);
        addAndMakeVisible(learn_.add(new LearnButton(proc_, fid)));
    }
    startTimerHz(8);
}

void PerformPage::timerCallback()
{
    const ParamStore& s = proc_.store();
    for (int d = 0; d < kDecks; ++d)
        for (int b = 0; b < 3; ++b)
            strips_[d].kill[b].setToggleState(s.get(s.id(Module::Deck, d, deck::Low + b)) <= -59.9f, juce::dontSendNotification);
    for (auto* l : learn_) l->refresh();
    if (proc_.pendingNow() < 0 && now_.getText().contains(" from bar ")) now_.setText("rewritten -- it plays on", juce::dontSendNotification);
}

void PerformPage::resized()
{
    auto r = getLocalBounds().reduced(16);
    // The mutes: a row of large pads, each with its learn button below.
    auto mutes = r.removeFromTop(110);
    const int w = mutes.getWidth() / perform::kMutes;
    int li = 0;
    for (int k = 0; k < perform::kMutes; ++k) {
        auto cell = mutes.removeFromLeft(w).reduced(5, 0);
        mutes_[k]->setBounds(cell.removeFromTop(78));
        learn_[li++]->setBounds(cell.removeFromTop(26).reduced(0, 3));
    }
    r.removeFromTop(28);
    auto line = [&](juce::Slider& sl) {
        auto row = r.removeFromTop(40);
        row.removeFromLeft(130);   // the name (paint)
        learn_[li++]->setBounds(row.removeFromRight(80).reduced(4, 8));
        sl.setBounds(row);
        r.removeFromTop(8);
    };
    line(filter_);
    line(throw_);
    line(wheel_);
    auto nowRow = r.removeFromTop(44);
    breakdownNow_.setBounds(nowRow.removeFromLeft(180).reduced(4));
    dropNow_.setBounds(nowRow.removeFromLeft(180).reduced(4));
    now_.setBounds(nowRow.reduced(12, 4));
    r.removeFromTop(16);
    // The decks: three strips, each three kills and a fader.
    const int sw = std::min(260, r.getWidth() / kDecks);
    for (int d = 0; d < kDecks; ++d) {
        auto strip = r.removeFromLeft(sw).reduced(8, 0);
        strip.removeFromTop(22);   // the deck's name (paint)
        auto kills = strip.removeFromLeft(strip.getWidth() / 2);
        for (int b = 2; b >= 0; --b) {
            auto k = kills.removeFromTop(40);
            strips_[d].kill[b].setBounds(k.removeFromLeft(k.getWidth() * 3 / 5).reduced(2));
            learn_[li + b]->setBounds(k.reduced(2, 8));
        }
        li += 3;
        learn_[li++]->setBounds(strip.removeFromBottom(26).reduced(10, 3));
        strips_[d].fader.setBounds(strip.reduced(10, 0));
    }
}

void PerformPage::paint(juce::Graphics& g)
{
    g.fillAll(parhui::colour::panel);
    g.setColour(parhui::colour::dim);
    g.setFont(juce::FontOptions(13.0f));
    auto r = getLocalBounds().reduced(16);
    g.drawText("MUTE -- the keys C3 to F#3 toggle them; a muted group's notes stop, its tails ring out", r.getX(), r.getY() + 112, r.getWidth(), 20,
               juce::Justification::left);
    g.drawText("Master Filter", r.getX(), filter_.getY(), 120, filter_.getHeight(), juce::Justification::centredLeft);
    g.drawText("Echo Throw", r.getX(), throw_.getY(), 120, throw_.getHeight(), juce::Justification::centredLeft);
    g.drawText("Mod Wheel", r.getX(), wheel_.getY(), 120, wheel_.getHeight(), juce::Justification::centredLeft);
    for (int d = 0; d < kDecks; ++d) {
        const auto b = strips_[d].kill[2].getBounds();
        g.setColour(parhui::deckColour(d));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(juce::String("DECK ") + juce::String::charToString(static_cast<juce::juce_wchar>('A' + d)) + (d == 2 ? " (loops)" : ""),
                   b.getX(), b.getY() - 22, 200, 18, juce::Justification::left);
    }
}

// ---------------------------------------------------------------------------------------------------

MixerPage::MixerPage(ParhelionProcessor& p)
    : proc_(p),
      knobs_(std::make_unique<ParamPage>(p, std::vector<std::pair<Module, int>>{ { Module::Mix, 0 }, { Module::Master, 0 }, { Module::Pump, 0 },
                                                                                  { Module::Deck, 0 }, { Module::Deck, 1 }, { Module::Deck, 2 } }))
{
    addAndMakeVisible(knobs_);
    startTimerHz(20);
}

void MixerPage::timerCallback()
{
    float pk[kDecks], rms[kDecks], out = 0.0f, lufs = -70.0f;
    proc_.takeMeters(pk, rms, out, lufs);
    for (int d = 0; d < kDecks; ++d) {
        peak_[d] = std::max(pk[d], peak_[d] * 0.85f);
        rms_[d] = rms[d];
        hold_[d] = std::max(toDb(pk[d]), hold_[d] - 0.6f);
    }
    out_ = std::max(out, out_ * 0.85f);
    hold_[kDecks] = std::max(toDb(out), hold_[kDecks] - 0.6f);
    lufs_ = lufs;
    repaint(getLocalBounds().withWidth(220));
}

void MixerPage::resized()
{
    knobs_.setBounds(getLocalBounds().withTrimmedLeft(220));
}

void MixerPage::paint(juce::Graphics& g)
{
    using namespace parhui::colour;
    g.fillAll(panel);
    auto r = getLocalBounds().withWidth(220).reduced(12);
    g.setColour(ink);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText(lufs_ > -69.0f ? juce::String(lufs_, 1) + " LUFS" : juce::String("-- LUFS"), r.removeFromTop(28), juce::Justification::centredLeft);
    g.setColour(dim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawFittedText("momentary (400 ms, K-weighted); the styles aim at -8 to -10 LUFS over their loudest 20 seconds", r.removeFromTop(30),
                     juce::Justification::topLeft, 3);
    r.removeFromTop(8);
    // Meters: the three decks after their channels, and the output; -48 .. +6 dB.
    const auto yOf = [&](float db, juce::Rectangle<int> m) {
        const float t = juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 54.0f);
        return static_cast<float>(m.getBottom()) - t * static_cast<float>(m.getHeight());
    };
    const int mw = r.getWidth() / (kDecks + 1);
    for (int i = 0; i <= kDecks; ++i) {
        auto m = r.withX(r.getX() + i * mw).withWidth(mw).reduced(8, 0).withTrimmedBottom(22);
        g.setColour(group);
        g.fillRect(m);
        const float pdb = toDb(i < kDecks ? peak_[i] : out_), rdb = i < kDecks ? toDb(rms_[i]) : pdb;
        const juce::Colour c = i < kDecks ? parhui::deckColour(i) : amber;
        g.setColour(c.withAlpha(0.35f));
        g.fillRect(juce::Rectangle<float>(static_cast<float>(m.getX()), yOf(pdb, m), static_cast<float>(m.getWidth()), static_cast<float>(m.getBottom()) - yOf(pdb, m)));
        if (i < kDecks) {
            g.setColour(c.withAlpha(0.85f));
            g.fillRect(juce::Rectangle<float>(static_cast<float>(m.getX()) + 3.0f, yOf(rdb, m), static_cast<float>(m.getWidth()) - 6.0f, static_cast<float>(m.getBottom()) - yOf(rdb, m)));
        }
        g.setColour(hold_[i] > -0.1f ? red : ink);
        g.fillRect(static_cast<float>(m.getX()), yOf(hold_[i], m) - 1.0f, static_cast<float>(m.getWidth()), 2.0f);
        g.setColour(dim);
        g.drawText(i < kDecks ? juce::String::charToString(static_cast<juce::juce_wchar>('A' + i)) : juce::String("Out"), m.getX() - 6, m.getBottom() + 2,
                   m.getWidth() + 12, 18, juce::Justification::centred);
    }
    g.setColour(faint);
    for (int db : { 0, -6, -12, -24, -36 }) {
        const float y = yOf(static_cast<float>(db), r.withTrimmedBottom(22));
        g.fillRect(static_cast<float>(r.getX()), y, static_cast<float>(r.getWidth()), 1.0f);
    }
}
