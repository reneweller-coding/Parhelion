/**
 * @file MidDuck.h
 * @brief The pads' centre dipped under the melody, in the melody's own bands only (21.10.2026).
 *
 * A mix engineer makes room for a lead over a pad with a dynamic equaliser on the pad, keyed by the lead: a few dB
 * out of the pad where, and while, the lead sounds -- and only in the mid, so the pad's width stays and the lead
 * stands in a gap in the centre (the Ultimae mixing notes the user brought, 21.10.2026: "Pad-Mitte unter der Melodie
 * ducken"). A broadband duck pumps the whole pad; this one leaves everything the lead does not reach alone.
 *
 * **Analysis**, every sample: the key (the lead, mono) and the pad's mid through three band passes (500 Hz, 1.4 kHz
 * and 4 kHz, Q 0.9: a lead's fundamental, its body and its edge), their power followed (5 ms up, 150 ms down).
 * **Aim**, every 32 samples: in each band where the key comes within 6 dB of the pad or above it, a dip that grows to
 * Depth over the next 12 dB of the key's lead; no dip where the key is silent (under -70 dBFS). The dips move at
 * Depth in 10 ms down and in 150 ms back.
 * **The cut** is three of Simper's trapezoidal state-variable bells on the mid, the side passed untouched; the bells
 * are not run at 0 dB. Depth 0 is a true bypass.
 * @note Parhelion's (21.10.2026). Phosphene's pad band under the lead does the same from the score (its Engine.cpp).
 * @author Rene Weller
 */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace parh
{

/** @brief A dynamic equaliser on a pad's mid, keyed by the lead. */
class MidDuck
{
public:
    static constexpr int Bands = 3;   ///< 500 Hz, 1.4 kHz, 4 kHz

    /**
     * @brief Sets the rate and clears everything.
     * @param[in] sampleRate  the sample rate in Hz
     */
    void prepare( double sampleRate )
    {
        m_rate = sampleRate;
        constexpr float Centres[Bands] = { 500.0f, 1400.0f, 4000.0f };
        for ( int b = 0; b < Bands; ++b )
        {
            const double f = std::min( static_cast<double>( Centres[b] ), 0.45 * sampleRate );
            m_t[b] = static_cast<float>( std::tan( 3.14159265358979 * f / sampleRate ) );
        }
        const float tick = static_cast<float>( Tick / sampleRate );
        m_up = 1.0f - std::exp( -1.0f / static_cast<float>( 0.005 * sampleRate ) );
        m_down = 1.0f - std::exp( -1.0f / static_cast<float>( 0.150 * sampleRate ) );
        m_attackStep = tick / 0.010f;    // of Depth, per tick
        m_releaseStep = tick / 0.150f;
        reset();
    }

    /** @brief Clears the filters, the followers and the dips. */
    void reset()
    {
        for ( int b = 0; b < Bands; ++b )
        {
            m_key[b] = m_pad[b] = Band{};
            m_bell[b] = Band{};
            m_keyPow[b] = m_padPow[b] = 0.0f;
            m_dip[b] = 0.0f;
            setBell( b );
        }
        m_count = 0;
    }

    /**
     * @brief The most the pad's mid dips in a band.
     * @param[in] depthDb  dB (0: off)
     */
    void set( float depthDb ) { m_depth = std::clamp( depthDb, 0.0f, 12.0f ); }

    /**
     * @brief One sample of the pad, in place, under one sample of the key.
     * @param[in,out] l    the pad, left
     * @param[in,out] r    the pad, right
     * @param[in]     key  the lead, mono
     */
    void tick( float &l, float &r, float key )
    {
        if ( m_depth <= 0.0f && m_off ) return;
        float m = 0.5f * ( l + r );
        const float s = 0.5f * ( l - r );
        for ( int b = 0; b < Bands; ++b )
        {
            const float kb = bandPass( m_key[b], b, key ), pb = bandPass( m_pad[b], b, m );
            const float kp = kb * kb, pp = pb * pb;
            m_keyPow[b] += ( kp - m_keyPow[b] ) * ( kp > m_keyPow[b] ? m_up : m_down );
            m_padPow[b] += ( pp - m_padPow[b] ) * ( pp > m_padPow[b] ? m_up : m_down );
        }
        if ( ++m_count >= Tick )
        {
            m_count = 0;
            aim();
        }
        if ( !m_off )
            for ( int b = 0; b < Bands; ++b )
                if ( m_dip[b] > 0.005f ) m = bell( b, m );
        l = m + s;
        r = m - s;
    }

    /**
     * @param[in] band  the band
     * @return its dip now, dB (positive: down)
     */
    float dip( int band ) const { return m_dip[static_cast<size_t>( band )]; }

private:
    static constexpr int Tick = 32;   ///< samples between two aims

    /** @brief A trapezoidal SVF's memory (a band pass for the meters, a bell for the cut). */
    struct Band
    {
        float m_s1 = 0.0f;   ///< the first integrator
        float m_s2 = 0.0f;   ///< the second
        float m_a1 = 1.0f;   ///< the bell's coefficients (Simper)
        float m_a2 = 0.0f;   ///< its second
        float m_a3 = 0.0f;   ///< its third
        float m_m1 = 0.0f;   ///< the band's share of its output
    };

    /**
     * @brief A band pass at the band's centre, Q 0.9.
     * @param[in,out] f  its memory
     * @param[in]     b  the band
     * @param[in]     x  the sample
     * @return the band's sample
     */
    float bandPass( Band &f, int b, float x ) const
    {
        const float g = m_t[b], k = 1.0f / 0.9f;
        const float a1 = 1.0f / ( 1.0f + g * ( g + k ) ), a2 = g * a1, a3 = g * a2;
        const float v3 = x - f.m_s2;
        const float v1 = a1 * f.m_s1 + a2 * v3;
        const float v2 = f.m_s2 + a2 * f.m_s1 + a3 * v3;
        f.m_s1 = 2.0f * v1 - f.m_s1;
        f.m_s2 = 2.0f * v2 - f.m_s2;
        return v1;
    }

    /**
     * @brief The bell's coefficients for its dip now.
     * @param[in] b  the band
     */
    void setBell( int b )
    {
        Band       &f = m_bell[b];
        const float A = std::pow( 10.0f, -m_dip[b] / 40.0f );
        const float g = m_t[b], k = 1.0f / ( 0.9f * A );
        f.m_a1 = 1.0f / ( 1.0f + g * ( g + k ) );
        f.m_a2 = g * f.m_a1;
        f.m_a3 = g * f.m_a2;
        f.m_m1 = k * ( A * A - 1.0f );
    }

    /**
     * @brief One sample through a band's bell.
     * @param[in] b  the band
     * @param[in] x  the sample
     * @return the bell's output
     */
    float bell( int b, float x )
    {
        Band       &f = m_bell[b];
        const float v3 = x - f.m_s2;
        const float v1 = f.m_a1 * f.m_s1 + f.m_a2 * v3;
        const float v2 = f.m_s2 + f.m_a2 * f.m_s1 + f.m_a3 * v3;
        f.m_s1 = 2.0f * v1 - f.m_s1;
        f.m_s2 = 2.0f * v2 - f.m_s2;
        return x + f.m_m1 * v1;
    }

    /** @brief Every Tick samples: the dips' aims, and a step of the dips towards them. */
    void aim()
    {
        bool off = true;
        for ( int b = 0; b < Bands; ++b )
        {
            float want = 0.0f;
            if ( m_depth > 0.0f && m_keyPow[b] > 1.0e-7f )   // -70 dBFS
            {
                const float lead = 10.0f * std::log10( m_keyPow[b] / std::max( m_padPow[b], 1.0e-12f ) );
                want = m_depth * std::clamp( ( lead + 6.0f ) / 12.0f, 0.0f, 1.0f );
            }
            const float step = m_depth * ( want > m_dip[b] ? m_attackStep : m_releaseStep );
            const float next = m_dip[b] + std::clamp( want - m_dip[b], -std::max( step, 0.01f ), std::max( step, 0.01f ) );
            if ( next != m_dip[b] )
            {
                m_dip[b] = next;
                setBell( b );
            }
            if ( m_dip[b] > 0.005f ) off = false;
        }
        m_off = off;
    }

    double                   m_rate = 48000.0;      ///< the sample rate
    float                    m_depth = 0.0f;        ///< the most a band dips, dB
    std::array<float, Bands> m_t{};                 ///< tan(pi f / fs) per band
    std::array<Band, Bands>  m_key{};               ///< the key's band passes
    std::array<Band, Bands>  m_pad{};               ///< the pad's mid's
    std::array<Band, Bands>  m_bell{};              ///< the bells on the pad's mid
    std::array<float, Bands> m_keyPow{};            ///< the key's power per band, followed
    std::array<float, Bands> m_padPow{};            ///< the pad's
    std::array<float, Bands> m_dip{};               ///< the dips now, dB
    float                    m_up = 0.0f;           ///< the followers' attack coefficient
    float                    m_down = 0.0f;         ///< and release
    float                    m_attackStep = 0.0f;   ///< a dip's step down per tick, as a share of Depth
    float                    m_releaseStep = 0.0f;  ///< and back
    int                      m_count = 0;           ///< samples since the last aim
    bool                     m_off = true;          ///< every dip at 0 dB: the bells are not run
};

}   // namespace parh
