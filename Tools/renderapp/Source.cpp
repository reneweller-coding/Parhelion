/**
 * @file Source.cpp
 * @brief Parhelion Sets: the render app's source for Parhelion -- its five styles, each composed into a track or a DJ
 *        set with the dice of the moment, and the sets the player saved (.parhset files in Documents/Parhelion, its
 *        subfolders too), each played back exactly: its seed, its rerolls, its knobs.
 *
 * A rendering is what parh_render writes for the same seed and knobs: composed (composeTrack, or composeSet for a set,
 * every track levelled before it is placed), levelled, the engine prepared at 48 kHz with blocks of 512 and loaded, two
 * seconds after the end for the rooms. The lengths in the table are the composed ones (composing without the levelling
 * takes a fraction of a second); the levelling renders the loudest part of every track first.
 * @note After Totality's Tools/renderapp/Source.cpp (16.10.2026), as parh_render follows tot_render.
 * @author Rene Weller
 */
#include "RenderKit.h"
#include "parh/Engine.h"
#include "parh/Leveler.h"
#include "parh/Params.h"
#include "parh/SetFile.h"
#include "parh/compose/Composer.h"
#include "parh/compose/Set.h"
#include "parh/compose/Style.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace
{

constexpr double Rate = 48000.0;      ///< the files' rate
constexpr int    EngineBlock = 512;   ///< the engine's block (parh_render's)
constexpr double Tail = 2.0;          ///< s after the end, for the rooms (parh_render's)
/** @brief The panel's "What": a track, or a DJ set of so many minutes. */
const double SetMinutes[] = { 0.0, 30.0, 60.0, 90.0, 120.0, 180.0 };

/** @brief A saved set as the table shows it. */
struct Saved
{
    juce::File    m_file;        ///< the .parhset
    std::string   m_style;       ///< its style
    double        m_set = 0.0;   ///< its set length, minutes (0: a track)
    std::uint64_t m_seed = 1;    ///< its seed
};

/** @brief What a row is: a style, or a saved set. */
struct Row
{
    int   m_style = -1;   ///< the style (StyleE), -1 for a saved set
    Saved m_saved;        ///< the saved set
};

/** @brief A score composed: a track or a set, its length, and what plays when. */
struct Composed
{
    parh::SetScore                               m_set;             ///< the score (a track on deck A)
    bool                                         m_isSet = false;   ///< a DJ set
    double                                       m_seconds = 0.0;   ///< the length, the tail included
    std::vector<std::pair<double, juce::String>> m_marks;           ///< s -> what begins there
};

/**
 * @brief The knobs of a row: a style with the panel's choices, or a saved set's own.
 * @param[in,out] p     the knobs (fresh)
 * @param[in]     row   the row
 * @param[in]     r     the request
 * @param[in]     seed  the dice for a style (renderkit::seedFor)
 * @param[out]    out   receives the seed to compose with (a saved set's own)
 * @param[out]    cur   receives the rerolls
 * @param[out]    why   receives why a saved set cannot be read
 * @return false when it cannot be set up
 */
bool knobsOf( parh::ParamStore &p, const Row &row, const renderkit::Request &r, std::uint64_t seed, std::uint64_t &out,
              parh::Curation &cur, juce::String &why )
{
    using namespace parh;
    if ( row.m_style >= 0 )
    {
        p.set( p.id( ModuleE::COMPOSE, 0, compose::STYLE ), static_cast<float>( row.m_style ) );
        p.set( p.id( ModuleE::SET, 0, set::MINUTES ),
               static_cast<float>( SetMinutes[std::clamp( r.option( "what" ), 0, 5 )] ) );
        p.set( p.id( ModuleE::SET, 0, set::DRAMATURGY ), static_cast<float>( r.option( "dramaturgy", 1 ) ) );
        out = seed;
        return true;
    }
    SetFile     sf;
    std::string error;
    if ( !loadSet( row.m_saved.m_file.getFullPathName().toRawUTF8(), sf, p, &error ) )
    {
        why = error;
        return false;
    }
    if ( sf.m_set > 0.0 ) p.set( p.id( ModuleE::SET, 0, set::MINUTES ), static_cast<float>( sf.m_set ) );
    cur = sf.m_curation;
    out = sf.m_seed;
    return true;
}

/**
 * @brief What a track is, for the status line.
 * @param[in] info  the track
 * @return "Uplifting, 138.0 BPM, A Aeolian (8A)"
 */
juce::String trackText( const parh::TrackInfo &info )
{
    return juce::String( info.m_style ) + ", " + juce::String( info.m_bpm, 1 ) + " BPM, " + parh::KeyNames[info.m_key] +
           " " + parh::ScaleNames[info.m_scale] + " (" + juce::String( info.m_camelot ) + ")";
}

/**
 * @brief Composes what the knobs ask for, as parh_render does: a set when set.minutes is above 0, else a track.
 * @param[in] p      the knobs
 * @param[in] seed   the seed
 * @param[in] cur    the rerolls
 * @param[in] level  every track levelled before it is placed (the rendering); off for the length alone
 * @return the score, its length and its marks
 */
Composed compose( const parh::ParamStore &p, std::uint64_t seed, const parh::Curation &cur, bool level )
{
    using namespace parh;
    Composed     out;
    const double minutes = p.get( p.id( ModuleE::SET, 0, set::MINUTES ) );
    if ( minutes > 0.0 )
    {
        SetInfo si;
        auto    leveller = [&p]( Score &s ) { levelScore( s, p ); };
        out.m_set = composeSet( p, seed, minutes, &cur, &si, nullptr,
                                level ? std::function<void( Score & )>( leveller ) : std::function<void( Score & )>() );
        out.m_isSet = true;
        const TempoMap &tm = out.m_set.m_decks[0].m_tempo;
        out.m_seconds = tm.secondsAt( out.m_set.m_lengthBeats ) + Tail;
        for ( size_t i = 0; i < si.m_tracks.size(); ++i )
            out.m_marks.push_back( { tm.secondsAt( si.m_tracks[i].m_start ),
                                     "track " + juce::String( static_cast<int>( i + 1 ) ) + " of " +
                                         juce::String( static_cast<int>( si.m_tracks.size() ) ) + ": " +
                                         trackText( si.m_tracks[i].m_info ) } );
    }
    else
    {
        TrackInfo info;
        Score    &score = out.m_set.m_decks[0];
        score = composeTrack( p, seed, TrackRequest{}, &cur, std::string(), &info );
        if ( level ) levelScore( score, p );
        out.m_seconds = score.m_tempo.secondsAt( score.m_lengthBeats ) + Tail;
        out.m_marks.push_back( { 0.0, trackText( info ) } );
    }
    return out;
}

/** @brief A track or a set being rendered: the engine offline, levelled first. */
class ParhelionRendering : public renderkit::Rendering
{
public:
    /**
     * @brief Composes, levels and loads.
     * @param[in]  row   the row
     * @param[in]  r     the request
     * @param[in]  seed  the dice for a style (renderkit::seedFor)
     * @param[out] why   receives why it cannot be rendered
     */
    ParhelionRendering( const Row &row, const renderkit::Request &r, std::uint64_t seed, juce::String &why )
    {
        using namespace parh;
        m_engine = std::make_unique<Engine>();
        ParamStore   &p = m_engine->params();
        Curation      cur;
        std::uint64_t s = seed;
        if ( !knobsOf( p, row, r, seed, s, cur, why ) )
        {
            m_engine.reset();
            return;
        }
        Composed c = compose( p, s, cur, true );
        m_engine->prepare( Rate, EngineBlock );
        if ( c.m_isSet )
            m_engine->loadSet( c.m_set );
        else
            m_engine->load( c.m_set.m_decks[0] );
        m_seconds = m_engine->lengthSeconds() + Tail;
        m_marks = std::move( c.m_marks );
    }
    /** @brief Whether it could be set up. @return true when it renders */
    bool ok() const { return m_engine != nullptr; }
    double seconds() const override { return m_seconds; }
    void render( float *left, float *right, int frames ) override
    {
        for ( int done = 0; done < frames; )
        {   // in the engine's own blocks, as parh_render renders
            const int n = std::min( EngineBlock, frames - done );
            m_engine->process( left + done, right + done, n );
            done += n;
        }
        const std::lock_guard<std::mutex> hold( m_lock );
        m_time += frames / Rate;
    }
    juce::String now() const override
    {
        const std::lock_guard<std::mutex> hold( m_lock );
        juce::String                      s;
        for ( const auto &[at, what] : m_marks )
            if ( at <= m_time ) s = what;
        return s;
    }

private:
    std::unique_ptr<parh::Engine>                m_engine;          ///< the instrument offline
    double                                       m_seconds = 0.0;   ///< the length
    std::vector<std::pair<double, juce::String>> m_marks;           ///< s -> what begins there
    mutable std::mutex                           m_lock;            ///< guards m_time
    double                                       m_time = 0.0;      ///< s rendered
};

/** @brief Parhelion's styles and saved sets for the render app. */
class ParhelionSource : public renderkit::Source
{
public:
    juce::String product() const override { return "Parhelion"; }
    juce::String title() const override { return "Parhelion Sets"; }
    juce::String subtitle() const override { return "Render tracks and DJ sets into files, faster than real time"; }
    juce::String noun() const override { return "set"; }
    renderkit::Palette palette() const override
    {
        renderkit::Palette c;
        c.m_back = juce::Colour( 0xff070b16 );
        c.m_panel = juce::Colour( 0xff131a2c );
        c.m_row = juce::Colour( 0xff0e1422 );
        c.m_raised = juce::Colour( 0xff1c2439 );
        c.m_edge = juce::Colour( 0xff2a3452 );
        c.m_chosen = juce::Colour( 0xff2b3d63 );
        c.m_text = juce::Colour( 0xffe9eff8 );
        c.m_dim = juce::Colour( 0xff8e98b0 );
        c.m_accent = juce::Colour( 0xfff2cf7e );
        return c;
    }
    std::vector<renderkit::Column> columns() const override
    {
        return { { "Kind", 90, false }, { "Style", 110, false }, { "Tempo", 110, true }, { "A track", 90, true } };
    }
    std::vector<renderkit::Item> list() override
    {
        using namespace parh;
        std::vector<renderkit::Item> out;
        auto                         add = [&out, this]( renderkit::Item &&item, Row &&row )
        {
            const std::lock_guard<std::mutex> hold( m_lock );
            item.m_index = static_cast<int>( m_store.size() );
            m_store.push_back( std::make_shared<const Row>( std::move( row ) ) );
            out.push_back( std::move( item ) );
        };
        for ( int s = 0; s < static_cast<int>( StyleE::COUNT ); ++s )
        {
            const StyleProfile &prof = styleProfile( static_cast<StyleE>( s ) );
            renderkit::Item     item;
            item.m_name = StyleNames[s];
            item.m_kind = "Style";
            item.m_cells.add( "Style" );
            item.m_cells.add( StyleNames[s] );
            item.m_cells.add( juce::String( prof.m_bpmLow, 0 ) + " .. " + juce::String( prof.m_bpmHigh, 0 ) + " BPM" );
            item.m_cells.add( juce::String( prof.m_minutesLow, 0 ) + " .. " + juce::String( prof.m_minutesHigh, 0 ) +
                              " min" );
            item.m_keys = { std::nan( "" ), std::nan( "" ), static_cast<double>( prof.m_bpmLow ),
                            static_cast<double>( prof.m_minutesLow ) };
            Row row;
            row.m_style = s;
            add( std::move( item ), std::move( row ) );
        }
        const juce::File docs =
            juce::File::getSpecialLocation( juce::File::userDocumentsDirectory ).getChildFile( "Parhelion" );
        juce::Array<juce::File> files = docs.findChildFiles( juce::File::findFiles, true, "*.parhset" );
        files.sort();
        for ( const juce::File &f : files )
        {
            ParamStore  p;
            SetFile     sf;
            std::string error;
            if ( !loadSet( f.getFullPathName().toRawUTF8(), sf, p, &error ) ) continue;
            Row row;
            row.m_saved.m_file = f;
            row.m_saved.m_style = StyleNames[std::clamp( p.getInt( p.id( ModuleE::COMPOSE, 0, compose::STYLE ) ), 0,
                                                         static_cast<int>( StyleE::COUNT ) - 1 )];
            row.m_saved.m_set = sf.m_set > 0.0 ? sf.m_set : p.get( p.id( ModuleE::SET, 0, set::MINUTES ) );
            row.m_saved.m_seed = sf.m_seed;
            renderkit::Item item;
            item.m_name = f.getFileNameWithoutExtension();
            item.m_kind = "Yours";
            item.m_dice = false;
            item.m_cells.add( row.m_saved.m_set > 0.0 ? "Your DJ set" : "Your track" );
            item.m_cells.add( juce::String( row.m_saved.m_style ) );
            item.m_cells.add( {} );
            item.m_cells.add( {} );
            item.m_keys = { std::nan( "" ), std::nan( "" ), std::nan( "" ), std::nan( "" ) };
            item.m_detail = f.getRelativePathFrom( docs );
            add( std::move( item ), std::move( row ) );
        }
        return out;
    }
    std::vector<renderkit::Option> options() const override
    {
        renderkit::Option dram{
            "dramaturgy", "Arc", "A DJ set's arc: how its energy goes over the night.", {}, 1, true
        };
        for ( const char *n : { "Warm-up", "Peak", "Closing", "Sunrise", "Journey" } ) dram.m_choices.add( n );
        return { { "what",
                   "What",
                   "A single track, or a DJ set: tracks on two decks, blended, with teases on the third.",
                   { "A track", "DJ set, 30 min", "DJ set, 1 hour", "DJ set, 90 min", "DJ set, 2 hours",
                     "DJ set, 3 hours" },
                   0,
                   true },
                 dram };
    }
    bool applies( const renderkit::Item &item, const juce::String &key ) const override
    {
        juce::ignoreUnused( key );
        return row( item )->m_style >= 0;   // a saved set brings its own
    }
    double length( const renderkit::Item &item, const renderkit::Request &r ) override
    {
        parh::ParamStore p;
        parh::Curation   cur;
        std::uint64_t    seed = 1;
        juce::String     why;
        if ( !knobsOf( p, *row( item ), r, renderkit::seedFor( item, r ), seed, cur, why ) ) return 0.0;
        return compose( p, seed, cur, false ).m_seconds;
    }
    juce::String describe( const renderkit::Item &item, const renderkit::Request &r ) override
    {
        const std::shared_ptr<const Row> rw = row( item );
        if ( rw->m_style < 0 )
        {
            const Saved &s = rw->m_saved;
            return juce::String( "Your " ) +
                   ( s.m_set > 0.0 ? "DJ set of " + juce::String( s.m_set, 0 ) + " min" : juce::String( "track" ) ) +
                   ", " + juce::String( s.m_style ) + "\nseed " + juce::String( static_cast<juce::int64>( s.m_seed ) ) +
                   ", played as you saved it";
        }
        const parh::StyleProfile &prof = parh::styleProfile( static_cast<parh::StyleE>( rw->m_style ) );
        const int                 what = std::clamp( r.option( "what" ), 0, 5 );
        juce::String s = juce::String( parh::StyleNames[rw->m_style] ) + ", " + juce::String( prof.m_bpmLow, 0 ) +
                         " .. " + juce::String( prof.m_bpmHigh, 0 ) + " BPM\n";
        if ( what == 0 )
            s << "A track of " << juce::String( prof.m_minutesLow, 0 ) << " .. "
              << juce::String( prof.m_minutesHigh, 0 ) << " minutes";
        else
            s << "A DJ set of " << juce::String( SetMinutes[what], 0 ) << " minutes, from "
              << parh::StyleNames[rw->m_style];
        return s;
    }
    std::unique_ptr<renderkit::Rendering> open( const renderkit::Item &item, const renderkit::Request &r,
                                                juce::String &why ) override
    {
        auto rendering = std::make_unique<ParhelionRendering>( *row( item ), r, renderkit::seedFor( item, r ), why );
        if ( !rendering->ok() ) return nullptr;
        return rendering;
    }
    juce::String testItem() const override { return "Uplifting"; }

private:
    /**
     * @brief A row's style or saved set.
     * @param[in] item  the row
     * @return it (shared: a rendering may outlive a Refresh)
     */
    std::shared_ptr<const Row> row( const renderkit::Item &item ) const
    {
        const std::lock_guard<std::mutex> hold( m_lock );
        return m_store[static_cast<size_t>( item.m_index )];
    }
    mutable std::mutex                      m_lock;    ///< guards m_store
    std::vector<std::shared_ptr<const Row>> m_store;   ///< every row listed (only ever added to)
};

}   // namespace

std::unique_ptr<renderkit::Source> renderkit::makeSource()
{
    return std::make_unique<ParhelionSource>();
}
