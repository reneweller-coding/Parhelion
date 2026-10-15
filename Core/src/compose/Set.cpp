/**
 * @file Set.cpp
 * @brief The set composer (Set.h).
 *
 * @note Built from Totality `Core/src/compose/Set.cpp` at 4d3c0d2 (29.09.2026): the tempo map, the decks, the knobs
 *       homed between tracks and the mixer's gestures are its; the dramaturgies, the keys, the placement and the
 *       tease are trance's.
 * @author Rene Weller
 */
#include "parh/compose/Set.h"
#include "parh/Dsp.h"
#include "parh/compose/Style.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <set>

namespace parh
{

const char *const DramaturgyNames[] = { "Warm-up", "Peak", "Closing", "Sunrise", "Journey" };

namespace
{

constexpr double Bar = 4.0;                      ///< beats per bar
constexpr double PiD = 3.14159265358979323846;   ///< pi

/**
 * @brief The FNV-1a hash of @p s.
 * @param[in] s  the text
 * @return its FNV-1a hash
 */
uint64_t hashName( const std::string &s )
{
    uint64_t h = 1469598103934665603ull;   // FNV-1a
    for ( char c : s )
    {
        h ^= static_cast<unsigned char>( c );
        h *= 1099511628211ull;
    }
    return h;
}

/**
 * @brief The style ladder (Set.h), a place on it morphed between neighbours.
 * @param[in] x  the place on the ladder, 0..4
 * @return the profile
 */
StyleProfile ladderProfile( float x )
{
    static const StyleE Ladder[5] = { StyleE::DEEP, StyleE::PROGRESSIVE, StyleE::DREAM_HOUSE, StyleE::UPLIFTING,
                                      StyleE::ACID };
    x = std::clamp( x, 0.0f, 4.0f );
    const int i = std::min( 3, static_cast<int>( x ) );
    return morphProfile( styleProfile( Ladder[i] ), styleProfile( Ladder[i + 1] ), x - static_cast<float>( i ) );
}

/**
 * @brief A minor mode after the profile's weights (Ionian left out), or Ionian for a major key.
 * @param[in] prof   the profile
 * @param[in] minor  true for a minor key
 * @param[in] u      a uniform draw, 0..1
 * @return the mode
 */
int scaleFor( const StyleProfile &prof, bool minor, float u )
{
    if ( !minor ) return static_cast<int>( ScaleE::IONIAN );
    std::array<float, 6> w = prof.m_scales;
    w[static_cast<size_t>( ScaleE::IONIAN )] = 0.0f;
    float sum = 0.0f;
    for ( float x : w ) sum += x;
    if ( sum <= 0.0f ) return static_cast<int>( ScaleE::AEOLIAN );
    return drawWeighted( w.data(), 6, u );
}

/**
 * @brief Appends @p src, moved @p offset beats later, to @p dst (tempo and length are the caller's).
 * @param[in,out] dst     the score, appended to
 * @param[in]     src     the score appended
 * @param[in]     offset  how many beats later it goes
 */
void appendShifted( Score &dst, const Score &src, double offset )
{
    for ( NoteEvent n : src.m_notes )
    {
        n.m_beat += offset;
        dst.m_notes.push_back( n );
    }
    for ( Gesture g : src.m_gestures )
    {
        g.m_beat += offset;
        dst.m_gestures.push_back( g );
    }
    for ( Marker m : src.m_markers )
    {
        m.m_beat += offset;
        dst.m_markers.push_back( m );
    }
    for ( LevelMark l : src.m_levels )
    {
        l.m_beat += offset;
        l.m_peakBeat += offset;
        dst.m_levels.push_back( l );
    }
    for ( KnobSet k : src.m_knobs )
    {
        k.m_beat += offset;
        dst.m_knobs.push_back( k );
    }
    for ( SoundPick s : src.m_sounds )
    {
        s.m_beat += offset;
        dst.m_sounds.push_back( s );
    }
    for ( Section s : src.m_sections )
    {
        s.m_beat += offset;
        dst.m_sections.push_back( s );
    }
    for ( LayerBlock b : src.m_layers )
    {
        b.m_beat += offset;
        dst.m_layers.push_back( b );
    }
}

/**
 * @brief A step of knob @p id to @p value at @p beat, as an offset from the knob in @p p.
 * @param[in] p      the knobs
 * @param[in] id     the knob
 * @param[in] beat   where the step is
 * @param[in] value  the value it steps to
 * @return the gesture
 */
Gesture stepOf( const ParamStore &p, int id, double beat, float value )
{
    Gesture g;
    g.m_param = id;
    g.m_beat = beat;
    g.m_length = 0.0;
    g.m_from = g.m_to = p.toNormalised( id, value ) - p.toNormalised( id, p.get( id ) );
    g.m_shape = GestureShapeE::STEP;
    return g;
}

/**
 * @brief A ramp of knob @p id from @p beat over @p length beats, @p from to @p to in @p shape, as offsets from the
 *        knob.
 * @param[in] p       the knobs
 * @param[in] id      the knob
 * @param[in] beat    where the ramp begins
 * @param[in] length  its length in beats
 * @param[in] from    the value it starts at
 * @param[in] to      the value it ends at
 * @param[in] shape   the curve's shape
 * @return the gesture
 */
Gesture rampOf( const ParamStore &p, int id, double beat, double length, float from, float to, GestureShapeE shape )
{
    Gesture g;
    g.m_param = id;
    g.m_beat = beat;
    g.m_length = length;
    g.m_from = p.toNormalised( id, from ) - p.toNormalised( id, p.get( id ) );
    g.m_to = p.toNormalised( id, to ) - p.toNormalised( id, p.get( id ) );
    g.m_shape = shape;
    return g;
}

/**
 * @brief Before each piece on a deck, every knob any piece on that deck moves goes back to the knob (a Step to offset 0
 *        at the piece's start, written before the piece's own), so no piece inherits the last one's automation.
 * @param[in,out] deck      the deck's score, appended to
 * @param[in]     pieces    the pieces on the deck: their start and their gestures
 * @param[in]     mixFirst  the first mixer parameter
 * @param[in]     mixEnd    one past the last
 */
void homeBetween( Score &deck, const std::vector<std::pair<double, std::vector<Gesture>>> &pieces, int mixFirst,
                  int mixEnd )
{
    std::set<int> moved;
    for ( const auto &piece : pieces )
        for ( const Gesture &g : piece.second )
            if ( g.m_param < mixFirst || g.m_param >= mixEnd ) moved.insert( g.m_param );
    for ( const auto &piece : pieces )
    {
        for ( int id : moved )
        {
            Gesture g;
            g.m_param = id;
            g.m_beat = piece.first;
            g.m_length = 0.0;
            g.m_shape = GestureShapeE::STEP;
            deck.m_gestures.push_back( g );
        }
        for ( const Gesture &g : piece.second ) deck.m_gestures.push_back( g );
    }
}

/**
 * @brief Whether scale @p scale is a minor one (all but Ionian).
 * @param[in] scale  the mode
 * @return true for a minor mode
 */
bool minorScale( int scale )
{
    return scale != static_cast<int>( ScaleE::IONIAN );
}

}   // namespace

float setEnergy( DramaturgyE d, float t )
{
    t = std::clamp( t, 0.0f, 1.0f );
    switch ( d )
    {
    case DramaturgyE::WARM_UP: return 0.3f + 0.4f * t;
    case DramaturgyE::CLOSING: return 0.85f - 0.45f * t;
    case DramaturgyE::SUNRISE: return 0.45f + 0.55f * t;
    case DramaturgyE::JOURNEY: return 0.45f + 0.15f * t + 0.3f * static_cast<float>( std::sin( 2.0 * PiD * 1.5 * t ) );
    default: return t < 0.7f ? 0.55f + 0.45f * t / 0.7f : 1.0f - 0.25f * ( t - 0.7f ) / 0.3f;   // Peak
    }
}

float setTempo( DramaturgyE d, float t )
{
    t = std::clamp( t, 0.0f, 1.0f );
    switch ( d )
    {
    case DramaturgyE::WARM_UP: return 130.0f + 6.0f * t;
    case DramaturgyE::CLOSING: return 139.0f - 7.0f * t;
    case DramaturgyE::SUNRISE: return 134.0f + 4.0f * t;
    case DramaturgyE::JOURNEY: return 132.0f + 6.0f * t;
    default: return t < 0.7f ? 136.0f + 4.0f * t / 0.7f : 140.0f;   // Peak
    }
}

float setLadder( DramaturgyE d, float t, float e )
{
    t = std::clamp( t, 0.0f, 1.0f );
    switch ( d )
    {
    case DramaturgyE::WARM_UP: return 1.2f * t;          // Deep -> Progressive
    case DramaturgyE::CLOSING: return 3.0f - 2.0f * t;   // Uplifting -> Progressive
    case DramaturgyE::SUNRISE: return 1.0f + 2.0f * t;   // Progressive -> Uplifting
    case DramaturgyE::JOURNEY: return 4.0f * std::clamp( e, 0.0f, 1.0f );
    default: return 2.6f + 1.4f * std::clamp( e, 0.0f, 1.0f );   // Peak: Uplifting and Acid
    }
}

SetScore composeSet( const ParamStore &p, uint64_t seed, double minutes, const Curation *cur, SetInfo *info,
                     const Preferences *prefs, const std::function<void( Score & )> &prepare,
                     const std::function<bool()> &stop )
{
    const DramaturgyE dram = static_cast<DramaturgyE>( p.getInt( p.id( ModuleE::SET, 0, set::DRAMATURGY ) ) );
    const bool        wander = p.getInt( p.id( ModuleE::SET, 0, set::JOURNEY ) ) != 0;
    const int         blendBars = p.getInt( p.id( ModuleE::SET, 0, set::BLEND_BARS ) ) == 0 ? 32 : 64;
    const float       trackMinutes = p.get( p.id( ModuleE::SET, 0, set::TRACK_MINUTES ) );
    const double      totalSeconds = std::max( 1.0, minutes ) * 60.0;
    Rng               sr;
    sr.seed( mixSeed( mixSeed( seed, hashName( "set" ) ),
                      static_cast<uint64_t>( cur != nullptr ? cur->count( "set" ) : 0 ) ) );
    const StyleProfile fixed = profileOf( p );

    SetInfo si;
    si.m_dramaturgy = dram;
    std::vector<Score> scores;
    TempoMap           tempo;
    double             seconds = 0.0;
    float              bpmPrev = 0.0f;
    int                keyPrev = -1;
    bool               minorPrev = true;
    for ( int i = 0; i < 256; ++i )
    {
        if ( i > 0 && stop && stop() ) break;
        const float  t = static_cast<float>( std::min( 1.0, seconds / totalSeconds ) );
        const float  e = setEnergy( dram, t );
        StyleProfile prof = wander ? ladderProfile( setLadder( dram, t, e ) ) : fixed;
        // Sunrise: the last track (the one that will end the set) the most euphoric -- Uplifting with its orchestra.
        const bool last = seconds + 0.85 * trackMinutes * 60.0 >= totalSeconds;
        if ( dram == DramaturgyE::SUNRISE && last )
        {
            prof = styleProfile( StyleE::UPLIFTING );
            prof.m_orchestra = 1.0f;
        }
        float bpm = std::round( 2.0f * setTempo( dram, t ) ) * 0.5f;
        if ( i > 0 ) bpm = std::clamp( bpm, bpmPrev - 2.0f, bpmPrev + 2.0f );
        // The key round the Camelot wheel (drawn every time, so the stream stays put).
        const float uk = sr.uniform(), ud = sr.uniform(), um = sr.uniform(), us = sr.uniform();
        const int   anyKey = sr.below( 12 );
        int         key = anyKey;
        bool        minor = um < prof.m_minor;
        if ( i > 0 )
        {
            minor = minorPrev;
            if ( uk < 0.3f )
                key = keyPrev;   // the same
            else if ( uk < 0.75f )
                key = ( keyPrev + ( ud < 0.5f ? 7 : 5 ) ) % 12;   // a fifth either way
            // The relative: from minor now and then, from major more often (trance lives in minor: 84.8 %, Knees et al.).
            else if ( uk < ( minorPrev ? 0.9f : 1.0f ) )
            {
                key = minorPrev ? ( keyPrev + 3 ) % 12 : ( keyPrev + 9 ) % 12;
                minor = !minorPrev;
            }
            else if ( uk < 0.95f )
                key = ( keyPrev + ( ud < 0.5f ? 1 : 2 ) ) % 12;   // the energy boost
        }
        const int         scale = scaleFor( prof, minor, us );
        const std::string unit = "track" + std::to_string( i + 1 ) + ".";
        const uint64_t    trackSeed =
            mixSeed( mixSeed( seed, 0x545241434Bull + static_cast<uint64_t>( i ) ),
                     static_cast<uint64_t>( cur != nullptr ? cur->count( "track" + std::to_string( i + 1 ) ) : 0 ) );
        TrackRequest req;
        req.m_profile = &prof;
        req.m_bpm = bpm;
        req.m_key = key;
        req.m_scale = scale;
        req.m_bars = std::max( 96, static_cast<int>( std::lround( trackMinutes * bpm / 4.0 / 32.0 ) ) * 32 );
        req.m_energy = e;
        req.m_mixable = true;
        req.m_opener = i == 0;   // (nothing is mixed into the first: it may open on its atmosphere)
        req.m_prefs = prefs;
        SetTrack st;
        st.m_energy = e;
        st.m_seed = trackSeed;
        Score sc = composeTrack( p, trackSeed, req, cur, unit, &st.m_info );
        if ( prepare ) prepare( sc );
        if ( i == 0 )
        {
            st.m_deck = 0;
            st.m_start = 0.0;
            st.m_swapIn = 0.0;
            tempo.setConstant( bpm );
        }
        else
        {
            SetTrack &prev = si.m_tracks.back();
            st.m_deck = 1 - prev.m_deck;
            // The incoming intro over the outgoing end; the swap 16 bars before the incoming groove.
            st.m_start = prev.m_end - st.m_info.m_introBars * Bar;
            const double swap = st.m_start + std::max( st.m_info.m_bassBar, st.m_info.m_introBars - 16 ) * Bar;
            prev.m_swapOut = swap;
            st.m_swapIn = swap;
            const double open = std::max( st.m_start, swap - blendBars * Bar );
            tempo.add( open, bpmPrev, true );
            tempo.add( swap, bpm, false );
        }
        st.m_end = st.m_start + st.m_info.m_bars * Bar;
        st.m_swapOut = st.m_end;
        seconds = tempo.secondsAt( st.m_start + st.m_info.m_outroBar * Bar );
        si.m_tracks.push_back( st );
        scores.push_back( std::move( sc ) );
        bpmPrev = bpm;
        keyPrev = key;
        minorPrev = minor;
        if ( seconds >= totalSeconds ) break;
    }
    const size_t n = si.m_tracks.size();

    SetScore out;
    out.m_lengthBeats = si.m_tracks.back().m_end;
    for ( Score &d : out.m_decks )
    {
        d.clear( 130.0 );
        d.m_tempo = tempo;
        d.m_lengthBeats = out.m_lengthBeats;
    }
    std::vector<std::pair<double, std::vector<Gesture>>> pieces[Decks];
    const int mixFirst = p.base( ModuleE::DECK, 0 ), mixEnd = mixFirst + Decks * deck::COUNT;
    for ( size_t i = 0; i < n; ++i )
    {
        const SetTrack &st = si.m_tracks[i];
        Score          &deckScore = out.m_decks[st.m_deck];
        Score           shifted;
        appendShifted( shifted, scores[i], st.m_start );
        deckScore.m_notes.insert( deckScore.m_notes.end(), shifted.m_notes.begin(), shifted.m_notes.end() );
        deckScore.m_markers.insert( deckScore.m_markers.end(), shifted.m_markers.begin(), shifted.m_markers.end() );
        deckScore.m_levels.insert( deckScore.m_levels.end(), shifted.m_levels.begin(), shifted.m_levels.end() );
        deckScore.m_knobs.insert( deckScore.m_knobs.end(), shifted.m_knobs.begin(), shifted.m_knobs.end() );
        deckScore.m_sounds.insert( deckScore.m_sounds.end(), shifted.m_sounds.begin(), shifted.m_sounds.end() );
        deckScore.m_sections.insert( deckScore.m_sections.end(), shifted.m_sections.begin(), shifted.m_sections.end() );
        deckScore.m_layers.insert( deckScore.m_layers.end(), shifted.m_layers.begin(),
                                   shifted.m_layers.end() );   // (the plugin's arrange view)
        deckScore.m_keyRoot = scores[i].m_keyRoot;
        deckScore.m_scale = scores[i].m_scale;
        std::vector<Gesture> g = shifted.m_gestures;
        const int            d = st.m_deck;
        const int            fader = p.id( ModuleE::DECK, d, deck::FADER ), low = p.id( ModuleE::DECK, d, deck::LOW );
        const int            mid = p.id( ModuleE::DECK, d, deck::MID ), high = p.id( ModuleE::DECK, d, deck::HIGH );
        const int filter = p.id( ModuleE::DECK, d, deck::FILTER ), send = p.id( ModuleE::DECK, d, deck::FX_SEND );
        g.push_back( stepOf( p, filter, st.m_start, 0.0f ) );
        g.push_back( stepOf( p, send, st.m_start, 0.0f ) );
        if ( i > 0 )
        {
            // In: the low band killed until the swap; the fader opens the blend's bars before it (or at the start) with the
            // highs 8 dB and the mids 10 dB down, back over the blend.
            const double open = std::max( st.m_start, st.m_swapIn - blendBars * Bar );
            g.push_back( stepOf( p, low, st.m_start, -60.0f ) );
            g.push_back( stepOf( p, fader, st.m_start, -60.0f ) );
            g.push_back( stepOf( p, high, st.m_start, -8.0f ) );
            g.push_back( stepOf( p, mid, st.m_start, -10.0f ) );
            g.push_back( rampOf( p, fader, open, 8.0 * Bar, -60.0f, 0.0f, GestureShapeE::EASE_OUT ) );
            g.push_back( rampOf( p, high, open, st.m_swapIn - open, -8.0f, 0.0f, GestureShapeE::LINEAR ) );
            g.push_back( rampOf( p, mid, open, st.m_swapIn - open, -10.0f, 0.0f, GestureShapeE::EASE_IN ) );
            g.push_back( stepOf( p, low, st.m_swapIn, 0.0f ) );
        }
        else
        {
            for ( int id : { low, fader, mid, high } ) g.push_back( stepOf( p, id, st.m_start, 0.0f ) );
        }
        if ( i + 1 < n )
        {
            // Out: the low band closed at the swap; the highs and hats play on, the mids down 6 dB over 8 bars, the fader
            // falling through the track's last 8 bars.
            g.push_back( stepOf( p, low, st.m_swapOut, -60.0f ) );
            g.push_back( rampOf( p, mid, st.m_swapOut, 8.0 * Bar, 0.0f, -6.0f, GestureShapeE::EASE_OUT ) );
            if ( st.m_end - st.m_swapOut > 8.0 * Bar )
                g.push_back(
                    rampOf( p, fader, st.m_end - 8.0 * Bar, 8.0 * Bar, 0.0f, -60.0f, GestureShapeE::EASE_IN ) );
        }
        pieces[d].push_back( { st.m_start, g } );
        out.m_markers.push_back(
            Marker{ st.m_start, "T" + std::to_string( i + 1 ) + " " + st.m_info.m_style + " " + st.m_info.m_camelot } );
        if ( i > 0 )
            out.m_markers.push_back(
                Marker{ st.m_swapIn, "Swap T" + std::to_string( i ) + " > T" + std::to_string( i + 1 ) } );
    }
    // The teases on deck C: the next track's hook before its blend, where the keys agree.
    Score &c = out.m_decks[2];
    for ( size_t i = 1; i < n; ++i )
    {
        const SetTrack &in = si.m_tracks[i];
        const SetTrack &prev = si.m_tracks[i - 1];
        const int       diff = ( ( in.m_info.m_key - prev.m_info.m_key ) % 12 + 12 ) % 12;
        const bool      agree = in.m_info.m_scale == prev.m_info.m_scale ||
                                minorScale( in.m_info.m_scale ) == minorScale( prev.m_info.m_scale );
        if ( !( agree && ( diff == 0 || diff == 5 || diff == 7 ) ) || in.m_info.m_mainDropBar < 0 ||
             sr.uniform() > 0.6f )
            continue;
        const double open = std::max( in.m_start, in.m_swapIn - blendBars * Bar );
        const double from = open - 16.0 * Bar;
        if ( from < prev.m_start + prev.m_info.m_introBars * Bar ) continue;
        const double           hook = in.m_info.m_mainDropBar * Bar;
        std::vector<NoteEvent> notes;
        for ( PartE part : { PartE::LEAD, PartE::PIANO, PartE::PLUCK } )
        {
            for ( const NoteEvent &note : scores[i].m_notes )
                if ( note.m_part == part && note.m_beat >= hook && note.m_beat < hook + 8.0 * Bar )
                    notes.push_back( note );
            if ( !notes.empty() ) break;
        }
        if ( notes.empty() ) continue;
        for ( int pass = 0; pass < 2; ++pass )
            for ( NoteEvent note : notes )
            {
                note.m_beat = note.m_beat - hook + from + pass * 8.0 * Bar;
                c.m_notes.push_back( note );
            }
        for ( KnobSet k : scores[i].m_knobs )
        {
            k.m_beat = from;
            c.m_knobs.push_back( k );
        }
        for ( SoundPick s : scores[i].m_sounds )
        {
            s.m_beat = from;
            c.m_sounds.push_back( s );
        }
        if ( !scores[i].m_levels.empty() )
        {
            LevelMark m;
            m.m_beat = from;
            m.m_peakBeat =
                in.m_start +
                scores[i].m_levels.front().m_peakBeat;   // (the source's mark on its deck, levelSet finds it)
            m.m_targetLufs = std::numeric_limits<float>::quiet_NaN();
            c.m_levels.push_back( m );
        }
        std::vector<Gesture> g;
        const int            fader = p.id( ModuleE::DECK, 2, deck::FADER ), low = p.id( ModuleE::DECK, 2, deck::LOW );
        const int            filter = p.id( ModuleE::DECK, 2, deck::FILTER );
        g.push_back( stepOf( p, low, from, -60.0f ) );
        g.push_back( stepOf( p, fader, from, -8.0f ) );
        g.push_back( rampOf( p, filter, from, 16.0 * Bar, 0.6f, 0.15f, GestureShapeE::EASE_IN ) );
        g.push_back( stepOf( p, fader, from + 16.0 * Bar, -60.0f ) );
        pieces[2].push_back( { from, g } );
        si.m_teases.push_back( SetTease{ static_cast<int>( i ), from, from + 16.0 * Bar } );
        out.m_markers.push_back( Marker{ from, "Tease T" + std::to_string( i + 1 ) } );
    }
    for ( int d = 0; d < Decks; ++d ) homeBetween( out.m_decks[d], pieces[d], mixFirst, mixEnd );
    for ( Score &d : out.m_decks ) d.sort();
    std::sort( out.m_markers.begin(), out.m_markers.end(),
               []( const Marker &a, const Marker &b ) { return a.m_beat < b.m_beat; } );
    if ( info != nullptr ) *info = si;
    return out;
}

Score flattenSet( const SetScore &set )
{
    Score s = set.m_decks[0];
    for ( int d = 1; d < Decks; ++d )
    {
        const Score &x = set.m_decks[d];
        s.m_notes.insert( s.m_notes.end(), x.m_notes.begin(), x.m_notes.end() );
        s.m_markers.insert( s.m_markers.end(), x.m_markers.begin(), x.m_markers.end() );
    }
    s.m_markers.insert( s.m_markers.end(), set.m_markers.begin(), set.m_markers.end() );
    s.m_gestures.clear();
    s.m_lengthBeats = set.m_lengthBeats;
    s.sort();
    return s;
}

}   // namespace parh
