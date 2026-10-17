/**
 * @file PluginProcessor.cpp
 * @brief The plugin's processor.
 * @note Copied from Totality `Plugin/PluginProcessor.cpp` at 4d3c0d2 (29.09.2026); renamed to Parhelion (namespace parh, prefix PARH_).
 * @author Rene Weller
 */
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "parh/Export.h"
#include "parh/FactorySets.h"
#include "parh/Leveler.h"
#include "parh/Midi.h"
#include "parh/Presets.h"
#include "parh/WavWriter.h"
#include <cstring>
#include <algorithm>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

using namespace parh;

// ---------------------------------------------------------------------------------------------------
// StoreParameter (after Phosphene's Plugin/PluginProcessor.cpp at 9a2f615, as in Ephemeris)

StoreParameter::StoreParameter( ParamStore &store, int id, const juce::String &name )
    : juce::RangedAudioParameter(
          juce::ParameterID( juce::String( store.key( id ) ), 1 ), name,
          juce::AudioProcessorParameterWithIDAttributes().withLabel( store.desc( id ).m_unit ) ),
      m_store( store ), m_id( id ), m_name( name )
{
    const ParamDesc &d = store.desc( id );
    ParamStore      *s = &store;
    m_range = juce::NormalisableRange<float>(
        d.m_minValue, d.m_maxValue, [s, id]( float, float, float n ) { return s->fromNormalised( id, n ); },
        [s, id]( float, float, float v ) { return s->toNormalised( id, v ); },
        [s, id]( float lo, float hi, float v )
        {
            const ParamDesc &dd = s->desc( id );
            const float      c = juce::jlimit( lo, hi, v );
            return ( dd.m_curve == CurveE::LINEAR || dd.m_curve == CurveE::LOG ) ? c : std::round( c );
        } );
}

float StoreParameter::getValue() const
{
    return m_store.toNormalised( m_id, m_store.get( m_id ) );
}
void StoreParameter::setValue( float newValue )
{
    m_store.setNormalised( m_id, newValue );
}
float StoreParameter::getDefaultValue() const
{
    return m_store.toNormalised( m_id, m_store.defaultValue( m_id ) );
}
// A length of 0 or less means no limit, as in JUCE's own parameters (the slider attachments ask with 0).
juce::String StoreParameter::getName( int maximumStringLength ) const
{
    return maximumStringLength > 0 ? m_name.substring( 0, maximumStringLength ) : m_name;
}
juce::String StoreParameter::getLabel() const
{
    return m_store.desc( m_id ).m_unit;
}

int StoreParameter::getNumSteps() const
{
    const ParamDesc &d = m_store.desc( m_id );
    if ( d.m_curve == CurveE::TOGGLE ) return 2;
    if ( d.m_curve == CurveE::CHOICE || d.m_curve == CurveE::INT )
        return static_cast<int>( std::lround( d.m_maxValue - d.m_minValue ) ) + 1;
    return juce::AudioProcessor::getDefaultNumParameterSteps();
}

bool StoreParameter::isDiscrete() const
{
    const CurveE c = m_store.desc( m_id ).m_curve;
    return c == CurveE::INT || c == CurveE::CHOICE || c == CurveE::TOGGLE;
}

bool StoreParameter::isBoolean() const
{
    return m_store.desc( m_id ).m_curve == CurveE::TOGGLE;
}

juce::String StoreParameter::getText( float normalisedValue, int maximumStringLength ) const
{
    const ParamDesc &d = m_store.desc( m_id );
    const float      v = m_store.fromNormalised( m_id, normalisedValue );
    juce::String     t;
    if ( d.m_curve == CurveE::CHOICE && d.m_choices != nullptr )
        t = d.m_choices[juce::jlimit( 0, static_cast<int>( d.m_maxValue ), static_cast<int>( std::lround( v ) ) )];
    else if ( d.m_curve == CurveE::TOGGLE )
        t = v >= 0.5f ? "On" : "Off";
    else if ( d.m_curve == CurveE::INT )
        t = juce::String( static_cast<int>( std::lround( v ) ) );
    // (juce::String(v, 0) would mean "as precise as it gets", not "no decimals": 159.126 ms)
    else
        t = std::fabs( v ) >= 100.0f ? juce::String( juce::roundToInt( v ) )
                                     : juce::String( v, std::fabs( v ) >= 10.0f ? 1 : 2 );
    return maximumStringLength > 0 ? t.substring( 0, maximumStringLength ) : t;
}

float StoreParameter::getValueForText( const juce::String &text ) const
{
    const ParamDesc &d = m_store.desc( m_id );
    if ( d.m_curve == CurveE::CHOICE && d.m_choices != nullptr )
        for ( int c = 0; c <= static_cast<int>( d.m_maxValue ); ++c )
            if ( text.equalsIgnoreCase( d.m_choices[c] ) ) return m_store.toNormalised( m_id, static_cast<float>( c ) );
    if ( d.m_curve == CurveE::TOGGLE ) return text.equalsIgnoreCase( "on" ) ? 1.0f : 0.0f;
    return m_store.toNormalised( m_id, text.getFloatValue() );
}

// ---------------------------------------------------------------------------------------------------
// The processor

namespace
{

/**
 * @brief An RBJ biquad (normalised by a0), for the K-weighting of the loudness meter.
 * @param[in]  b0   the analogue numerator's b0
 * @param[in]  b1   its b1
 * @param[in]  b2   its b2
 * @param[in]  a0   the denominator's a0
 * @param[in]  a1   its a1
 * @param[in]  a2   its a2
 * @param[out] ob0  receives the normalised b0
 * @param[out] ob1  receives b1
 * @param[out] ob2  receives b2
 * @param[out] oa1  receives a1
 * @param[out] oa2  receives a2
 */
void designBiquad( double b0, double b1, double b2, double a0, double a1, double a2, double &ob0, double &ob1,
                   double &ob2, double &oa1, double &oa2 )
{
    ob0 = b0 / a0;
    ob1 = b1 / a0;
    ob2 = b2 / a0;
    oa1 = a1 / a0;
    oa2 = a2 / a0;
}

}   // namespace

ParhelionProcessor::ParhelionProcessor() : juce::AudioProcessor( busLayout() ), juce::Thread( "Parhelion composer" )
{
    ParamStore &s = store();
    for ( int id = 0; id < s.count(); ++id )
    {
        auto *p = new StoreParameter(
            s, id, juce::String( s.key( id ) ).replaceCharacter( '.', ' ' ) + " (" + s.desc( id ).m_name + ")" );
        m_params.push_back( p );
        addParameter( p );
        p->addListener( this );   // undo: the panel's gestures (parameterGestureChanged)
    }
    m_engine.setLive( true );   // the performer's controls act, the mixer is in a track's path (Engine.h)
    m_seed = static_cast<uint64_t>( juce::Time::currentTimeMillis() % 100000 );
    if ( const char *env = std::getenv( "PARH_SEED" ) ) m_seed = std::strtoull( env, nullptr, 10 );
    m_autoPlay = std::getenv( "PARH_PLAY" ) != nullptr;
    if ( const char *set = std::getenv( "PARH_SET" ) )
        s.set( s.id( ModuleE::SET, 0, set::MINUTES ),
               static_cast<float>( std::atof( set ) ) );   // a set of so many minutes
    // PARH_MUTE=1 (and the screenshot mode): silent from the first sample, never unmuted from inside. The house rule for
    // every automated run -- tests, screenshots, the manual -- is that nothing makes a sound.
    m_forceMute = std::getenv( "PARH_MUTE" ) != nullptr || std::getenv( "PARH_SHOT" ) != nullptr;
    m_mute = m_forceMute;
    // The performer's controllers as a keyboard has them: the mod wheel the master filter, the expression pedal the throw.
    for ( auto &c : m_ccMap ) c = -1;
    m_ccMap[1] =
        s.id( ModuleE::PERFORM, 0, perform::WHEEL );   // the wheel: a source of every voice's matrix (Modulation.h)
    m_ccMap[74] = s.id( ModuleE::PERFORM, 0, perform::FILTER );   // brightness: the master filter
    m_ccMap[11] = s.id( ModuleE::PERFORM, 0, perform::THROW );    // the expression pedal: the echo throw
    // Phase 17: the player's ratings, if any.
    m_ratings = loadRatings( ratingsFile().getFullPathName().toStdString() );
    startTimerHz( 10 );
    m_headsetTick.startTimerHz( 30 );
    compose();
}

ParhelionProcessor::~ParhelionProcessor()
{
    m_headsetTick.stopTimer();
    stopTimer();
    m_newer = true;
    stopThread( 10000 );
    if ( m_exporter && m_exporter->joinable() ) m_exporter->join();
}

