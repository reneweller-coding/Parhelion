/**
 * @file EditorTheme.cpp
 * @brief The palette, the families, the modules' panels and the look and feel (EditorTheme.h).
 * @note Copied from Totality `Plugin/EditorTheme.cpp` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 * @author Rene Weller
 */
#include "EditorTheme.h"
#include "PluginEditor.h"   // drawLogo
#include "ParhelionData.h"
#include <cmath>

namespace parhui
{

using namespace colour;

juce::Colour familyColour( Family f )
{
    switch ( f )
    {
    case Family::SOURCE: return juce::Colour( 0xffe6c178 );     // the corona's gold
    case Family::FILTER: return juce::Colour( 0xffd9825b );     // copper
    case Family::ENVELOPE: return juce::Colour( 0xff9fbf6f );   // sage
    case Family::MOTION: return juce::Colour( 0xff6fb8ae );     // teal
    default: return juce::Colour( 0xff6f8fb8 );                 // steel blue
    }
}

juce::Colour deckColour( int d )
{
    static const juce::uint32 Deck[3] = { 0xffe6c178, 0xff6fb8ae, 0xffc47fb0 };
    return juce::Colour( Deck[static_cast<size_t>( juce::jlimit( 0, 2, d ) )] );
}

const std::vector<GroupSpec> &layoutOf( parh::ModuleE m )
{
    using F = Family;
    using MT = parh::ModuleE;
    static const std::vector<GroupSpec> none;
    // The modulation block every melodic voice carries (parh/synth/Modulation.h, Phase 5b): the envelope, four LFOs,
    // the matrix's eight slots (source, target, amount).
    auto withMod = []( std::vector<GroupSpec> g )
    {
        g.push_back( { "Mod Envelope", F::ENVELOPE, { "menv_attack", "menv_decay", "menv_sustain", "menv_release" } } );
        g.push_back( { "LFO 1", F::MOTION, { "lfo1_rate", "~lfo1_shape", "~lfo1_sync", "lfo1_retrig", "lfo1_fade" } } );
        g.push_back( { "LFO 2", F::MOTION, { "lfo2_rate", "~lfo2_shape", "~lfo2_sync", "lfo2_retrig", "lfo2_fade" } } );
        g.push_back( { "LFO 3", F::MOTION, { "lfo3_rate", "~lfo3_shape", "~lfo3_sync", "lfo3_retrig", "lfo3_fade" } } );
        g.push_back( { "LFO 4", F::MOTION, { "lfo4_rate", "~lfo4_shape", "~lfo4_sync", "lfo4_retrig", "lfo4_fade" } } );
        // (Two slots a box: four in one line wrapped a slot's target away from its source.)
        g.push_back( { "Matrix 1-2",
                       F::MOTION,
                       { "~mx1_src", "~mx1_dst", "mx1_amount", "~mx2_src", "~mx2_dst", "mx2_amount" } } );
        g.push_back( { "Matrix 3-4",
                       F::MOTION,
                       { "~mx3_src", "~mx3_dst", "mx3_amount", "~mx4_src", "~mx4_dst", "mx4_amount" } } );
        g.push_back( { "Matrix 5-6",
                       F::MOTION,
                       { "~mx5_src", "~mx5_dst", "mx5_amount", "~mx6_src", "~mx6_dst", "mx6_amount" } } );
        g.push_back( { "Matrix 7-8",
                       F::MOTION,
                       { "~mx7_src", "~mx7_dst", "mx7_amount", "~mx8_src", "~mx8_dst", "mx8_amount" } } );
        return g;
    };
    static const std::vector<GroupSpec> kick = {
        { "Engine", F::SOURCE, { "~engine", "~tune", "*level", "drive", "clip" } },
        { "Pitch", F::ENVELOPE, { "pitch_start", "*pitch_end", "pitch_decay" } },
        { "Punch and Amp",
          F::ENVELOPE,
          { "punch", "punch_decay", "amp_attack", "amp_hold", "*amp_decay", "tail_limit" } },
        { "Click", F::SOURCE, { "click_level", "click_tone", "click_decay" } },
        { "Tone", F::FILTER, { "tone", "low_cut", "dip_freq", "dip" } },
        { "Top Layer", F::SOURCE, { "top_level", "top_pitch", "top_decay", "top_drive", "top_cut" } },
    };
    static const std::vector<GroupSpec> sub = {
        { "Sub", F::SOURCE, { "*level", "octave", "drive", "low_pass", "lock" } },
        { "Envelope", F::ENVELOPE, { "attack", "decay", "sustain", "release" } },
        { "Duck", F::MOTION, { "*duck", "duck_hold", "duck_release" } },
    };
    static const std::vector<GroupSpec> perc = {
        { "Lane", F::SOURCE, { "active", "~role", "~engine", "*level", "pan", "choke", "density" } },
        { "Pitch", F::SOURCE, { "*pitch", "tune", "pitch_amount", "pitch_decay" } },
        { "FM, Modes, Metal", F::SOURCE, { "fm_ratio", "fm_index", "~mode_set", "mode_damp", "metal_scale" } },
        { "Noise", F::SOURCE, { "noise", "~noise_type", "noise_decay", "bursts", "burst_spacing" } },
        { "Shape", F::ENVELOPE, { "*decay", "shift" } },
        { "Filter", F::FILTER, { "~filter", "*cutoff", "resonance", "low_cut", "drive", "cut_track" } },
        { "Space", F::SPACE, { "pan_depth", "pan_bars" } },
    };
    static const std::vector<GroupSpec> mix = {
        { "Hats", F::SPACE, { "*hats_level", "hats_cut" } },
        { "Perc", F::SPACE, { "*perc_level", "perc_cut" } },
        { "Buses", F::SOURCE, { "drum_sat", "low_cut", "*synth_level" } },
    };
    static const std::vector<GroupSpec> master = {
        { "Tone", F::FILTER, { "*tilt", "mono_below" } },
        { "Glue", F::ENVELOPE, { "threshold", "ratio" } },
        { "Out", F::SPACE, { "*level", "clip", "ceiling" } },
    };
    static const std::vector<GroupSpec> synth = withMod( {
        { "Oscillator", F::SOURCE, { "*level", "wave", "pulse_width", "sub_osc", "pan", "glide", "drive" } },
        { "Filter",
          F::FILTER,
          { "~filter", "*cutoff", "*resonance", "env_amount", "decay", "accent", "key_track", "low_cut", "high_cut" } },
        { "Amp", F::ENVELOPE, { "amp_attack", "amp_decay", "amp_sustain", "amp_release" } },
        { "Sends and Duck", F::SPACE, { "plate_send", "room_send", "duck", "duck_release" } },
    } );
    static const std::vector<GroupSpec> poly = [&]
    {
        std::vector<GroupSpec> g = {
            { "Oscillator", F::SOURCE, { "~osc", "*detune", "mix", "dynamic_detune", "wave", "pulse_width", "drift" } },
            { "Second Oscillator", F::SOURCE, { "~osc2", "osc2_mix", "~osc2_interval", "osc2_detune" } },
            { "FM and Wavetable",
              F::SOURCE,
              { "fm_ratio", "fm_index", "fm_decay", "~table", "position", "pos_env", "pos_decay", "pos_lfo_depth",
                "pos_lfo_beats" } },
            { "Filter",
              F::FILTER,
              { "~filter_model", "~filter_type", "filter_mode", "*cutoff", "*resonance", "env_amount", "key_track",
                "hp_floor", "hp_track" } },
            { "Filter Envelope", F::ENVELOPE, { "filt_attack", "filter_decay", "filt_sustain", "filt_release" } },
            { "Amp", F::ENVELOPE, { "amp_attack", "amp_decay", "amp_sustain", "amp_release", "vel_sens", "glide" } },
            { "Voice LFO", F::MOTION, { "lfo_beats", "lfo_cutoff", "lfo_pitch", "lfo_amp", "slow_mod" } },
            { "Trance Gate",
              F::MOTION,
              { "gate", "~gate_pattern", "gate_depth", "gate_duty", "gate_attack", "gate_release", "gate_tone" } },
            { "Delay",
              F::MOTION,
              { "delay_send", "~delay_left", "~delay_right", "delay_feedback", "delay_high_pass", "delay_low_pass" } },
            { "Out",
              F::SPACE,
              { "*level", "pan", "width", "disperse", "disperse_freq", "room_send", "plate_send", "hall_send",
                "duck" } },
        };
        return withMod( g );
    }();
    static const std::vector<GroupSpec> piano = withMod( {
        { "Instrument",
          F::SOURCE,
          { "~instrument", "*hardness", "strike", "unison", "inharm", "impedance", "stretch", "condition" } },
        { "Playing", F::ENVELOPE, { "*level", "pedal", "sympathetic", "phantom", "damper_noise", "mechanics" } },
        { "Out", F::SPACE, { "width", "low_cut", "duck", "room_send", "plate_send", "hall_send" } },
    } );
    static const std::vector<GroupSpec> strings = withMod( {
        { "Section", F::SOURCE, { "*level", "players", "vibrato" } },
        { "Bow", F::ENVELOPE, { "*pressure", "position", "speed", "attack", "release" } },
        { "Out", F::SPACE, { "width", "low_cut", "duck", "room_send", "plate_send", "hall_send" } },
    } );
    static const std::vector<GroupSpec> choir = withMod( {
        { "Voices", F::SOURCE, { "*level", "singers", "*vowel", "vibrato", "breath", "tension" } },
        { "Envelope", F::ENVELOPE, { "attack", "release" } },
        { "Out", F::SPACE, { "width", "low_cut", "duck", "room_send", "plate_send", "hall_send" } },
    } );
    static const std::vector<GroupSpec> brass = withMod( {
        { "Players", F::SOURCE, { "*level", "players", "*pressure", "brassiness", "vibrato" } },
        { "Envelope", F::ENVELOPE, { "attack", "release" } },
        { "Out", F::SPACE, { "width", "low_cut", "duck", "room_send", "plate_send", "hall_send" } },
    } );
    static const std::vector<GroupSpec> timpani = withMod( {
        { "Drums", F::SOURCE, { "*level", "*hardness", "decay", "strike" } },
        { "Out", F::SPACE, { "width", "low_cut", "duck", "room_send", "plate_send", "hall_send" } },
    } );
    static const std::vector<GroupSpec> sfx = {
        { "Effects", F::SOURCE, { "*level", "noise", "resonance", "*brightness", "vowel", "width" } },
        { "Shapes", F::ENVELOPE, { "impact_decay", "swell_decay", "sub_level", "sub_duck", "wander", "wander_send" } },
        { "Families",
          F::SOURCE,
          { "preset_riser", "preset_downlifter", "preset_impact", "preset_sweep", "preset_formant_shot",
            "preset_reverse_swell", "preset_zap", "preset_squelch", "preset_bubble", "preset_reverse_crash",
            "preset_atmosphere" } },
        { "Sends", F::SPACE, { "room_send", "plate_send", "hall_send", "duck" } },
    };
    static const std::vector<GroupSpec> cloud = {
        { "Grains", F::MOTION, { "*level", "density", "size", "pitch", "spray" } },
        { "Sources", F::SPACE, { "pad_send", "keys_send", "plate_send" } },
    };
    static const std::vector<GroupSpec> pump = {
        { "The Pump", F::MOTION, { "*attack", "hold", "*release", "return_duck" } },
    };
    static const std::vector<GroupSpec> sends = {
        { "Room", F::SPACE, { "room_size", "room_decay", "room_return" } },
        { "Plate", F::SPACE, { "plate_decay", "plate_damping", "plate_predelay", "plate_return" } },
        { "Hall", F::SPACE, { "hall_size", "*hall_decay", "hall_damping", "hall_predelay", "hall_return" } },
        { "Returns", F::FILTER, { "low_cut", "high_cut", "hats_send", "perc_send" } },
    };
    static const std::vector<GroupSpec> compose = {
        { "Track", F::SOURCE, { "~style", "~key", "~scale", "*bpm", "minutes", "auto" } },
        { "Style Morph", F::MOTION, { "~morph_to", "morph" } },
        { "Sounds and Feel", F::ENVELOPE, { "pick_sounds", "use_ratings", "humanize" } },
    };
    static const std::vector<GroupSpec> set = {
        { "Set", F::SOURCE, { "*minutes", "~dramaturgy", "~journey", "blend", "track_minutes" } },
    };
    static const std::vector<GroupSpec> deck = {
        { "Channel", F::SPACE, { "*fader", "low", "mid", "high", "*filter", "fx_send" } },
    };
    static const std::vector<GroupSpec> djfx = {
        { "Mixer Effects", F::MOTION, { "~echo_time", "feedback", "echo_return", "hall_decay", "hall_return" } },
    };
    static const std::vector<GroupSpec> perform = {
        { "Hands", F::MOTION, { "*filter", "*throw", "*wheel", "pressure" } },
        { "Mutes",
          F::SOURCE,
          { "mute_kick", "mute_bass", "mute_hats", "mute_perc", "mute_lead", "mute_synths", "mute_pads" } },
    };
    static const std::vector<GroupSpec> cue = {
        { "OSC Cues", F::MOTION, { "enabled", "port" } },
    };
    switch ( m )
    {
    case MT::KICK: return kick;
    case MT::SUB: return sub;
    case MT::PERC: return perc;
    case MT::MIX: return mix;
    case MT::MASTER: return master;
    case MT::BASS:
    case MT::ACID: return synth;
    case MT::POLY: return poly;
    case MT::PIANO: return piano;
    case MT::STRINGS: return strings;
    case MT::CHOIR: return choir;
    case MT::BRASS: return brass;
    case MT::TIMPANI: return timpani;
    case MT::SFX: return sfx;
    case MT::CLOUD: return cloud;
    case MT::PUMP: return pump;
    case MT::SENDS: return sends;
    case MT::COMPOSE: return compose;
    case MT::SET: return set;
    case MT::DECK: return deck;
    case MT::DJ_FX: return djfx;
    case MT::PERFORM: return perform;
    case MT::CUE: return cue;
    default: return none;
    }
}

// ---------------------------------------------------------------------------------------------------------------------

const frame::Skin &skin()
{
    static const frame::Skin s = []
    {
        frame::Skin k;
        k.m_name = "Parhelion";
        k.m_bg = Bg;
        k.m_panel = Panel;
        k.m_group = Group;
        k.m_raised = Raised;
        k.m_edge = Edge;
        k.m_ink = Ink;
        k.m_dim = Dim;
        k.m_faint = Faint;
        k.m_accent = Accent;
        k.m_onset = Onset;
        k.m_good = Green;
        k.m_bad = Red;
        for ( int f = 0; f < 5; ++f ) k.m_families[f] = familyColour( static_cast<Family>( f ) );
        for ( int d = 0; d < 3; ++d ) k.m_decks[d] = deckColour( d );
        k.m_radius = 7.0f;   // soft, as light through ice
        k.m_glow = true;
        k.m_tracking = 0.2f;
        k.m_typeface = "Segoe UI Light";
        k.m_titleBold = false;
        k.m_backdropData = ParhelionData::backdrop_jpg;
        k.m_backdropSize = ParhelionData::backdrop_jpgSize;
        k.m_backdropTop = 0.8f;
        k.m_backdropPage = 0.5f;
        k.m_logo = []( juce::Graphics &g, juce::Rectangle<float> r ) { drawLogo( g, r ); };
        return k;
    }();
    return s;
}

}   // namespace parhui
