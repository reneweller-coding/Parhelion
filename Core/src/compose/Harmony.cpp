/**
 * @file Harmony.cpp
 * @brief The harmony of a track (Harmony.h).
 * @author Rene Weller
 */
#include "parh/compose/Harmony.h"
#include <algorithm>
#include <cmath>

namespace parh
{

namespace
{

/**
 * @brief The scale whose triads the chords are built from (Harmony.h).
 * @param[in] scale  the mode
 * @return the chords' scale
 */
int chordScale( int scale )
{
    switch ( static_cast<ScaleE>( scale ) )
    {
    case ScaleE::DORIAN: return static_cast<int>( ScaleE::DORIAN );
    case ScaleE::PHRYGIAN: return static_cast<int>( ScaleE::PHRYGIAN );
    case ScaleE::IONIAN: return static_cast<int>( ScaleE::IONIAN );
    default: return static_cast<int>( ScaleE::AEOLIAN );
    }
}

/** @brief The degrees of a progression in a major key: the same functions from the tonic (vi-IV-I-V and relatives). */
const int MajorDegrees[Progressions][4] = {
    { 5, 3, 0, 4 },   // vi-IV-I-V: the Anthem's pitches from the relative major
    { 0, 4, 5, 3 },   // I-V-vi-IV
    { 3, 4, 0, 0 },   // IV-V-I
    { 0, 5, 3, 4 },   // I-vi-IV-V
    { 5, 3, 4, 2 },   // vi-IV-V-iii
};

}   // namespace

int Harmony::pc( int bar, int i ) const
{
    const size_t b = static_cast<size_t>( std::clamp( bar, 0, static_cast<int>( m_bars.size() ) - 1 ) );
    return ( ( m_key + m_shift[b] + m_bars[b].m_tones[i] ) % 12 + 12 ) % 12;
}

void drawKey( const StyleProfile &prof, Rng &r, int &key, int &scale )
{
    // Tonics weighted towards D .. A (Dok. 4), A and F the workhorses.
    static const float Tonic[12] = { 0.6f, 0.6f, 1.0f, 0.9f, 1.0f, 1.4f, 1.0f, 1.0f, 0.9f, 1.4f, 0.8f, 0.6f };
    key = drawWeighted( Tonic, 12, r.uniform() );
    if ( r.uniform() >= prof.m_minor )
    {
        scale = static_cast<int>( ScaleE::IONIAN );
        return;
    }
    std::array<float, static_cast<int>( ScaleE::COUNT )> w = prof.m_scales;
    w[static_cast<size_t>( ScaleE::IONIAN )] = 0.0f;
    scale = drawWeighted( w.data(), static_cast<int>( w.size() ), r.uniform() );
}

Chord chordOn( int scale, int degree, bool majorFive )
{
    const ScaleDef &sc = scaleDef( chordScale( scale ) );
    Chord           c;
    c.m_degree = ( ( degree % 7 ) + 7 ) % 7;
    for ( int k = 0; k < 3; ++k )
    {
        const int idx = c.m_degree + 2 * k;
        c.m_tones[k] = sc.m_steps[idx % 7] + 12 * ( idx / 7 );
    }
    // Harmonic minor's major V (its raised seventh as the third of v).
    if ( majorFive && c.m_degree == 4 && chordScale( scale ) == static_cast<int>( ScaleE::AEOLIAN ) ) c.m_tones[1] += 1;
    return c;
}

Harmony composeHarmony( const Plan &plan, const StyleProfile &prof, int key, int scale, uint64_t seed )
{
    Rng r;
    r.seed( seed );
    Harmony h;
    h.m_key = key;
    h.m_scale = scale;
    h.m_progression = drawWeighted( prof.m_progressions.data(), Progressions, r.uniform() );
    if ( h.m_progression == static_cast<int>( ProgressionE::LIFT ) )
        h.m_progression = static_cast<int>( ProgressionE::ANTHEM );
    // The breakdown's progression: the same, or the melancholic one (the piano's), now and then.
    const int  breakProg = r.uniform() < 0.3f ? static_cast<int>( ProgressionE::MELANCHOLY ) : h.m_progression;
    const bool major = scale == static_cast<int>( ScaleE::IONIAN );
    const bool acid = prof.m_barsPerChord >= 16;
    auto       degreeOf = [&]( int prog, int step )
    { return major ? MajorDegrees[prog][step % 4] : ProgressionDegrees[prog][step % 4]; };
    h.m_bars.assign( static_cast<size_t>( plan.m_bars ), Chord{} );
    h.m_shift.assign( static_cast<size_t>( plan.m_bars ), 0 );
    // Acid's static harmony: i, then a fourth or a fifth away, every 16 bars (Dok. 4).
    static const int AcidMoves[2][4] = { { 0, 0, 3, 0 }, { 0, 4, 0, 3 } };
    const int        acidMoves = r.below( 2 );
    for ( size_t si = 0; si < plan.m_sections.size(); ++si )
    {
        const Section &s = plan.m_sections[si];
        const int      start = static_cast<int>( s.m_beat / 4.0 ), n = static_cast<int>( s.m_length / 4.0 );
        const bool     breakdown = s.m_kind == SectionKindE::BREAKDOWN || s.m_kind == SectionKindE::BREAK;
        const int      per = std::max( 1, breakdown ? prof.m_barsPerChordBreak : prof.m_barsPerChord );
        const int      prog = s.m_kind == SectionKindE::BREAKDOWN ? breakProg : h.m_progression;
        for ( int b = 0; b < n; ++b )
        {
            Chord c;
            if ( acid )
                c = chordOn( scale, AcidMoves[acidMoves][( ( start + b ) / 16 ) % 4] );
            else
                c = chordOn( scale, degreeOf( prog, b / per ),
                             breakdown && prog == static_cast<int>( ProgressionE::TENSION ) &&
                                 scale == static_cast<int>( ScaleE::HARMONIC_MINOR ) );
            h.m_bars[static_cast<size_t>( start + b )] = c;
        }
    }
    // The lift: the last two bars before every drop hold VII (in major: V), the drop resolves to i.
    for ( size_t si = 0; si < plan.m_sections.size(); ++si )
    {
        if ( plan.m_sections[si].m_kind != SectionKindE::DROP || si == 0 || acid ) continue;
        const int drop = static_cast<int>( plan.m_sections[si].m_beat / 4.0 );
        for ( int b = std::max( 0, drop - 2 ); b < drop; ++b )
            h.m_bars[static_cast<size_t>( b )] = chordOn( scale, major ? 4 : 6 );
        h.m_bars[static_cast<size_t>( drop )] = chordOn( scale, degreeOf( h.m_progression, 0 ) );
    }
    // The Cinematic key change: a minor third up from the section after the second breakdown or break.
    if ( !acid && r.uniform() < prof.m_keyChange )
    {
        int breaks = 0;
        for ( size_t si = 0; si < plan.m_sections.size(); ++si )
        {
            const SectionKindE k = plan.m_sections[si].m_kind;
            if ( k == SectionKindE::BREAK || k == SectionKindE::BREAKDOWN ) ++breaks;
            if ( breaks >= 2 && k == SectionKindE::DROP )
            {
                h.m_changeBar = static_cast<int>( plan.m_sections[si].m_beat / 4.0 );
                break;
            }
        }
        if ( h.m_changeBar >= 0 )
            for ( int b = h.m_changeBar; b < plan.m_bars; ++b ) h.m_shift[static_cast<size_t>( b )] = 3;
    }
    return h;
}

int atOrAbove( int pc, int floor )
{
    int p = floor - ( ( floor - pc ) % 12 + 12 ) % 12;
    if ( p < floor ) p += 12;
    return p;
}

std::array<int, 4> voicePad( const Harmony &h, int bar, const std::array<int, 4> &prev, bool first, int floor )
{
    std::array<int, 4> best{};
    int                bestCost = 1 << 30;
    for ( int bottom = 1; bottom <= 2; ++bottom )
    {   // third or fifth, never the root
        std::array<int, 4> v{};
        v[0] = atOrAbove( h.pc( bar, bottom ), floor );
        int t = bottom;
        for ( int k = 1; k < 4; ++k )
        {
            t = ( t + 1 ) % 3;
            v[static_cast<size_t>( k )] = atOrAbove( h.pc( bar, t ), v[static_cast<size_t>( k - 1 )] + 1 );
        }
        int cost = 0;
        for ( int k = 0; k < 4; ++k ) cost += std::abs( v[static_cast<size_t>( k )] - prev[static_cast<size_t>( k )] );
        if ( first ) cost = std::abs( v[0] - ( floor + 3 ) );
        if ( cost < bestCost )
        {
            bestCost = cost;
            best = v;
        }
    }
    return best;
}

}   // namespace parh