Playing ParhelionProcessor::composeNow( ParamStore &snapshot )
{
    snapshot.copyValuesFrom( store() );
    Curation cur;
    uint64_t seed;
    {
        std::lock_guard<std::mutex> g( m_lock );
        cur = m_curation;
        seed = m_seed;
    }
    // Phase 17: the ratings' weights while compose.use_ratings is on.
    Preferences prefs;
    const bool  useRatings = snapshot.getBool( snapshot.id( ModuleE::COMPOSE, 0, compose::USE_RATINGS ) );
    if ( useRatings )
    {
        std::lock_guard<std::mutex> g( m_lock );
        prefs = preferencesFrom( m_ratings );
    }
    Playing      out;
    const double setMinutes = snapshot.get( snapshot.id( ModuleE::SET, 0, set::MINUTES ) );
    if ( setMinutes > 0.0 )
    {
        out.m_isSet = true;
        // A newer score asked for breaks a long set off (its rest is thrown away, the timer composes the new one).
        out.m_set = composeSet( snapshot, seed, setMinutes, &cur, &out.m_setInfo, useRatings ? &prefs : nullptr, {},
                                [this] { return m_again.load() || threadShouldExit(); } );
        for ( const SetTrack &t : out.m_setInfo.m_tracks )
            out.m_tracks.push_back( { t.m_start, t.m_swapIn, t.m_end, t.m_deck, t.m_info } );
    }
    else
    {
        TrackInfo    info;
        TrackRequest req;
        req.m_prefs = useRatings ? &prefs : nullptr;
        if ( const int k = m_jamComposeKey.load(); k >= 0 )
        {   // following the family jam: the leader's key and mode
            req.m_key = k;
            req.m_scale = m_jamComposeScale.load();
        }
        // The performer's "now": the same track rewritten from the bar asked for, loaded where it plays (performNow).
        if ( const int bar = m_nowBar.load(); bar >= 0 )
        {
            std::lock_guard<std::mutex> g( m_lock );
            req.m_rewriteBar = bar;
            req.m_rewriteKind = m_nowKind;
            req.m_earlier = m_current.m_rewrites;   // a second "now" keeps the first (Phase 7)
            out.m_rewrites = m_current.m_rewrites;
            out.m_rewrites.push_back( { bar, m_nowKind } );
            out.m_resume = true;
        }
        out.m_set.m_decks[0] = composeTrack( snapshot, seed, req, &cur, std::string(), &info );
        if ( out.m_resume )
        {
            // Its level corrections as the track had them, until the Leveler has measured the new one.
            std::lock_guard<std::mutex>   g( m_lock );
            const std::vector<LevelMark> &was = m_current.m_set.m_decks[0].m_levels;
            std::vector<LevelMark>       &now = out.m_set.m_decks[0].m_levels;
            if ( !was.empty() && !now.empty() )
            {
                now[0].m_trimDb = was[0].m_trimDb;
                now[0].m_balDb = was[0].m_balDb;
                now[0].m_breakDb = was[0].m_breakDb;
                now[0].m_buildDb = was[0].m_buildDb;
            }
        }
        out.m_set.m_lengthBeats = out.m_set.m_decks[0].m_lengthBeats;
        out.m_tracks.push_back( { 0.0, 0.0, out.m_set.m_lengthBeats, 0, info } );
    }
    return out;
}

int ParhelionProcessor::performNow( SectionKindE kind )
{
    if ( m_composing ) return -1;
    double start = 0.0, bpm = 138.0;
    {
        std::lock_guard<std::mutex> g( m_lock );
        if ( m_current.m_isSet || m_current.m_tracks.empty() ) return -1;
        start = m_current.m_tracks[0].m_start;
        bpm = m_current.m_set.m_decks[0].m_tempo.bpmAt( m_position.load() );
        m_nowKind = kind;
    }
    // The next 8-bar line of the track; the one after it where it is under six seconds away.
    const double beat = m_position.load() - start;
    int          bar = ( static_cast<int>( std::floor( beat / 4.0 ) ) / 8 + 1 ) * 8;
    if ( ( 4.0 * bar - beat ) * 60.0 / std::max( 60.0, bpm ) < 6.0 ) bar += 8;
    m_nowBar = bar;
    compose();
    return bar;
}

void ParhelionProcessor::compose()
{
    // One at a time: a press while composing is remembered and composed when the first is done.
    if ( m_composing.exchange( true ) )
    {
        m_again = true;
        return;
    }
    m_newer = true;   // the last score's measuring, if it still runs, is broken off
    if ( isThreadRunning() ) waitForThreadToExit( -1 );
    m_newer = false;
    startThread();
}

void ParhelionProcessor::newSeed()
{
    beginStep( "New seed" );
    {
        std::lock_guard<std::mutex> g( m_lock );
        m_seed = static_cast<uint64_t>( juce::Random::getSystemRandom().nextInt64() & 0xFFFFFF );
        m_curation = Curation{};
    }
    endStep();
    compose();
}

void ParhelionProcessor::reroll( const juce::String &unit )
{
    beginStep( "reroll " + unit );
    {
        std::lock_guard<std::mutex> g( m_lock );
        m_curation.reroll( unit.toStdString() );
    }
    endStep();
    compose();
}

bool ParhelionProcessor::mixChosen() const
{
    return store().get( store().id( ModuleE::SET, 0, set::MINUTES ) ) > 0.0f;
}

void ParhelionProcessor::chooseMix( bool mix )
{
    const int  id = store().id( ModuleE::SET, 0, set::MINUTES );
    const bool was = mixChosen();
    beginStep( mix ? "DJ mix" : "Track" );
    if ( was && !mix ) m_mixMinutes = store().get( id );
    if ( was != mix ) setFromUi( id, mix ? std::max( 10.0f, m_mixMinutes ) : 0.0f );
    endStep();
    if ( was != mix || playingMix() != mix ) compose();
}

juce::String ParhelionProcessor::curationText() const
{
    std::lock_guard<std::mutex> g( m_lock );
    juce::String                t;
    for ( const auto &r : m_curation.m_rerolls ) t << r.first << " x" << r.second << "  ";
    return t.isEmpty() ? juce::String( "nothing rerolled" ) : t.trimEnd();
}

int ParhelionProcessor::trackAt( double beat ) const
{
    std::lock_guard<std::mutex> g( m_lock );
    int                         found = -1;
    for ( size_t i = 0; i < m_current.m_tracks.size(); ++i )
    {
        const TrackPlace &t = m_current.m_tracks[i];
        // In a set, a track owns the time from its swap to the next's; its intro before belongs to the one it follows.
        const double from = m_current.m_isSet && i > 0 ? t.m_swapIn : t.m_start;
        if ( beat >= from - 1e-9 ) found = static_cast<int>( i );
    }
    return found;
}

int ParhelionProcessor::composedPreset( ModuleE m, int instance ) const
{
    int                         lead = m_engine.leadDeck();
    const double                beat = m_position.load();
    std::lock_guard<std::mutex> g( m_lock );
    if ( lead < 0 || lead >= Decks )
    {
        // Before the engine has played a track's knob settings (stopped, a score just composed): the deck of the track
        // under the playhead, so the panel names the sounds before the first note.
        lead = 0;
        for ( const TrackPlace &t : m_current.m_tracks )
            if ( beat >= t.m_start && beat < t.m_end )
            {
                lead = t.m_deck;
                break;
            }
    }
    int preset = -1;
    for ( const SoundPick &k :
          m_current.m_set.m_decks[lead].m_sounds )   // in beat order: the last one begun is the one that holds
        if ( k.m_module == static_cast<int>( m ) && k.m_instance == instance && k.m_beat <= beat + 1.0 )
            preset = k.m_preset;
    return preset;
}

void ParhelionProcessor::applyPreset( ModuleE m, int instance, int index )
{
    const std::vector<SoundPreset> &list = factoryPresets( m, instance );
    if ( index < 0 || index >= static_cast<int>( list.size() ) ) return;
    beginStep( juce::String( "preset " ) + list[static_cast<size_t>( index )].m_name );
    const juce::ScopedValueSetter<bool> one( m_restoring,
                                             true );   // the knobs' gestures join the step, not steps of their own
    for ( const auto &[k, v] : presetKnobs( m, instance, list[static_cast<size_t>( index )] ) )
    {
        const int       id = store().id( m, instance, k );
        StoreParameter *p = parameter( id );
        if ( p == nullptr ) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost( store().toNormalised( id, v ) );
        p->endChangeGesture();
    }
    m_restoring = false;
    endStep();
}

juce::File ParhelionProcessor::userPresetFolder( ModuleE m, int instance ) const
{
    const std::string key = store().key( store().id( m, instance, 0 ) );
    juce::String      synth( key.substr( 0, key.find( '.' ) ) );
    if ( m == ModuleE::PERC ) synth = synth.trimCharactersAtEnd( "0123456789" );   // every lane of the kit
    const char      *dir = std::getenv( "PARH_USER_DIR" );
    const juce::File user =
        dir != nullptr
            ? juce::File( dir )
            : juce::File::getSpecialLocation( juce::File::userDocumentsDirectory ).getChildFile( "Parhelion" );
    return user.getChildFile( "Presets" ).getChildFile( synth );
}

juce::Array<juce::File> ParhelionProcessor::userPresets( ModuleE m, int instance ) const
{
    juce::Array<juce::File> out =
        userPresetFolder( m, instance ).findChildFiles( juce::File::findFiles, false, "*.txt" );
    std::sort( out.begin(), out.end(), []( const juce::File &a, const juce::File &b )
               { return a.getFileNameWithoutExtension().compareNatural( b.getFileNameWithoutExtension() ) < 0; } );
    return out;
}

juce::File ParhelionProcessor::saveUserPreset( ModuleE m, int instance, const juce::String &name )
{
    const juce::String clean = juce::File::createLegalFileName( name.trim() );
    if ( clean.isEmpty() ) return {};
    const juce::String text( userPresetText( store(), m, instance ) );
    const juce::File   dir = userPresetFolder( m, instance );
    dir.createDirectory();
    const juce::File f = dir.getChildFile( clean + ".txt" );
    // "\n" line ends: JUCE's default is "\r\n", and the preset reads back as the text it was.
    return f.replaceWithText( text, false, false, "\n" ) ? f : juce::File();
}

bool ParhelionProcessor::applyUserPreset( ModuleE m, int instance, const juce::File &file )
{
    if ( !file.existsAsFile() ) return false;
    beginStep( "preset " + file.getFileNameWithoutExtension() );
    {
        const juce::ScopedValueSetter<bool> one( m_restoring, true );   // one step, not one per knob
        // The whole sound, as a factory preset has it: its knobs, the defaults for the rest it would set.
        for ( const auto &[k, v] : userPresetKnobs( store(), m, instance, file.loadFileAsString().toStdString() ) )
        {
            const int       id = store().id( m, instance, k );
            StoreParameter *p = parameter( id );
            if ( p == nullptr ) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost( store().toNormalised( id, v ) );
            p->endChangeGesture();
        }
    }
    endStep();
    return true;
}

void ParhelionProcessor::applyKnobs( const juce::String &what, const std::vector<std::pair<int, float>> &values )
{
    endPreview();
    beginStep( what );
    {
        const juce::ScopedValueSetter<bool> one( m_restoring, true );   // the knobs' gestures join the step
        for ( const auto &[id, v] : values )
        {
            StoreParameter *p = parameter( id );
            if ( p == nullptr ) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost( store().toNormalised( id, v ) );
            p->endChangeGesture();
        }
    }
    endStep();
}

void ParhelionProcessor::previewKnobs( const std::vector<std::pair<int, float>> &values )
{
    endPreview();
    for ( const auto &[id, v] : values )
    {
        if ( id < 0 || id >= store().count() ) continue;
        m_previewSaved.emplace_back( id, store().get( id ) );
        store().set( id, v );
    }
}

