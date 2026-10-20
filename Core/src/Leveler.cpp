/**
 * @file Leveler.cpp
 * @brief Every track as loud as its style means (Leveler.h).
 * @note After Totality `Core/src/Leveler.cpp` at 4d3c0d2 (29.09.2026): the readings, the ranked corrections and the
 *       two-pass loudness are its; the windows and the breakdown's correction are Parhelion's.
 * @author Rene Weller
 */
#include "parh/Leveler.h"
#include "parh/Dsp.h"
#include "parh/Engine.h"
#include "parh/Loudness.h"
#include "parh/fx/Resonance.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>

namespace parh
{

namespace
{
constexpr double Rate = 48000.0;   ///< the rate the parts are rendered at to be measured, Hz
constexpr int    Block = 512;      ///< the block they are rendered in
constexpr double Warm = 4.0;       ///< seconds before a measured part: the rooms fill, the notes sounding on are found
constexpr float  MostDb = 6.0f;    ///< the largest loudness correction either way (Deep's soft drops need more than 4)
constexpr double BalSeconds = 12.0;   ///< how much the balance reads in each of its places
constexpr double BalWarm = 2.0;       ///< and before it
constexpr float  BalDown = -8.0f;     ///< the largest correction of a part down, dB
constexpr float  BalUp = 10.0f;       ///< the largest correction of a part up, dB
constexpr float  BreakMost = 8.0f;    ///< the breakdown's correction, either way
constexpr float  ToneMost = 3.0f;     ///< 20.10.2026: the largest tone trim either way, dB
constexpr int    ToneFft = 8192;      ///< the tone reading's Welch segment (Tools/analyze_ref.py's)

/** @brief 20.10.2026: the tone bands' edges in Hz -- the whole band (20-16000) first, then 20-60, 60-150, 150-400,
 *         400-2000, 2000-5000, 5000-16000 (Tools/analyze_ref.py's shares of the drop's power). */
constexpr double ToneEdges[ToneBands + 1][2] = { { 20, 16000 },  { 20, 60 },     { 60, 150 },  { 150, 400 },
                                                 { 400, 2000 }, { 2000, 5000 }, { 5000, 16000 } };

/**
 * @brief 20.10.2026: plays @p seconds from @p beat (after BalWarm seconds before it) and adds the mid signal's power
 *        spectrum to @p power as the references were read (Tools/analyze_ref.py: Welch, Hann, half overlap; 8192 points
 *        there, here their equal in Hz at 48 kHz); @p kick receives the whole band's energy of this stretch alone.
 * @param[in,out] e        the engine that plays the score
 * @param[in]     s        the score
 * @param[in]     beat     where the stretch begins
 * @param[in]     seconds  how long it lasts
 * @param[out]    power    receives this stretch's spectrum (ToneFft / 2 + 1 bins)
 * @param[out]    kick     receives its energy in 20-16000 Hz
 * @param[in]     stop     asked now and then; true stops the reading
 * @return false when stopped
 */
bool readSpectrum( Engine &e, const Score &s, double beat, double seconds, std::vector<double> &power, double &kick,
                   const std::function<bool()> &stop )
{
    const double at = s.m_tempo.secondsAt( beat );
    e.seek( s.m_tempo.beatAt( std::max( 0.0, at - BalWarm ) ) );
    std::vector<float> l( Block ), r( Block ), mid;
    mid.reserve( static_cast<size_t>( seconds * Rate ) + Block );
    const double warm = std::min( BalWarm, at );
    for ( int done = 0; done < static_cast<int>( warm * Rate ); done += Block )
    {
        if ( stop && stop() ) return false;
        e.process( l.data(), r.data(), Block );
    }
    for ( int done = 0; done < static_cast<int>( seconds * Rate ); done += Block )
    {
        if ( stop && stop() ) return false;
        e.process( l.data(), r.data(), Block );
        for ( size_t i = 0; i < static_cast<size_t>( Block ); ++i ) mid.push_back( 0.5f * ( l[i] + r[i] ) );
    }
    std::vector<float> re( ToneFft ), im( ToneFft );
    power.assign( ToneFft / 2 + 1, 0.0 );
    for ( size_t a = 0; a + ToneFft <= mid.size(); a += ToneFft / 2 )
    {
        for ( int j = 0; j < ToneFft; ++j )
        {
            const double w = 0.5 - 0.5 * std::cos( 2.0 * 3.14159265358979 * j / ToneFft );
            re[static_cast<size_t>( j )] = static_cast<float>( mid[a + static_cast<size_t>( j )] * w );
            im[static_cast<size_t>( j )] = 0.0f;
        }
        ResonanceSuppressor::transform( re, im );
        for ( size_t k = 0; k < power.size(); ++k )
            power[k] += static_cast<double>( re[k] ) * re[k] + static_cast<double>( im[k] ) * im[k];
    }
    kick = 0.0;
    for ( size_t k = 0; k < power.size(); ++k )
    {
        const double hz = static_cast<double>( k ) * Rate / ToneFft;
        if ( hz >= ToneEdges[0][0] && hz < ToneEdges[0][1] ) kick += power[k];
    }
    return true;
}

/**
 * @brief 20.10.2026: the tone bands of @p power through the track bus's shelves and bells at @p trims, dB of the whole
 *        band's power (the references' shares).
 * @param[in]  power  the spectrum
 * @param[in]  eq     the shelves and bells
 * @param[in]  trims  their gains
 * @param[out] out    receives the bands, dB of 20-16000 Hz
 */
void toneLevels( const std::vector<double> &power, const ToneEq &eq, const ToneDbT &trims, ToneDbT &out )
{
    double e[ToneBands + 1] = {};
    for ( size_t k = 1; k < power.size(); ++k )
    {
        const double hz = static_cast<double>( k ) * Rate / ToneFft;
        if ( hz >= 16000.0 ) break;
        const double p = power[k] * eq.powerGain( trims.data(), hz );
        for ( int b = 0; b <= ToneBands; ++b )
            if ( hz >= ToneEdges[b][0] && hz < ToneEdges[b][1] ) e[b] += p;
    }
    for ( int b = 0; b < ToneBands; ++b )
        out[static_cast<size_t>( b )] = static_cast<float>( 10.0 * std::log10( std::max( e[b + 1], 1e-30 ) /
                                                                                std::max( e[0], 1e-30 ) ) );
}

/**
 * @brief 20.10.2026: the smallest trims that bring the tone bands of @p power into [@p lo, @p hi] through the track bus's
 *        shelves and bells -- least squares by Gauss-Newton on the spectrum itself: a band costs only by how far it
 *        lies outside its range, a trim by its size (Tikhonov), each trim within ToneMost. The bands are shares of the
 *        whole, and the shelves and bells overlap; solved together, a band's neighbours do not undo its trim, and a band
 *        inside its range may move within it rather than be held by a counter-move (Totality, 20.10.2026).
 * @param[in] power  the loudest part's spectrum
 * @param[in] lo     the bands' lower edges, dB of 20-16000 Hz
 * @param[in] hi     their upper edges
 * @return the trims, dB
 */
ToneDbT fitTone( const std::vector<double> &power, const ToneDbT &lo, const ToneDbT &hi )
{
    ToneEq eq;
    eq.prepare( Rate, DeckToneBands, ToneBands );
    constexpr int N = ToneBands;
    ToneDbT       t{};
    for ( int iter = 0; iter < 8; ++iter )
    {
        ToneDbT now;
        toneLevels( power, eq, t, now );
        double J[N][N], r[N];
        bool   out[N];
        for ( int i = 0; i < N; ++i )
        {   // the distance outside the range (0 inside, and then no pull either way)
            const size_t q = static_cast<size_t>( i );
            r[i] = now[q] > hi[q] ? now[q] - hi[q] : ( now[q] < lo[q] ? now[q] - lo[q] : 0.0 );
            out[i] = r[i] != 0.0;
        }
        for ( int j = 0; j < N; ++j )
        {
            ToneDbT step = t, moved;
            step[static_cast<size_t>( j )] += 0.5f;
            toneLevels( power, eq, step, moved );
            for ( int i = 0; i < N; ++i )
                J[i][j] = out[i] ? ( moved[static_cast<size_t>( i )] - now[static_cast<size_t>( i )] ) / 0.5 : 0.0;
        }
        // The step of (J^T J + lambda I) d = -(J^T r + lambda t): the bands' distances and the trims' sizes together,
        // by elimination with partial pivoting.
        constexpr double Lambda = 0.15;   // a dB of trim weighs as much as 0.4 dB outside a band's range
        double           M[N][N + 1];
        for ( int a = 0; a < N; ++a )
        {
            for ( int b = 0; b < N; ++b )
            {
                double v = a == b ? Lambda : 0.0;
                for ( int i = 0; i < N; ++i ) v += J[i][a] * J[i][b];
                M[a][b] = v;
            }
            double v = -Lambda * t[static_cast<size_t>( a )];
            for ( int i = 0; i < N; ++i ) v -= J[i][a] * r[i];
            M[a][N] = v;
        }
        for ( int c = 0; c < N; ++c )
        {
            int piv = c;
            for ( int q = c + 1; q < N; ++q )
                if ( std::fabs( M[q][c] ) > std::fabs( M[piv][c] ) ) piv = q;
            for ( int q = 0; q <= N; ++q ) std::swap( M[c][q], M[piv][q] );
            if ( std::fabs( M[c][c] ) < 1e-12 ) continue;
            for ( int q = c + 1; q < N; ++q )
            {
                const double f = M[q][c] / M[c][c];
                for ( int z = c; z <= N; ++z ) M[q][z] -= f * M[c][z];
            }
        }
        double d[N] = {};
        for ( int c = N - 1; c >= 0; --c )
        {
            double v = M[c][N];
            for ( int q = c + 1; q < N; ++q ) v -= M[c][q] * d[q];
            d[c] = std::fabs( M[c][c] ) < 1e-12 ? 0.0 : v / M[c][c];
        }
        for ( int j = 0; j < N; ++j )
            t[static_cast<size_t>( j )] =
                std::clamp( t[static_cast<size_t>( j )] + static_cast<float>( d[j] ), -ToneMost, ToneMost );
    }
    for ( float &v : t ) v = std::round( v * 10.0f ) / 10.0f;
    return t;
}

/** @brief The loudest samples of the kick and of every part. */
struct PartPeaks
{
    float m_kick = 0.0f;           ///< the kick's loudest sample
    float m_part[BalParts] = {};   ///< every part's loudest sample
};

/**
 * @brief Plays @p seconds from @p beat (after BalWarm before it) and keeps the loudest samples. False if stopped.
 * @param[in,out] e        the engine
 * @param[in]     s        the score
 * @param[in]     beat     the first beat
 * @param[in]     seconds  the seconds played
 * @param[out]    peaks    the loudest samples per part
 * @param[in]     stop     asked between blocks: true breaks off
 * @return false if stopped
 */
bool readParts( Engine &e, const Score &s, double beat, double seconds, PartPeaks &peaks,
                const std::function<bool()> &stop )
{
    const double at = s.m_tempo.secondsAt( beat );
    e.seek( s.m_tempo.beatAt( std::max( 0.0, at - BalWarm ) ) );
    std::vector<float> l( Block ), r( Block );
    const double       warm = std::min( BalWarm, at );
    for ( int done = 0; done < static_cast<int>( warm * Rate ); done += Block )
    {
        if ( stop && stop() ) return false;
        e.process( l.data(), r.data(), Block );
    }
    e.watchPeaks( true );
    for ( int done = 0; done < static_cast<int>( seconds * Rate ); done += Block )
    {
        if ( stop && stop() )
        {
            e.watchPeaks( false );
            return false;
        }
        e.process( l.data(), r.data(), Block );
    }
    e.watchPeaks( false );
    const Deck &d = e.deck( 0 );
    peaks.m_kick = std::max( peaks.m_kick, d.kickPeak() );
    for ( int p = 0; p < BalParts; ++p ) peaks.m_part[p] = std::max( peaks.m_part[p], d.partPeak( p ) );
    return true;
}

/**
 * @brief Every part's loudest sample against the loudest kick, dB (NaN: more than 60 dB under it).
 * @param[in] peaks  the parts' loudest samples
 * @return each part against the loudest kick in dB, NaN more than 60 dB under it
 */
BalanceDbT against( const PartPeaks &peaks )
{
    BalanceDbT out;
    for ( int p = 0; p < BalParts; ++p )
    {
        const float v = peaks.m_part[p];
        out[static_cast<size_t>( p )] = peaks.m_kick > 1e-4f && v > peaks.m_kick * 1e-3f
                                            ? 20.0f * std::log10( v / peaks.m_kick )
                                            : std::numeric_limits<float>::quiet_NaN();
    }
    return out;
}

/**
 * @brief The corrections that bring @p found into the windows. A rank within the kit's two kinds (the hat-like lanes, the
 *        percussion): the two loudest keep their windows, the third's lies 3 dB lower, the others' 6 (Totality's rule:
 *        ten parts pushed up at once flood the mids). @p windowDb moves the voices' windows (LevelMark::windowDb).
 * @param[in] e         the engine
 * @param[in] found     the levels found, dB
 * @param[in] windowDb  the windows' width, dB
 * @return the corrections, dB
 */
BalanceDbT corrections( const Engine &e, const BalanceDbT &found, float windowDb )
{
    const ParamStore &ps = e.params();
    int               role[BalLanes] = {};
    for ( int l = 0; l < BalLanes; ++l )
        role[l] = static_cast<int>( std::lround( e.deck( 0 ).played( ps.id( ModuleE::PERC, l, perc::ROLE ) ) ) );
    auto hatLike = []( int r )
    {
        const PercRoleE pr = static_cast<PercRoleE>( r );
        return pr == PercRoleE::CLOSED_HAT || pr == PercRoleE::ROLLING_HAT || pr == PercRoleE::OPEN_HAT ||
               pr == PercRoleE::RIDE || pr == PercRoleE::SHAKER || pr == PercRoleE::TAMBOURINE ||
               pr == PercRoleE::CRASH;
    };
    float lower[BalParts] = {};
    for ( int kind = 0; kind < 2; ++kind )
    {
        std::vector<std::pair<float, int>> heard;
        for ( int p = 0; p < BalLanes; ++p )
            if ( ( hatLike( role[p] ) ? 0 : 1 ) == kind && !std::isnan( found[static_cast<size_t>( p )] ) )
                heard.emplace_back( found[static_cast<size_t>( p )], p );
        std::stable_sort( heard.begin(), heard.end(),
                          []( const auto &a, const auto &b ) { return a.first > b.first; } );
        for ( size_t k = 0; k < heard.size(); ++k ) lower[heard[k].second] = k < 2 ? 0.0f : k == 2 ? 3.0f : 6.0f;
    }
    BalanceDbT out{};
    for ( int p = 0; p < BalParts; ++p )
    {
        const float f = found[static_cast<size_t>( p )];
        if ( std::isnan( f ) || p == static_cast<int>( BalPartE::ROOM ) ) continue;
        float lo, hi;
        balanceWindow( p, p < BalLanes ? role[p] : -1, lo, hi );
        lo -= lower[p];
        hi -= lower[p];
        if ( p > static_cast<int>( BalPartE::BASS ) && p != static_cast<int>( BalPartE::FX ) )
        {
            lo += windowDb;
            hi += windowDb;
        }
        out[static_cast<size_t>( p )] = std::clamp( f < lo ? lo - f : f > hi ? hi - f : 0.0f, BalDown, BalUp );
    }
    return out;
}

/**
 * @brief The loudness of @p seconds from @p beat (after Warm before it): integrated and the short-term maximum.
 * @param[in,out] e            the engine
 * @param[in]     s            the score
 * @param[in]     beat         the first beat
 * @param[in]     seconds      the seconds measured
 * @param[in]     stop         asked between blocks: true breaks off
 * @param[in]     warmSeconds  the seconds played before
 * @return the loudness, nothing if stopped
 */
std::optional<LoudnessReport> measure( Engine &e, const Score &s, double beat, double seconds,
                                       const std::function<bool()> &stop, double warmSeconds = Warm )
{
    const double at = s.m_tempo.secondsAt( beat );
    e.seek( s.m_tempo.beatAt( std::max( 0.0, at - warmSeconds ) ) );
    std::vector<float> l( Block ), r( Block );
    const double       warm = std::min( warmSeconds, at );
    for ( int done = 0; done < static_cast<int>( warm * Rate ); done += Block )
    {
        if ( stop && stop() ) return std::nullopt;
        e.process( l.data(), r.data(), Block );
    }
    LoudnessMeter meter;
    meter.prepare( Rate );
    for ( int done = 0; done < static_cast<int>( seconds * Rate ); done += Block )
    {
        if ( stop && stop() ) return std::nullopt;
        e.process( l.data(), r.data(), Block );
        meter.process( l.data(), r.data(), Block );
    }
    return meter.report();
}

/**
 * @brief The drop as the references were measured (Tools/analyze_ref.py): the whole of it from @p beat for @p seconds
 *        (at most 150), every 20-second window hopping 5 s -- the loudest window's integrated loudness is the drop's
 *        (the references' loud20), the loudest 3 s anywhere in it its short-term maximum. A drop that grows (its second
 *        half an octave up, the choir coming in) is measured where it is loudest, not where it begins.
 * @param[in,out] e        the engine
 * @param[in]     s        the score
 * @param[in]     beat     the drop's first beat
 * @param[in]     seconds  its seconds
 * @param[in]     stop     asked between blocks: true breaks off
 * @return the drop's loudness, nothing if stopped
 */
std::optional<LoudnessReport> measureDrop( Engine &e, const Score &s, double beat, double seconds,
                                           const std::function<bool()> &stop )
{
    seconds = std::clamp( seconds, 20.0, 150.0 );
    const double at = s.m_tempo.secondsAt( beat );
    e.seek( s.m_tempo.beatAt( std::max( 0.0, at - Warm ) ) );
    std::vector<float> l( Block ), r( Block );
    const double       warm = std::min( Warm, at );
    for ( int done = 0; done < static_cast<int>( warm * Rate ); done += Block )
    {
        if ( stop && stop() ) return std::nullopt;
        e.process( l.data(), r.data(), Block );
    }
    const int                  windows = static_cast<int>( ( seconds - 20.0 ) / 5.0 ) + 1;
    std::vector<LoudnessMeter> meters( static_cast<size_t>( windows ) );
    for ( LoudnessMeter &m : meters ) m.prepare( Rate );
    LoudnessMeter whole;
    whole.prepare( Rate );
    const int total = static_cast<int>( seconds * Rate );
    for ( int done = 0; done < total; done += Block )
    {
        if ( stop && stop() ) return std::nullopt;
        e.process( l.data(), r.data(), Block );
        whole.process( l.data(), r.data(), Block );
        const double t = static_cast<double>( done ) / Rate;
        for ( int w = 0; w < windows; ++w )
            if ( t >= 5.0 * w && t < 5.0 * w + 20.0 )
                meters[static_cast<size_t>( w )].process( l.data(), r.data(), Block );
    }
    LoudnessReport rep = whole.report();
    double         loudest = -120.0;
    for ( LoudnessMeter &m : meters ) loudest = std::max( loudest, m.report().m_integrated );
    rep.m_integrated = loudest;
    return rep;
}

/**
 * @brief The length in seconds of the section that begins at or contains @p beat.
 * @param[in] s     the score
 * @param[in] beat  the position in beats
 * @return the section's length in seconds
 */
double sectionSeconds( const Score &s, double beat )
{
    for ( const Section &sec : s.m_sections )
        if ( beat >= sec.m_beat - 1e-9 && beat < sec.m_beat + sec.m_length )
            return s.m_tempo.secondsAt( sec.m_beat + sec.m_length ) - s.m_tempo.secondsAt( beat );
    return 20.0;
}

/**
 * @brief The main breakdown of the track that begins at @p from: the longest Breakdown section (else Break) after it.
 * @param[in] s     the score
 * @param[in] from  the track's first beat
 * @param[in] to    its end in beats
 * @return the main breakdown, or null
 */
const Section *mainBreakdown( const Score &s, double from, double to )
{
    const Section *best = nullptr;
    for ( const Section &sec : s.m_sections )
    {
        if ( sec.m_beat < from || sec.m_beat >= to ) continue;
        const bool bd = sec.m_kind == SectionKindE::BREAKDOWN, br = sec.m_kind == SectionKindE::BREAK;
        if ( !bd && !br ) continue;
        const bool bestBd = best != nullptr && best->m_kind == SectionKindE::BREAKDOWN;
        if ( best == nullptr || ( bd && !bestBd ) || ( bd == bestBd && sec.m_length > best->m_length ) ) best = &sec;
    }
    return best;
}

}   // namespace

std::vector<LevelReading> levelSet( SetScore &set, const ParamStore &params, double seconds,
                                    const std::function<bool()> &stop )
{
    std::vector<LevelReading> out;
    for ( int d = 0; d < 2; ++d )
    {
        if ( set.m_decks[d].m_levels.empty() ) continue;
        const std::vector<LevelReading> r = levelScore( set.m_decks[d], params, seconds, stop );
        if ( r.empty() && stop && stop() ) return {};
        out.insert( out.end(), r.begin(), r.end() );
    }
    // The teases: the corrections of their source.
    for ( LevelMark &m : set.m_decks[2].m_levels )
    {
        if ( !std::isnan( m.m_targetLufs ) ) continue;
        for ( int d = 0; d < 2; ++d )
            for ( const LevelMark &src : set.m_decks[d].m_levels )
                if ( src.m_peakBeat == m.m_peakBeat )
                {
                    m.m_trimDb = src.m_trimDb;
                    m.m_balDb = src.m_balDb;
                    m.m_toneDb = src.m_toneDb;
                    m.m_breakDb = src.m_breakDb;
                    m.m_buildDb = src.m_buildDb;
                }
    }
    return out;
}

void balanceWindow( int part, int role, float &lo, float &hi )
{
    // The loudest sample against the kick's, dB. The lanes after Totality's research fader levels (closed hat -8, open hat
    // -10, conga -15) -- a trance mix carries its hats like a techno one; the clap at the snare's; the crash and the ride
    // under the hats. The voices after Dok. 5 and 7: the lead up front, a few dB under the kick's peak (a supersaw's peak
    // is far above its level); the pluck and the arp behind it; the pad and the counter further back; the bass's harmonics
    // near the kick; the effects a swell under the lead. [I], to be heard.
    if ( part < BalLanes )
    {
        //                                 closed rolling open  ride  clap  ghost snare  rim shaker  tom conga noise crash tamb
        static const float LaneLo[NumPercRoles] = {
            -14, -16, -16, -18, -10, -18, -12, -16, -20, -16, -18, -20, -18, -20
        };
        static const float LaneHi[NumPercRoles] = { -8, -10, -10, -12, -5, -12, -6, -10, -14, -10, -12, -12, -10, -14 };
        const int          r = std::clamp( role, 0, NumPercRoles - 1 );
        lo = LaneLo[r];
        hi = LaneHi[r];
        return;
    }
    switch ( static_cast<BalPartE>( part ) )
    {
    case BalPartE::BASS:
        lo = -9.0f;
        hi = -4.0f;
        return;
    case BalPartE::ACID:
        lo = -8.0f;
        hi = -3.0f;
        return;
    case BalPartE::LEAD:
        lo = -7.0f;
        hi = -2.0f;
        return;
    case BalPartE::COUNTER:
        lo = -15.0f;
        hi = -9.0f;
        return;
    case BalPartE::PLUCK:
        lo = -11.0f;
        hi = -6.0f;
        return;
    case BalPartE::ARP:
        lo = -13.0f;
        hi = -8.0f;
        return;
    case BalPartE::PAD:
        lo = -13.0f;
        hi = -8.0f;
        return;
    case BalPartE::STAB:
        lo = -12.0f;
        hi = -7.0f;
        return;
    // The piano by its loudest sample reads its hammer: after the voicing against Pianoteq (Phase 8; the knock, the
    // harder felt) its attack stands far over its body, which at -9 .. -3 sat 20 dB under the mix in Dream House, whose
    // melody it carries (and the low mids, 150 .. 400 Hz, under the references' corridor).
    case BalPartE::PIANO:
        lo = -5.0f;
        hi = -1.0f;
        return;
    case BalPartE::STRINGS:
        lo = -16.0f;
        hi = -10.0f;
        return;   // (sustained: their energy is more than their peak says)
    case BalPartE::CHOIR:
        lo = -17.0f;
        hi = -11.0f;
        return;
    case BalPartE::BRASS:
        lo = -10.0f;
        hi = -4.0f;
        return;
    case BalPartE::TIMPANI:
        lo = -12.0f;
        hi = -5.0f;
        return;
    case BalPartE::FX:
        lo = -12.0f;
        hi = -5.0f;
        return;
    default:
        lo = -60.0f;
        hi = 0.0f;
        return;
    }
}

void toneEdges( const std::array<float, 5> &styleMix, ToneDbT &lo, ToneDbT &hi )
{
    // The references' shares of the drop's power (Tools/ref_stats.json, 20.10.2026; Uplifting 7 records, Progressive 6,
    // Dream House 6, Acid 6, Deep 5), in dB: each style's median, and either side of it half the interquartile range of
    // every record's distance from its style's median, pooled over the styles (at least 1.5 dB) -- one style's quartiles
    // from six records lay 0.6 dB apart.
    static const float Lo[5][ToneBands] = { { -10.1f, -5.7f, -10.1f, -9.3f, -14.3f, -15.1f },
                                            { -8.8f, -4.7f, -10.6f, -11.3f, -16.5f, -15.5f },
                                            { -9.0f, -5.6f, -9.0f, -11.6f, -17.9f, -16.0f },
                                            { -7.9f, -5.5f, -10.7f, -14.1f, -19.5f, -21.3f },
                                            { -9.7f, -5.9f, -7.2f, -10.5f, -16.5f, -16.8f } };
    static const float Hi[5][ToneBands] = { { -6.0f, -2.7f, -7.1f, -6.3f, -11.3f, -12.1f },
                                            { -4.7f, -1.7f, -7.6f, -8.3f, -13.5f, -12.5f },
                                            { -5.0f, -2.6f, -6.0f, -8.6f, -14.9f, -13.0f },
                                            { -3.8f, -2.5f, -7.7f, -11.1f, -16.5f, -18.3f },
                                            { -5.6f, -2.9f, -4.2f, -7.5f, -13.5f, -13.8f } };
    float sum = 0.0f;
    for ( const float w : styleMix ) sum += w;
    lo.fill( 0.0f );
    hi.fill( 0.0f );
    if ( sum <= 0.0f ) return;
    for ( size_t st = 0; st < 5; ++st )
        for ( size_t b = 0; b < static_cast<size_t>( ToneBands ); ++b )
        {
            lo[b] += styleMix[st] / sum * Lo[st][b];
            hi[b] += styleMix[st] / sum * Hi[st][b];
        }
}

std::vector<LevelReading> levelScore( Score &score, const ParamStore &params, double seconds,
                                      const std::function<bool()> &stop )
{
    std::vector<LevelReading> out;
    if ( score.m_levels.empty() ) return out;
    auto        e = std::make_unique<Engine>();
    ParamStore &p = e->params();
    p.copyValuesFrom( params );
    const int master = p.id( ModuleE::MASTER, 0, master::LEVEL );
    p.set( master, p.defaultValue( master ) );   // the player's master level comes on top of the correction
    e->prepare( Rate, Block );
    Score s = score;
    for ( LevelMark &m : s.m_levels )
        if ( !std::isnan( m.m_targetLufs ) )
        {
            m.m_trimDb = 0.0f;
            m.m_balDb = BalanceDbT{};
            m.m_toneDb = ToneDbT{};
            m.m_breakDb = 0.0f;
            m.m_buildDb = 0.0f;
        }
    out.resize( s.m_levels.size() );

    // 1. The balance: the main drop and 32 bars before it, against the loudest kick of both.
    e->load( s );
    for ( size_t i = 0; i < s.m_levels.size(); ++i )
    {
        const LevelMark &m = s.m_levels[i];
        out[i].m_beat = m.m_beat;
        out[i].m_target = m.m_targetLufs;
        out[i].m_found.fill( std::numeric_limits<float>::quiet_NaN() );
        if ( std::isnan( m.m_targetLufs ) ) continue;
        PartPeaks peaks;
        for ( const double back : { 0.0, 128.0 } )
        {
            const double at = m.m_peakBeat + ( back > 0.0 ? 64.0 : 0.0 );   // the drop's start and its second half
            if ( !readParts( *e, s, at, BalSeconds, peaks, stop ) ) return {};
        }
        out[i].m_found = against( peaks );
        out[i].m_bal = corrections( *e, out[i].m_found, m.m_windowDb );
        s.m_levels[i].m_balDb = out[i].m_bal;
    }

    // 1b. The tone (20.10.2026), with the balance in: the spectrum of the drop's start and its second half, and the
    // smallest trims that bring every band into the references' range for the style (toneEdges), fitted on that spectrum
    // (fitTone) -- known before the track plays, they hold for all of it (ToneEq on the track bus).
    e->load( s );
    for ( size_t i = 0; i < s.m_levels.size(); ++i )
    {
        const LevelMark &m = s.m_levels[i];
        float            mix = 0.0f;
        for ( const float w : m.m_styleMix ) mix += w;
        if ( std::isnan( m.m_targetLufs ) || mix <= 0.0f ) continue;
        std::vector<double> power, part;
        for ( const double back : { 0.0, 128.0 } )
        {
            double       whole = 0.0;
            const double at = m.m_peakBeat + ( back > 0.0 ? 64.0 : 0.0 );
            if ( !readSpectrum( *e, s, at, BalSeconds, part, whole, stop ) ) return {};
            if ( power.empty() ) power.assign( part.size(), 0.0 );
            for ( size_t q = 0; q < power.size(); ++q ) power[q] += part[q];
        }
        ToneEq flat;
        flat.prepare( Rate, DeckToneBands, ToneBands );
        ToneDbT lo, hi;
        toneLevels( power, flat, ToneDbT{}, out[i].m_bands );
        toneEdges( m.m_styleMix, lo, hi );
        out[i].m_tone = fitTone( power, lo, hi );
        s.m_levels[i].m_toneDb = out[i].m_tone;
    }

    // 2. The breakdown against the drop (the correction, checked and refined, and checked again: the gap reported is the
    // one the last correction left).
    const double trackEnd = s.m_lengthBeats;
    for ( size_t i = 0; i < s.m_levels.size(); ++i )
    {
        LevelMark &m = s.m_levels[i];
        out[i].m_gapBefore = out[i].m_gapAfter = std::numeric_limits<float>::quiet_NaN();
        if ( std::isnan( m.m_targetLufs ) ) continue;
        const double   end = i + 1 < s.m_levels.size() ? s.m_levels[i + 1].m_beat : trackEnd;
        const Section *bd = mainBreakdown( s, m.m_beat, end );
        if ( bd == nullptr ) continue;
        for ( int round = 0; round < 3; ++round )
        {
            e->load( s );
            // The breakdown's own loudness begins after its first three seconds (the meter's short-term window).
            const double bdSeconds =
                s.m_tempo.secondsAt( bd->m_beat + bd->m_length ) - s.m_tempo.secondsAt( bd->m_beat ) - 3.0;
            const std::optional<LoudnessReport> rb =
                measure( *e, s, s.m_tempo.beatAt( s.m_tempo.secondsAt( bd->m_beat ) + 3.0 ), std::max( 3.5, bdSeconds ),
                         stop, 3.0 );
            const std::optional<LoudnessReport> rd =
                measureDrop( *e, s, m.m_peakBeat, std::max( seconds, sectionSeconds( s, m.m_peakBeat ) ), stop );
            if ( !rb || !rd ) return {};
            const float gap = static_cast<float>( rd->m_shortTermMax - rb->m_shortTermMax );
            if ( round == 0 ) out[i].m_gapBefore = gap;
            out[i].m_gapAfter = gap;
            if ( std::fabs( gap - m.m_gapLu ) < 0.4f || round == 2 ) break;
            m.m_breakDb = std::clamp( m.m_breakDb + ( gap - m.m_gapLu ), -BreakMost, BreakMost );
        }
        out[i].m_breakDb = m.m_breakDb;
    }

    // 2b. Every build under the drop by a LU at least (the energy script: build 7 -> 8, drop 10; Dok. 6): the riser, the
    // snare roll and the opening pad must not outshout the drop they announce.
    for ( size_t i = 0; i < s.m_levels.size(); ++i )
    {
        LevelMark &m = s.m_levels[i];
        if ( std::isnan( m.m_targetLufs ) ) continue;
        const double end = i + 1 < s.m_levels.size() ? s.m_levels[i + 1].m_beat : trackEnd;
        for ( int round = 0; round < 3; ++round )
        {
            e->load( s );
            float worst = -100.0f;
            for ( const Section &sec : s.m_sections )
            {
                if ( sec.m_kind != SectionKindE::BUILD || sec.m_beat < m.m_beat || sec.m_beat >= end ) continue;
                const double secs =
                    s.m_tempo.secondsAt( sec.m_beat + sec.m_length ) - s.m_tempo.secondsAt( sec.m_beat ) - 3.0;
                const std::optional<LoudnessReport> rb =
                    measure( *e, s, s.m_tempo.beatAt( s.m_tempo.secondsAt( sec.m_beat ) + 3.0 ), std::max( 3.5, secs ),
                             stop, 3.0 );
                if ( !rb ) return {};
                worst = std::max( worst, static_cast<float>( rb->m_shortTermMax ) );
            }
            if ( worst <= -99.0f ) break;
            const std::optional<LoudnessReport> rd =
                measureDrop( *e, s, m.m_peakBeat, std::max( seconds, sectionSeconds( s, m.m_peakBeat ) ), stop );
            if ( !rd ) return {};
            const float over = worst - static_cast<float>( rd->m_shortTermMax - 1.0 );
            if ( over <= 0.25f ) break;
            m.m_buildDb = std::max( -BreakMost, m.m_buildDb - over - 0.3f );
        }
    }

    // 3. The loudness: the main drop as composed (with the balance and the breakdown), then with the correction.
    e->load( s );
    for ( size_t i = 0; i < s.m_levels.size(); ++i )
    {
        const LevelMark &m = s.m_levels[i];
        if ( std::isnan( m.m_targetLufs ) ) continue;
        const std::optional<LoudnessReport> got =
            measureDrop( *e, s, m.m_peakBeat, std::max( seconds, sectionSeconds( s, m.m_peakBeat ) ), stop );
        if ( !got ) return {};
        out[i].m_measured = static_cast<float>( got->m_integrated );
        out[i].m_trim =
            out[i].m_measured > -70.0f ? std::clamp( m.m_targetLufs - out[i].m_measured, -MostDb, MostDb ) : 0.0f;
        s.m_levels[i].m_trimDb = out[i].m_trim;
    }
    // The second step a secant (20.10.2026): over -2 dBTP the limiter and the clipper take back part of every dB, a
    // loud drop half of it (Uplifting, seed 5: +3.4 dB of trim, +1.7 LU), so the step is divided by the slope the first
    // one measured. Then the drop measured once more with it: m_after is what the track plays at.
    e->load( s );
    for ( size_t i = 0; i < s.m_levels.size(); ++i )
    {
        if ( std::isnan( s.m_levels[i].m_targetLufs ) ) continue;
        const std::optional<LoudnessReport> got = measureDrop(
            *e, s, s.m_levels[i].m_peakBeat, std::max( seconds, sectionSeconds( s, s.m_levels[i].m_peakBeat ) ), stop );
        if ( !got ) return {};
        const float again = static_cast<float>( got->m_integrated );
        if ( again > -70.0f )
        {
            const float slope = std::fabs( out[i].m_trim ) > 0.5f
                                    ? std::clamp( ( again - out[i].m_measured ) / out[i].m_trim, 0.3f, 1.0f )
                                    : 1.0f;
            out[i].m_trim = std::clamp( out[i].m_trim + ( out[i].m_target - again ) / slope, -MostDb, MostDb );
        }
        out[i].m_after = again;
        s.m_levels[i].m_trimDb = out[i].m_trim;
    }
    e->load( s );
    for ( size_t i = 0; i < s.m_levels.size(); ++i )
    {
        if ( std::isnan( s.m_levels[i].m_targetLufs ) ) continue;
        const std::optional<LoudnessReport> got = measureDrop(
            *e, s, s.m_levels[i].m_peakBeat, std::max( seconds, sectionSeconds( s, s.m_levels[i].m_peakBeat ) ), stop );
        if ( !got ) return {};
        out[i].m_after = static_cast<float>( got->m_integrated );
    }
    for ( size_t i = 0; i < out.size() && i < s.m_levels.size(); ++i ) out[i].m_buildDb = s.m_levels[i].m_buildDb;
    for ( size_t i = 0; i < score.m_levels.size() && i < out.size(); ++i )
    {
        score.m_levels[i].m_trimDb = out[i].m_trim;
        score.m_levels[i].m_balDb = out[i].m_bal;
        score.m_levels[i].m_toneDb = out[i].m_tone;
        score.m_levels[i].m_breakDb = s.m_levels[i].m_breakDb;
        score.m_levels[i].m_buildDb = s.m_levels[i].m_buildDb;
    }
    return out;
}

}   // namespace parh
