/**
 * @file Reverb.cpp
 * @brief FDN reverb implementation.
 * @note Copied from Phosphene `Core/src/Reverb.cpp` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 * @note Copied from Ephemeris `Core/src/fx/Reverb.cpp` at d047d79 (27.09.2026); namespace parh, prefix PARH_.
 * @note Copied from Totality `Core/src/fx/Reverb.cpp` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 * @author Rene Weller
 */
#include "parh/fx/Reverb.h"
#include "parh/Clock.h"
#include "parh/Dsp.h"
#include "parh/Vec.h"
#include <algorithm>
#include <cmath>

namespace parh
{

namespace
{
/**
 * @brief The smallest power of two at least @p n.
 * @param[in] n  the lower bound
 * @return the smallest power of two at least n
 */
int pow2At( int n )
{
    int p = 1;
    while ( p < n ) p <<= 1;
    return p;
}
constexpr double PiD = 3.14159265358979323846;   ///< pi

/**
 * @brief Which output channel each delay line feeds, and which input it is driven by (1 = left).
 *
 * Not the first half against the second. The line set is ordered by length (29.7 .. 89.0 ms at size
 * one), so splitting it in the middle gave the left channel the four short lines and the right the
 * four long ones -- and a line's length is where its comb peaks sit, so the two returns were not the
 * same instrument: measured over the third octaves from 500 Hz to 8 kHz they differed by 1.45 dB rms
 * in the room and 1.36 in the hall, worst band 2.05 and 2.55 dB. Each channel now takes two short
 * lines and two long ones ({29.7, 46.6, 58.6, 68.4} against {31.4, 39.3, 55.1, 89.0}), which leaves
 * the two returns as decorrelated as before -- they still share no line -- while giving them the same
 * distribution of delays and therefore the same colour. The sign an output tap carries is the sign
 * that line's input carries, so the first pass through a line still adds rather than cancels.
 */
constexpr int Left[8] = { 1, 0, 0, 1, 0, 1, 1, 0 };

/**
 * @brief Linear-interpolated read @p delay samples behind the write position @p w.
 * @param[in] buf    the ring
 * @param[in] mask   its length minus one
 * @param[in] w      the write position
 * @param[in] delay  how many samples behind it, fractional
 * @return the interpolated sample
 */
inline float ringRead( const float *buf, int mask, int w, float delay )
{
    // In double: a float write index of 2^17 resolves only 1/64 of a sample.
    const double pos = static_cast<double>( w ) - static_cast<double>( delay );
    const double fl = std::floor( pos );
    const float  frac = static_cast<float>( pos - fl );
    const int    i0 = static_cast<int>( fl ) & mask;
    return buf[i0] + frac * ( buf[( i0 + 1 ) & mask] - buf[i0] );
}
}   // namespace

void Reverb::prepare( double sampleRate )
{
    m_sr = sampleRate;
    // The longest line at size 3 plus the pre-delay of up to half a second.
    const int size = pow2At( static_cast<int>( 0.1 * 3.0 * m_sr + 0.6 * m_sr ) + 64 );
    m_mask = size - 1;
    m_lines.assign( static_cast<size_t>( Lines ) * static_cast<size_t>( size ), 0.0f );
    for ( auto &a : m_ap ) a.assign( static_cast<size_t>( size ), 0.0f );
    for ( auto &a : m_apR ) a.assign( static_cast<size_t>( size ), 0.0f );
    m_scs.assign( static_cast<size_t>( Lines ) * static_cast<size_t>( size ), 0.0f );
    m_pre.assign( static_cast<size_t>( size ), 0.0f );
    m_preR.assign( static_cast<size_t>( size ), 0.0f );
    static const float ApMs[Allpasses] = { 5.1f, 7.3f, 11.3f, 13.7f };
    for ( int k = 0; k < Allpasses; ++k ) m_apLen[k] = std::max( 1, static_cast<int>( ApMs[k] * m_sr / 1000.0 ) );
    static const float ScMs[Lines] = { 1.9f, 2.3f, 2.9f, 3.7f, 4.3f, 5.3f, 6.1f, 7.1f };
    for ( int l = 0; l < Lines; ++l ) m_scLen[l] = std::max( 1, static_cast<int>( ScMs[l] * m_sr / 1000.0 ) );
    static const float ModHz[Lines] = { 0.11f, 0.13f, 0.17f, 0.19f, 0.23f, 0.29f, 0.31f, 0.37f };
    for ( int l = 0; l < Lines; ++l ) m_modRate[l] = ModHz[l];
    m_dcR = 1.0f - TwoPi * 5.0f / static_cast<float>( m_sr );
    reset();
    set( 1.0f, 2.0f, 0.4f, 0.0f, 250.0f, 9000.0f );
}

void Reverb::reset()
{
    std::fill( m_lines.begin(), m_lines.end(), 0.0f );
    for ( auto &a : m_ap ) std::fill( a.begin(), a.end(), 0.0f );
    for ( auto &a : m_apR ) std::fill( a.begin(), a.end(), 0.0f );
    std::fill( m_scs.begin(), m_scs.end(), 0.0f );
    std::fill( m_pre.begin(), m_pre.end(), 0.0f );
    std::fill( m_preR.begin(), m_preR.end(), 0.0f );
    m_w = 0;
    for ( int l = 0; l < Lines; ++l )
    {
        m_lp[l] = 0.0f;
        m_modPh[l] = l / static_cast<double>( Lines );
        m_lenCur[l] = 0.0f;
    }
    m_preCur = -1.0f;
    m_dcX[0] = m_dcX[1] = m_dcY[0] = m_dcY[1] = 0.0f;
    m_hcL = m_hcR = m_lcL1 = m_lcR1 = m_lcL2 = m_lcR2 = 0.0f;
    m_duckEnv = 0.0f;
}

void Reverb::set( float size, float decaySeconds, float damping, float preDelaySamples, float lowCutHz,
                  float highCutHz )
{
    // Noctuary's colourless line set (Tools/optimise_fdn.py, 0.314 dB third-octave spread).
    static const float FlatMs[Lines] = { 29.7f, 31.4f, 39.3f, 46.6f, 55.1f, 58.6f, 68.4f, 89.0f };
    const float        s = clampv( size, 0.3f, 3.0f );
    const float        decay = std::max( decaySeconds, 0.1f );
    m_damp = clampv( damping, 0.0f, 1.0f );
    const float maxLen = static_cast<float>( m_mask ) - 8.0f;
    for ( int l = 0; l < Lines; ++l )
    {
        m_lenTarget[l] = std::min( FlatMs[l] * s * static_cast<float>( m_sr / 1000.0 ), maxLen );
        if ( m_lenCur[l] <= 0.0f ) m_lenCur[l] = m_lenTarget[l];
        m_gain[l] = std::pow( 10.0f, -3.0f * m_lenTarget[l] / ( decay * static_cast<float>( m_sr ) ) );
    }
    m_preTarget = clampv( preDelaySamples, 0.0f, maxLen - m_lenTarget[Lines - 1] );
    if ( m_preCur < 0.0f ) m_preCur = m_preTarget;
    const float hc = clampv( highCutHz, 500.0f, 20000.0f );
    m_hcCoef = hc >= 19000.0f ? 1.0f : 1.0f - std::exp( -TwoPi * hc / static_cast<float>( m_sr ) );
    // Two cascaded one-poles are 6 dB down at their corner and 3 dB down at 1.554 times it.
    // Phosphene kept the return above 150 Hz for its phase-locked kick and bass; Ephemeris has none,
    // and a hall under a drone may reach lower. The caller's knob decides, down to 40 Hz.
    const float lc = std::max( 40.0f, lowCutHz );
    m_lcCoef = 1.0f - std::exp( -TwoPi * ( lc / 1.5538f ) / static_cast<float>( m_sr ) );
}

void Reverb::process( const float *inL, const float *inR, float *outL, float *outR, int n )
{
    const float     lpc = 1.0f - 0.92f * m_damp;
    constexpr float inGain = 0.5f, glide = 0.0005f;
#if PARH_VEC_PATH == 1
    // The eight lines on the eight lanes of an AVX2 register (the optimisation pass, 29.09.2026): the scalar loop's
    // operations lane by lane, in its order and precision -- the modulation's phase and the read position in double,
    // the rest in float, sin01 and ringRead spelled out --, the reads gathered from the one buffer of the lines and of
    // their allpasses, the writes stored one by one. Bit for bit the scalar reverb below (the renders compare equal).
    const int     size = m_mask + 1;
    const __m256i lineAt = _mm256_setr_epi32( 0, size, 2 * size, 3 * size, 4 * size, 5 * size, 6 * size, 7 * size );
    const __m256i maskV = _mm256_set1_epi32( m_mask ), oneI = _mm256_set1_epi32( 1 );
    const __m256i scLenV = _mm256_loadu_si256( reinterpret_cast<const __m256i *>( m_scLen ) );
    const __m256  lenTarget = _mm256_loadu_ps( m_lenTarget ), glideV = _mm256_set1_ps( glide ),
                  lpcV = _mm256_set1_ps( lpc );
    const __m256  half = _mm256_set1_ps( 0.5f );
    __m256        lenCur = _mm256_loadu_ps( m_lenCur ), lp = _mm256_loadu_ps( m_lp );
    __m256d       phA = _mm256_loadu_pd( m_modPh ), phB = _mm256_loadu_pd( m_modPh + 4 );
    alignas( 32 ) double inc[Lines];
    for ( int l = 0; l < Lines; ++l ) inc[l] = m_modRate[l] / m_sr;
    const __m256d incA = _mm256_load_pd( inc ), incB = _mm256_load_pd( inc + 4 ), oneD = _mm256_set1_pd( 1.0 );
    const __m256d nD = _mm256_set1_pd( static_cast<double>( SineTable::N ) );
    const float  *sine = sineTable().m_v;
    const float  *lines = m_lines.data();
    const float  *scs = m_scs.data();
#endif
    for ( int i = 0; i < n; ++i )
    {
        const float xl0 = inL[i] - m_dcX[0] + m_dcR * m_dcY[0];
        m_dcX[0] = inL[i];
        m_dcY[0] = xl0;
        const float xr0 = inR[i] - m_dcX[1] + m_dcR * m_dcY[1];
        m_dcX[1] = inR[i];
        m_dcY[1] = xr0;
        m_pre[static_cast<size_t>( m_w & m_mask )] = xl0;
        m_preR[static_cast<size_t>( m_w & m_mask )] = xr0;
        m_preCur += ( m_preTarget - m_preCur ) * glide;
        float xl = ringRead( m_pre.data(), m_mask, m_w, m_preCur + 1.0f );
        float xr = ringRead( m_preR.data(), m_mask, m_w, m_preCur + 1.0f );
        for ( int k = 0; k < Allpasses; ++k )
        {
            float      *a = m_ap[k].data();
            float      *ar = m_apR[k].data();
            const float d = a[( m_w - m_apLen[k] ) & m_mask];
            const float dr = ar[( m_w - m_apLen[k] ) & m_mask];
            const float y = d - 0.6f * xl;
            const float yr = dr - 0.6f * xr;
            a[m_w & m_mask] = xl + 0.6f * y;
            ar[m_w & m_mask] = xr + 0.6f * yr;
            xl = y;
            xr = yr;
        }
        alignas( 32 ) float o[Lines];
        float               sum = 0.0f;
#if PARH_VEC_PATH == 1
        {
            lenCur = _mm256_add_ps( lenCur, _mm256_mul_ps( _mm256_sub_ps( lenTarget, lenCur ), glideV ) );
            phA = _mm256_add_pd( phA, incA );
            phB = _mm256_add_pd( phB, incB );
            phA = _mm256_blendv_pd( phA, _mm256_sub_pd( phA, oneD ), _mm256_cmp_pd( phA, oneD, _CMP_GE_OQ ) );
            phB = _mm256_blendv_pd( phB, _mm256_sub_pd( phB, oneD ), _mm256_cmp_pd( phB, oneD, _CMP_GE_OQ ) );
            // sin01: x = phase N, i = int(x), t[i] + f (t[i + 1] - t[i]) with f = float(x - i)
            const __m256d xA = _mm256_mul_pd( phA, nD ), xB = _mm256_mul_pd( phB, nD );
            const __m128i iA = _mm256_cvttpd_epi32( xA ), iB = _mm256_cvttpd_epi32( xB );
            const __m256i si = _mm256_set_m128i( iB, iA );
            const __m256  sf = _mm256_set_m128( _mm256_cvtpd_ps( _mm256_sub_pd( xB, _mm256_cvtepi32_pd( iB ) ) ),
                                                _mm256_cvtpd_ps( _mm256_sub_pd( xA, _mm256_cvtepi32_pd( iA ) ) ) );
            const __m256  s0 = _mm256_i32gather_ps( sine, si, 4 ),
                          s1 = _mm256_i32gather_ps( sine, _mm256_add_epi32( si, oneI ), 4 );
            const __m256  s = _mm256_add_ps( s0, _mm256_mul_ps( sf, _mm256_sub_ps( s1, s0 ) ) );
            const __m256  d = _mm256_add_ps( _mm256_add_ps( lenCur, _mm256_mul_ps( _mm256_set1_ps( 1.5f ), s ) ),
                                             _mm256_set1_ps( 2.0f ) );
            // ringRead: pos = w - d in double, its floor, the fraction in float, the two samples around it
            const __m256d wD = _mm256_set1_pd( static_cast<double>( m_w ) );
            const __m256d posA = _mm256_sub_pd( wD, _mm256_cvtps_pd( _mm256_castps256_ps128( d ) ) );
            const __m256d posB = _mm256_sub_pd( wD, _mm256_cvtps_pd( _mm256_extractf128_ps( d, 1 ) ) );
            const __m256d flA = _mm256_floor_pd( posA ), flB = _mm256_floor_pd( posB );
            const __m256  frac = _mm256_set_m128( _mm256_cvtpd_ps( _mm256_sub_pd( posB, flB ) ),
                                                  _mm256_cvtpd_ps( _mm256_sub_pd( posA, flA ) ) );
            const __m256i i0 =
                _mm256_and_si256( _mm256_set_m128i( _mm256_cvttpd_epi32( flB ), _mm256_cvttpd_epi32( flA ) ), maskV );
            const __m256i i1 = _mm256_and_si256( _mm256_add_epi32( i0, oneI ), maskV );
            const __m256  a0 = _mm256_i32gather_ps( lines, _mm256_add_epi32( i0, lineAt ), 4 );
            const __m256  a1 = _mm256_i32gather_ps( lines, _mm256_add_epi32( i1, lineAt ), 4 );
            __m256        v = _mm256_add_ps( a0, _mm256_mul_ps( frac, _mm256_sub_ps( a1, a0 ) ) );
            // the allpass: its read gathered, its write stored lane by lane
            const __m256i sAt = _mm256_add_epi32(
                _mm256_and_si256( _mm256_sub_epi32( _mm256_set1_epi32( m_w ), scLenV ), maskV ), lineAt );
            const __m256        sd = _mm256_i32gather_ps( scs, sAt, 4 );
            const __m256        sy = _mm256_sub_ps( sd, _mm256_mul_ps( half, v ) );
            alignas( 32 ) float sw[Lines];
            _mm256_store_ps( sw, _mm256_add_ps( v, _mm256_mul_ps( half, sy ) ) );
            for ( int l = 0; l < Lines; ++l ) sc( l )[m_w & m_mask] = sw[l];
            v = sy;
            lp = _mm256_add_ps( lp, _mm256_mul_ps( lpcV, _mm256_sub_ps( v, lp ) ) );
            _mm256_store_ps( o, lp );
            for ( int l = 0; l < Lines; ++l ) sum += o[l];
        }
#else
        for ( int l = 0; l < Lines; ++l )
        {
            m_lenCur[l] += ( m_lenTarget[l] - m_lenCur[l] ) * glide;
            m_modPh[l] += m_modRate[l] / m_sr;
            if ( m_modPh[l] >= 1.0 ) m_modPh[l] -= 1.0;
            const float d = m_lenCur[l] + 1.5f * sin01( m_modPh[l] ) + 2.0f;
            float       v = ringRead( line( l ), m_mask, m_w, d );
            float      *sb = sc( l );
            const float sd = sb[( m_w - m_scLen[l] ) & m_mask];
            const float sy = sd - 0.5f * v;
            sb[m_w & m_mask] = v + 0.5f * sy;
            v = sy;
            m_lp[l] += lpc * ( v - m_lp[l] );
            o[l] = m_lp[l];
            sum += o[l];
        }
#endif
        const float hh = sum * ( 2.0f / static_cast<float>( Lines ) );   // Householder reflection
        float       wl = 0.0f, wr = 0.0f;
        for ( int l = 0; l < Lines; ++l )
        {
            const float in = Left[l] ? xl : xr;
            line( l )[m_w & m_mask] = m_gain[l] * ( o[l] - hh ) + ( ( l & 1 ) ? -inGain : inGain ) * in;
            const float tap = ( l & 1 ) ? -o[l] : o[l];
            if ( Left[l] != 0 )
                wl += tap;
            else
                wr += tap;
        }
        const float wetL = 0.3f * wl;
        const float wetR = 0.3f * wr;
        m_hcL += m_hcCoef * ( wetL - m_hcL );
        m_hcR += m_hcCoef * ( wetR - m_hcR );
        m_lcL1 += m_lcCoef * ( m_hcL - m_lcL1 );
        const float aL = m_hcL - m_lcL1;
        m_lcR1 += m_lcCoef * ( m_hcR - m_lcR1 );
        const float aR = m_hcR - m_lcR1;
        m_lcL2 += m_lcCoef * ( aL - m_lcL2 );
        m_lcR2 += m_lcCoef * ( aR - m_lcR2 );
        outL[i] = aL - m_lcL2;
        outR[i] = aR - m_lcR2;
        m_w = ( m_w + 1 ) & m_mask;
    }
#if PARH_VEC_PATH == 1
    _mm256_storeu_ps( m_lenCur, lenCur );
    _mm256_storeu_ps( m_lp, lp );
    _mm256_storeu_pd( m_modPh, phA );
    _mm256_storeu_pd( m_modPh + 4, phB );
#endif
}

void Reverb::setDuck( float depth, float thresholdDb, float attackS, float releaseS )
{
    m_duckDepth = clampv( depth, 0.0f, 1.0f );
    m_duckThreshold = dbToGain( thresholdDb );
    m_duckAttackC = 1.0f - std::exp( -1.0f / ( std::max( attackS, 0.0005f ) * static_cast<float>( m_sr ) ) );
    m_duckReleaseC = 1.0f - std::exp( -1.0f / ( std::max( releaseS, 0.0005f ) * static_cast<float>( m_sr ) ) );
}

void Reverb::processDucked( const float *inL, const float *inR, float *outL, float *outR, int n )
{
    process( inL, inR, outL, outR, n );
    for ( int i = 0; i < n; ++i )
    {
        // The send's own peak, above or below the threshold: no note events needed, the duck follows
        // whatever actually reached this bus (stutters, level automation, the works).
        const float lvl = std::max( std::fabs( inL[i] ), std::fabs( inR[i] ) );
        const float target = lvl > m_duckThreshold ? 1.0f : 0.0f;
        m_duckEnv += ( target - m_duckEnv ) * ( target > m_duckEnv ? m_duckAttackC : m_duckReleaseC );
        const float g = 1.0f - m_duckDepth * m_duckEnv;
        outL[i] *= g;
        outR[i] *= g;
    }
}

float Reverb::barGate( double beat, double closeBeats, double holdBeats, double openBeats, float floorGain )
{
    const double bars = beat / static_cast<double>( BeatsPerBar );
    double       sinceBar = ( bars - std::floor( bars ) ) * static_cast<double>( BeatsPerBar );
    if ( sinceBar < 0.0 ) sinceBar += static_cast<double>( BeatsPerBar );   // guard: beat < 0 in a test
    const double c = std::max( closeBeats, 1e-6 ), h = std::max( holdBeats, 0.0 ), o = std::max( openBeats, 1e-6 );
    if ( sinceBar < c )
    {
        const double x = sinceBar / c;
        return floorGain + ( 1.0f - floorGain ) * static_cast<float>( 0.5 + 0.5 * std::cos( PiD * x ) );
    }
    if ( sinceBar < c + h ) return floorGain;
    if ( sinceBar < c + h + o )
    {
        const double x = ( sinceBar - c - h ) / o;
        return floorGain + ( 1.0f - floorGain ) * static_cast<float>( 0.5 - 0.5 * std::cos( PiD * x ) );
    }
    return 1.0f;
}

}   // namespace parh
