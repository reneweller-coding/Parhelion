/**
 * @file EditorStyle.cpp
 * @brief The Style tab (EditorStyle.h).
 * @author Rene Weller
 */
#include "EditorStyle.h"
#include "parh/Presets.h"
#include "parh/compose/Style.h"
#include <functional>

using namespace parh;

namespace
{

/** @brief One of the six axes: its name, its range on the panel, its value in a profile, how it reads. */
struct Axis
{
    const char                                  *m_name;    ///< the axis' name
    float                                        m_lo;      ///< the low end of its range on the panel
    float                                        m_hi;      ///< ... and the high end
    std::function<float( const StyleProfile & )> m_value;   ///< the axis' value in a profile
    std::function<juce::String( float )>         m_text;    ///< how a value reads
};

/** @brief the lead archetypes, in the order of LeadKind */
const char *const LeadNames[static_cast<int>( LeadKindE::COUNT )] = { "Supersaw", "Piano", "303", "Pluck and Arp",
                                                                      "Pad" };
/**
 * @brief Where each lead archetype sits on its axis (from the dry riff to the wide pad).
 * @param[in] k  the lead archetype
 * @return its place on the axis, 0..1
 */
float leadPlace( LeadKindE k )
{
    switch ( k )
    {
    case LeadKindE::ACID: return 0.0f;
    case LeadKindE::PLUCK_ARP: return 1.0f;
    case LeadKindE::SUPERSAW: return 2.0f;
    case LeadKindE::PIANO: return 3.0f;
    default: return 4.0f;
    }
}

/**
 * @brief The six axes, built on first use.
 * @return the axes
 */
const std::vector<Axis> &axes()
{
    static const std::vector<Axis> a = {
        { "Tempo", 120.0f, 145.0f, []( const StyleProfile &p ) { return 0.5f * ( p.m_bpmLow + p.m_bpmHigh ); },
          []( float v ) { return juce::String( v, 1 ) + " BPM"; } },
        { "Drum density", 0.0f, 1.0f,
          []( const StyleProfile &p ) { return 0.4f * p.m_hats16 + 0.3f * p.m_perc + 0.3f * p.m_ride; },
          []( float v ) { return juce::String( juce::roundToInt( 100.0f * v ) ) + " %"; } },
        { "Lead", 0.0f, 4.0f, []( const StyleProfile &p ) { return leadPlace( p.m_lead ); },
          []( float v )
          {
              for ( int k = 0; k < static_cast<int>( LeadKindE::COUNT ); ++k )
                  if ( leadPlace( static_cast<LeadKindE>( k ) ) == v ) return juce::String( LeadNames[k] );
              return juce::String();
          } },
        { "Breakdown share", 0.0f, 0.35f,
          []( const StyleProfile &p ) { return 0.5f * ( p.m_breakdownLow + p.m_breakdownHigh ); },
          []( float v ) { return juce::String( juce::roundToInt( 100.0f * v ) ) + " %"; } },
        { "Reverb (breakdown)", 0.0f, 10.0f, []( const StyleProfile &p ) { return p.m_hallBreakS; },
          []( float v ) { return juce::String( v, 1 ) + " s"; } },
        { "Palette", 0.3f, 0.7f, []( const StyleProfile &p ) { return styleBrightness( p.m_mix.data() ); },
          []( float v )
          {
              return v < 0.45f ? juce::String( "dark" ) : v < 0.58f ? juce::String( "warm" ) : juce::String( "bright" );
          } },
    };
    return a;
}

/** The references' medians per style (Tools/ref_stats.json, 30 recordings, 29.09.2026). */
struct RefRow
{
    const char *m_what;        ///< the measure
    const char *m_values[5];   ///< its median per style
};
/** @brief the references' medians, a row per measure */
const RefRow Refs[] = {
    { "Tempo, BPM", { "138", "135", "137", "134", "131" } },
    { "Length, min", { "8.6", "8.2", "6.9", "7.5", "8.7" } },
    { "Loudness, LUFS", { "-9.5", "-10.2", "-11.8", "-11.0", "-11.5" } },
    { "Loudest 20 s, LUFS", { "-7.8", "-8.2", "-10.0", "-9.6", "-8.6" } },
    { "Loudness range, LU", { "6.7", "8.8", "7.0", "5.0", "9.9" } },
    { "Breakdown share", { "0.14", "0.13", "0.00", "0.10", "0.15" } },
    { "Breakdown under drop, LU", { "2.1", "3.3", "6.0", "2.9", "3.2" } },
    { "Rise into the drop, dB", { "2.7", "3.0", "5.8", "5.9", "5.1" } },
    { "Centroid, Hz", { "724", "604", "580", "262", "579" } },
    { "Side over mid (> 200 Hz), dB", { "-4.9", "-6.5", "-7.5", "-11.8", "-7.5" } },
};
const char *const StyleShort[5] = { "Uplifting", "Progr.", "Dream H.", "Acid", "Deep" };   ///< (the full names crowded)

/** @brief the styles' colours: Uplifting, Progressive, Dream House, Acid, Deep */
const juce::Colour StyleColour[5] = { juce::Colour( 0xffe6c178 ), juce::Colour( 0xff6fb8ae ),
                                      juce::Colour( 0xffc47fb0 ), juce::Colour( 0xffd9825b ),
                                      juce::Colour( 0xff6f8fb8 ) };

}   // namespace

StylePage::StylePage( ParhelionProcessor &p )
    : m_proc( p ),
      m_knobs( std::make_unique<ParamPage>( p, std::vector<std::pair<ModuleE, int>>{ { ModuleE::COMPOSE, 0 } } ) )
{
    addAndMakeVisible( m_knobs );
    startTimerHz( 4 );
}

