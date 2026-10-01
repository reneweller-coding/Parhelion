/**
 * @file EditorTheme.cpp
 * @brief The palette, the families, the modules' panels and the look and feel (EditorTheme.h).
 * @note Copied from Totality `Plugin/EditorTheme.cpp` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 */
#include "EditorTheme.h"
#include "ParhelionData.h"

void drawLogo(juce::Graphics& g, juce::Rectangle<float> r);   ///< PluginEditor.cpp
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

const frame::Skin& skin()
{
    static const frame::Skin s = [] {
        frame::Skin k;
        k.name = "Parhelion";
        k.bg = bg; k.panel = panel; k.group = group; k.raised = raised; k.edge = edge;
        k.ink = ink; k.dim = dim; k.faint = faint; k.accent = accent; k.onset = onset; k.good = green; k.bad = red;
        for (int f = 0; f < 5; ++f) k.families[f] = familyColour(static_cast<Family>(f));
        for (int d = 0; d < 3; ++d) k.decks[d] = deckColour(d);
        k.radius = 7.0f;               // soft, as light through ice
        k.glow = true;
        k.tracking = 0.2f;
        k.typeface = "Segoe UI Light";
        k.titleBold = false;
        k.backdropData = ParhelionData::backdrop_jpg;
        k.backdropSize = ParhelionData::backdrop_jpgSize;
        k.backdropTop = 0.8f;
        k.backdropPage = 0.5f;
        k.logo = [](juce::Graphics& g, juce::Rectangle<float> r) { drawLogo(g, r); };
        return k;
    }();
    return s;
}

} // namespace parhui