void ParhelionProcessor::endPreview()
{
    for ( const auto &[id, v] : m_previewSaved ) store().set( id, v );
    m_previewSaved.clear();
}

void ParhelionProcessor::length( double &beats, double &seconds ) const
{
    std::lock_guard<std::mutex> g( m_lock );
    beats = m_current.m_set.m_lengthBeats;
    seconds = m_current.m_set.m_decks[0].m_tempo.secondsAt( beats );
}

void ParhelionProcessor::run()
{
    ParamStore snapshot;
    Playing    p = composeNow( snapshot );
    uint64_t   id = 0;
    {
        std::lock_guard<std::mutex> g( m_lock );
        m_pending = std::make_unique<Playing>( p );
        id = m_pendingId = ++m_composed;
    }
    m_composing = false;
    // Then its loudness (Leveler.h), while it already plays: 20 seconds of each track's loudest part rendered. The
    // corrections follow and glide in; a newer score asked for, or the plugin closing, breaks the measuring off.
    const auto stop = [this] { return m_newer.load() || threadShouldExit(); };
    const bool done = p.m_isSet ? !levelSet( p.m_set, snapshot, 20.0, stop ).empty()
                                : !levelScore( p.m_set.m_decks[0], snapshot, 20.0, stop ).empty();
    if ( !done ) return;
    std::lock_guard<std::mutex> g( m_lock );
    for ( int d = 0; d < Decks; ++d )
    {
        m_trims[d].clear();
        m_bal[d].clear();
        for ( const LevelMark &m : p.m_set.m_decks[d].m_levels )
        {
            m_trims[d].push_back( m.m_trimDb );
            m_bal[d].push_back( m.m_balDb );
        }
    }
    m_trimsFor = id;
}

void ParhelionProcessor::takeTrims()
{
    std::vector<float>      trims[Decks];
    std::vector<BalanceDbT> bal[Decks];
    {
        std::lock_guard<std::mutex> g( m_lock );
        if ( m_trimsFor == 0 || m_trimsFor > m_playingId ) return;   // (none, or for a score not yet loaded)
        const bool mine = m_trimsFor == m_playingId;
        m_trimsFor = 0;
        if ( !mine ) return;
        for ( int d = 0; d < Decks; ++d )
        {
            trims[d].swap( m_trims[d] );
            bal[d].swap( m_bal[d] );
            for ( size_t i = 0; i < m_current.m_set.m_decks[d].m_levels.size() && i < trims[d].size(); ++i )
                m_current.m_set.m_decks[d].m_levels[i].m_trimDb = trims[d][i];
            for ( size_t i = 0; i < m_current.m_set.m_decks[d].m_levels.size() && i < bal[d].size(); ++i )
                m_current.m_set.m_decks[d].m_levels[i].m_balDb = bal[d][i];
        }
        m_levelled = true;
    }
    // A few numbers: the audio thread waits for them instead of losing a block (suspendProcessing would silence one).
    const juce::ScopedLock sl( getCallbackLock() );
    for ( int d = 0; d < Decks; ++d )
    {
        if ( !trims[d].empty() ) m_engine.setLevelTrims( d, trims[d] );
        if ( !bal[d].empty() ) m_engine.setLevelBalance( d, bal[d] );
    }
}

SetScore ParhelionProcessor::forPlayback( const Playing &p ) const
{
    SetScore     out = p.m_set;
    const double bpm = m_hostBpm.load();
    if ( followsClock() && bpm > 0.0 )
        for ( Score &s : out.m_decks ) s.m_tempo.setConstant( bpm );
    return out;
}

void ParhelionProcessor::loadEngine( const Playing &p )
{
    const SetScore s = forPlayback( p );
    m_engine.prepare( m_sampleRate, m_blockSize );
    m_engine.setMeterSink( &m_meterSink );   // the Mixer page's strips (a handful of sums per sample)
    if ( p.m_isSet )
        m_engine.loadSet( s );
    else
        m_engine.load( s.m_decks[0] );
    std::lock_guard<std::mutex> g( m_lock );
    if ( m_levelled )   // the corrections already found go with the score (a reload at a new host tempo)
        for ( int d = 0; d < Decks; ++d )
        {
            std::vector<float>      t;
            std::vector<BalanceDbT> b;
            for ( const LevelMark &m : p.m_set.m_decks[d].m_levels )
            {
                t.push_back( m.m_trimDb );
                b.push_back( m.m_balDb );
            }
            if ( !t.empty() ) m_engine.setLevelTrims( d, t );
            if ( !b.empty() ) m_engine.setLevelBalance( d, b );
        }
}

void ParhelionProcessor::timerCallback()
{
    // Ableton Link (02.10.2026): the standalone joins the session while the setting is on; a DAW's transport rules the
    // plugin. The follow flag falls when the session goes.
    m_link.setEnabled( wrapperType == wrapperType_Standalone &&
                       ( frame::Settings::of( "Parhelion" ).link() || frame::LinkClock::forcedOn() ) );
    m_link.tick();
    if ( !m_link.enabled() ) m_linkFollowing = false;
    // The family jam (02.10.2026, Jam.h): the role from the settings; a follower composes its next track in the
    // leader's key and mode.
    m_jam.setRole( frame::Settings::of( "Parhelion" ).jamRole() );
    // FAMILY_JAM_LOG=\<file\>: what the jam does here, a line whenever it changes (a test aid).
    if ( const char *logFile = std::getenv( "FAMILY_JAM_LOG" ) )
    {
        const juce::String line = m_jam.status() + "; transposed " + juce::String( m_jamTransposeOut.load() );
        if ( line != m_jamLogged )
        {
            m_jamLogged = line;
            juce::File( logFile ).appendText( juce::Time::getCurrentTime().toString( false, true, true, true ) + "  " +
                                              line + "\n" );
        }
    }
    if ( m_jam.role() == frame::Settings::JamRoleE::FOLLOW && m_jam.leaderRoot() >= 0 )
    {
        m_jamComposeKey = m_jam.leaderRoot();
        m_jamComposeScale = scaleOfMode( m_jam.leaderMode() );
    }
    else
    {
        m_jamComposeKey = -1;
        m_jamComposeScale = -1;
    }
    // The cue sender follows its two parameters (message thread: the socket is opened and closed here).
    {
        const ParamStore &s = store();
        const int         port =
            s.getBool( s.id( ModuleE::CUE, 0, cue::ENABLED ) ) ? s.getInt( s.id( ModuleE::CUE, 0, cue::PORT ) ) : 0;
        if ( port != m_cuePort )
        {
            m_cues.stop();
            m_cuePort = port;
            if ( port > 0 )
            {
                const char *host = std::getenv( "PARH_CUE_HOST" );
                if ( !m_cues.start( host != nullptr ? host : "127.0.0.1", port ) ) m_cuePort = 0;
            }
        }
    }
    // A track began (or a jump landed in another): the engine wrote its sounds and mix on the knobs -- the host and the
    // pages are told (the values are in the store already).
    if ( const uint32_t v = m_engine.soundsVersion(); v != m_toldSounds )
    {
        m_toldSounds = v;
        for ( StoreParameter *p : m_params ) p->sendValueChangedMessageToListeners( p->getValue() );
    }
    std::unique_ptr<Playing> next;
    uint64_t                 nextId = 0;
    {
        std::lock_guard<std::mutex> g( m_lock );
        next = std::move( m_pending );
        nextId = m_pendingId;
    }
    // A score asked for again while it was composed is out of date: the next one comes.
    if ( next && m_again && !m_composing )
    {
        next.reset();
        m_again = false;
        compose();
        return;
    }
    if ( !next )
    {
        // In a host the score plays at the host's tempo. A new tempo means new sample positions for every event, and
        // the engine allocates when it loads: so the score is loaded again here, and the engine goes on from its beat.
        const double bpm = m_hostBpm.load();
        // A Link session the standalone followed and now leads again (the others left, or Link went off): back to the
        // composed tempo (02.10.2026).
        const bool back = !followsClock() && m_playedBpm.load() > 0.0;
        if ( back || ( followsClock() && bpm > 0.0 && std::fabs( bpm - m_playedBpm.load() ) > 1.0e-3 ) )
        {
            Playing p;
            copyPlaying( p );
            suspendProcessing( true );
            const double beat = m_engine.beat();
            loadEngine( p );
            m_engine.seek( beat );
            m_playedBpm = back ? 0.0 : bpm;
            suspendProcessing( false );
        }
        if ( m_again && !m_composing )
        {
            m_again = false;
            compose();
        }
        takeTrims();
        return;
    }
    // The engine allocates when it loads: never on the audio thread.
    suspendProcessing( true );
    {
        std::lock_guard<std::mutex> g( m_lock );
        m_levelled = false;
    }
    const bool   resume = next->m_resume;
    const double at = m_engine.beat();
    loadEngine( *next );
    m_playedBpm = followsClock() ? m_hostBpm.load() : 0.0;
    {
        std::lock_guard<std::mutex> g( m_lock );
        m_current = std::move( *next );
        m_playingId = nextId;
    }
    ++m_scoreVersion;
    if ( resume )
    {
        // The performer's "now": the rewritten track goes on from where the old one was.
        m_engine.seek( at );
        m_position = at;
        m_nowBar = -1;
    }
    else
    {
        m_position = 0.0;
    }
    m_cueTap.reset();
    if ( m_autoPlay )
    {
        m_autoPlay = false;
        m_playing = true;
    }
    suspendProcessing( false );
    takeTrims();
}

void ParhelionProcessor::takeStripMeters( float *peak, float *rms )
{
    const double n =
        static_cast<double>( std::max<int64_t>( 1, m_stripSamples.exchange( 0, std::memory_order_relaxed ) ) );
    for ( int s = 0; s < MeterSink::Strips; ++s )
    {
        peak[s] = m_meterSink.m_peak[s].exchange( 0.0f, std::memory_order_relaxed );
        rms[s] = static_cast<float>( std::sqrt( m_meterSink.m_sum[s].exchange( 0.0, std::memory_order_relaxed ) / n ) );
    }
}