void StylePage::timerCallback()
{
    const StyleProfile pr = profileOf( m_proc.store() );
    juce::String       now;
    for ( const Axis &a : axes() ) now << a.m_value( pr ) << " ";
    if ( now != m_shown )
    {
        m_shown = now;
        repaint();
    }
}

void StylePage::resized()
{
    auto r = getLocalBounds().reduced( 10 );
    m_knobsH = std::min(
        m_knobs.contentHeight( r.getWidth() ),
        std::max( 120, r.getHeight() -
                           300 ) );   // the chart keeps 300 (330 until 01.10.2026: a section of the knobs fits whole)
    m_knobs.setBounds( r.removeFromTop( m_knobsH ) );
}

void StylePage::paint( juce::Graphics &g )
{

    const StyleProfile now = profileOf( m_proc.store() );
    auto               r = getLocalBounds().reduced( 10 );
    r.removeFromTop( m_knobsH + 10 );
    auto chart = r.removeFromLeft( r.getWidth() * 3 / 5 ).reduced( 4 );
    auto table = r.reduced( 4 );

    // The six axes: a track each, the five styles as marks, the profile the knobs describe as a bar.
    g.setFont( juce::FontOptions( 13.0f ) );
    g.setColour( parhui::colour::Accent );
    g.drawText( "The profile on the six axes of the sub-genres", chart.removeFromTop( 22 ),
                juce::Justification::centredLeft );
    const int   rows = static_cast<int>( axes().size() );
    const float rowH = std::min( 56.0f, static_cast<float>( chart.getHeight() - 24 ) / static_cast<float>( rows ) );
    for ( int i = 0; i < rows; ++i )
    {
        const Axis                  &a = axes()[static_cast<size_t>( i )];
        const float                  y = static_cast<float>( chart.getY() ) + rowH * static_cast<float>( i );
        const juce::Rectangle<float> label( static_cast<float>( chart.getX() ), y, 150.0f, rowH );
        const juce::Rectangle<float> track( static_cast<float>( chart.getX() ) + 156.0f, y + rowH * 0.45f,
                                            static_cast<float>( chart.getWidth() ) - 250.0f, 4.0f );
        g.setColour( parhui::colour::Dim );
        g.setFont( juce::FontOptions( 12.0f ) );
        g.drawText( a.m_name, label, juce::Justification::centredLeft );
        g.setColour( parhui::colour::Faint );
        g.fillRoundedRectangle( track, 2.0f );
        auto xOf = [&]( float v )
        { return track.getX() + track.getWidth() * juce::jlimit( 0.0f, 1.0f, ( v - a.m_lo ) / ( a.m_hi - a.m_lo ) ); };
        for ( int s = 0; s < 5; ++s )
        {
            const float x = xOf( a.m_value( styleProfile( static_cast<StyleE>( s ) ) ) );
            g.setColour( StyleColour[s].withAlpha( 0.8f ) );
            g.fillEllipse( x - 4.0f, track.getCentreY() - 10.0f, 8.0f, 8.0f );
        }
        const float v = a.m_value( now ), x = xOf( v );
        g.setColour( parhui::colour::Accent );
        g.fillRect( juce::Rectangle<float>( track.getX(), track.getY(), x - track.getX(), track.getHeight() ) );
        g.fillEllipse( x - 6.0f, track.getCentreY() - 6.0f, 12.0f, 12.0f );
        g.setColour( parhui::colour::Ink );
        g.drawText( a.m_text( v ), juce::Rectangle<float>( track.getRight() + 8.0f, y, 90.0f, rowH ),
                    juce::Justification::centredLeft );
    }
    // The legend.
    auto legend = juce::Rectangle<int>( chart.getX() + 156,
                                        chart.getY() + static_cast<int>( rowH * static_cast<float>( rows ) ) + 4,
                                        chart.getWidth() - 156, 18 );
    g.setFont( juce::FontOptions( 11.0f ) );
    for ( int s = 0; s < 5; ++s )
    {
        auto cell = legend.removeFromLeft( legend.getWidth() / ( 5 - s ) );
        g.setColour( StyleColour[s] );
        g.fillEllipse( static_cast<float>( cell.getX() ), static_cast<float>( cell.getCentreY() ) - 4.0f, 8.0f, 8.0f );
        g.setColour( parhui::colour::Dim );
        g.drawText( StyleShort[s], cell.withTrimmedLeft( 12 ), juce::Justification::centredLeft );
    }

    // The references' medians.
    g.setColour( parhui::colour::Accent );
    g.setFont( juce::FontOptions( 13.0f ) );
    g.drawText( "The references (medians of 30 recordings)", table.removeFromTop( 22 ),
                juce::Justification::centredLeft );
    const int cols = 5;
    const int nameW = 160, colW = std::max( 40, ( table.getWidth() - nameW ) / cols );
    g.setFont( juce::FontOptions( 11.0f ) );
    auto head = table.removeFromTop( 18 );
    head.removeFromLeft( nameW );
    for ( int c = 0; c < cols; ++c )
    {
        g.setColour( StyleColour[c] );
        g.drawFittedText( StyleShort[c], head.removeFromLeft( colW ), juce::Justification::centred, 1 );
    }
    for ( const RefRow &row : Refs )
    {
        auto line = table.removeFromTop( 18 );
        g.setColour( parhui::colour::Dim );
        g.drawText( row.m_what, line.removeFromLeft( nameW ), juce::Justification::centredLeft );
        g.setColour( parhui::colour::Ink );
        for ( int c = 0; c < cols; ++c )
            g.drawText( row.m_values[c], line.removeFromLeft( colW ), juce::Justification::centred );
    }
}
