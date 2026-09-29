/**
 * @file EditorTheme.cpp
 * @brief The palette, the families, the modules' panels and the look and feel (EditorTheme.h).
 * @note Copied from Totality `Plugin/EditorTheme.cpp` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 */
#include "EditorTheme.h"
#include <cmath>

namespace parhui {

using namespace colour;

juce::Colour familyColour(Family f)
{
    switch (f) {
    case Family::Source:   return juce::Colour(0xffe6c178);   // the corona's gold
    case Family::Filter:   return juce::Colour(0xffd9825b);   // copper
    case Family::Envelope: return juce::Colour(0xff9fbf6f);   // sage
    case Family::Motion:   return juce::Colour(0xff6fb8ae);   // teal
    default:               return juce::Colour(0xff6f8fb8);   // steel blue
    }
}

juce::Colour deckColour(int d)
{
    static const juce::uint32 kDeck[3] = { 0xffe6c178, 0xff6fb8ae, 0xffc47fb0 };
    return juce::Colour(kDeck[static_cast<size_t>(juce::jlimit(0, 2, d))]);
}

const std::vector<GroupSpec>& layoutOf(parh::Module m)
{
    using F = Family;
    using M = parh::Module;
    static const std::vector<GroupSpec> none;
    // The modulation block every melodic voice carries (parh/synth/Modulation.h, Phase 5b): the envelope, four LFOs,
    // the matrix's eight slots (source, target, amount).
    auto withMod = [](std::vector<GroupSpec> g) {
        g.push_back({ "Mod Envelope", F::Envelope, { "menv_attack", "menv_decay", "menv_sustain", "menv_release" } });
        g.push_back({ "LFO 1", F::Motion, { "lfo1_rate", "~lfo1_shape", "~lfo1_sync", "lfo1_retrig", "lfo1_fade" } });
        g.push_back({ "LFO 2", F::Motion, { "lfo2_rate", "~lfo2_shape", "~lfo2_sync", "lfo2_retrig", "lfo2_fade" } });
        g.push_back({ "LFO 3", F::Motion, { "lfo3_rate", "~lfo3_shape", "~lfo3_sync", "lfo3_retrig", "lfo3_fade" } });
        g.push_back({ "LFO 4", F::Motion, { "lfo4_rate", "~lfo4_shape", "~lfo4_sync", "lfo4_retrig", "lfo4_fade" } });
        // (Two slots a box: four in one line wrapped a slot's target away from its source.)
        g.push_back({ "Matrix 1-2", F::Motion, { "~mx1_src", "~mx1_dst", "mx1_amount", "~mx2_src", "~mx2_dst", "mx2_amount" } });
        g.push_back({ "Matrix 3-4", F::Motion, { "~mx3_src", "~mx3_dst", "mx3_amount", "~mx4_src", "~mx4_dst", "mx4_amount" } });
        g.push_back({ "Matrix 5-6", F::Motion, { "~mx5_src", "~mx5_dst", "mx5_amount", "~mx6_src", "~mx6_dst", "mx6_amount" } });
        g.push_back({ "Matrix 7-8", F::Motion, { "~mx7_src", "~mx7_dst", "mx7_amount", "~mx8_src", "~mx8_dst", "mx8_amount" } });
        return g;
    };
    static const std::vector<GroupSpec> kick = {
        { "Engine", F::Source, { "~engine", "~tune", "*level", "drive", "clip" } },
        { "Pitch", F::Envelope, { "pitch_start", "*pitch_end", "pitch_decay" } },
        { "Punch and Amp", F::Envelope, { "punch", "punch_decay", "amp_attack", "amp_hold", "*amp_decay", "tail_limit" } },
        { "Click", F::Source, { "click_level", "click_tone", "click_decay" } },
        { "Tone", F::Filter, { "tone", "low_cut", "dip_freq", "dip" } },
        { "Top Layer", F::Source, { "top_level", "top_pitch", "top_decay", "top_drive", "top_cut" } },
    };
    static const std::vector<GroupSpec> sub = {
        { "Sub", F::Source, { "*level", "octave", "drive", "low_pass", "lock" } },
        { "Envelope", F::Envelope, { "attack", "decay", "sustain", "release" } },
        { "Duck", F::Motion, { "*duck", "duck_hold", "duck_release" } },
    };
    static const std::vector<GroupSpec> perc = {
        { "Lane", F::Source, { "active", "~role", "~engine", "*level", "pan", "choke", "density" } },
        { "Pitch", F::Source, { "*pitch", "tune", "pitch_amount", "pitch_decay" } },
        { "FM, Modes, Metal", F::Source, { "fm_ratio", "fm_index", "~mode_set", "mode_damp", "metal_scale" } },
        { "Noise", F::Source, { "noise", "~noise_type", "noise_decay", "bursts", "burst_spacing" } },
        { "Shape", F::Envelope, { "*decay", "shift" } },
        { "Filter", F::Filter, { "~filter", "*cutoff", "resonance", "low_cut", "drive", "cut_track" } },
        { "Space", F::Space, { "pan_depth", "pan_bars" } },
    };
    static const std::vector<GroupSpec> mix = {
        { "Hats", F::Space, { "*hats_level", "hats_cut" } },
        { "Perc", F::Space, { "*perc_level", "perc_cut" } },
        { "Buses", F::Source, { "drum_sat", "low_cut", "*synth_level" } },
    };
    static const std::vector<GroupSpec> master = {
        { "Tone", F::Filter, { "*tilt", "mono_below" } },
        { "Glue", F::Envelope, { "threshold", "ratio" } },
        { "Out", F::Space, { "*level", "clip", "ceiling" } },
    };
    static const std::vector<GroupSpec> synth = withMod({
        { "Oscillator", F::Source, { "*level", "wave", "pulse_width", "sub_osc", "pan", "glide", "drive" } },
        { "Filter", F::Filter, { "~filter", "*cutoff", "*resonance", "env_amount", "decay", "accent", "key_track", "low_cut", "high_cut" } },
        { "Amp", F::Envelope, { "amp_attack", "amp_decay", "amp_sustain", "amp_release" } },
        { "Sends and Duck", F::Space, { "plate_send", "room_send", "duck", "duck_release" } },
    });
    static const std::vector<GroupSpec> poly = [&] {
        std::vector<GroupSpec> g = {
            { "Oscillator", F::Source, { "~osc", "*detune", "mix", "dynamic_detune", "wave", "pulse_width", "drift" } },
            { "Second Oscillator", F::Source, { "~osc2", "osc2_mix", "~osc2_interval", "osc2_detune" } },
            { "FM and Wavetable", F::Source, { "fm_ratio", "fm_index", "fm_decay", "~table", "position", "pos_env", "pos_decay",
                                               "pos_lfo_depth", "pos_lfo_beats" } },
            { "Filter", F::Filter, { "~filter_model", "~filter_type", "filter_mode", "*cutoff", "*resonance", "env_amount",
                                     "key_track", "hp_floor", "hp_track" } },
            { "Filter Envelope", F::Envelope, { "filt_attack", "filter_decay", "filt_sustain", "filt_release" } },
            { "Amp", F::Envelope, { "amp_attack", "amp_decay", "amp_sustain", "amp_release", "vel_sens", "glide" } },
            { "Voice LFO", F::Motion, { "lfo_beats", "lfo_cutoff", "lfo_pitch", "lfo_amp", "slow_mod" } },
            { "Trance Gate", F::Motion, { "gate", "~gate_pattern", "gate_depth", "gate_duty", "gate_attack", "gate_release", "gate_tone" } },
            { "Delay", F::Motion, { "delay_send", "~delay_left", "~delay_right", "delay_feedback", "delay_high_pass", "delay_low_pass" } },
            { "Out", F::Space, { "*level", "pan", "width", "disperse", "disperse_freq", "room_send", "plate_send", "hall_send", "duck" } },
        };
        return withMod(g);
    }();
    static const std::vector<GroupSpec> piano = withMod({
        { "Instrument", F::Source, { "~instrument", "*hardness", "strike", "unison", "inharm", "impedance", "stretch", "condition" } },
        { "Playing", F::Envelope, { "*level", "pedal", "sympathetic", "phantom", "damper_noise", "mechanics" } },
        { "Out", F::Space, { "width", "low_cut", "duck", "room_send", "plate_send", "hall_send" } },
    });
    static const std::vector<GroupSpec> strings = withMod({
        { "Section", F::Source, { "*level", "players", "vibrato" } },
        { "Bow", F::Envelope, { "*pressure", "position", "speed", "attack", "release" } },
        { "Out", F::Space, { "width", "low_cut", "duck", "room_send", "plate_send", "hall_send" } },
    });
    static const std::vector<GroupSpec> choir = withMod({
        { "Voices", F::Source, { "*level", "singers", "*vowel", "vibrato", "breath", "tension" } },
        { "Envelope", F::Envelope, { "attack", "release" } },
        { "Out", F::Space, { "width", "low_cut", "duck", "room_send", "plate_send", "hall_send" } },
    });
    static const std::vector<GroupSpec> brass = withMod({
        { "Players", F::Source, { "*level", "players", "*pressure", "brassiness", "vibrato" } },
        { "Envelope", F::Envelope, { "attack", "release" } },
        { "Out", F::Space, { "width", "low_cut", "duck", "room_send", "plate_send", "hall_send" } },
    });
    static const std::vector<GroupSpec> timpani = withMod({
        { "Drums", F::Source, { "*level", "*hardness", "decay", "strike" } },
        { "Out", F::Space, { "width", "low_cut", "duck", "room_send", "plate_send", "hall_send" } },
    });
    static const std::vector<GroupSpec> sfx = {
        { "Effects", F::Source, { "*level", "noise", "resonance", "*brightness", "vowel", "width" } },
        { "Shapes", F::Envelope, { "impact_decay", "swell_decay", "sub_level", "sub_duck", "wander", "wander_send" } },
        { "Families", F::Source, { "preset_riser", "preset_downlifter", "preset_impact", "preset_sweep", "preset_formant_shot",
                                   "preset_reverse_swell", "preset_zap", "preset_squelch", "preset_bubble", "preset_reverse_crash",
                                   "preset_atmosphere" } },
        { "Sends", F::Space, { "room_send", "plate_send", "hall_send", "duck" } },
    };
    static const std::vector<GroupSpec> cloud = {
        { "Grains", F::Motion, { "*level", "density", "size", "pitch", "spray" } },
        { "Sources", F::Space, { "pad_send", "keys_send", "plate_send" } },
    };
    static const std::vector<GroupSpec> pump = {
        { "The Pump", F::Motion, { "*attack", "hold", "*release", "return_duck" } },
    };
    static const std::vector<GroupSpec> sends = {
        { "Room", F::Space, { "room_size", "room_decay", "room_return" } },
        { "Plate", F::Space, { "plate_decay", "plate_damping", "plate_predelay", "plate_return" } },
        { "Hall", F::Space, { "hall_size", "*hall_decay", "hall_damping", "hall_predelay", "hall_return" } },
        { "Returns", F::Filter, { "low_cut", "high_cut", "hats_send", "perc_send" } },
    };
    static const std::vector<GroupSpec> compose = {
        { "Track", F::Source, { "~style", "~key", "~scale", "*bpm", "minutes", "auto" } },
        { "Style Morph", F::Motion, { "~morph_to", "morph" } },
        { "Sounds and Feel", F::Envelope, { "pick_sounds", "use_ratings", "humanize" } },
    };
    static const std::vector<GroupSpec> set = {
        { "Set", F::Source, { "*minutes", "~dramaturgy", "~journey", "blend", "track_minutes" } },
    };
    static const std::vector<GroupSpec> deck = {
        { "Channel", F::Space, { "*fader", "low", "mid", "high", "*filter", "fx_send" } },
    };
    static const std::vector<GroupSpec> djfx = {
        { "Mixer Effects", F::Motion, { "~echo_time", "feedback", "echo_return", "hall_decay", "hall_return" } },
    };
    static const std::vector<GroupSpec> perform = {
        { "Hands", F::Motion, { "*filter", "*throw", "*wheel", "pressure" } },
        { "Mutes", F::Source, { "mute_kick", "mute_bass", "mute_hats", "mute_perc", "mute_lead", "mute_synths", "mute_pads" } },
    };
    static const std::vector<GroupSpec> cue = {
        { "OSC Cues", F::Motion, { "enabled", "port" } },
    };
    switch (m) {
    case M::Kick: return kick;
    case M::Sub: return sub;
    case M::Perc: return perc;
    case M::Mix: return mix;
    case M::Master: return master;
    case M::Bass: case M::Acid: return synth;
    case M::Poly: return poly;
    case M::Piano: return piano;
    case M::Strings: return strings;
    case M::Choir: return choir;
    case M::Brass: return brass;
    case M::Timpani: return timpani;
    case M::Sfx: return sfx;
    case M::Cloud: return cloud;
    case M::Pump: return pump;
    case M::Sends: return sends;
    case M::Compose: return compose;
    case M::Set: return set;
    case M::Deck: return deck;
    case M::DjFx: return djfx;
    case M::Perform: return perform;
    case M::Cue: return cue;
    default: return none;
    }
}

// ---------------------------------------------------------------------------------------------------------------------

LookAndFeel::LookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, bg);
    setColour(juce::Label::textColourId, ink);
    setColour(juce::Slider::textBoxTextColourId, dim);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, amber.withAlpha(0.35f));
    setColour(juce::Slider::rotarySliderFillColourId, amber);
    setColour(juce::Slider::thumbColourId, amber);
    setColour(juce::Slider::trackColourId, amber.withAlpha(0.6f));
    setColour(juce::Slider::backgroundColourId, faint);
    setColour(juce::ComboBox::backgroundColourId, raised);
    setColour(juce::ComboBox::outlineColourId, edge);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::arrowColourId, dim);
    setColour(juce::PopupMenu::backgroundColourId, group);
    setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, amber.withAlpha(0.25f));
    setColour(juce::PopupMenu::highlightedTextColourId, ink);
    setColour(juce::PopupMenu::headerTextColourId, amber);
    setColour(juce::TextButton::buttonColourId, raised);
    setColour(juce::TextButton::buttonOnColourId, amber.withAlpha(0.35f));
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::TextButton::textColourOnId, ink);
    setColour(juce::ToggleButton::textColourId, dim);
    setColour(juce::ToggleButton::tickColourId, amber);
    setColour(juce::TextEditor::backgroundColourId, raised);
    setColour(juce::TextEditor::textColourId, ink);
    setColour(juce::TextEditor::outlineColourId, edge);
    setColour(juce::TabbedComponent::backgroundColourId, panel);
    setColour(juce::TabbedComponent::outlineColourId, edge);
    setColour(juce::ScrollBar::thumbColourId, faint);
    setColour(juce::TooltipWindow::backgroundColourId, group);
    setColour(juce::TooltipWindow::textColourId, ink);
    setColour(juce::TooltipWindow::outlineColourId, edge);
    setColour(juce::AlertWindow::backgroundColourId, group);
    setColour(juce::AlertWindow::textColourId, ink);
    setColour(juce::AlertWindow::outlineColourId, edge);
}

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float startAngle,
                                   float endAngle, juce::Slider& slider)
{
    // A knob as on an instrument's panel: a dark cap with its pointer, and around it the value as an arc in the colour
    // of its family -- from the top where the parameter spans zero (pan, transposition), from the start otherwise.
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    const float size = std::min(bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float ring = size * 0.5f - 2.0f, track = std::max(2.5f, size * 0.07f);
    const juce::Colour colour = slider.findColour(juce::Slider::rotarySliderFillColourId);
    const float angle = startAngle + pos * (endAngle - startAngle);
    float from = startAngle;
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        from = startAngle + static_cast<float>(slider.valueToProportionOfLength(0.0)) * (endAngle - startAngle);
    juce::Path arc;
    arc.addCentredArc(c.x, c.y, ring - track * 0.5f, ring - track * 0.5f, 0.0f, startAngle, endAngle, true);
    g.setColour(faint.withAlpha(0.55f));
    g.strokePath(arc, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path value;
    value.addCentredArc(c.x, c.y, ring - track * 0.5f, ring - track * 0.5f, 0.0f, std::min(from, angle), std::max(from, angle), true);
    g.setColour(slider.isEnabled() ? colour : faint);
    g.strokePath(value, juce::PathStrokeType(track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // The cap, lit a little from above, with a hairline rim.
    const float cap = ring - track - std::max(2.0f, size * 0.05f);
    g.setGradientFill(juce::ColourGradient(raised.brighter(0.25f), c.x, c.y - cap, raised.darker(0.35f), c.x, c.y + cap, false));
    g.fillEllipse(c.x - cap, c.y - cap, 2.0f * cap, 2.0f * cap);
    g.setColour(edge.brighter(0.2f));
    g.drawEllipse(c.x - cap, c.y - cap, 2.0f * cap, 2.0f * cap, 1.0f);
    // The pointer.
    const float s = std::sin(angle), co = -std::cos(angle);
    g.setColour(ink);
    g.drawLine(c.x + s * cap * 0.25f, c.y + co * cap * 0.25f, c.x + s * cap * 0.85f, c.y + co * cap * 0.85f, std::max(1.5f, size * 0.035f));
}

void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float pos, float, float,
                                   juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, pos, 0.0f, 0.0f, style, slider);
        return;
    }
    const float cy = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
    g.setColour(faint);
    g.fillRoundedRectangle(static_cast<float>(x), cy - 2.0f, static_cast<float>(width), 4.0f, 2.0f);
    g.setColour(slider.findColour(juce::Slider::trackColourId));
    // A slider that spans zero (a matrix slot's amount) fills from its middle.
    float from = static_cast<float>(x);
    if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
        from = static_cast<float>(x) + static_cast<float>(slider.valueToProportionOfLength(0.0)) * static_cast<float>(width);
    g.fillRoundedRectangle(std::min(from, pos), cy - 2.0f, std::fabs(pos - from), 4.0f, 2.0f);
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.fillEllipse(pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
}

void LookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // A small switch with its lamp: lit in the button's colour when on.
    const auto r = b.getLocalBounds().toFloat();
    const float h = std::min(16.0f, r.getHeight() - 4.0f);
    const juce::Rectangle<float> box(r.getX() + 2.0f, r.getCentreY() - h * 0.5f, h * 1.7f, h);
    const bool on = b.getToggleState();
    const juce::Colour colour = b.findColour(juce::ToggleButton::tickColourId);
    g.setColour(on ? colour.withAlpha(0.35f) : raised);
    g.fillRoundedRectangle(box, h * 0.5f);
    g.setColour(highlighted ? edge.brighter(0.4f) : edge);
    g.drawRoundedRectangle(box, h * 0.5f, 1.0f);
    const float d = h - 6.0f;
    g.setColour(on ? colour : dim);
    g.fillEllipse(on ? box.getRight() - d - 3.0f : box.getX() + 3.0f, box.getY() + 3.0f, d, d);
    if (b.getButtonText().isNotEmpty()) {
        g.setColour(on ? ink : dim);
        g.setFont(juce::FontOptions(13.0f));
        g.drawFittedText(b.getButtonText(), box.getRight() + 6.0f > r.getRight() ? r.toNearestInt() : r.withLeft(box.getRight() + 6.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1);
    }
}

void LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(box.hasKeyboardFocus(true) || box.isMouseOver(true) ? edge.brighter(0.4f) : box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
    const float ax = static_cast<float>(width) - 14.0f, ay = static_cast<float>(height) * 0.5f;
    juce::Path arrow;
    arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}

void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& background, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    juce::Colour fill = b.getToggleState() ? b.findColour(juce::TextButton::buttonOnColourId) : background;
    if (down) fill = fill.brighter(0.15f);
    else if (highlighted) fill = fill.brighter(0.08f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(highlighted ? edge.brighter(0.45f) : edge);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
}

void LookAndFeel::drawTabButton(juce::TabBarButton& b, juce::Graphics& g, bool isMouseOver, bool)
{
    // Flat tabs: the name, the page in front lit amber and underlined.
    const auto r = b.getLocalBounds().toFloat();
    const bool front = b.isFrontTab();
    if (front) {
        g.setColour(panel);
        g.fillRect(r);
        g.setColour(amber);
        g.fillRect(r.getX() + 6.0f, r.getBottom() - 2.5f, r.getWidth() - 12.0f, 2.5f);
    } else if (isMouseOver) {
        g.setColour(group);
        g.fillRect(r);
    }
    g.setColour(front ? amber : (isMouseOver ? ink : dim));
    g.setFont(getTabButtonFont(b, r.getHeight()));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

void LookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    g.setColour(bg);
    g.fillRect(bar.getLocalBounds());
    g.setColour(edge);
    g.fillRect(0, bar.getHeight() - 1, bar.getWidth(), 1);
}

void LookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) {}

int LookAndFeel::getTabButtonBestWidth(juce::TabBarButton& b, int tabDepth)
{
    return juce::GlyphArrangement::getStringWidthInt(getTabButtonFont(b, static_cast<float>(tabDepth)), b.getButtonText()) + 22;
}

juce::Font LookAndFeel::getTabButtonFont(juce::TabBarButton&, float) { return juce::FontOptions(14.0f); }
juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&) { return juce::FontOptions(13.5f); }
juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int) { return juce::FontOptions(13.5f); }
juce::Font LookAndFeel::getPopupMenuFont() { return juce::FontOptions(14.0f); }

juce::Label* LookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(slider);
    l->setFont(juce::FontOptions(12.0f));
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::textColourId, dim);
    return l;
}


} // namespace parhui