void ParhelionProcessor::takeMeters( float *deckPeak, float *deckRms, float &outPeak, float &momentaryLufs )
{
    const int n = m_meterCount.exchange( 0, std::memory_order_acquire );
    for ( int d = 0; d < Decks; ++d )
    {
        const size_t k = static_cast<size_t>( d );
        deckPeak[d] = m_meterPeak[k].exchange( 0.0f, std::memory_order_relaxed );
        const double s = m_meterSum[k].exchange( 0.0, std::memory_order_relaxed );
        deckRms[d] = n > 0 ? static_cast<float>( std::sqrt( s / n ) ) : 0.0f;
    }
    outPeak = m_outPeak.exchange( 0.0f, std::memory_order_relaxed );
    momentaryLufs = m_lufs.load( std::memory_order_relaxed );
}

void ParhelionProcessor::prepareToPlay( double sampleRate, int samplesPerBlock )
{
    m_sampleRate = sampleRate;
    m_blockSize = std::max( 1, samplesPerBlock );
    // The deck taps for the meters (reading only: the mix is the same to the bit).
    m_tapBuf.assign( static_cast<size_t>( Decks * 2 * m_blockSize ), 0.0f );
    for ( int d = 0; d < Decks; ++d )
    {
        m_tapL[static_cast<size_t>( d )] = m_tapBuf.data() + static_cast<size_t>( 2 * d * m_blockSize );
        m_tapR[static_cast<size_t>( d )] = m_tapBuf.data() + static_cast<size_t>( ( 2 * d + 1 ) * m_blockSize );
    }
    m_engine.setDeckTaps( m_tapL.data(), m_tapR.data() );
    // The stems' buffers for their outputs (02.10.2026); the engine writes them only while a stem's bus is on.
    m_stemBuf.assign( static_cast<size_t>( Engine::STEMS * 2 * m_blockSize ), 0.0f );
    for ( int s = 0; s < Engine::STEMS; ++s )
    {
        m_stemL[static_cast<size_t>( s )] = m_stemBuf.data() + static_cast<size_t>( 2 * s * m_blockSize );
        m_stemR[static_cast<size_t>( s )] = m_stemBuf.data() + static_cast<size_t>( ( 2 * s + 1 ) * m_blockSize );
    }
    m_engine.setStems( nullptr, nullptr );
    m_stemsOn = false;
    m_engine.setNoteTap( &m_noteTap );   // MIDI out (02.10.2026)
    // The K-weighting of BS.1770 at this rate (the pre-filter's shelf and the RLB high pass, as pyloudnorm designs them).
    {
        const double fs = sampleRate;
        double       K = std::tan( juce::MathConstants<double>::pi * 1681.974450955533 / fs );
        const double Vh = std::pow( 10.0, 3.999843853973347 / 20.0 ), Vb = std::pow( Vh, 0.4996667741545416 ),
                     Q = 0.7071752369554196;
        const double a0 = 1.0 + K / Q + K * K;
        for ( Biquad &b : m_kShelf )
        {
            designBiquad( Vh + Vb * K / Q + K * K, 2.0 * ( K * K - Vh ), Vh - Vb * K / Q + K * K, a0,
                          2.0 * ( K * K - 1.0 ), 1.0 - K / Q + K * K, b.m_b0, b.m_b1, b.m_b2, b.m_a1, b.m_a2 );
            b.m_z1 = b.m_z2 = 0.0;
        }
        K = std::tan( juce::MathConstants<double>::pi * 38.13547087602444 / fs );
        const double Q2 = 0.5003270373238773, a02 = 1.0 + K / Q2 + K * K;
        for ( Biquad &b : m_kHigh )
        {
            designBiquad( 1.0, -2.0, 1.0, a02, 2.0 * ( K * K - 1.0 ), 1.0 - K / Q2 + K * K, b.m_b0, b.m_b1, b.m_b2,
                          b.m_a1, b.m_a2 );
            b.m_z1 = b.m_z2 = 0.0;
        }
        m_kCoef = 1.0 - std::exp( -1.0 / ( 0.4 * fs / 3.0 ) );   // a 400 ms window as an exponential of a third of it
        m_kMs = 0.0;
    }
    if ( const char *secs = std::getenv( "PARH_PLAY" ); secs != nullptr && std::getenv( "PARH_RECORD" ) != nullptr )
    {
        m_recordTarget = static_cast<size_t>( std::atof( secs ) * sampleRate ) * 2;
        m_record.assign( m_recordTarget, 0.0f );
        m_recordPos = 0;
    }
    Playing p;
    copyPlaying( p );
    loadEngine( p );
    m_playedBpm = followsClock() ? m_hostBpm.load() : 0.0;
}

juce::AudioProcessor::BusesProperties ParhelionProcessor::busLayout()
{
    BusesProperties b = BusesProperties().withOutput( "Output", juce::AudioChannelSet::stereo(), true );
    for ( int s = 0; s < Engine::STEMS; ++s )
    {
        const juce::String name( Engine::stemName( s ) );
        b = b.withOutput( name.substring( 0, 1 ).toUpperCase() + name.substring( 1 ), juce::AudioChannelSet::stereo(),
                          false );
    }
    return b;
}

bool ParhelionProcessor::isBusesLayoutSupported( const BusesLayout &layouts ) const
{
    if ( layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo() ) return false;
    for ( int b = 1; b < layouts.outputBuses.size(); ++b )
    {   // a stem's bus: stereo, or off
        const juce::AudioChannelSet &set = layouts.outputBuses.getReference( b );
        if ( !set.isDisabled() && set != juce::AudioChannelSet::stereo() ) return false;
    }
    return true;
}

void ParhelionProcessor::processBlock( juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi )
{
    juce::ScopedNoDenormals noDenormals;
    perform( midi );
    midi.clear();
    const int n = buffer.getNumSamples();
    bool      play = m_playing.load();
    // In a host, the host's playhead is the clock.
    if ( wrapperType != wrapperType_Standalone )
    {
        play = false;
        if ( auto *ph = getPlayHead() )
        {
            if ( const auto pos = ph->getPosition() )
            {
                play = pos->getIsPlaying();
                if ( const auto bpm = pos->getBpm(); bpm && *bpm > 0.0 ) m_hostBpm = *bpm;
                // Until the message thread has loaded the score at a new host tempo, the positions drift apart by
                // design: only a real jump of the host's playhead is followed then.
                const bool tempoPending = std::fabs( m_hostBpm.load() - m_playedBpm.load() ) > 1.0e-3;
                if ( play )
                    if ( const auto ppq = pos->getPpqPosition() )
                        if ( std::fabs( *ppq - m_engine.beat() ) > ( tempoPending ? 4.0 : 0.05 ) )
                            m_engine.seek( *ppq );
            }
        }
    }
    else if ( m_link.enabled() )
    {
        // Ableton Link (02.10.2026, LinkClock.h): with other apps in the session the standalone follows it as a DAW's
        // playhead -- its tempo (the score reloaded at it on the message thread), its bar phase (the bars line up, the
        // place in the track stays), its start and stop; alone in it, the standalone leads (the tempo goes out below).
        const double                latency = static_cast<double>( getLatencySamples() + n ) / m_sampleRate;
        const frame::LinkClock::Now ln = m_link.capture( latency, 4.0 );
        m_linkFollowing = ln.m_peers > 0;
        if ( ln.m_peers > 0 )
        {
            if ( play != m_linkPlayed )
            {
                m_link.setPlaying( play, latency, 4.0 );   // Play or Stop pressed here: to the session
            }
            else if ( ln.m_playing != play )
            {
                play = ln.m_playing;   // pressed elsewhere: here too
                m_playing = play;
            }
            m_hostBpm = ln.m_bpm;
            m_linkBeat = ln.m_beat;
            const bool tempoPending = std::fabs( m_hostBpm.load() - m_playedBpm.load() ) > 1.0e-3;
            if ( play && !tempoPending )
            {
                double d = std::fmod( ln.m_beat - m_engine.beat(), 4.0 );
                if ( d > 2.0 )
                    d -= 4.0;
                else if ( d <= -2.0 )
                    d += 4.0;
                if ( std::fabs( d ) > 0.05 ) m_engine.seek( m_engine.beat() + d );
            }
        }
        m_linkPlayed = play;
    }
    else
    {
        m_linkFollowing = false;
    }
    const double seek = m_seekRequest.exchange( -1.0 );
    if ( seek >= 0.0 ) m_engine.seek( seek );
    // The family jam's shared timeline (Jam.h): the host's beat in a DAW, Link's with others in the session; none alone.
    double shared = -1.0;
    if ( play && wrapperType != wrapperType_Standalone )
        shared = m_engine.beat();
    else if ( play && m_linkFollowing.load( std::memory_order_relaxed ) )
        shared = m_linkBeat;
    {   // Stopped, or another keyboard target: every played key is released (01.10.2026).
        const int target = store().getInt( store().id( ModuleE::PERFORM, 0, perform::KEYBOARD_PART ) );
        if ( !play || target != m_keyboardSeen ) m_engine.liveAllOff();
        m_keyboardSeen = target;
    }
    if ( !play || buffer.getNumChannels() < 2 || n > m_blockSize )
    {
        silenceMidi( midi );
        buffer.clear();
        return;
    }
    const double  before = m_engine.beat();
    const int64_t start = m_engine.samplePosition();
    if ( start != m_midiExpect ) silenceMidi( midi );   // a jump: whatever sounded is released
    m_noteTap.clear();
    {   // The stems' outputs (02.10.2026): the engine writes the stems while the host has one of their buses on.
        bool want = false;
        for ( int b = 1; b < getBusCount( false ) && !want; ++b )
            if ( const auto *bus = getBus( false, b ) ) want = bus->isEnabled();
        if ( want != m_stemsOn )
        {
            m_engine.setStems( want ? m_stemL.data() : nullptr, want ? m_stemR.data() : nullptr );
            m_stemsOn = want;
        }
    }
    jamFollow( before, shared );
    m_engine.process( buffer.getWritePointer( 0 ), buffer.getWritePointer( 1 ), n );
    jamLead( before, m_engine.beat(), shared );
    emitMidi( midi, start, n );
    if ( m_stemsOn )
        for ( int b = 1; b < getBusCount( false ) && b - 1 < Engine::STEMS; ++b )
        {
            const auto *bus = getBus( false, b );
            if ( bus == nullptr || !bus->isEnabled() ) continue;
            auto out = getBusBuffer( buffer, false, b );
            if ( out.getNumChannels() < 2 ) continue;
            out.copyFrom( 0, 0, m_stemL[static_cast<size_t>( b - 1 )], n );
            out.copyFrom( 1, 0, m_stemR[static_cast<size_t>( b - 1 )], n );
        }
    if ( m_link.enabled() && !m_linkFollowing.load( std::memory_order_relaxed ) &&
         n > 0 )   // alone in the session: it takes our tempo
        m_link.proposeTempo( 60.0 * ( m_engine.beat() - before ) * m_sampleRate / static_cast<double>( n ), 0.0 );
    m_midiExpect = m_engine.samplePosition();
    m_stripSamples.fetch_add( n, std::memory_order_relaxed );
    m_position = m_engine.beat();
    if ( m_cues.running() )
    {
        // The cues of this block, stamped with the moment it is heard (Cue.h); a jump starts the marks again.
        if ( std::fabs( before - m_lastBeat ) > 1.0e-6 ) m_cueTap.reset();
        const double blockSeconds = static_cast<double>( n ) / m_sampleRate;
        const double bpm = 60.0 * ( m_engine.beat() - before ) / std::max( 1e-9, blockSeconds );
        m_cueTap.scan( m_engine.cueMarks(), before, m_engine.beat(), static_cast<float>( bpm ), CueSender::nowNanos(),
                       static_cast<int64_t>( blockSeconds * 1.0e9 ), static_cast<int64_t>( blockSeconds * 1.0e9 ),
                       m_cues.ring() );
    }
    m_lastBeat = m_engine.beat();
    {
        // The meters: raised here, taken by the editor (takeMeters). One writer, so a load and a store will do.
        for ( int d = 0; d < Decks; ++d )
        {
            const size_t k = static_cast<size_t>( d );
            float        pk = 0.0f;
            double       ss = 0.0;
            for ( int i = 0; i < n; ++i )
            {
                const float l = m_tapL[k][i], r = m_tapR[k][i];
                pk = std::max( pk, std::max( std::fabs( l ), std::fabs( r ) ) );
                ss += 0.5 * ( static_cast<double>( l ) * l + static_cast<double>( r ) * r );
            }
            if ( pk > m_meterPeak[k].load( std::memory_order_relaxed ) )
                m_meterPeak[k].store( pk, std::memory_order_relaxed );
            m_meterSum[k].store( m_meterSum[k].load( std::memory_order_relaxed ) + ss, std::memory_order_relaxed );
        }
        m_meterCount.fetch_add( n, std::memory_order_release );
        const float *l = buffer.getReadPointer( 0 );
        const float *r = buffer.getReadPointer( 1 );
        float        pk = m_outPeak.load( std::memory_order_relaxed );
        for ( int i = 0; i < n; ++i )
        {
            pk = std::max( pk, std::max( std::fabs( l[i] ), std::fabs( r[i] ) ) );
            const double kl = m_kHigh[0].run( m_kShelf[0].run( l[i] ) ), kr = m_kHigh[1].run( m_kShelf[1].run( r[i] ) );
            m_kMs += m_kCoef * ( kl * kl + kr * kr - m_kMs );
        }
        m_outPeak.store( pk, std::memory_order_relaxed );
        m_lufs.store( m_kMs > 1e-10 ? static_cast<float>( -0.691 + 10.0 * std::log10( m_kMs ) ) : -70.0f,
                      std::memory_order_relaxed );
    }
    if ( m_recordTarget > 0 )
    {
        const float *l = buffer.getReadPointer( 0 );
        const float *r = buffer.getReadPointer( 1 );
        size_t       pos = m_recordPos.load();
        for ( int i = 0; i < n && pos + 1 < m_recordTarget; ++i )
        {
            m_record[pos++] = l[i];
            m_record[pos++] = r[i];
        }
        m_recordPos = pos + 1 >= m_recordTarget ? m_recordTarget : pos;
    }
    if ( m_mute.load( std::memory_order_relaxed ) ) buffer.clear();   // after the meters and the recording
    // The standalone stops at the end, with the rooms rung out.
    if ( wrapperType == wrapperType_Standalone && m_engine.seconds() > m_engine.lengthSeconds() + 8.0 )
        m_playing = false;
}

