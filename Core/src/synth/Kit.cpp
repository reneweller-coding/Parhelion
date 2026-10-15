/**
 * @file Kit.cpp
 * @brief Percussion kit: coefficients, hits, the metal table and rendering.
 * @note Adapted from Phosphene `Core/src/Perc.cpp` at 76f7100 (27.09.2026); see Kit.h for what changed.
 * @note Copied from Totality `Core/src/synth/Kit.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 * @author Rene Weller
 */
#include "parh/synth/Kit.h"
#include <algorithm>
#include <cmath>

namespace parh
{

namespace
{

constexpr double PiD = 3.141592653589793;      ///< pi
constexpr double Ln1000 = 6.907755278982137;   ///< ln 1000: a decay of 60 dB

/** @brief Mode frequency ratios: membrane (Bessel zeros), free-free bar, loaded membrane (tabla). */
constexpr double ModeRatios[3][PercModes] = {
    { 1.0, 1.593, 2.136, 2.295 },
    { 1.0, 2.756, 5.404, 8.933 },
    { 1.0, 2.0, 3.0, 4.0 },
};
/** @brief Strike amplitudes of the modes, before normalisation. */
constexpr double ModeAmps[3][PercModes] = {
    { 1.0, 0.7, 0.5, 0.4 },
    { 1.0, 0.5, 0.25, 0.12 },
    { 1.0, 0.5, 0.33, 0.25 },
};
/** @brief TR-808 cymbal oscillator frequencies. */
constexpr double MetalHz[PercMetalOsc] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
/** @brief How far a metal lane's noise pulls its squares down (Phosphene, 25.09.2026). */
constexpr float MetalNoiseTrade = 0.8f;
/** @brief The auto-pan's swing amplitude and the largest angle the kernel's series may be asked for (Phosphene). */
constexpr double PanRms = 1.4142135623730951;
constexpr double PanMaxAngle = 1.6;   ///< the auto-pan's widest swing, radians

/**
 * @brief The per-sample factor of a decay of 60 dB in @p seconds at rate @p sr.
 * @param[in] seconds  the time to fall by 60 dB
 * @param[in] sr       the sample rate in Hz
 * @return the per-sample factor
 */
double decayFactor( double seconds, double sr )
{
    return std::exp( -Ln1000 / ( std::max( seconds, 1.0e-4 ) * sr ) );
}

/**
 * @brief Trapezoidal SVF coefficients (Phosphene's Util.h).
 * @param[in]  fc       the corner in Hz
 * @param[in]  damping  the damping
 * @param[in]  sr       the sample rate in Hz
 * @param[out] a1       receives the first coefficient
 * @param[out] a2       receives the second
 * @param[out] a3       receives the third
 */
void svfCoefs( double fc, double damping, double sr, float &a1, float &a2, float &a3 )
{
    const double g = std::tan( PiD * std::clamp( fc, 10.0, 0.45 * sr ) / sr );
    const double d1 = 1.0 / ( 1.0 + g * ( g + damping ) );
    a1 = static_cast<float>( d1 );
    a2 = static_cast<float>( g * d1 );
    a3 = static_cast<float>( g * g * d1 );
}

/**
 * @brief The phase group of each role (Phosphene's rule: a property of what a lane plays, never of a level). The
 *        closed hat against the rolling hat, the ride, the shaker and the rim; clap, snare and the hand drums with it.
 */
constexpr int8_t RolePanGroup[NumPercRoles] = {
    +1,   // Closed Hat
    -1,   // Rolling Hat
    +1,   // Open Hat
    -1,   // Ride
    +1,   // Clap (centred: no swing)
    +1,   // Clap Ghost
    +1,   // Snare
    -1,   // Rim
    -1,   // Shaker
    +1,   // Tom
    +1,   // Conga
    -1,   // Noise
};

}   // namespace

double PercKit::tuneToScale( double hz, int keyRoot, int scale )
{
    const double note = 69.0 + 12.0 * std::log2( hz / 440.0 );
    const int    base = static_cast<int>( std::floor( note ) );
    double       best = hz, bestDist = 1e9;
    for ( int n = base - 2; n <= base + 3; ++n )
    {
        if ( !inScale( scale, n - keyRoot ) ) continue;
        const double d = std::fabs( n - note );
        if ( d < bestDist )
        {
            bestDist = d;
            best = midiToHz( n );
        }
    }
    return best;
}

void PercKit::setTempo( double bpm )
{
    const double b = std::clamp( bpm, 20.0, 400.0 );
    if ( b == m_bpm ) return;
    m_bpm = b;
    for ( int l = 0; l < PercLanes; ++l )
        if ( m_valid[l] ) updatePanRate( l );
}

void PercKit::assignPanGroups()
{
    for ( int l = 0; l < PercLanes; ++l )
    {
        const double dir = m_values[l][perc::PAN] < 0.0f ? -1.0 : 1.0;
        const int role = std::clamp( static_cast<int>( std::lround( m_values[l][perc::ROLE] ) ), 0, NumPercRoles - 1 );
        m_c.m_panA[l] = static_cast<float>( std::fabs( static_cast<double>( m_c.m_panA[l] ) ) * dir *
                                            static_cast<double>( RolePanGroup[role] ) );
    }
    m_panning = false;
    for ( int l = 0; l < PercLanes; ++l ) m_panning = m_panning || m_c.m_panA[l] != 0.0f || m_c.m_panB[l] != 0.0f;
}

void PercKit::buildMetalTable()
{
    // A fixed seed: the table is part of the instrument, the same on every machine and in every render.
    Rng rng;
    rng.seed( 0x3930394D4554414Cull );                       // "909METAL"
    const int           n = static_cast<int>( MetalRate );   // one second
    std::vector<double> acc( static_cast<size_t>( n ), 0.0 );
    constexpr int       Partials = 60;
    for ( int p = 0; p < Partials; ++p )
    {
        const double f = 2800.0 * std::pow( 17000.0 / 2800.0, static_cast<double>( rng.uniform() ) );
        const double amp = ( 0.5 + 0.5 * rng.uniform() ) * std::pow( f / 2800.0, -0.3 );
        const double tau = 0.25 + 1.0 * rng.uniform();
        const double w = 2.0 * PiD * f / MetalRate;
        const double r = std::exp( -1.0 / ( tau * MetalRate ) );
        const double ph = 2.0 * PiD * rng.uniform();
        // A damped rotating phasor per partial: exact, and far cheaper than a sine per sample.
        double       zr = amp * std::cos( ph ), zi = amp * std::sin( ph );
        const double cr = r * std::cos( w ), ci = r * std::sin( w );
        for ( int i = 0; i < n; ++i )
        {
            acc[static_cast<size_t>( i )] += zi;
            const double nr = zr * cr - zi * ci;
            zi = zr * ci + zi * cr;
            zr = nr;
        }
    }
    const double noiseR = std::exp( -1.0 / ( 0.5 * MetalRate ) );
    double       noiseAmp = 0.15;
    double       peak = 0.0;
    for ( int i = 0; i < n; ++i )
    {
        acc[static_cast<size_t>( i )] += noiseAmp * rng.bipolar();
        noiseAmp *= noiseR;
        peak = std::max( peak, std::fabs( acc[static_cast<size_t>( i )] ) );
    }
    m_metal.assign( static_cast<size_t>( n ), 0.0f );
    for ( int i = 0; i < n; ++i )
    {
        // Six bits: 64 levels, -32 .. 31 steps of 1/32.
        const double q = std::round( acc[static_cast<size_t>( i )] / peak * 31.0 );
        m_metal[static_cast<size_t>( i )] = static_cast<float>( std::clamp( q, -32.0, 31.0 ) / 32.0 );
    }
}

void PercKit::prepare( double sampleRate )
{
    m_sr = sampleRate;
    const size_t n = static_cast<size_t>( MaxBlock * PercLaneSlots );
    m_noise.assign( n, 0.0f );
    m_reset.assign( n, 0.0f );
    m_dNoise.assign( n, 1.0f );
    m_outL.assign( n, 0.0f );
    m_outR.assign( n, 0.0f );
    m_chokeFactor = static_cast<float>( decayFactor( 0.008, m_sr ) );
    for ( int l = 0; l < PercLanes; ++l )
    {
        m_valid[l] = false;
        m_shiftMul[l] = 1.0;
    }
    if ( m_metal.empty() ) buildMetalTable();
    reset();
}

void PercKit::reset()
{
    m_s = PercState{};
    for ( int l = 0; l < PercLaneSlots; ++l )
    {
        m_s.m_cr[l] = 1.0f;
        m_s.m_mr[l] = 1.0f;
        m_s.m_choke[l] = 1.0f;
        m_c.m_chokeD[l] = 1.0f;
        m_c.m_dP[l] = 1.0f;
        m_c.m_dA[l] = 1.0f;
        m_s.m_pr[l] = 1.0f;
        m_s.m_pi[l] = 0.0f;
        m_c.m_panC[l] = 1.0f;
        m_c.m_panS[l] = 0.0f;
    }
    for ( int l = 0; l < PercLanes; ++l )
    {
        m_noiseRng[l].seed( 0x5045524300ull + static_cast<uint64_t>( l ) );
        m_burstsLeft[l] = 0;
        m_burstTimer[l] = 0.0;
        m_metalPos[l] = 1.0e30;   // past the end: silent until a hit
    }
    // The coefficients describe the lanes' settings, not their state: recompute them from the last values.
    for ( int l = 0; l < PercLanes; ++l )
        if ( m_valid[l] ) computeCoefs( l );
}

void PercKit::update( int lane, const float *v, int keyRoot, int scale )
{
    bool same = m_valid[lane] && keyRoot == m_keyRoot[lane] && scale == m_scale[lane];
    for ( int i = 0; same && i < perc::COUNT; ++i ) same = m_values[lane][i] == v[i];
    if ( same ) return;
    std::copy( v, v + perc::COUNT, m_values[lane] );
    m_keyRoot[lane] = keyRoot;
    m_scale[lane] = scale;
    m_valid[lane] = true;
    computeCoefs( lane );
}

void PercKit::computeCoefs( int l )
{
    const float *v = m_values[l];
    const bool   active = v[perc::ACTIVE] >= 0.5f;
    m_role[l] = static_cast<int>( std::lround( v[perc::ROLE] ) );
    m_engine[l] = static_cast<int>( std::lround( v[perc::ENGINE] ) );
    m_choke[l] = static_cast<int>( std::lround( v[perc::CHOKE] ) );
    m_metalNoise[l] = std::lround( v[perc::NOISE_TYPE] ) == static_cast<long>( NoiseTypeE::METAL909 );
    const PercEngineE engine = static_cast<PercEngineE>( m_engine[l] );

    double hz = v[perc::PITCH];
    if ( v[perc::TUNE] >= 0.5f ) hz = tuneToScale( hz, m_keyRoot[l], m_scale[l] );
    m_tunedHz[l] = hz;
    const double f = hz * m_shiftMul[l];

    m_c.m_wTone[l] = ( engine == PercEngineE::TONE || engine == PercEngineE::FM ) ? 1.0f : 0.0f;
    m_c.m_wModal[l] = engine == PercEngineE::MODAL ? 1.0f : 0.0f;
    m_c.m_wMetal[l] = engine == PercEngineE::METAL ? 1.0f - MetalNoiseTrade * v[perc::NOISE] : 0.0f;
    m_c.m_wNoise[l] = engine == PercEngineE::NOISE ? 1.0f : v[perc::NOISE];

    const double w0 = std::min( 2.0 * PiD * f / m_sr, 1.2 );
    m_c.m_w0[l] = static_cast<float>( w0 );
    m_c.m_pAmt[l] = v[perc::PITCH_AMOUNT] - 1.0f;
    m_c.m_dP[l] = static_cast<float>( std::exp( -1.0 / ( std::max( 0.5e-3, v[perc::PITCH_DECAY] * 0.001 ) * m_sr ) ) );
    m_c.m_fmIdx[l] = engine == PercEngineE::FM ? v[perc::FM_INDEX] : 0.0f;
    const double wm = std::min( w0 * v[perc::FM_RATIO], 0.95 * PiD );
    m_c.m_mc[l] = static_cast<float>( std::cos( wm ) );
    m_c.m_ms[l] = static_cast<float>( std::sin( wm ) );
    const double decay = v[perc::DECAY] * 0.001;
    m_c.m_dA[l] = static_cast<float>( decayFactor( decay, m_sr ) );

    const int set = std::clamp( static_cast<int>( std::lround( v[perc::MODE_SET] ) ), 0, 2 );
    double    ampSum = 0.0;
    for ( int m = 0; m < PercModes; ++m ) ampSum += ModeAmps[set][m];
    for ( int m = 0; m < PercModes; ++m )
    {
        const double wk = 2.0 * PiD * f * ModeRatios[set][m] / m_sr;
        const bool   audible = wk < 0.95 * PiD;
        const double tau = decay * std::pow( ModeRatios[set][m], -1.5 * v[perc::MODE_DAMP] );
        const double r = audible ? decayFactor( tau, m_sr ) : 0.0;
        m_modeW[m][l] = wk;
        m_modeR[m][l] = r;
        m_modeAmp[m][l] = audible ? static_cast<float>( ModeAmps[set][m] / ampSum ) : 0.0f;
        m_c.m_modeC[m][l] = static_cast<float>( r * std::cos( wk ) );
        m_c.m_modeS[m][l] = static_cast<float>( r * std::sin( wk ) );
    }

    const double scale = v[perc::METAL_SCALE];
    for ( int j = 0; j < PercMetalOsc; ++j )
    {
        const double dt = std::min( MetalHz[j] * scale / m_sr, 0.45 );
        m_c.m_dt[j][l] = static_cast<float>( dt );
        m_c.m_inv[j][l] = static_cast<float>( 1.0 / dt );
    }
    // The metal table's playback rate: Metal Scale times its own rate (the 909's tuning, 0.43 .. 1.7).
    m_metalStep[l] = scale * MetalRate / m_sr;

    const double spacing = v[perc::BURST_SPACING] * 0.001;
    m_burstSpacing[l] = spacing * m_sr;
    m_noiseTail[l] = static_cast<float>( decayFactor( v[perc::NOISE_DECAY] * 0.001, m_sr ) );
    m_noiseFast[l] = static_cast<float>( std::exp( std::log( 0.03 ) / std::max( 1.0, spacing * m_sr ) ) );

    const int    mode = std::clamp( static_cast<int>( std::lround( v[perc::FILTER] ) ), 0, 2 );
    const double damping = 2.0 - 1.9 * std::clamp( static_cast<double>( v[perc::RESONANCE] ), 0.0, 1.0 );
    svfCoefs( v[perc::CUTOFF], damping, m_sr, m_c.m_a1[l], m_c.m_a2[l], m_c.m_a3[l] );
    m_c.m_k[l] = static_cast<float>( damping );
    m_c.m_mLp[l] = mode == 0 ? 1.0f : 0.0f;
    m_c.m_mBp[l] = mode == 1 ? 1.0f : 0.0f;
    m_c.m_mHp[l] = mode == 2 ? 1.0f : 0.0f;
    // Low cut, never below 150 Hz: under it only kick, rumble and sub play (PLAN 5.4).
    const double cutTrack = std::clamp( static_cast<double>( v[perc::CUT_TRACK] ), 0.0, 2.0 );
    const double lowCut =
        static_cast<double>( v[perc::LOW_CUT] ) * ( cutTrack > 0.0 ? std::pow( m_shiftMul[l], cutTrack ) : 1.0 );
    svfCoefs( std::max( 150.0, lowCut ), std::sqrt( 2.0 ), m_sr, m_c.m_c1[l], m_c.m_c2[l], m_c.m_c3[l] );

    const double g = 0.1 + 6.0 * v[perc::DRIVE];
    m_c.m_drvG[l] = static_cast<float>( g );
    m_c.m_drvN[l] = static_cast<float>( 1.0 / g );
    const double level = active ? std::pow( 10.0, v[perc::LEVEL] / 20.0 ) : 0.0;
    const double p0 = std::clamp( static_cast<double>( v[perc::PAN] ), -1.0, 1.0 );
    const double theta = ( p0 + 1.0 ) * PiD / 4.0;
    m_c.m_gL[l] = static_cast<float>( level * std::cos( theta ) * std::sqrt( 2.0 ) );
    m_c.m_gR[l] = static_cast<float>( level * std::sin( theta ) * std::sqrt( 2.0 ) );

    const double depth = std::clamp( static_cast<double>( v[perc::PAN_DEPTH] ), 0.0, 1.0 );
    double       stand = std::sqrt( std::max( 0.0, 1.0 - depth * depth ) ), swing = PanRms * depth;
    const double reach = std::fabs( p0 ) * ( stand + swing );
    if ( reach > 1.0 )
    {
        stand /= reach;
        swing /= reach;
    }
    double       a = p0 * swing * PiD / 4.0;
    double       b = p0 * ( stand - 1.0 ) * PiD / 4.0;
    const double span = std::fabs( a ) + std::fabs( b );
    if ( span > PanMaxAngle )
    {
        const double k = PanMaxAngle / span;
        a *= k;
        b *= k;
    }
    m_c.m_panA[l] = static_cast<float>( a );
    m_c.m_panB[l] = static_cast<float>( b );
    updatePanRate( l );
    assignPanGroups();
}

void PercKit::updatePanRate( int l )
{
    const double bars = std::clamp( static_cast<double>( m_values[l][perc::PAN_BARS] ), 0.0625, 64.0 );
    const double w = 2.0 * PiD / std::max( 1.0, bars * 4.0 * 60.0 / m_bpm * m_sr );
    m_c.m_panC[l] = static_cast<float>( std::cos( w ) );
    m_c.m_panS[l] = static_cast<float>( std::sin( w ) );
}

void PercKit::trigger( int l, float velocity, int shift, double late )
{
    if ( l < 0 || l >= PercLanes || !m_valid[l] ) return;
    const double mul = std::pow( 2.0, shift / 12.0 );
    if ( mul != m_shiftMul[l] )
    {
        m_shiftMul[l] = mul;
        computeCoefs( l );
    }
    const double vel = clampv( static_cast<double>( velocity ), 0.0, 1.0 );

    m_s.m_envA[l] = static_cast<float>( vel * std::pow( static_cast<double>( m_c.m_dA[l] ), late ) );
    m_s.m_envP[l] = static_cast<float>( std::pow( static_cast<double>( m_c.m_dP[l] ), late ) );
    const int bursts = static_cast<int>( std::lround( m_values[l][perc::BURSTS] ) );
    m_burstsLeft[l] = bursts - 1;
    m_burstTimer[l] = m_burstSpacing[l] - late;
    m_burstVel[l] = static_cast<float>( vel );
    const double nf = bursts > 1 ? m_noiseFast[l] : m_noiseTail[l];
    m_s.m_envN[l] = static_cast<float>( vel * std::pow( nf, late ) );
    m_metalPos[l] = m_metalStep[l] * late;

    const double wStart = std::min( static_cast<double>( m_c.m_w0[l] ) * ( 1.0 + m_c.m_pAmt[l] ), 1.2 );
    m_s.m_cr[l] = static_cast<float>( std::cos( wStart * late ) );
    m_s.m_ci[l] = static_cast<float>( std::sin( wStart * late ) );
    m_s.m_mr[l] = static_cast<float>( std::cos( std::atan2( m_c.m_ms[l], m_c.m_mc[l] ) * late ) );
    m_s.m_mi[l] = static_cast<float>( std::sin( std::atan2( m_c.m_ms[l], m_c.m_mc[l] ) * late ) );
    for ( int m = 0; m < PercModes; ++m )
    {
        const double a = m_modeAmp[m][l] * vel * std::pow( m_modeR[m][l], late );
        m_s.m_zr[m][l] += static_cast<float>( a * std::cos( m_modeW[m][l] * late ) );
        m_s.m_zi[m][l] += static_cast<float>( a * std::sin( m_modeW[m][l] * late ) );
    }
    m_s.m_choke[l] = 1.0f;
    m_c.m_chokeD[l] = 1.0f;
    if ( m_choke[l] > 0 )
    {
        for ( int j = 0; j < PercLanes; ++j )
        {
            if ( j != l && m_choke[j] == m_choke[l] ) m_c.m_chokeD[j] = m_chokeFactor;
        }
    }
}

template <class VT>
void PercKit::processLanesWith( int n )
{
    n = std::min( n, MaxBlock );
    const int width = laneWidth<VT>();
    bool      tone[2] = {}, modal[2] = {}, metal[2] = {}, noisy[2] = {};
    for ( int l = 0; l < PercLanes; ++l )
    {
        const int g = l / 8;
        tone[g] = tone[g] || m_c.m_wTone[l] != 0.0f;
        modal[g] = modal[g] || m_c.m_wModal[l] != 0.0f;
        metal[g] = metal[g] || m_c.m_wMetal[l] != 0.0f;
        noisy[g] = noisy[g] || m_c.m_wNoise[l] != 0.0f;
    }
    const size_t tableLen = m_metal.size();
    for ( int l = 0; l < PercLaneSlots; ++l )
    {
        const bool live = l < PercLanes;
        for ( int i = 0; i < n; ++i )
        {
            const size_t row = static_cast<size_t>( i * PercLaneSlots + l );
            m_reset[row] = 0.0f;
            if ( !live )
            {
                m_noise[row] = 0.0f;
                m_dNoise[row] = 1.0f;
                continue;
            }
            float nz = 0.0f;
            if ( noisy[l / 8] )
            {
                if ( m_metalNoise[l] )
                {
                    const double pos = m_metalPos[l];
                    if ( pos < static_cast<double>( tableLen - 1 ) )
                    {
                        const size_t k = static_cast<size_t>( pos );
                        const float  fr = static_cast<float>( pos - static_cast<double>( k ) );
                        nz = m_metal[k] + fr * ( m_metal[k + 1] - m_metal[k] );
                        m_metalPos[l] = pos + m_metalStep[l];
                    }
                }
                else
                {
                    nz = m_noiseRng[l].bipolar();
                }
            }
            m_noise[row] = nz;
            if ( m_burstsLeft[l] > 0 )
            {
                m_burstTimer[l] -= 1.0;
                if ( m_burstTimer[l] <= 0.0 )
                {
                    m_reset[row] = m_burstVel[l];
                    --m_burstsLeft[l];
                    m_burstTimer[l] += m_burstSpacing[l];
                }
            }
            m_dNoise[row] = m_burstsLeft[l] > 0 ? m_noiseFast[l] : m_noiseTail[l];
        }
    }
    for ( int l = 0; l < PercLaneSlots; l += width )
    {
        const int g = std::min( l / 8, 1 );
        percKernel<VT>( m_s, m_c, l, n, m_noise.data(), m_reset.data(), m_dNoise.data(), tone[g], modal[g], metal[g],
                        m_panning, m_outL.data(), m_outR.data() );
    }
}

template void PercKit::processLanesWith<float>( int );
#if PARH_VEC_PATH != 0
template void PercKit::processLanesWith<VecF>( int );
#endif

void PercKit::processLanes( int n )
{
    processLanesWith<VecF>( n );
}

void PercKit::process( float *L, float *R, int total )
{
    int done = 0;
    while ( done < total )
    {
        const int n = std::min( MaxBlock, total - done );
        processLanes( n );
        for ( int i = 0; i < n; ++i )
        {
            float sl = 0.0f, sr = 0.0f;
            for ( int l = 0; l < PercLanes; ++l )
            {
                sl += m_outL[static_cast<size_t>( i * PercLaneSlots + l )];
                sr += m_outR[static_cast<size_t>( i * PercLaneSlots + l )];
            }
            L[done + i] = sl;
            R[done + i] = sr;
        }
        done += n;
    }
}

}   // namespace parh