void ParhelionProcessor::silenceMidi( juce::MidiBuffer &midi )
{
    m_pendingOffs = 0;
    m_midiExpect = -1;
    if ( !m_midiSounding ) return;
    m_midiSounding = false;
    for ( int ch = 1; ch <= 16; ++ch ) midi.addEvent( juce::MidiMessage::allNotesOff( ch ), 0 );
}

void ParhelionProcessor::emitMidi( juce::MidiBuffer &midi, int64_t start, int n )
{
    const int64_t end = start + n;
    auto          at = [&]( int64_t s ) { return static_cast<int>( std::clamp<int64_t>( s - start, 0, n - 1 ) ); };
    // The one-shots' offs that are due (an off and an on at one sample: the off first).
    for ( int i = 0; i < m_pendingOffs; )
    {
        const PendingOff &p = m_pendingOff[static_cast<size_t>( i )];
        if ( p.m_sample < end )
        {
            midi.addEvent( juce::MidiMessage::noteOff( p.m_channel, p.m_pitch ), at( p.m_sample ) );
            m_pendingOff[static_cast<size_t>( i )] = m_pendingOff[static_cast<size_t>( --m_pendingOffs )];
        }
        else
        {
            ++i;
        }
    }
    for ( int i = 0; i < m_noteTap.m_count; ++i )
    {
        const parh::NoteTap::Note &nt = m_noteTap.m_notes[i];
        const int                  channel = parh::midiChannelOf( static_cast<parh::PartE>( nt.m_part ) ) + 1;
        if ( nt.m_velocity == 0 )
        {
            midi.addEvent( juce::MidiMessage::noteOff( channel, nt.m_pitch ), at( nt.m_sample ) );
            continue;
        }
        midi.addEvent( juce::MidiMessage::noteOn( channel, nt.m_pitch, static_cast<juce::uint8>( nt.m_velocity ) ),
                       at( nt.m_sample ) );
        m_midiSounding = true;
        if ( nt.m_offSample >= 0 && m_pendingOffs < static_cast<int>( m_pendingOff.size() ) )
            m_pendingOff[static_cast<size_t>( m_pendingOffs++ )] =
                PendingOff{ std::max( nt.m_offSample, nt.m_sample + 1 ), static_cast<uint8_t>( channel ), nt.m_pitch };
    }
}

int ParhelionProcessor::scaleOfMode( const juce::String &mode )
{
    const juce::String m = mode.trim();
    for ( int i = 0; i < static_cast<int>( parh::ScaleE::COUNT ); ++i )
        if ( m.equalsIgnoreCase( parh::ScaleNames[i] ) ) return i;
    // The family's other names for its scales: a minor is an Aeolian, the just minor too.
    if ( m.containsIgnoreCase( "minor" ) && !m.containsIgnoreCase( "penta" ) ) return scaleOfMode( "Aeolian" );
    if ( m.containsIgnoreCase( "penta" ) ) return scaleOfMode( "Minor Pentatonic" );
    if ( m.containsIgnoreCase( "major" ) || m.containsIgnoreCase( "ionian" ) )
        return scaleOfMode( "Ionian" );   // where there is one
    return -1;
}

int ParhelionProcessor::homeRootAt( double beat ) const
{
    int root = m_engine.score().m_keyRoot;
    for ( const CueMark &m : m_engine.cueMarks() )
    {
        if ( m.m_beat > beat + 1.0e-6 ) break;
        if ( m.m_kind != CueKindE::KEY ) continue;
        const int n =
            std::atoi( m.m_text );   // a Camelot label: "8A" the minor key 7 (n - 5) (mod 12), "8B" its relative major
        if ( n >= 1 && n <= 12 )
            root = ( ( 7 * ( n - 5 ) + ( std::strchr( m.m_text, 'B' ) != nullptr ? 3 : 0 ) ) % 12 + 12 ) % 12;
    }
    return root;
}

void ParhelionProcessor::jamFollow( double before, double shared )
{
    if ( m_jam.role() != frame::Settings::JamRoleE::FOLLOW )
    {
        if ( m_jamFollowing ) m_engine.setJam( 0, -1.0f, false );
        m_jamFollowing = false;
        m_jamTranspose = 0;
        m_jamTransposeOut = 0;
        m_jamLastBefore = before;
        return;
    }
    // The block's length in beats, from the last one (none after a jump or a stop).
    const double step = ( m_jamLastBefore >= 0.0 && before > m_jamLastBefore && before - m_jamLastBefore < 1.0 )
                            ? before - m_jamLastBefore
                            : 0.0;
    m_jamLastBefore = before;
    // What the leader says, as it stands by the end of this block: a drop on a bar line inside it is played on it.
    const frame::JamState js = m_jam.stateAt( shared >= 0.0 ? shared + step : -1.0 );
    int                   want = 0;
    if ( js.m_root >= 0 )
    {
        const int d = ( ( js.m_root - homeRootAt( before ) ) % 12 + 12 ) % 12;
        want = d > 6 ? d - 12 : d;   // the shortest way, up to a tritone either side
    }
    // The root moves on a bar line: inside this block, or at once when there is no step to tell.
    const bool barLine = step <= 0.0 || std::floor( ( before + step ) / 4.0 ) > std::floor( before / 4.0 );
    if ( want != m_jamTranspose && barLine ) m_jamTranspose = want;
    m_jamTransposeOut = m_jamTranspose;
    m_engine.setJam( m_jamTranspose, js.m_root >= 0 ? js.m_energy : -1.0f, js.m_root >= 0 && js.m_rhythmOut );
    m_jamFollowing = true;
}

void ParhelionProcessor::jamLead( double before, double after, double shared )
{
    if ( m_jam.role() != frame::Settings::JamRoleE::LEAD )
    {
        m_jamLeadStarted = false;
        return;
    }
    const auto &marks = m_engine.cueMarks();
    const char *mode =
        parh::ScaleNames[juce::jlimit( 0, static_cast<int>( parh::ScaleE::COUNT ) - 1, m_engine.score().m_scale )];
    const auto barOf = [&]( double beat )
    {
        return shared >= 0.0 ? static_cast<int64_t>( std::llround( ( shared + beat - before ) / 4.0 ) ) : int64_t( -1 );
    };
    const auto keyOf = []( const CueMark &m )
    {
        const int n = std::atoi( m.m_text );
        return n >= 1 && n <= 12
                   ? ( ( 7 * ( n - 5 ) + ( std::strchr( m.m_text, 'B' ) != nullptr ? 3 : 0 ) ) % 12 + 12 ) % 12
                   : -1;
    };
    // The first block, or a jump: where it stands now -- the last section and key at the coming beat, at once.
    if ( !m_jamLeadStarted || std::fabs( before - m_jamLeadLast ) > 1.0e-6 )
    {
        const CueMark *block = nullptr;
        const CueMark *key = nullptr;
        for ( const CueMark &m : marks )
        {
            if ( m.m_beat > before + 1.0 ) break;
            if ( m.m_kind == CueKindE::BLOCK )
                block = &m;
            else if ( m.m_kind == CueKindE::KEY )
                key = &m;
        }
        m_jam.postKey( key != nullptr ? keyOf( *key ) : m_engine.score().m_keyRoot, mode );
        if ( block != nullptr )
        {
            float                    e = 0.5f;
            bool                     drop = false;
            const frame::JamSectionE s = frame::jamSectionOf( block->m_text, e, drop );   // before e is read
            m_jam.postSection( s, e, false, -1 );
        }
        m_jamLeadStarted = true;
    }
    else
    {
        // One beat ahead: a follower acts on the very bar line the leader means.
        for ( const CueMark &m : marks )
        {
            if ( m.m_beat < before + 1.0 ) continue;
            if ( m.m_beat >= after + 1.0 ) break;
            if ( m.m_kind == CueKindE::BLOCK )
            {
                float                    e = 0.5f;
                bool                     drop = false;
                const frame::JamSectionE s = frame::jamSectionOf( m.m_text, e, drop );
                m_jam.postSection( s, e, drop, barOf( m.m_beat ) );
            }
            else if ( m.m_kind == CueKindE::KEY )
            {
                const int k = keyOf( m );
                if ( k >= 0 ) m_jam.postKey( k, mode );
            }
        }
    }
    m_jamLeadLast = after;
}

juce::String ParhelionProcessor::linkStatus() const
{
    if ( !m_link.enabled() ) return "off";
    const int n = m_link.peers();
    return n == 0 ? "alone in the session: it takes this tempo"
                  : juce::String( n ) + ( n == 1 ? " other app" : " other apps" );
}

bool ParhelionProcessor::saveSet( const juce::File &file )
{
    SetFile sf;
    {
        std::lock_guard<std::mutex> g( m_lock );
        sf.m_seed = m_seed;
        sf.m_curation = m_curation;
    }
    sf.m_minutes = store().get( store().id( ModuleE::COMPOSE, 0, compose::MINUTES ) );
    sf.m_set = store().get( store().id( ModuleE::SET, 0, set::MINUTES ) );
    return parh::saveSet( file.getFullPathName().toRawUTF8(), sf, store() );
}

bool ParhelionProcessor::loadSet( const juce::File &file )
{
    if ( !file.existsAsFile() ) return false;
    return loadSetText( file.getFileName(), file.loadFileAsString().toStdString() );
}

bool ParhelionProcessor::loadFactorySet( int index )
{
    if ( index < 0 || index >= numFactorySets() ) return false;
    const FactorySet &f = factorySet( index );
    return loadSetText( f.m_name, f.m_text );
}

bool ParhelionProcessor::loadSetText( const juce::String &name, const std::string &text )
{
    SetFile     sf;
    std::string err;
    beginStep( "load " + name );
    if ( !parh::loadSetText( text, sf, store(), &err ) )
    {
        endStep();
        return false;
    }
    {
        std::lock_guard<std::mutex> g( m_lock );
        m_seed = sf.m_seed;
        m_curation = sf.m_curation;
    }
    if ( sf.m_minutes > 0.0 )
        store().set( store().id( ModuleE::COMPOSE, 0, compose::MINUTES ), static_cast<float>( sf.m_minutes ) );
    store().set( store().id( ModuleE::SET, 0, set::MINUTES ), static_cast<float>( sf.m_set ) );
    for ( int id = 0; id < store().count(); ++id )
        if ( StoreParameter *p = parameter( id ) ) p->sendValueChangedMessageToListeners( p->getValue() );
    endStep();
    compose();
    return true;
}

void ParhelionProcessor::exportTo( const juce::File &wav, int extras )
{
    if ( m_exporting.exchange( true ) ) return;
    if ( m_exporter && m_exporter->joinable() ) m_exporter->join();
    Playing  p;
    bool     levelled = true;
    uint64_t seed = 0;
    {
        std::lock_guard<std::mutex> g( m_lock );
        p = m_current;
        levelled = m_levelled;
        seed = m_seed;
    }
    auto params = std::make_shared<ParamStore>();
    params->copyValuesFrom( store() );
    m_exporter = std::make_unique<std::thread>(
        [this, p, params, wav, extras, levelled, seed]() mutable
        {
            if ( !levelled )
            {   // exported before its measuring was done: measured here
                if ( p.m_isSet )
                    levelSet( p.m_set, *params );
                else
                    levelScore( p.m_set.m_decks[0], *params );
            }
            constexpr double Rate = 48000.0;
            Engine           e;
            e.params().copyValuesFrom( *params );
            e.prepare( Rate, 512 );
            if ( p.m_isSet )
                e.loadSet( p.m_set );
            else
                e.load( p.m_set.m_decks[0] );
            // The cues (PLAN 9): in the WAV and as JSON beside it; the tags.
            std::vector<CueAt> cues;
            std::string        title;
            const TempoMap    &tm = p.m_set.m_decks[0].m_tempo;
            if ( p.m_isSet )
            {
                cues = setCues( p.m_setInfo, tm );
                title = std::string( "Parhelion set " ) +
                        DramaturgyNames[static_cast<int>( p.m_setInfo.m_dramaturgy )] + " seed " +
                        std::to_string( seed );
            }
            else if ( !p.m_tracks.empty() )
            {
                const TrackInfo &info = p.m_tracks[0].m_info;
                trackCues( info, 0.0, tm, std::string(), cues );
                title = "Parhelion " + info.m_style + " " + FormTemplateNames[static_cast<int>( info.m_form )] + " " +
                        KeyNames[info.m_key] + " " + info.m_camelot + " seed " + std::to_string( seed );
            }
            WavWriter w;
            bool      ok = w.open( wav.getFullPathName().toRawUTF8(), static_cast<int>( Rate ), 2, WavFormatE::PCM24 );
            for ( const CueAt &c : cues )
                w.addCue( static_cast<uint64_t>( std::llround( c.m_seconds * Rate ) ), c.m_label );
            w.setInfo( "INAM", title );
            w.setInfo( "ISFT", std::string( "Parhelion " ) + PARH_VERSION );
            w.setInfo( "IGNR", "Trance" );
            ok = ok && writeCuesJson( wav.getFullPathName().toStdString() + ".cues.json", cues, Rate );
            // Phase 16: the WAV as a rekordbox collection (its grid and its cues) beside it.
            {
                const int key = p.m_isSet ? ( p.m_setInfo.m_tracks.empty() ? -1 : p.m_setInfo.m_tracks[0].m_info.m_key )
                                          : ( p.m_tracks.empty() ? -1 : p.m_tracks[0].m_info.m_key );
                const std::string wavPath = wav.getFullPathName().toStdString();
                ok = ok && writeRekordboxXml( wavPath + ".rekordbox.xml", wavPath, title,
                                              key >= 0 ? std::string( KeyNames[key] ) + "m" : std::string(), cues, tm,
                                              p.m_set.m_lengthBeats, Rate );
            }
            // The stems: a WAV per element, their sum the mix before the master (Engine.h).
            const bool                      stems = ( extras & STEMS ) != 0;
            std::vector<std::vector<float>> stemBuf( stems ? 2 * Engine::STEMS : 0, std::vector<float>( 512 ) );
            std::vector<float *>            stemL( Engine::STEMS ), stemR( Engine::STEMS );
            std::vector<WavWriter>          stemWav( stems ? Engine::STEMS : 0 );
            if ( stems && ok )
            {
                const juce::File dir =
                    wav.getParentDirectory().getChildFile( wav.getFileNameWithoutExtension() + "_stems" );
                dir.createDirectory();
                for ( int s = 0; s < Engine::STEMS; ++s )
                {
                    stemL[static_cast<size_t>( s )] = stemBuf[static_cast<size_t>( 2 * s )].data();
                    stemR[static_cast<size_t>( s )] = stemBuf[static_cast<size_t>( 2 * s + 1 )].data();
                    const juce::String name =
                        juce::String( s + 1 ).paddedLeft( '0', 2 ) + "_" + Engine::stemName( s ) + ".wav";
                    ok = ok &&
                         stemWav[static_cast<size_t>( s )].open( dir.getChildFile( name ).getFullPathName().toRawUTF8(),
                                                                 static_cast<int>( Rate ), 2, WavFormatE::FLOAT32 );
                }
                e.setStems( stemL.data(), stemR.data() );
            }
            const int64_t      total = static_cast<int64_t>( ( e.lengthSeconds() + 2.0 ) * Rate );
            std::vector<float> L( 512 ), R( 512 );
            for ( int64_t done = 0; ok && done < total; done += 512 )
            {
                const int n = static_cast<int>( std::min<int64_t>( 512, total - done ) );
                e.process( L.data(), R.data(), n );
                w.write( L.data(), R.data(), n );
                for ( size_t s = 0; s < stemWav.size(); ++s ) stemWav[s].write( stemL[s], stemR[s], n );
            }
            w.close();
            for ( WavWriter &sw : stemWav ) sw.close();
            const juce::File mid = wav.withFileExtension( ".mid" );
            ok = ok && writeMidiFile( p.m_isSet ? flattenSet( p.m_set ) : p.m_set.m_decks[0],
                                      mid.getFullPathName().toRawUTF8(), "Parhelion", params.get() );
            // The DJ loops of a track.
            if ( ok && ( extras & LOOPS ) != 0 && !p.m_isSet && !p.m_tracks.empty() )
            {
                const juce::File dir =
                    wav.getParentDirectory().getChildFile( wav.getFileNameWithoutExtension() + "_loops" );
                ok = renderLoops( *params, p.m_set.m_decks[0], p.m_tracks[0].m_info,
                                  dir.getFullPathName().toStdString(), Rate );
            }
            {
                std::lock_guard<std::mutex> g( m_lock );
                m_lastExport =
                    ok ? "exported " + wav.getFileName() + ", its MIDI and cues" +
                             ( stems ? juce::String( ", stems" ) : juce::String() ) +
                             ( ( extras & LOOPS ) != 0 && !p.m_isSet ? juce::String( ", loops" ) : juce::String() )
                       : "could not write " + wav.getFileName();
            }
            m_exporting = false;
        } );
}

void ParhelionProcessor::writeRecording()
{
    const char *path = std::getenv( "PARH_RECORD" );
    if ( path == nullptr || m_record.empty() ) return;
    WavWriter w;
    if ( !w.open( path, static_cast<int>( m_sampleRate ), 2, WavFormatE::FLOAT32 ) ) return;
    std::vector<float> L( m_record.size() / 2 ), R( m_record.size() / 2 );
    for ( size_t i = 0; i < L.size(); ++i )
    {
        L[i] = m_record[2 * i];
        R[i] = m_record[2 * i + 1];
    }
    w.write( L.data(), R.data(), static_cast<int>( L.size() ) );
    w.close();
    m_recordTarget = 0;
}

juce::File ParhelionProcessor::ratingsFile()
{
    return juce::File::getSpecialLocation( juce::File::userDocumentsDirectory )
        .getChildFile( "Parhelion" )
        .getChildFile( "ratings.tsv" );
}

juce::String ParhelionProcessor::rate( int value )
{
    const int    t = trackAt( positionBeats() );
    Rating       r;
    juce::String what;
    {
        std::lock_guard<std::mutex> g( m_lock );
        if ( t < 0 || t >= static_cast<int>( m_current.m_tracks.size() ) ) return {};
        const TrackInfo &info = m_current.m_tracks[static_cast<size_t>( t )].m_info;
        r.m_value = value > 0 ? 1 : -1;
        r.m_form = static_cast<int>( info.m_form );
        r.m_style = info.m_style;
        r.m_seed = m_current.m_isSet ? m_current.m_setInfo.m_tracks[static_cast<size_t>( t )].m_seed : m_seed;
        // The groups of the presets it plays (its SoundPicks where it begins, on its deck).
        const TrackPlace &place = m_current.m_tracks[static_cast<size_t>( t )];
        for ( const SoundPick &s : m_current.m_set.m_decks[place.m_deck].m_sounds )
        {
            if ( s.m_beat != place.m_start ) continue;
            const std::vector<SoundPreset> &ps = factoryPresets( static_cast<ModuleE>( s.m_module ), s.m_instance );
            if ( s.m_preset < 0 || static_cast<size_t>( s.m_preset ) >= ps.size() ) continue;
            const std::string &grp = ps[static_cast<size_t>( s.m_preset )].m_group;
            if ( std::find( r.m_groups.begin(), r.m_groups.end(), grp ) == r.m_groups.end() )
                r.m_groups.push_back( grp );
        }
        m_ratings.push_back( r );
        what << ( r.m_value > 0 ? "liked: " : "not liked: " )
             << ( m_current.m_isSet ? "T" + juce::String( t + 1 ) + " " : juce::String() )
             << juce::String( info.m_style ) << " " << FormTemplateNames[static_cast<int>( info.m_form )] << " ("
             << static_cast<int>( m_ratings.size() ) << " ratings)";
    }
    ratingsFile().getParentDirectory().createDirectory();
    appendRating( ratingsFile().getFullPathName().toStdString(), r );
    return what;
}

juce::String ParhelionProcessor::status() const
{
    if ( m_composing ) return "composing ...";
    if ( m_exporting ) return "exporting ...";
    std::lock_guard<std::mutex> g( m_lock );
    juce::String                t;
    const double                secs = m_current.m_set.m_decks[0].m_tempo.secondsAt( m_current.m_set.m_lengthBeats );
    t << "seed " << juce::String( static_cast<juce::int64>( m_seed ) ) << "   ";
    if ( m_current.m_isSet )
        t << "DJ mix of " << static_cast<int>( m_current.m_tracks.size() ) << " tracks, "
          << DramaturgyNames[static_cast<int>( m_current.m_setInfo.m_dramaturgy )];
    else if ( !m_current.m_tracks.empty() )
    {
        const TrackInfo &i = m_current.m_tracks[0].m_info;
        t << "single track, " << juce::String( i.m_style ) << " " << FormTemplateNames[static_cast<int>( i.m_form )]
          << ", " << juce::String( i.m_bpm, 1 ) << " BPM, " << KeyNames[i.m_key] << " " << ScaleNames[i.m_scale] << " ("
          << juce::String( i.m_camelot ) << ")";
    }
    t << ", " << juce::String( secs / 60.0, 1 ) << " min";
    if ( !m_levelled && !m_current.m_tracks.empty() ) t << ", levelling";
    if ( m_lastExport.isNotEmpty() ) t << "   " << m_lastExport;
    return t;
}

int ParhelionProcessor::controllerFor( int id ) const
{
    for ( int c = 0; c < 128; ++c )
        if ( m_ccMap[static_cast<size_t>( c )].load() == id ) return c;
    return -1;
}

void ParhelionProcessor::forget( int id )
{
    for ( auto &c : m_ccMap )
        if ( c.load() == id ) c = -1;
}

void ParhelionProcessor::setFromMidi( int id, float value )
{
    StoreParameter *p = parameter( id );
    if ( p == nullptr ) return;
    const float norm = store().toNormalised( id, value );
    if ( std::fabs( p->getValue() - norm ) < 1.0e-6f ) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost( norm );
    p->endChangeGesture();
}

void ParhelionProcessor::perform( const juce::MidiBuffer &midi )
{
    const ParamStore &s = store();
    // A keyboard that plays (perform.keyboard_part, 01.10.2026): its keys go to the engine, on their samples; else the
    // keys from middle C toggle the mutes.
    const bool keys = s.getInt( s.id( ModuleE::PERFORM, 0, perform::KEYBOARD_PART ) ) != perform::keys::OFF;
    for ( const auto meta : midi )
    {
        const juce::MidiMessage m = meta.getMessage();
        if ( m.isAllNotesOff() || m.isAllSoundOff() )
        {
            m_engine.liveAllOff();
            m_keyMemory.clear();
        }
        else if ( keys && ( m.isNoteOn() || m.isNoteOff() ) )
        {
            // The keyboard's options (02.10.2026): the keys under the split play the lower voice, Scale Lock moves a key
            // to the nearest note of the track's key and scale (not on the kit, whose keys are its instruments), the
            // velocity goes through its curve. Where a press went, its release goes too.
            const int ch = m.getChannel() - 1, key = m.getNoteNumber();
            if ( m.isNoteOn() )
            {
                const int lower = s.getInt( s.id( ModuleE::PERFORM, 0, perform::KEYBOARD_LOWER ) );
                const int split = 36 + 12 * s.getInt( s.id( ModuleE::PERFORM, 0, perform::KEYBOARD_SPLIT ) );
                const int target = lower != perform::keys::OFF && key < split ? lower : -1;
                int       pitch = key;
                if ( s.getBool( s.id( ModuleE::PERFORM, 0, perform::KEYBOARD_SCALE ) ) &&
                     ( target >= 0 ? target : m_engine.keyTargetFor( ch ) ) != perform::keys::KIT )
                {
                    const Score &sc = m_engine.deck( std::max( 0, m_engine.leadDeck() ) ).score();
                    pitch = frame::snapToScale( key, sc.m_keyRoot,
                                                frame::scaleMask( ScaleNames[std::clamp( sc.m_scale, 0, 5 )] ) );
                }
                const int velocity = frame::shapeVelocity(
                    m.getVelocity(), s.getInt( s.id( ModuleE::PERFORM, 0, perform::KEYBOARD_VELOCITY ) ) );
                m_keyMemory.press( ch, key, target, pitch );
                m_engine.queueLive( meta.samplePosition, pitch, velocity, ch, true, target );
            }
            else
            {
                int target = -1, pitch = key;
                m_keyMemory.release( ch, key, target, pitch );
                m_engine.queueLive( meta.samplePosition, pitch, 0, ch, false, target );
            }
        }
        else if ( m.isNoteOn() )
        {
            // The keys from middle C: C kick, C# bass, D hats, D# perc, E lead, F synths, F# pads -- each press toggles.
            const int k = m.getNoteNumber() - 60;
            if ( k >= 0 && k < perform::Mutes )
            {
                const int id = s.id( ModuleE::PERFORM, 0, perform::MUTE_KICK + k );
                setFromMidi( id, s.getBool( id ) ? 0.0f : 1.0f );
            }
        }
        else if ( m.isChannelPressure() )
        {
            // The pressure: a source of every voice's matrix (perform.pressure).
            setFromMidi( s.id( ModuleE::PERFORM, 0, perform::PRESSURE ),
                         static_cast<float>( m.getChannelPressureValue() ) / 127.0f );
        }
        else if ( m.isController() )
        {
            const int cc = m.getControllerNumber();
            if ( cc < 0 || cc > 127 ) continue;
            const int learning = m_learn.exchange( -1 );
            if ( learning >= 0 )
            {
                for ( auto &c : m_ccMap )
                    if ( c.load() == learning ) c = -1;   // one controller per control
                m_ccMap[static_cast<size_t>( cc )] = learning;
            }
            const int id = m_ccMap[static_cast<size_t>( cc )].load();
            if ( id < 0 ) continue;
            const ParamDesc &d = s.desc( id );
            const float      u = static_cast<float>( m.getControllerValue() ) / 127.0f;
            float            v = d.m_minValue + u * ( d.m_maxValue - d.m_minValue );
            // A bipolar control has its middle on the controller's middle (64), exactly; a switch turns at the middle.
            if ( d.m_minValue < 0.0f && d.m_maxValue > 0.0f && m.getControllerValue() == 64 ) v = 0.0f;
            if ( d.m_curve == CurveE::TOGGLE ) v = m.getControllerValue() >= 64 ? 1.0f : 0.0f;
            setFromMidi( id, v );
        }
    }
}

void ParhelionProcessor::getStateInformation( juce::MemoryBlock &destData )
{
    juce::XmlElement xml( "Parhelion" );
    {
        std::lock_guard<std::mutex> g( m_lock );
        xml.setAttribute( "seed", juce::String( static_cast<juce::int64>( m_seed ) ) );
        juce::String rerolls;
        for ( const auto &r : m_curation.m_rerolls ) rerolls << r.first << "=" << r.second << ";";
        xml.setAttribute( "rerolls", rerolls );
    }
    xml.setAttribute( "params", juce::String( store().toText( true ) ) );
    juce::String cc;
    for ( int c = 0; c < 128; ++c )
        if ( const int id = m_ccMap[static_cast<size_t>( c )].load(); id >= 0 )
            cc << c << "=" << juce::String( store().key( id ) ) << ";";
    xml.setAttribute( "controllers", cc );
    xml.setAttribute( "mixMinutes", static_cast<double>( m_mixMinutes ) );
    copyXmlToBinary( xml, destData );
}

void ParhelionProcessor::setStateInformation( const void *data, int sizeInBytes )
{
    const auto xml = getXmlFromBinary( data, sizeInBytes );
    if ( xml == nullptr || !xml->hasTagName( "Parhelion" ) ) return;
    store().resetDefaults();
    store().parseText( xml->getStringAttribute( "params" ).toStdString() );
    if ( xml->hasAttribute( "mixMinutes" ) )
        m_mixMinutes = static_cast<float>( xml->getDoubleAttribute( "mixMinutes", 60.0 ) );
    if ( xml->hasAttribute( "controllers" ) )
    {
        for ( auto &c : m_ccMap ) c = -1;
        for ( const auto &item : juce::StringArray::fromTokens( xml->getStringAttribute( "controllers" ), ";", "" ) )
        {
            const int c = item.upToFirstOccurrenceOf( "=", false, false ).getIntValue();
            const int id = store().find( item.fromFirstOccurrenceOf( "=", false, false ).toStdString() );
            if ( c >= 0 && c < 128 && id >= 0 ) m_ccMap[static_cast<size_t>( c )] = id;
        }
    }
    {
        std::lock_guard<std::mutex> g( m_lock );
        m_seed = static_cast<uint64_t>( xml->getStringAttribute( "seed" ).getLargeIntValue() );
        m_curation = Curation{};
        for ( const auto &item : juce::StringArray::fromTokens( xml->getStringAttribute( "rerolls" ), ";", "" ) )
            if ( item.contains( "=" ) )
                m_curation.m_rerolls[item.upToFirstOccurrenceOf( "=", false, false ).toStdString()] =
                    item.fromFirstOccurrenceOf( "=", false, false ).getIntValue();
    }
    m_history.clear();   // a host's state is a new beginning
    compose();
}

// ---------------------------------------------------------------------------------------------------------------- undo

std::vector<float> ParhelionProcessor::values() const
{
    const ParamStore  &s = m_engine.params();
    std::vector<float> v( static_cast<size_t>( s.count() ) );
    for ( int id = 0; id < s.count(); ++id ) v[static_cast<size_t>( id )] = s.get( id );
    return v;
}

juce::String ParhelionProcessor::extraState() const
{
    std::lock_guard<std::mutex> g( m_lock );
    juce::String                t;
    t << "seed=" << juce::String( static_cast<juce::int64>( m_seed ) ) << "\nmix=" << juce::String( m_mixMinutes )
      << "\nrerolls=";
    for ( const auto &r : m_curation.m_rerolls ) t << r.first << ":" << r.second << ";";
    return t;
}

void ParhelionProcessor::applyExtra( const juce::String &text )
{
    if ( text == extraState() ) return;
    bool again = false;
    {
        std::lock_guard<std::mutex> g( m_lock );
        for ( const juce::String &line : juce::StringArray::fromLines( text ) )
        {
            const juce::String k = line.upToFirstOccurrenceOf( "=", false, false ),
                               v = line.fromFirstOccurrenceOf( "=", false, false );
            if ( k == "seed" )
            {
                const uint64_t seed = static_cast<uint64_t>( v.getLargeIntValue() );
                again = again || seed != m_seed;
                m_seed = seed;
            }
            else if ( k == "mix" )
            {
                m_mixMinutes = v.getFloatValue();
            }
            else if ( k == "rerolls" )
            {
                Curation c;
                for ( const juce::String &item : juce::StringArray::fromTokens( v, ";", "" ) )
                    if ( item.contains( ":" ) )
                        c.m_rerolls[item.upToLastOccurrenceOf( ":", false, false ).toStdString()] =
                            item.fromLastOccurrenceOf( ":", false, false ).getIntValue();
                again = again || c.m_rerolls != m_curation.m_rerolls;
                m_curation = c;
            }
        }
    }
    if ( again ) compose();
}

void ParhelionProcessor::beginStep( const juce::String &what )
{
    m_history.begin( what, values(), extraState(), true );
}

void ParhelionProcessor::endStep()
{
    m_history.end( values(), extraState() );
}

void ParhelionProcessor::parameterGestureChanged( int parameterIndex, bool gestureIsStarting )
{
    // Only the panel's gestures, which come on the message thread: a controller's (perform(), the audio thread) and an
    // undo's own are not steps.
    if ( m_restoring || !juce::MessageManager::existsAndIsCurrentThread() ) return;
    if ( gestureIsStarting )
    {
        const ParamStore &s = store();
        m_history.begin( parameterIndex >= 0 && parameterIndex < s.count()
                             ? juce::String( s.desc( parameterIndex ).m_name )
                             : juce::String( "knob" ),
                         values(), extraState(), false );
        m_history.touch( parameterIndex );
    }
    else
    {
        m_history.end( values(), extraState() );
    }
}

void ParhelionProcessor::applyStep( const frame::UndoStep &step, bool after )
{
    const juce::ScopedValueSetter<bool> quiet( m_restoring, true );
    for ( const auto &[id, before, now] : step.m_values ) setFromUi( id, after ? now : before );
    applyExtra( after ? step.m_extraAfter : step.m_extraBefore );
}

bool ParhelionProcessor::undo()
{
    if ( const frame::UndoStep *s = m_history.undo() )
    {
        applyStep( *s, false );
        return true;
    }
    return false;
}

bool ParhelionProcessor::redo()
{
    if ( const frame::UndoStep *s = m_history.redo() )
    {
        applyStep( *s, true );
        return true;
    }
    return false;
}

void ParhelionProcessor::resetToDefault( int id )
{
    if ( id < 0 || id >= store().count() ) return;
    beginStep( juce::String( store().desc( id ).m_name ) + " to its default" );
    setFromUi( id, store().desc( id ).m_defValue );
    endStep();
}

float ParhelionProcessor::playedNormalised( int id ) const
{
    const ParamStore &s = m_engine.params();
    if ( id < 0 || id >= s.count() ) return std::numeric_limits<float>::quiet_NaN();
    const int   lead = std::clamp( m_engine.leadDeck(), 0, Decks - 1 );
    const float v = m_engine.deck( lead ).played( id ), k = s.get( id );
    if ( std::fabs( s.toNormalised( id, v ) - s.toNormalised( id, k ) ) < 1.0e-4f )
        return std::numeric_limits<float>::quiet_NaN();
    return s.toNormalised( id, v );
}

// ------------------------------------------------------------------------------------------------------------- headset

void ParhelionProcessor::pollHeadset()
{
    frame::Settings &st = frame::Settings::of( "Parhelion" );
    m_headset.listen( st.headset() == frame::Settings::HeadsetModeE::OFF ? 0 : st.headsetPort() );
    const frame::HeadsetEvents e = m_headset.poll();
    const ParamStore          &s = store();
    {
        // The hands' moves are performing, not editing: no steps of their own.
        const juce::ScopedValueSetter<bool> quiet( m_restoring, true );
        if ( e.m_playStop && wrapperType == wrapperType_Standalone ) setPlaying( !isPlaying() );
        if ( e.m_action )
        {
            const int id = s.id( ModuleE::PERFORM, 0, perform::MUTE_KICK );
            setFromUi( id, s.getBool( id ) ? 0.0f : 1.0f );
        }
        if ( e.m_filterMoved ) setFromUi( s.id( ModuleE::PERFORM, 0, perform::FILTER ), e.m_filter );
        if ( e.m_throwMoved ) setFromUi( s.id( ModuleE::PERFORM, 0, perform::THROW ), e.m_throwAmount );
    }
    if ( e.m_hold )
    {
        // The performer's "now", as the Quest app has it: a drop in a breakdown, a break or a build, else a breakdown.
        SectionKindE kind = SectionKindE::BREAKDOWN;
        const double beat = positionBeats();
        const int    t = trackAt( beat );
        {
            std::lock_guard<std::mutex> g( m_lock );
            if ( t >= 0 && t < static_cast<int>( m_current.m_tracks.size() ) )
            {
                const Score &deck =
                    m_current.m_set.m_decks[static_cast<size_t>( m_current.m_tracks[static_cast<size_t>( t )].m_deck )];
                const Section *sec = nullptr;
                for ( const Section &x : deck.m_sections )
                {
                    if ( x.m_beat > beat ) break;
                    sec = &x;
                }
                if ( sec != nullptr && ( sec->m_kind == SectionKindE::BREAKDOWN || sec->m_kind == SectionKindE::BREAK ||
                                         sec->m_kind == SectionKindE::BUILD ) )
                    kind = SectionKindE::DROP;
            }
        }
        performNow( kind );
    }
    if ( e.m_next ) newSeed();
}

juce::AudioProcessorEditor *ParhelionProcessor::createEditor()
{
    return new ParhelionEditor( *this );
}

/**
 * @brief The plugin's factory, called by the JUCE wrappers.
 * @return a new processor
 */
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter()
{
    return new ParhelionProcessor();
}
