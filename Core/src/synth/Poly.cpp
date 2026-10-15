/**
 * @file Poly.cpp
 * @brief Polyphonic engine: voice allocation, supersaw coefficients, rendering.
 * @note Copied from Phosphene `Core/src/Poly.cpp` at 76f7100 (29.09.2026); namespace parh, prefix PARH_.
 * @author Rene Weller
 */
#include "parh/synth/Poly.h"
#include "parh/Params.h"
#include "parh/Util.h"
#include "parh/synth/WaveTableFile.h"
#include <algorithm>
#include <cmath>

namespace parh
{

namespace
{

constexpr double PiD = 3.141592653589793;   ///< pi

/** @brief Pan position of each unison oscillator: low and high partners on opposite sides. */
constexpr double UnisonPan[PolyUnison] = { -1.0, 0.67, -0.33, 0.0, 0.33, -0.67, 1.0 };

/**
 * @brief Frequency factor of a drift of @p cents, as 1 + cents ln2 / 1200.
 *
 * The first two terms of 2^(cents/1200). At the parameter's ceiling of 8 cents the walk practically
 * never leaves +-5 standard deviations, that is +-40 cents, where the truncation error of the
 * expansion is 2.7e-4 -- 0.46 cents. At the default of 1 cent and the +-4 cents a walk actually
 * covers the error is 5.4e-6, that is 0.009 cents, three orders under the smallest pitch difference
 * anyone hears. A pow() per slot per note would be correct to the last bit and would also be fine
 * here; the expansion is kept because it is the same arithmetic on every platform and no library's
 * pow() is.
 * @param[in] cents  the drift in cents
 * @return the frequency factor
 */
inline double driftFactor( double cents )
{
    return 1.0 + cents * ( 0.6931471805599453 / 1200.0 );
}

/**
 * @brief The share of the unison detune the second oscillator's pair plays (24.09.2026).
 *
 * The pair sits on the outermost kept slots, which in a supersaw carry the widest detune (Szabo's outer
 * offset, +-11 % of the curve) -- right for the two quietest of seven saws, wrong for a second oscillator
 * that carries up to half the voice's power in two copies. Seed 5's first pad (a supersaw partner in
 * unison) played every chord tone with a loud pair a third of a semitone either side: 15 %
 * of its peak power more than a quarter tone off the grid, after every FM pad was already back on it.
 * A quarter keeps the pair's stereo beat and puts it within the few cents osc2_detune means (seed 5: +7 .. +15
 * cents, and no first pad of seeds 1..24 with more than a tenth of its power off the grid).
 */
constexpr double Osc2Spread = 0.25;

/**
 * @brief The FM ratio a voice plays: poly.fm_ratio to the nearest half, never under 0.5.
 *
 * A two-operator pair puts its sidebands at f_c (1 +- k r). With r = p/q in lowest terms every one of
 * them is a multiple of f_c / q, so the spectrum is harmonic; with any other r it is not, and that is
 * the bell and the struck metal (Chowning, JAES 21(7), 1973 -- his bell is 1 : 1.4). A half keeps q at 1 or 2:
 * the note's own harmonic series, or the one an octave under it.
 *
 * 24.09.2026, the user on the first track's pad: "klingen einfach nur schraeg und nicht, wie irgend
 * etwas, was ich jemals in einem Psytrance-Song gehoert habe". Measured over the first track of seeds
 * 1..24 (all Full-On, F# Phrygian): in 9 of 24 more than a tenth of the pad's peak power lay more
 * than a quarter tone off the grid, and every one of the FM pads was among them. Seed 1's pad played
 * at 1.91 -- the knob's 2.0 plus its recipe's thickness offset (ComposerControls.cpp, kVoiceLoadings) -- so
 * each chord tone carried partials at 0.91 and 2.91 times itself, a quarter tone off every note of
 * the key, 7 dB under the chord. Every pitched voice plays chords or lines in a key; the metallic FM
 * of the set lives in the percussion (the rim, the zap: perc.fm_ratio is not rounded).
 * @param[in] knob  poly.fm_ratio
 * @return the ratio played
 */
inline double harmonicFmRatio( double knob )
{
    return std::max( 0.5, std::round( knob * 2.0 ) * 0.5 );
}

}   // namespace

double Poly::detuneCurve( double x )
{
    // Szabo (2010), the eleventh-degree fit to the JP-8000's detune, highest power first.
    static constexpr double c[12] = { 10028.7312891634,  -50818.8652045924, 111363.4808729368, -138150.6761080548,
                                      106649.6679158292, -53046.9642751875, 17019.9518580080,  -3425.0836591318,
                                      404.2703938388,    -24.1878824391,    0.6717417634,      0.0030115596 };
    const double            t = std::clamp( x, 0.0, 1.0 );
    double                  y = 0.0;
    for ( double k : c ) y = y * t + k;
    return std::max( 0.0, y );
}

void Poly::mixGains( double x, double &center, double &side )
{
    const double t = std::clamp( x, 0.0, 1.0 );
    center = -0.55366 * t + 0.99785;
    side = -0.73764 * t * t + 1.2841 * t + 0.044372;
}

double Poly::dynamicDetune( double knob, double amount, double lengthBeats )
{
    // 0 at a sixteenth or shorter, 1 from a beat on, linear in log2 of the length in between.
    const double s = std::clamp( std::log2( std::max( lengthBeats, 1e-6 ) / 0.25 ) / 2.0, 0.0, 1.0 );
    return knob * ( 1.0 - std::clamp( amount, 0.0, 1.0 ) * ( 1.0 - s ) );
}

void Poly::prepare( double sampleRate )
{
    m_sr = sampleRate;
    for ( Envelope &e : m_amp ) e.setSampleRate( m_sr );
    for ( Envelope &e : m_fAdsr ) e.setSampleRate( m_sr );
    for ( int v = 0; v < PolyVoices; ++v ) m_mod[v].prepare( m_sr, static_cast<uint64_t>( v ) + 1u );
    m_ampTimes.setSampleRate( m_sr );
    m_delay.prepare( m_sr );
    m_slotL.assign( static_cast<size_t>( PolyBlock * PolySlotCount ), 0.0f );
    m_slotR.assign( static_cast<size_t>( PolyBlock * PolySlotCount ), 0.0f );
    m_chanIn.assign( static_cast<size_t>( PolyBlock * PolyLanes ), 0.0f );
    m_chanAmp.assign( static_cast<size_t>( PolyBlock * PolyLanes ), 0.0f );
    m_chanOut.assign( static_cast<size_t>( PolyBlock * PolyLanes ), 0.0f );
    m_sendBuf.assign( static_cast<size_t>( PolyBlock ), 0.0f );
    m_wtRow.assign( static_cast<size_t>( PolyBlock * PolySlotCount ), 0.0f );
    // Every table this engine can be pointed at is built here, never on the audio thread: a build
    // is ten inverse FFTs per frame. loadWaveTableLibrary() is idempotent, so the three Poly
    // instances share the one load; a missing file is not an error, the built-in tables are the
    // fallback and waveTable() hands one out in its place.
    for ( int i = 0; i < NumBuiltinWaveTables; ++i ) builtinWaveTable( i );
    loadWaveTableLibrary();
    m_table = &waveTable( 0 );
    m_sawTable = &builtinWaveTable( ClassicTable );
    reset();
}

void Poly::reset()
{
    m_slots = PolySlots{};
    m_ch = PolyChannels{};
    m_ch.m_newton = m_filterSteps;   // (the quality outlives a reset, setQuality)
    for ( int s = 0; s < PolySlotCount; ++s )
    {
        m_slots.m_dt[s] = 0.001f;
        m_slots.m_inv[s] = 1000.0f;
        m_slots.m_idxDecay[s] = 1.0f;
        m_slots.m_pw[s] = 0.5f;
        m_wtPh[s] = 0.0;
        m_wtDt[s] = 0.0;
        m_wtLevel[s] = 0;
        m_slotSaw[s] = false;
        m_slotHzMul[s] = 1.0;
        m_slotSpread[s] = 1.0;
    }
    for ( int v = 0; v < PolyVoices; ++v )
    {
        m_amp[v].kill();
        m_fAdsr[v].kill();
        m_mod[v].prepare( m_sr, static_cast<uint64_t>( v ) +
                                    1u );   // (the LFOs from their start as well: the count starts again)
        for ( float &m : m_modSum[v] ) m = 0.0f;
        m_modGainL[v] = m_modGainR[v] = 1.0f;
        m_fenv[v] = 0.0f;
        m_accent[v] = 1.0f;
        m_gate[v] = 0;
        m_age[v] = 0;
        m_pitch[v] = 60;
        m_hpHz[v] = 220.0f;
        m_posEnv[v] = 0.0f;
        m_lfoPh[v] = 0.0;
        m_sawVoice[v] = false;
        m_liveCount[v] = 0;
        m_glidePitch[v] = m_glideTarget[v] = 60.0;
        m_glideY[v] = m_glideScale[v] = m_glideFmRatio[v] = 0.0;
        voiceCoefs( v );
    }
    m_lastPitch = -1.0;
    m_lfo2Ph = 0.0;
    m_lfo2Value = 0.0f;
    m_newest = 0;
    m_counter = 0;
    m_pos = 0;
    m_tableReads = 0;
    seedPhases( 0x504F4C59ull );
    for ( float &d : m_driftSlot ) d = 0.0f;
    for ( float &d : m_driftVoice ) d = 0.0f;
    m_dispL.reset();
    m_dispR.reset();
    m_delay.reset();
}

void Poly::advanceDrift()
{
    for ( int s = 0; s < PolySlotCount; ++s )
        m_driftSlot[s] += ( m_driftRng.bipolar() - m_driftSlot[s] ) * m_driftAlpha;
    for ( int v = 0; v < PolyVoices; ++v ) m_driftVoice[v] += ( m_driftRng.bipolar() - m_driftVoice[v] ) * m_driftAlpha;
}

void Poly::writeSlotPitch( int voice )
{
    const int uFirst = ( PolyUnison - m_unisonLimit ) / 2, uLast = uFirst + m_unisonLimit;
    // The LFO's pitch destination rides here, on the same 16-sample grid the portamento steps on:
    // a factor on the note's frequency, so it reaches the second oscillator's interval as well.
    const double vib =
        m_lfo2Pitch > 0.0f
            ? std::pow( 2.0, static_cast<double>( m_lfo2Pitch ) * static_cast<double>( m_lfo2Value ) / 1200.0 )
            : 1.0;
    // The matrix's pitch destinations (26.09.2026): semitones on the note, and on the second oscillator alone.
    const double f0 =
        midiToHz( m_glidePitch[voice] + static_cast<double>( m_modSum[voice][static_cast<int>( ModDestE::PITCH )] ) ) *
        vib;
    const double o2 =
        m_modSum[voice][static_cast<int>( ModDestE::OSC2_PITCH )] != 0.0f
            ? std::pow( 2.0, static_cast<double>( m_modSum[voice][static_cast<int>( ModDestE::OSC2_PITCH )] ) / 12.0 )
            : 1.0;
    const double y = m_glideY[voice], scale = m_glideScale[voice], fmRatio = m_glideFmRatio[voice];
    // The unison's spread doubled or shut by the matrix (Phase 5b).
    const double det =
        m_modOn && m_mod[voice].targets( ModDestE::DETUNE )
            ? std::clamp( 1.0 + static_cast<double>( m_modSum[voice][static_cast<int>( ModDestE::DETUNE )] ), 0.0, 2.0 )
            : 1.0;
    for ( int u = uFirst; u < uLast; ++u )
    {
        const int    s = voice * PolyUnison + u;
        const double hz = f0 * m_slotHzMul[s] * ( m_slotOsc2[s] ? o2 : 1.0 ) *
                          ( 1.0 + SupersawOffsets[u] * m_slotSpread[s] * y * scale * det ) * m_driftFactorHeld[s];
        const double dt = std::min( hz / m_sr, 0.45 );
        m_slots.m_dt[s] = static_cast<float>( dt );
        m_slots.m_inv[s] = static_cast<float>( 1.0 / dt );
        m_slots.m_mdt[s] = static_cast<float>( std::min( hz * fmRatio / m_sr, 0.45 ) );
        m_wtDt[s] = hz / m_sr;
        m_wtLevel[s] = waveLevelFor( hz, m_sr, -1 );
    }
}

void Poly::advanceGlide()
{
    for ( int v = 0; v < PolyVoices; ++v )
    {
        if ( !m_amp[v].isActive() ) continue;
        const double d = m_glideTarget[v] - m_glidePitch[v];
        if ( d != 0.0 )
        {
            // Within a thousandth of a semitone the bend has arrived: snapping there keeps the pitch of
            // a long held note exactly the note's, so nothing wobbles under a sustained pad or drone.
            m_glidePitch[v] = std::abs( d ) < 1.0e-3 ? m_glideTarget[v] : m_glidePitch[v] + d * m_glideAlpha;
            writeSlotPitch( v );
        }
    }
    // The instance's last sounding pitch, for the next note to start from -- and nothing to start from
    // once every voice has fallen silent, so the first note after a rest arrives without a bend.
    m_lastPitch = -1.0;
    for ( int v = 0; v < PolyVoices; ++v )
        if ( m_amp[v].isActive() )
        {
            m_lastPitch = m_glidePitch[m_newest];
            break;
        }
}

void Poly::update( const float *v, double bpm )
{
    std::copy( v, v + poly::COUNT, m_values );
    m_fDecay = static_cast<float>( std::exp( std::log( 1.0e-3 ) / ( v[poly::FILTER_DECAY] * 0.001 * m_sr ) ) );
    // The modulation (26.09.2026, Modulation.h): the matrix, the LFOs and the envelope, the same for every voice.
    {
        static_assert( poly::MX8_AMOUNT - poly::FILT_ATTACK + 1 == ModBlockSize, "the block's layout (Modulation.h)" );
        const ModSettings ms = readModBlock( v + poly::FILT_ATTACK );
        for ( Modulator &m : m_mod ) m.set( ms );
        m_modOn = m_mod[0].active();
        // The filter envelope as an ADSR once it asks for more than a decay: attack at its minimum and sustain 0 is
        // the exponential decay every voice had, which keeps every older set's sound.
        m_fAdsrOn = v[poly::FILT_ATTACK] > 0.1001f || v[poly::FILT_SUSTAIN] > 0.0f;
        for ( Envelope &e : m_fAdsr )
            e.setTimes( v[poly::FILT_ATTACK] * 0.001f, v[poly::FILTER_DECAY] * 0.001f, v[poly::FILT_SUSTAIN],
                        std::max( 0.005f, v[poly::FILT_RELEASE] * 0.001f ) );
    }
    // Once, then copied: the same numbers setTimes would give each voice, without eight times three exp().
    m_ampTimes.setTimes( v[poly::AMP_ATTACK] * 0.001f, v[poly::AMP_DECAY] * 0.001f, v[poly::AMP_SUSTAIN],
                         std::max( 0.005f, v[poly::AMP_RELEASE] * 0.001f ) );
    refreshEnvelopeTimes();
    // waveTable() addresses the built-in tables and the library with one index, and hands back a
    // built-in when a library table's file is missing -- so this stays a pointer swap with no
    // branch on the audio thread and no chance of a null table.
    m_table = &waveTable( static_cast<int>( std::lround( v[poly::TABLE] ) ) );
    m_posDecay =
        static_cast<float>( std::exp( std::log( 1.0e-3 ) / ( std::max( 1.0f, v[poly::POS_DECAY] ) * 0.001 * m_sr ) ) );
    m_bpm = bpm;
    m_lfoInc = static_cast<float>( bpm / 60.0 / ( std::max( 0.05f, v[poly::POS_LFO_BEATS] ) * m_sr ) );
    // The voice LFO (22.09.2026). Free-running for the instance and tempo-synced, so a held chord
    // breathes as one body and the breathing stays on the bar however long the note is.
    m_lfo2Inc = static_cast<float>( bpm / 60.0 / ( std::max( 0.05f, v[poly::LFO_BEATS] ) * m_sr ) );
    m_lfo2Cut = v[poly::LFO_CUTOFF];
    m_lfo2Pitch = v[poly::LFO_PITCH];
    m_lfo2Amp = std::clamp( v[poly::LFO_AMP], 0.0f, 1.0f );
    // Thermal drift (Poly.h). The walks step once per PolyBlock samples, so their corner frequency
    // is DriftHz against that grid rate; m_driftNorm turns the process's standing deviation,
    // sqrt(alpha / ((2 - alpha) * 3)) for a uniform excitation, into the drift depth in cents.
    const double gridRate = m_sr / static_cast<double>( PolyBlock );
    m_driftAlpha = static_cast<float>( 1.0 - std::exp( -2.0 * PiD * DriftHz / gridRate ) );
    const double sd = std::sqrt( static_cast<double>( m_driftAlpha ) / ( ( 2.0 - m_driftAlpha ) * 3.0 ) );
    m_drift = v[poly::DRIFT];
    m_driftNorm = static_cast<float>( sd > 0.0 ? m_drift / sd : 0.0 );
    m_disperse.set( static_cast<int>( std::lround( v[poly::DISPERSE] ) ), v[poly::DISPERSE_FREQ], m_sr );
    m_send = v[poly::DELAY_SEND];
    m_level = dbToGain( v[poly::LEVEL] );
    // Portamento (Poly.h): a one-pole slew on the pitch, stepped once per PolyBlock samples like the
    // drift walks. The parameter is the time constant, so after glide ms a step has covered 1 - 1/e of
    // its interval and after 0.693 glide exactly half of it -- which is what the self test measures.
    m_glideMs = std::max( 0.0f, v[poly::GLIDE] );
    const double gridPeriod = static_cast<double>( PolyBlock ) / m_sr;
    m_glideAlpha =
        m_glideMs > 0.0f ? static_cast<float>( 1.0 - std::exp( -gridPeriod / ( m_glideMs * 0.001 ) ) ) : 0.0f;
    // Pan, constant power. At the centre cos(pi/4) * sqrt(2) is 1 to the last bit of a double, so a
    // voice that does not pan multiplies by exactly 1.0f and renders as it would without a pan.
    const double pan = std::clamp( static_cast<double>( v[poly::PAN] ), -1.0, 1.0 );
    const double theta = ( pan + 1.0 ) * PiD / 4.0;
    m_panL = static_cast<float>( std::cos( theta ) * std::sqrt( 2.0 ) );
    m_panR = static_cast<float>( std::sin( theta ) * std::sqrt( 2.0 ) );
    const int dl = std::clamp( static_cast<int>( std::lround( v[poly::DELAY_LEFT] ) ), 0, NumDelayTimes - 1 );
    const int dr = std::clamp( static_cast<int>( std::lround( v[poly::DELAY_RIGHT] ) ), 0, NumDelayTimes - 1 );
    m_delay.set( DelayBeats[dl], DelayBeats[dr], bpm, v[poly::DELAY_FEEDBACK], v[poly::DELAY_HIGH_PASS],
                 v[poly::DELAY_LOW_PASS] );
}

bool Poly::tableStale() const
{
    return m_table != &waveTable( static_cast<int>( std::lround( m_values[poly::TABLE] ) ) );
}

void Poly::voiceCoefs( int voice )
{
    // High pass: fixed for the note. The low pass follows its envelope in renderSegment().
    for ( int c = 0; c < 2; ++c )
        svfCoefs( m_hpHz[voice], std::sqrt( 2.0 ), m_sr, m_ch.m_c1[voice * 2 + c], m_ch.m_c2[voice * 2 + c],
                  m_ch.m_c3[voice * 2 + c] );
}

int Poly::activeVoices() const
{
    int n = 0;
    for ( const Envelope &e : m_amp ) n += e.isActive() ? 1 : 0;
    return n;
}

void Poly::noteOn( int pitch, float velocity, double lengthBeats, int gateSamples, double late, bool accent,
                   bool slide )
{
    const float *v = m_values;
    // A free voice, else the one that started longest ago.
    int voice = -1;
    for ( int i = 0; i < PolyVoices && voice < 0; ++i )
        if ( !m_amp[i].isActive() ) voice = i;
    if ( voice < 0 )
    {
        voice = 0;
        for ( int i = 1; i < PolyVoices; ++i )
            if ( m_age[i] < m_age[voice] ) voice = i;
    }
    const bool wasIdle = !m_amp[voice].isActive();
    m_age[voice] = ++m_counter;
    m_pitch[voice] = pitch;
    m_vel[voice] = clampv( velocity, 0.0f, 1.0f );
    m_gate[voice] = std::max( 1, gateSamples );
    // The accent (22.09.2026): the filter envelope reaches half again as far for this note, read by
    // lowPassCoefs on every block of the note. 1 for every unaccented note, so nothing else moves.
    m_accent[voice] = accent ? 1.5f : 1.0f;

    const int osc =
        std::clamp( static_cast<int>( std::lround( v[poly::OSC] ) ), 0, static_cast<int>( PolyOscE::COUNT ) - 1 );
    const double x = dynamicDetune( v[poly::DETUNE], v[poly::DYNAMIC_DETUNE], lengthBeats );
    const double y = detuneCurve( x );
    double       center = 1.0, side = 0.0;
    mixGains( v[poly::MIX], center, side );
    // Portamento (Poly.h): the note's oscillators start at the pitch this instance was last sounding
    // at and slew to the note's own. Without a glide time, without a note before, or on a repeated
    // pitch there is nothing to slew and the oscillators start where they always did -- m_glidePitch is
    // then bit for bit the note's pitch and every number below is the number without a glide.
    m_glideTarget[voice] = static_cast<double>( pitch );
    // `slide` (22.09.2026): a note the composer did not flag starts on its own pitch even with a glide
    // time set -- the lead slides where a tension degree resolves, not on every step.
    const bool bend = slide && m_glideMs > 0.0f && m_lastPitch >= 0.0 && m_lastPitch != static_cast<double>( pitch );
    m_glidePitch[voice] = bend ? m_lastPitch : static_cast<double>( pitch );
    m_glideY[voice] = y;
    m_newest = voice;
    const double f0 = midiToHz( m_glidePitch[voice] );
    // Everything that is a property of the *note* rather than of the moment reads the note's own pitch:
    // the FM index headroom, the tracking high pass and the filter's key tracking. A glide bends the
    // oscillators, not the patch.
    const double f0Note = midiToHz( pitch );
    const double velGain = 1.0 - v[poly::VEL_SENS] + v[poly::VEL_SENS] * m_vel[voice];
    const double width = v[poly::WIDTH];
    const double wave = v[poly::WAVE];
    // Frequency modulation is band-limited by Carson's rule instead of by oversampling: the significant
    // sidebands of a two-operator pair reach f_c + (I + 1) f_m, so an index above
    // ((0.45 sr) - f_c) / f_m - 1 folds its outer sidebands back as aliasing. Measured on 16.09.2026 at
    // C6 with ratio 3.5: index 10 left aliasing 15.7 dB under the carrier, index 2.5 (the knob's
    // default) 103.8 dB under it; at ratio 7.3 the aliasing was 6 dB *above* the carrier. Two-times
    // oversampling with the half-band only moved that to -39 dB and costs a second oscillator pass,
    // while clamping the index costs nothing and is where the classic instruments draw the line as well
    // (Chowning, "The synthesis of complex audio spectra by means of frequency modulation", JAES 21(7),
    // 1973, section 3, on the bandwidth of the modulated spectrum).
    const double fmRatio = harmonicFmRatio( v[poly::FM_RATIO] );
    const double fmRoom = std::max( 0.0, ( 0.45 * m_sr - f0Note ) / ( f0Note * fmRatio ) - 1.0 );
    const double fmI = std::min( static_cast<double>( v[poly::FM_INDEX] ), fmRoom );
    const float  idxDecay =
        static_cast<float>( std::exp( -3.0 / ( std::max( 1.0, v[poly::FM_DECAY] * 0.001 * m_sr ) ) ) );

    double gains[PolyUnison] = {}, detuneScale = 1.0;
    switch ( static_cast<PolyOscE>( osc ) )
    {
    case PolyOscE::SUPERSAW:
    case PolyOscE::WAVETABLE:
        for ( int u = 0; u < PolyUnison; ++u ) gains[u] = u == 3 ? center : side;
        break;
    case PolyOscE::VA:
        gains[2] = gains[4] = 0.5 * side;
        gains[3] = center;
        break;
    case PolyOscE::FM:
        gains[2] = gains[4] = 0.35 * side;
        gains[3] = center;
        detuneScale = 0.5;
        break;
    default: break;
    }
    // The unison limit of the quality level (Quality.h). Only the middle m_unisonLimit of the seven
    // oscillators are set up; the rest get gain 0, dt 0 and no source weight, so their slots produce
    // exactly zero and the render loop can leave them out (renderSegment). The power that normalises
    // the mix is then the power of the kept oscillators alone, which is the same renormalisation the
    // level needs: the voice stays as loud as the full unison. At the default limit uFirst is 0 and
    // uLast is PolyUnison, and every loop below is the loop over all seven it always was.
    const int uFirst = ( PolyUnison - m_unisonLimit ) / 2, uLast = uFirst + m_unisonLimit;

    // The second oscillator (22.09.2026, round "Klangfarben"). A voice is already seven unison slots,
    // each with its own frequency and its own four source weights, so a second oscillator needs no
    // second render pass: it is the outermost pair of those slots given another source, another
    // interval and a share of the power. For the VA and the FM those two slots carry nothing anyway
    // (the switch above leaves gains[0] and gains[6] at zero), so there it is free in the literal
    // sense; for the supersaw and the wavetable it trades the two furthest-detuned copies of the
    // first oscillator for a second one, which is the trade a two-oscillator synth is.
    //
    // The interval is the point rather than the mix: an octave under the note turns a pad into a
    // body, a fifth into an organ, a unison with a few cents of detune into a slow beat -- three
    // sounds a single oscillator cannot make whatever its table is.
    //
    // The pair is the outermost *kept* slots, not slots 0 and 6: under the Quest's unison limit only
    // the middle three are set up at all, and a second oscillator parked on a slot the limit throws
    // away would be inaudible there while still taking its share of the first one's power. It needs
    // three kept slots to exist, so that at least one is left for the first oscillator.
    const int osc2 =
        std::clamp( static_cast<int>( std::lround( v[poly::OSC2] ) ), 0, static_cast<int>( PolyOsc2E::COUNT ) - 1 );
    const double mix2 = osc2 == 0 ? 0.0 : std::clamp( static_cast<double>( v[poly::OSC2_MIX] ), 0.0, 1.0 );
    const bool   two = mix2 > 0.0 && m_unisonLimit >= 3;
    bool         bSlot[PolyUnison] = {};
    if ( two )
    {
        // Equal-power between the two: the first oscillator keeps 1 - mix2 of the power, the pair
        // splits mix2. The `norm` below then brings the whole voice back to the level it had.
        const double keep = std::sqrt( 1.0 - mix2 ), give = std::sqrt( 0.5 * mix2 );
        for ( int u = 0; u < PolyUnison; ++u ) gains[u] *= keep;
        for ( const int u : { uFirst, uLast - 1 } )
        {
            bSlot[u] = true;
            gains[u] = give;
        }
    }
    const int    oscB = two ? osc2 - 1 : 0;   // PolyOsc2 counts Off first; PolyOsc does not
    const int    interval = std::clamp( static_cast<int>( std::lround( v[poly::OSC2_INTERVAL] ) ), 0,
                                        static_cast<int>( PolyOsc2IntervalE::COUNT ) - 1 );
    const double hzMul2 = two ? std::pow( 2.0, ( static_cast<double>( Osc2IntervalSemis[interval] ) +
                                                 static_cast<double>( v[poly::OSC2_DETUNE] ) / 100.0 ) /
                                                   12.0 )
                              : 1.0;

    double power = 0.0;
    for ( int u = uFirst; u < uLast; ++u ) power += gains[u] * gains[u];
    const double norm = power > 0.0 ? 1.0 / std::sqrt( power ) : 0.0;

    // The supersaw reads the mipmapped saw of the Classic table; only the VA blends a PolyBLEP ramp
    // into a pulse. A table frame is normalised to another RMS than the ramp, hence SawTableGain.
    const bool sup = osc == static_cast<int>( PolyOscE::SUPERSAW );
    const bool supB = oscB == static_cast<int>( PolyOscE::SUPERSAW );
    // The fast path (renderSegment: one Classic saw frame, no position, no LFO) needs *every* slot of
    // the voice to be the supersaw. With a second oscillator of another kind the voice takes the
    // general path, where each slot says for itself which table it reads (m_slotSaw).
    m_sawVoice[voice] = sup && ( !two || supB );
    // SawTableGain is per slot since 22.09.2026: with two oscillators one slot may read the Classic
    // saw frame while its neighbour reads the voice's table, and the two are normalised to different
    // RMS values.
    m_glideScale[voice] = detuneScale;
    m_glideFmRatio[voice] = fmRatio;

    for ( int u = 0; u < PolyUnison; ++u )
    {
        const int s = voice * PolyUnison + u;
        // Szabo: a new random phase for every oscillator at every note. Both draws happen for all
        // seven slots even when the limit plays three, so the oscillators that are kept start on the
        // same phases they would have on the desktop level and the two levels stay comparable.
        const float wtRand = m_phaseRng.uniform(), phRand = m_phaseRng.uniform();
        if ( u < uFirst || u >= uLast )
        {
            m_slots.m_dt[s] = m_slots.m_inv[s] = m_slots.m_mdt[s] = 0.0f;
            m_slots.m_ph[s] = m_slots.m_mph[s] = 0.0f;
            m_slots.m_idx[s] = m_slots.m_idxFloor[s] = 0.0f;
            m_slots.m_idxDecay[s] = 1.0f;
            m_slots.m_pw[s] = 0.5f;
            m_slots.m_wSaw[s] = m_slots.m_wPulse[s] = m_slots.m_wFm[s] = m_slots.m_wWt[s] = 0.0f;
            m_slots.m_gL[s] = m_slots.m_gR[s] = 0.0f;
            m_wtPh[s] = m_wtDt[s] = 0.0;
            m_wtLevel[s] = 0;
            m_slotSaw[s] = false;
            m_slotHzMul[s] = 1.0;
            m_slotSpread[s] = 1.0;
            m_slotOsc2[s] = false;
            m_idxFloor0[s] = 0.0f;
            continue;
        }
        // Thermal drift: each unison slot has its own walk, so the seven saws of a supersaw age
        // against each other. The walk is read once here and held for the note (Poly.h) -- held in
        // m_driftFactorHeld since 20.09.2026, because a gliding note rewrites its frequencies later and
        // must keep reading the walk it started with.
        m_driftFactorHeld[s] = driftFactor( slotDrift( s ) );
        // Which of the two oscillators this slot is, and at which pitch (22.09.2026).
        const int slotOsc = bSlot[u] ? oscB : osc;
        m_slotHzMul[s] = bSlot[u] ? hzMul2 : 1.0;
        m_slotOsc2[s] = bSlot[u];
        m_slotSpread[s] = bSlot[u] ? Osc2Spread : 1.0;
        const double hz = f0 * m_slotHzMul[s] * ( 1.0 + SupersawOffsets[u] * m_slotSpread[s] * y * detuneScale ) *
                          m_driftFactorHeld[s];
        const double dt = std::min( hz / m_sr, 0.45 );
        const double mdt = std::min( hz * fmRatio / m_sr, 0.45 );
        m_slots.m_dt[s] = static_cast<float>( dt );
        m_slots.m_inv[s] = static_cast<float>( 1.0 / dt );
        m_slots.m_mdt[s] = static_cast<float>( mdt );
        m_slots.m_pw[s] = v[poly::PULSE_WIDTH];
        const bool slotSup = slotOsc == static_cast<int>( PolyOscE::SUPERSAW );
        const bool fm = slotOsc == static_cast<int>( PolyOscE::FM );
        const bool wt = slotOsc == static_cast<int>( PolyOscE::WAVETABLE ) || slotSup;
        m_slotSaw[s] = slotSup;
        m_slots.m_wWt[s] = wt ? 1.0f : 0.0f;
        m_wtDt[s] = hz / m_sr;
        m_wtLevel[s] = waveLevelFor( hz, m_sr, -1 );
        {
            const double wp = static_cast<double>( wtRand ) + m_wtDt[s] * late;
            m_wtPh[s] = wp - std::floor( wp );
        }
        m_slots.m_wSaw[s] =
            ( fm || wt ) ? 0.0f : static_cast<float>( slotOsc == static_cast<int>( PolyOscE::VA ) ? 1.0 - wave : 1.0 );
        m_slots.m_wPulse[s] = slotOsc == static_cast<int>( PolyOscE::VA ) ? static_cast<float>( wave ) : 0.0f;
        m_slots.m_wFm[s] = fm ? 1.0f : 0.0f;
        m_slots.m_idxFloor[s] = m_idxFloor0[s] = static_cast<float>( 0.3 * fmI );
        m_slots.m_idxDecay[s] = idxDecay;
        const double theta = ( std::clamp( width * UnisonPan[u], -1.0, 1.0 ) + 1.0 ) * PiD / 4.0;
        const double g = gains[u] * norm * velGain * ( slotSup ? static_cast<double>( SawTableGain ) : 1.0 );
        m_slots.m_gL[s] = static_cast<float>( g * std::cos( theta ) * std::sqrt( 2.0 ) );
        m_slots.m_gR[s] = static_cast<float>( g * std::sin( theta ) * std::sqrt( 2.0 ) );
        // `late` samples have already passed since the note ideally started.
        double ph = static_cast<double>( phRand ) + dt * late;
        m_slots.m_ph[s] = static_cast<float>( ph - std::floor( ph ) );
        const double mp = mdt * late;
        m_slots.m_mph[s] = static_cast<float>( mp - std::floor( mp ) );
        m_slots.m_idx[s] = static_cast<float>( 0.7 * fmI * std::pow( static_cast<double>( idxDecay ), late ) );
    }
    // The slots that carry a gain, in unison order: renderSegment sums only these. The others -- outside the
    // unison limit, or the four a VA or FM voice leaves at gain 0 -- render exactly zero, and x + 0 is x.
    m_liveCount[voice] = 0;
    for ( int u = 0; u < PolyUnison; ++u )
    {
        const int s = voice * PolyUnison + u;
        if ( m_slots.m_gL[s] != 0.0f || m_slots.m_gR[s] != 0.0f ) m_liveSlot[voice][m_liveCount[voice]++] = s;
    }
    m_hpHz[voice] = std::max( v[poly::HP_FLOOR], static_cast<float>( v[poly::HP_TRACK] * f0Note ) );
    if ( wasIdle )
    {
        for ( int c = 0; c < 2; ++c )
        {
            const int l = voice * 2 + c;
            m_ch.m_ic1[l] = m_ch.m_ic2[l] = m_ch.m_ha1[l] = m_ch.m_ha2[l] = m_ch.m_hb1[l] = m_ch.m_hb2[l] = 0.0f;
            for ( int j = 0; j < 4; ++j )
                m_ch.m_fv[j][l] = m_ch.m_fs[j][l] = 0.0f;   // the filter models' nodes (26.09.2026)
            m_ch.m_fxp[l] = 0.0f;
        }
    }
    voiceCoefs( voice );
    m_fenv[voice] = static_cast<float>( std::pow( static_cast<double>( m_fDecay ), late ) );
    m_posEnv[voice] = static_cast<float>( std::pow( static_cast<double>( m_posDecay ), late ) );
    m_lfoPh[voice] = 0.0;
    // The voice's own walk: the filter moves by a quarter of the pitch deviation and the attack by
    // one per cent per cent, both held for the note (Poly.h). update() has already given every voice
    // the written envelope times; this replaces them for this voice only, so a voice with drift 0
    // keeps exactly the times update() set.
    const double vDrift = voiceDrift( voice );
    m_driftOct[voice] = static_cast<float>( vDrift * DriftCutoffRatio / 1200.0 );
    if ( m_drift > 0.0f )
    {
        const float attack = v[poly::AMP_ATTACK] * 0.001f * static_cast<float>( 1.0 + vDrift * DriftAttackRatio );
        m_amp[voice].setTimes( std::max( 1.0e-5f, attack ), v[poly::AMP_DECAY] * 0.001f, v[poly::AMP_SUSTAIN],
                               std::max( 0.005f, v[poly::AMP_RELEASE] * 0.001f ) );
    }
    // The modulation (26.09.2026): the note's random value, the envelope and the retriggered LFOs, the sums of the
    // last note gone -- they are evaluated again on the next grid step -- and the filter envelope as an ADSR.
    m_noteRand[voice] = m_modRng.bipolar();
    for ( float &m : m_modSum[voice] ) m = 0.0f;
    m_modGainL[voice] = m_modGainR[voice] = 1.0f;
    m_mod[voice].noteOn( static_cast<int64_t>( m_pos ), m_beat );
    m_lastVoice = voice;
    if ( m_fAdsrOn )
    {
        m_fAdsr[voice].noteOn();
        m_fAdsr[voice].advanceAttack( late );
        m_fenv[voice] = m_fAdsr[voice].level();
    }
    if ( m_modOn ) applyModulation( voice, m_beat );
    lowPassCoefs( voice, 2.0 - 1.9 * std::clamp( static_cast<double>( v[poly::RESONANCE] ), 0.0, 1.0 ) );
    m_amp[voice].noteOn();
    m_amp[voice].advanceAttack( late );
}

void Poly::applyModulation( int voice, double beat )
{
    const float *v = m_values;
    float        ext[ModSources] = {};
    ext[static_cast<int>( ModSourceE::FILTER_ENV )] = m_fenv[voice];
    ext[static_cast<int>( ModSourceE::VELOCITY )] = m_vel[voice];
    ext[static_cast<int>( ModSourceE::KEY )] =
        std::clamp( static_cast<float>( m_pitch[voice] - 60 ) / 24.0f, -1.0f, 1.0f );
    ext[static_cast<int>( ModSourceE::RANDOM )] = m_noteRand[voice];
    fillGlobals( ext, m_modGlob );
    float *sum = m_modSum[voice];
    m_mod[voice].evaluate( static_cast<int64_t>( m_pos ), beat, ext, sum );
    const Modulator &m = m_mod[voice];
    // Pitch: the slots' frequencies (writeSlotPitch reads the sums).
    if ( m.targets( ModDestE::PITCH ) || m.targets( ModDestE::OSC2_PITCH ) || m.targets( ModDestE::DETUNE ) )
        writeSlotPitch( voice );
    const int s0 = voice * PolyUnison;
    if ( m.targets( ModDestE::PULSE_WIDTH ) )
    {
        const float pw =
            std::clamp( v[poly::PULSE_WIDTH] + sum[static_cast<int>( ModDestE::PULSE_WIDTH )], 0.05f, 0.95f );
        for ( int u = 0; u < PolyUnison; ++u )
            if ( m_slots.m_wPulse[s0 + u] != 0.0f ) m_slots.m_pw[s0 + u] = pw;
    }
    if ( m.targets( ModDestE::FM_INDEX ) )
    {
        const float f = std::clamp( 1.0f + sum[static_cast<int>( ModDestE::FM_INDEX )], 0.0f, 2.0f );
        for ( int u = 0; u < PolyUnison; ++u ) m_slots.m_idxFloor[s0 + u] = m_idxFloor0[s0 + u] * f;
    }
    // Level doubled or shut, and the pan as constant power -- both exactly 1 where no slot reaches them.
    const float gain =
        m.targets( ModDestE::LEVEL ) ? std::clamp( 1.0f + sum[static_cast<int>( ModDestE::LEVEL )], 0.0f, 2.0f ) : 1.0f;
    if ( m.targets( ModDestE::PAN ) )
    {
        const double theta =
            ( std::clamp( static_cast<double>( sum[static_cast<int>( ModDestE::PAN )] ), -1.0, 1.0 ) + 1.0 ) * PiD /
            4.0;
        m_modGainL[voice] = gain * static_cast<float>( std::cos( theta ) * std::sqrt( 2.0 ) );
        m_modGainR[voice] = gain * static_cast<float>( std::sin( theta ) * std::sqrt( 2.0 ) );
    }
    else
    {
        m_modGainL[voice] = m_modGainR[voice] = gain;
    }
}

template <class VT>
void Poly::renderSegment( float *L, float *R, int n )
{
    const int    width = laneWidth<VT>();
    const float *v = m_values;
    const double damping = 2.0 - 1.9 * std::clamp( static_cast<double>( v[poly::RESONANCE] ), 0.0, 1.0 );
    // The drift walks step on the absolute grid, for every slot and voice whether it sounds or not:
    // a walk that only advanced while a note was held would depend on the note history and would no
    // longer be a function of the sample index. processWith() cuts every segment at the grid, so this
    // runs exactly once per PolyBlock samples however the host splits its blocks.
    if ( m_drift > 0.0f && m_pos % PolyBlock == 0 ) advanceDrift();
    // The portamento steps on the same grid and for the same reason (Poly.h): a bend that advanced with
    // the host's block boundaries would not be a function of the sample index. At glide 0 nothing here
    // runs and no slot coefficient is touched.
    if ( m_glideMs > 0.0f && m_pos % PolyBlock == 0 ) advanceGlide();
    // The voice LFO (22.09.2026). It steps on the same absolute grid and for the same reason: a
    // modulation that advanced with the host's block boundaries would not be a function of the sample
    // index, and the offline render is the determinism oracle. One sine per 16 samples for the whole
    // instance, whose value the cutoff (lowPassCoefs), the pitch (writeSlotPitch) and the level (the
    // amplitude loop below) all read -- which is why "kostet praktisch nichts" is literally true here.
    const bool lfoOn = m_lfo2Cut > 0.0f || m_lfo2Pitch > 0.0f || m_lfo2Amp > 0.0f;
    if ( lfoOn && m_pos % PolyBlock == 0 )
    {
        m_lfo2Value = static_cast<float>( std::sin( 2.0 * PiD * m_lfo2Ph ) );
        m_lfo2Ph += static_cast<double>( m_lfo2Inc ) * PolyBlock;
        m_lfo2Ph -= std::floor( m_lfo2Ph );
        // The pitch destination has to reach the slots, which only writeSlotPitch touches.
        if ( m_lfo2Pitch > 0.0f )
            for ( int voice = 0; voice < PolyVoices; ++voice )
                if ( m_amp[voice].isActive() ) writeSlotPitch( voice );
    }
    // Tremolo: a gain between 1 - depth and 1, so the LFO can only take level away and a voice never
    // gets louder than the level match measured it at.
    const float lfoGain = m_lfo2Amp > 0.0f ? 1.0f - m_lfo2Amp * 0.5f * ( 1.0f - m_lfo2Value ) : 1.0f;
    bool        voiceOn[PolyVoices] = {};
    for ( int voice = 0; voice < PolyVoices; ++voice )
    {
        voiceOn[voice] = m_amp[voice].isActive();
        if ( !voiceOn[voice] )
        {
            for ( int i = 0; i < n; ++i )
                m_chanAmp[static_cast<size_t>( i * PolyLanes + voice * 2 )] =
                    m_chanAmp[static_cast<size_t>( i * PolyLanes + voice * 2 + 1 )] = 0.0f;
            continue;
        }
        // Low pass from the envelope, renewed on the absolute 16-sample grid only: where the host cuts
        // its blocks must not decide when a coefficient changes. The modulation is evaluated on the same
        // grid, before the coefficients read it (26.09.2026).
        if ( m_pos % PolyBlock == 0 )
        {
            if ( m_modOn ) applyModulation( voice, m_beat );
            lowPassCoefs( voice, damping );
        }
        const float gl = m_modGainL[voice], gr = m_modGainR[voice];
        for ( int i = 0; i < n; ++i )
        {
            if ( m_gate[voice] > 0 && --m_gate[voice] == 0 )
            {
                m_amp[voice].noteOff();
                m_mod[voice].noteOff();
                m_fAdsr[voice].noteOff();
            }
            const float a = m_amp[voice].process() * lfoGain;
            m_chanAmp[static_cast<size_t>( i * PolyLanes + voice * 2 )] = a * gl;
            m_chanAmp[static_cast<size_t>( i * PolyLanes + voice * 2 + 1 )] = a * gr;
            if ( m_fAdsrOn )
                m_fenv[voice] = m_fAdsr[voice].process();
            else
                m_fenv[voice] *= m_fDecay;
            m_mod[voice].tick();
        }
    }
    // The sounding slots, packed (the optimisation pass, 29.09.2026). The kernels run on groups of eight lanes, and
    // with the slots laid out voice by voice (slot = voice x 7 + unison) a voice that sounds three of its seven -- a VA
    // or an FM voice, any voice under the Quest's unison limit -- kept whole groups busy with silent lanes. Now every
    // segment gathers the slots that carry a gain, of the voices that sound, voice by voice in unison order, into the
    // first lanes of m_pk: four VA voices take two groups instead of four, a Quest supersaw voice three lanes instead of
    // seven. A lane computes what it computed in its old place (the arithmetic is lane by lane; which lane does not
    // matter), its state goes back to its slot after the segment, a voice's slots are summed in unison order as before,
    // and a source a group weighs in for another lane adds a zero to this one -- so every render is the render it was.
    int count = 0;
    for ( int voice = 0; voice < PolyVoices; ++voice )
    {
        m_pkFirst[voice] = count;
        if ( !voiceOn[voice] ) continue;
        for ( int k = 0; k < m_liveCount[voice]; ++k ) m_pkSlot[count++] = m_liveSlot[voice][k];
    }
    const int used = ( count + 7 ) & ~7;
    for ( int k = 0; k < used; ++k )
    {
        if ( k < count )
        {
            const int s = m_pkSlot[k];
            m_pk.m_ph[k] = m_slots.m_ph[s];
            m_pk.m_mph[k] = m_slots.m_mph[s];
            m_pk.m_idx[k] = m_slots.m_idx[s];
            m_pk.m_dt[k] = m_slots.m_dt[s];
            m_pk.m_inv[k] = m_slots.m_inv[s];
            m_pk.m_mdt[k] = m_slots.m_mdt[s];
            m_pk.m_idxDecay[k] = m_slots.m_idxDecay[s];
            m_pk.m_idxFloor[k] = m_slots.m_idxFloor[s];
            m_pk.m_pw[k] = m_slots.m_pw[s];
            m_pk.m_wSaw[k] = m_slots.m_wSaw[s];
            m_pk.m_wPulse[k] = m_slots.m_wPulse[s];
            m_pk.m_wFm[k] = m_slots.m_wFm[s];
            m_pk.m_wWt[k] = m_slots.m_wWt[s];
            m_pk.m_gL[k] = m_slots.m_gL[s];
            m_pk.m_gR[k] = m_slots.m_gR[s];
        }
        else
        {
            // The rest of the last group: silent and finite (no step, no weight, no gain).
            m_pk.m_ph[k] = m_pk.m_mph[k] = m_pk.m_idx[k] = m_pk.m_dt[k] = m_pk.m_inv[k] = m_pk.m_mdt[k] =
                m_pk.m_idxFloor[k] = 0.0f;
            m_pk.m_idxDecay[k] = 1.0f;
            m_pk.m_pw[k] = 0.5f;
            m_pk.m_wSaw[k] = m_pk.m_wPulse[k] = m_pk.m_wFm[k] = m_pk.m_wWt[k] = m_pk.m_gL[k] = m_pk.m_gR[k] = 0.0f;
        }
    }
    // Wavetable rows, on the scalar side (a table read is a gather; AVX2 has one, sawReads8): position from the knob,
    // the voice's envelope and its LFO, one position per voice per sample -- into the voice's packed lanes. A lane
    // with no table (another voice, the rest of a group) reads zero: a group that weighs tables in multiplies it by 0.
    const int uFirst = ( PolyUnison - m_unisonLimit ) / 2, uLast = uFirst + m_unisonLimit;
    auto      zeroLanes = [&]( int from, int to )
    {
        for ( int i = 0; i < n; ++i )
            for ( int k = from; k < to; ++k ) m_wtRow[static_cast<size_t>( i * PolySlotCount + k )] = 0.0f;
    };
    zeroLanes( count, used );
    for ( int voice = 0; voice < PolyVoices; ++voice )
    {
        if ( !voiceOn[voice] ) continue;
        const int  s0 = voice * PolyUnison;
        const int  first = m_pkFirst[voice], lanesOf = m_liveCount[voice];
        const int *live = m_liveSlot[voice];
        // The source weights are read from the slots inside the limit: noteOn() clears the weights of
        // the slots outside it, so slot 0 of a limited voice says nothing about its oscillator type.
        // Any of them, not just the first: with a second oscillator (22.09.2026) the outer pair can
        // read tables while the middle does not, or the other way round.
        bool wt = false;
        for ( int u = uFirst; u < uLast && !wt; ++u ) wt = m_slots.m_wWt[s0 + u] != 0.0f;
        if ( !wt )
        {
            zeroLanes( first, first + lanesOf );
            continue;
        }
        m_tableReads += static_cast<uint64_t>( n ) * static_cast<uint64_t>( m_unisonLimit );
        if ( m_sawVoice[voice] )
        {
#if PARH_VEC_PATH == 1
            if constexpr ( laneWidth<VT>() == 8 )
            {
                sawReads8( voice, n );
                continue;
            }
#endif
            // Supersaw: one frame, no position, no LFO -- a single Catmull-Rom read per slot.
            for ( int i = 0; i < n; ++i )
            {
                for ( int k = 0; k < lanesOf; ++k )
                {
                    const int s = live[k];
                    m_wtRow[static_cast<size_t>( i * PolySlotCount + first + k )] =
                        m_sawTable->sample( m_wtLevel[s], ClassicSawFrame, m_wtPh[s] );
                    m_wtPh[s] += m_wtDt[s];
                    if ( m_wtPh[s] >= 1.0 ) m_wtPh[s] -= 1.0;
                }
            }
            continue;
        }
#if PARH_VEC_PATH == 1
        if constexpr ( laneWidth<VT>() == 8 )
        {
            bool saw = false;
            for ( int k = 0; k < lanesOf; ++k ) saw = saw || m_slotSaw[live[k]];
            if ( !saw )
            {
                tableReads8( voice, n );
                continue;
            }
        }
#endif
        for ( int i = 0; i < n; ++i )
        {
            const float lfo = static_cast<float>( std::sin( 2.0 * PiD * m_lfoPh[voice] ) );
            float       pos = v[poly::POSITION] + v[poly::POS_ENV] * m_posEnv[voice] + v[poly::POS_LFO_DEPTH] * lfo;
            // The free slow movement of colour (25.09.2026), kept inside the table: at a position near an end the sine
            // would carry the read outside it. Only where it runs, so every render without it is the one before.
            if ( v[poly::SLOW_MOD] > 0.0f )
                pos = std::clamp( pos + 0.15f * v[poly::SLOW_MOD] * m_slowColour, 0.0f, 1.0f );
            // The matrix's table destination (26.09.2026: "z.B. um durch das Wavetable zu interpolieren").
            if ( m_modOn && m_mod[voice].targets( ModDestE::TABLE_POS ) )
                pos = std::clamp( pos + m_modSum[voice][static_cast<int>( ModDestE::TABLE_POS )], 0.0f, 1.0f );
            m_posEnv[voice] *= m_posDecay;
            m_lfoPh[voice] += m_lfoInc;
            if ( m_lfoPh[voice] >= 1.0 ) m_lfoPh[voice] -= 1.0;
            for ( int k = 0; k < lanesOf; ++k )
            {
                const int s = live[k];
                // Per slot since 22.09.2026: a second oscillator may be the supersaw while the first
                // is a wavetable, and the supersaw reads one frame of the Classic table whatever the
                // Table parameter says (the DSP round's aliasing figures depend on that frame).
                m_wtRow[static_cast<size_t>( i * PolySlotCount + first + k )] =
                    m_slotSaw[s] ? m_sawTable->sample( m_wtLevel[s], ClassicSawFrame, m_wtPh[s] )
                                 : m_table->sampleAt( m_wtLevel[s], pos, m_wtPh[s] );
                m_wtPh[s] += m_wtDt[s];
                if ( m_wtPh[s] >= 1.0 ) m_wtPh[s] -= 1.0;
            }
        }
    }
    // The packed groups, each with the sources its lanes weigh in (the same decision on every vector path), then the
    // lanes' state back to their slots.
    for ( int g = 0; g < used / 8; ++g )
    {
        bool blep = false, fm = false, wt = false;
        for ( int k = g * 8; k < g * 8 + 8 && k < count; ++k )
        {
            blep = blep || m_pk.m_wSaw[k] != 0.0f || m_pk.m_wPulse[k] != 0.0f;
            fm = fm || m_pk.m_wFm[k] != 0.0f;
            wt = wt || m_pk.m_wWt[k] != 0.0f;
        }
        for ( int k = g * 8; k < g * 8 + 8; k += width )
            polySlotKernel<VT>( m_pk, k, n, m_wtRow.data(), blep, fm, wt, m_slotL.data(), m_slotR.data() );
    }
    for ( int k = 0; k < count; ++k )
    {
        const int s = m_pkSlot[k];
        m_slots.m_ph[s] = m_pk.m_ph[k];
        m_slots.m_mph[s] = m_pk.m_mph[k];
        m_slots.m_idx[s] = m_pk.m_idx[k];
    }
    for ( int voice = 0; voice < PolyVoices; ++voice )
    {
        // A silent voice's channels get silence (the optimisation pass, 29.09.2026): its output is its envelope's zero
        // whatever they hold, and its filter starts from zero when it next sounds (noteOn from idle) -- so what its
        // slots would have summed to changes nothing. A sounding voice sums its packed lanes, in unison order: the slots
        // without a gain are exactly zero, and a sum that starts at +0 is not changed by adding a zero of either sign.
        if ( !voiceOn[voice] )
        {
            for ( int i = 0; i < n; ++i )
                m_chanIn[static_cast<size_t>( i * PolyLanes + voice * 2 )] =
                    m_chanIn[static_cast<size_t>( i * PolyLanes + voice * 2 + 1 )] = 0.0f;
            continue;
        }
        const int first = m_pkFirst[voice], lanesOf = m_liveCount[voice];
        for ( int i = 0; i < n; ++i )
        {
            const float *rowL = m_slotL.data() + static_cast<size_t>( i * PolySlotCount + first );
            const float *rowR = m_slotR.data() + static_cast<size_t>( i * PolySlotCount + first );
            float        sl = 0.0f, sr = 0.0f;
            for ( int k = 0; k < lanesOf; ++k )
            {
                sl += rowL[k];
                sr += rowR[k];
            }
            m_chanIn[static_cast<size_t>( i * PolyLanes + voice * 2 )] = sl;
            m_chanIn[static_cast<size_t>( i * PolyLanes + voice * 2 + 1 )] = sr;
        }
    }
    for ( int g = 0; g < PolyLanes / 8; ++g )
    {
        bool on = false;
        for ( int l = g * 8; l < g * 8 + 8; ++l ) on = on || voiceOn[l / 2];
        if ( on )
        {
            for ( int l = g * 8; l < g * 8 + 8; l += width )
                polyChannelKernel<VT>( m_ch, l, n, m_chanIn.data(), m_chanAmp.data(), m_chanOut.data() );
        }
        else
        {
            for ( int i = 0; i < n; ++i )
                for ( int l = g * 8; l < g * 8 + 8; ++l ) m_chanOut[static_cast<size_t>( i * PolyLanes + l )] = 0.0f;
        }
    }
    for ( int i = 0; i < n; ++i )
    {
        float l = 0.0f, r = 0.0f;
        for ( int voice = 0; voice < PolyVoices; ++voice )
        {
            l += m_chanOut[static_cast<size_t>( i * PolyLanes + voice * 2 )];
            r += m_chanOut[static_cast<size_t>( i * PolyLanes + voice * 2 + 1 )];
        }
        if ( m_disperse.m_stages > 0 )
        {
            l = m_dispL.tick( l, m_disperse.m_c, m_disperse.m_d, m_disperse.m_stages );
            r = m_dispR.tick( r, m_disperse.m_c, m_disperse.m_d, m_disperse.m_stages );
        }
        // The delay send is taken from the *unpanned* sum, so a voice's place in the image and its
        // delay's own left/right times stay independent (Poly.h). At the centre m_panL and m_panR are
        // exactly 1 and both lines are the lines without a pan.
        m_sendBuf[static_cast<size_t>( i )] = 0.5f * ( l * m_level + r * m_level ) * m_send;
        L[i] = l * m_level * m_panL;
        R[i] = r * m_level * m_panR;
    }
    m_delay.process( m_sendBuf.data(), L, R, n );
    m_pos += static_cast<uint64_t>( n );
    m_beat += m_beatsPerSample * static_cast<double>( n );
}

#if PARH_VEC_PATH == 1
namespace
{

/** @brief The lanes of one voice's slots: phases, steps, lengths and cycle offsets, lane 7 and the slots outside the limit idle. */
struct TableLanes
{
    __m256d m_phA, m_phB, m_dtA, m_dtB, m_lenA, m_lenB;   ///< phase, step and cycle length, lanes 0-3 and 4-7
    __m256i m_lenM1;                                      ///< the last index of each lane's cycle
    __m256i m_live;                                       ///< all bits where the lane is a slot inside the unison limit
};

/**
 * @brief Catmull-Rom reads of eight lanes, as WaveTable::sample: x = phase * len in double, the index truncated and held
 *        inside the cycle, t = float(x - index), then y1 + t (a + t (b + t d)) in the scalar read's order.
 * @param[in]  l    the eight lanes: phase, step and cycle length
 * @param[out] idx  each lane's index (a second frame reads at the same place)
 * @param[out] t    each lane's fraction
 */
inline void tablePlace( const TableLanes &l, __m256i &idx, __m256 &t )
{
    const __m256d xA = _mm256_mul_pd( l.m_phA, l.m_lenA ), xB = _mm256_mul_pd( l.m_phB, l.m_lenB );
    __m256i       i = _mm256_set_m128i( _mm256_cvttpd_epi32( xB ), _mm256_cvttpd_epi32( xA ) );
    i = _mm256_max_epi32( _mm256_min_epi32( i, l.m_lenM1 ), _mm256_setzero_si256() );
    const __m128 tA = _mm256_cvtpd_ps( _mm256_sub_pd( xA, _mm256_cvtepi32_pd( _mm256_castsi256_si128( i ) ) ) );
    const __m128 tB = _mm256_cvtpd_ps( _mm256_sub_pd( xB, _mm256_cvtepi32_pd( _mm256_extracti128_si256( i, 1 ) ) ) );
    t = _mm256_set_m128( tB, tA );
    idx = i;
}

inline __m256 catmullRom( const float *data, __m256i at, __m256 t )
{
    const __m256i one = _mm256_set1_epi32( 1 );
    const __m256  y0 = _mm256_i32gather_ps( data, _mm256_sub_epi32( at, one ), 4 );
    const __m256  y1 = _mm256_i32gather_ps( data, at, 4 );
    const __m256  y2 = _mm256_i32gather_ps( data, _mm256_add_epi32( at, one ), 4 );
    const __m256  y3 = _mm256_i32gather_ps( data, _mm256_add_epi32( at, _mm256_set1_epi32( 2 ) ), 4 );
    const __m256  half = _mm256_set1_ps( 0.5f );
    const __m256  a = _mm256_mul_ps( half, _mm256_sub_ps( y2, y0 ) );
    // y0 - 2.5 y1 + 2 y2 - 0.5 y3, left to right
    const __m256 b = _mm256_sub_ps( _mm256_add_ps( _mm256_sub_ps( y0, _mm256_mul_ps( _mm256_set1_ps( 2.5f ), y1 ) ),
                                                   _mm256_mul_ps( _mm256_set1_ps( 2.0f ), y2 ) ),
                                    _mm256_mul_ps( half, y3 ) );
    const __m256 d = _mm256_add_ps( _mm256_mul_ps( half, _mm256_sub_ps( y3, y0 ) ),
                                    _mm256_mul_ps( _mm256_set1_ps( 1.5f ), _mm256_sub_ps( y1, y2 ) ) );
    return _mm256_add_ps(
        _mm256_mul_ps( _mm256_add_ps( _mm256_mul_ps( _mm256_add_ps( _mm256_mul_ps( d, t ), b ), t ), a ), t ), y1 );
}

/** @brief phase += step, less one where it reached one (the scalar loop's wrap). */
inline void tableAdvance( TableLanes &l )
{
    const __m256d one = _mm256_set1_pd( 1.0 );
    l.m_phA = _mm256_add_pd( l.m_phA, l.m_dtA );
    l.m_phB = _mm256_add_pd( l.m_phB, l.m_dtB );
    l.m_phA = _mm256_blendv_pd( l.m_phA, _mm256_sub_pd( l.m_phA, one ), _mm256_cmp_pd( l.m_phA, one, _CMP_GE_OQ ) );
    l.m_phB = _mm256_blendv_pd( l.m_phB, _mm256_sub_pd( l.m_phB, one ), _mm256_cmp_pd( l.m_phB, one, _CMP_GE_OQ ) );
}

}   // namespace

void Poly::sawReads8( int voice, int n )
{
    alignas( 32 ) double ph[8] = {}, dt[8] = {}, len[8];
    alignas( 32 ) int    lenM1[8], off[8], live[8];
    const float         *data = m_sawTable->m_data.data();
    const int            lanesOf = m_liveCount[voice];
    for ( int lane = 0; lane < 8; ++lane )
    {
        const bool in = lane < lanesOf;   // (lane 7 never: at most seven slots)
        const int  s = in ? m_liveSlot[voice][lane] : 0;
        const int  level = in ? m_wtLevel[s] : 0;
        const int  l = WaveTable::levelLength( level );
        len[lane] = static_cast<double>( l );
        lenM1[lane] = l - 1;
        off[lane] = static_cast<int>( m_sawTable->cycle( level, ClassicSawFrame ) - data );
        ph[lane] = in ? m_wtPh[s] : 0.0;
        dt[lane] = in ? m_wtDt[s] : 0.0;
        live[lane] = in ? -1 : 0;
    }
    TableLanes    l{ _mm256_load_pd( ph ),
                     _mm256_load_pd( ph + 4 ),
                     _mm256_load_pd( dt ),
                     _mm256_load_pd( dt + 4 ),
                     _mm256_load_pd( len ),
                     _mm256_load_pd( len + 4 ),
                     _mm256_load_si256( reinterpret_cast<const __m256i *>( lenM1 ) ),
                     _mm256_load_si256( reinterpret_cast<const __m256i *>( live ) ) };
    const __m256i base = _mm256_load_si256( reinterpret_cast<const __m256i *>( off ) );
    float        *rows = m_wtRow.data() + m_pkFirst[voice];
    for ( int i = 0; i < n; ++i )
    {
        __m256i idx;
        __m256  t;
        tablePlace( l, idx, t );
        const __m256 y = catmullRom( data, _mm256_add_epi32( base, idx ), t );
        _mm256_maskstore_ps( rows + static_cast<size_t>( i * PolySlotCount ), l.m_live, y );
        tableAdvance( l );
    }
    _mm256_store_pd( ph, l.m_phA );
    _mm256_store_pd( ph + 4, l.m_phB );
    for ( int k = 0; k < lanesOf; ++k ) m_wtPh[m_liveSlot[voice][k]] = ph[k];
}

void Poly::tableReads8( int voice, int n )
{
    const float         *v = m_values;
    const WaveTable     &table = *m_table;
    alignas( 32 ) double ph[8] = {}, dt[8] = {}, len[8];
    alignas( 32 ) int    lenM1[8], first[8], stride[8], live[8];
    const float         *data = table.m_data.data();
    const int            lanesOf = m_liveCount[voice];
    for ( int lane = 0; lane < 8; ++lane )
    {
        const bool in = lane < lanesOf;
        const int  s = in ? m_liveSlot[voice][lane] : 0;
        const int  level = in ? m_wtLevel[s] : 0;
        const int  l = WaveTable::levelLength( level );
        len[lane] = static_cast<double>( l );
        lenM1[lane] = l - 1;
        first[lane] = static_cast<int>( table.cycle( level, 0 ) - data );   // frame f's cycle: first + f * stride
        stride[lane] = l + WaveTable::Guard;
        ph[lane] = in ? m_wtPh[s] : 0.0;
        dt[lane] = in ? m_wtDt[s] : 0.0;
        live[lane] = in ? -1 : 0;
    }
    TableLanes    lanes8{ _mm256_load_pd( ph ),
                          _mm256_load_pd( ph + 4 ),
                          _mm256_load_pd( dt ),
                          _mm256_load_pd( dt + 4 ),
                          _mm256_load_pd( len ),
                          _mm256_load_pd( len + 4 ),
                          _mm256_load_si256( reinterpret_cast<const __m256i *>( lenM1 ) ),
                          _mm256_load_si256( reinterpret_cast<const __m256i *>( live ) ) };
    const __m256i firstV = _mm256_load_si256( reinterpret_cast<const __m256i *>( first ) );
    const __m256i strideV = _mm256_load_si256( reinterpret_cast<const __m256i *>( stride ) );
    const int     frames = table.m_frames;
    for ( int i = 0; i < n; ++i )
    {
        // The position, exactly as the scalar loop takes it (renderSegment).
        const float lfo = static_cast<float>( std::sin( 2.0 * PiD * m_lfoPh[voice] ) );
        float       pos = v[poly::POSITION] + v[poly::POS_ENV] * m_posEnv[voice] + v[poly::POS_LFO_DEPTH] * lfo;
        if ( v[poly::SLOW_MOD] > 0.0f ) pos = std::clamp( pos + 0.15f * v[poly::SLOW_MOD] * m_slowColour, 0.0f, 1.0f );
        if ( m_modOn && m_mod[voice].targets( ModDestE::TABLE_POS ) )
            pos = std::clamp( pos + m_modSum[voice][static_cast<int>( ModDestE::TABLE_POS )], 0.0f, 1.0f );
        m_posEnv[voice] *= m_posDecay;
        m_lfoPh[voice] += m_lfoInc;
        if ( m_lfoPh[voice] >= 1.0 ) m_lfoPh[voice] -= 1.0;
        // WaveTable::sampleAt: the frame and the blend, then two reads at the same place.
        const float p = ( pos < 0.0f ? 0.0f : ( pos > 1.0f ? 1.0f : pos ) ) * static_cast<float>( frames - 1 );
        int         f = static_cast<int>( p );
        if ( f >= frames - 1 ) f = frames - 2 < 0 ? 0 : frames - 2;
        __m256i idx;
        __m256  t;
        tablePlace( lanes8, idx, t );
        const __m256i at =
            _mm256_add_epi32( _mm256_add_epi32( firstV, _mm256_mullo_epi32( _mm256_set1_epi32( f ), strideV ) ), idx );
        __m256 y = catmullRom( data, at, t );
        if ( frames > 1 )
        {
            const __m256 tf = _mm256_set1_ps( p - static_cast<float>( f ) );
            const __m256 b = catmullRom( data, _mm256_add_epi32( at, strideV ), t );
            y = _mm256_add_ps( y, _mm256_mul_ps( tf, _mm256_sub_ps( b, y ) ) );
        }
        _mm256_maskstore_ps( m_wtRow.data() + static_cast<size_t>( i * PolySlotCount + m_pkFirst[voice] ),
                             lanes8.m_live, y );
        tableAdvance( lanes8 );
    }
    _mm256_store_pd( ph, lanes8.m_phA );
    _mm256_store_pd( ph + 4, lanes8.m_phB );
    for ( int k = 0; k < lanesOf; ++k ) m_wtPh[m_liveSlot[voice][k]] = ph[k];
}
#endif

void Poly::lowPassCoefs( int voice, double damping )
{
    const float *v = m_values;
    const double oct = v[poly::ENV_AMOUNT] * m_fenv[voice] * m_accent[voice] +
                       v[poly::KEY_TRACK] * ( m_pitch[voice] - 60 ) / 12.0 + static_cast<double>( m_driftOct[voice] ) +
                       static_cast<double>( m_lfo2Cut ) * static_cast<double>( m_lfo2Value ) +
                       static_cast<double>( v[poly::SLOW_MOD] ) * static_cast<double>( m_slowCut );
    const float *mods = m_modSum[voice];
    const bool   modded = m_modOn && m_mod[voice].active();
    const double modOct = modded && m_mod[voice].targets( ModDestE::CUTOFF )
                              ? static_cast<double>( mods[static_cast<int>( ModDestE::CUTOFF )] )
                              : 0.0;
    const double fc = std::min( static_cast<double>( v[poly::CUTOFF] ) * std::pow( 2.0, oct + modOct ), 0.45 * m_sr );
    // The resonance per voice where a slot moves it (26.09.2026); the instance's damping otherwise.
    const float res = modded && m_mod[voice].targets( ModDestE::RESONANCE )
                          ? std::clamp( v[poly::RESONANCE] + mods[static_cast<int>( ModDestE::RESONANCE )], 0.0f, 1.0f )
                          : std::clamp( v[poly::RESONANCE], 0.0f, 1.0f );
    if ( modded && m_mod[voice].targets( ModDestE::RESONANCE ) ) damping = 2.0 - 1.9 * static_cast<double>( res );
    svfCoefs( fc, damping, m_sr, m_ch.m_a1[voice * 2], m_ch.m_a2[voice * 2], m_ch.m_a3[voice * 2] );
    m_ch.m_a1[voice * 2 + 1] = m_ch.m_a1[voice * 2];
    m_ch.m_a2[voice * 2 + 1] = m_ch.m_a2[voice * 2];
    m_ch.m_a3[voice * 2 + 1] = m_ch.m_a3[voice * 2];
    // The response (PolyKernel.h, PolyChannels::m0): all zero is the low pass.
    const float k = static_cast<float>( damping );
    float       m0 = 0.0f, m1 = 0.0f, m2 = 0.0f;
    switch ( static_cast<PolyFilterE>( std::clamp( static_cast<int>( std::lround( v[poly::FILTER_TYPE] ) ), 0,
                                                   static_cast<int>( PolyFilterE::COUNT ) - 1 ) ) )
    {
    case PolyFilterE::BAND_PASS:
        m1 = k;
        m2 = -1.0f;
        break;
    case PolyFilterE::HIGH_PASS:
        m0 = 1.0f;
        m1 = -k;
        m2 = -2.0f;
        break;
    case PolyFilterE::NOTCH:
        m0 = 1.0f;
        m1 = -k;
        m2 = -1.0f;
        break;
    default: break;
    }
    for ( int c = 0; c < 2; ++c )
    {
        m_ch.m_m0[voice * 2 + c] = m0;
        m_ch.m_m1[voice * 2 + c] = m1;
        m_ch.m_m2[voice * 2 + c] = m2;
    }
    // The filter models (26.09.2026, Filters.h): the same cutoff as g, the resonance as the model's own feedback.
    m_ch.m_model = std::clamp( static_cast<int>( std::lround( v[poly::FILTER_MODEL] ) ), 0, VoiceFilterModels - 1 );
    if ( m_ch.m_model != 0 )
    {
        const FilterModelE fm = static_cast<FilterModelE>( m_ch.m_model - 1 );
        const float        g =
            static_cast<float>( std::tan( PiD * fc / ( 2.0 * m_sr ) ) );   // the models run at twice the rate
        const float fk = FilterVoicing::feedback( fm, res );
        // The level against the state-variable filter, made good (26.09.2026): measured on the lead of the listening
        // seed at resonance 0.2, 0.45 and 0.7, the ladders and cascades lost 2 .. 3.7 dB, the Juno 3.3 .. 6.7, the diode
        // ladder 6.6 .. 8.6 -- the passband a filter's feedback takes, which FilterVoicing::makeup gives back only in
        // part. A line a + b res through those points; the SEM, the Korg35, the Polivoks and the Wasp were within 1 dB.
        static const float TrimA[VoiceFilterModels] = {
            0.0f, 1.32f, 1.28f, 1.94f, 0.0f, 1.28f, 5.8f, 0.4f, 0.0f, 0.0f
        };
        static const float TrimB[VoiceFilterModels] = { 0.0f, 3.4f, 3.6f, 6.8f, 0.0f, 3.6f, 4.0f, 0.0f, 0.0f, 0.0f };
        const float mk = FilterVoicing::makeup( fm, fk ) * dbToGain( TrimA[m_ch.m_model] + TrimB[m_ch.m_model] * res );
        const float mode = std::clamp(
            v[poly::FILTER_MODE] + ( modded ? mods[static_cast<int>( ModDestE::FILTER_MODE )] : 0.0f ), 0.0f, 1.0f );
        const float *w = poleMix( mode );
        for ( int c = 0; c < 2; ++c )
        {
            const int l = voice * 2 + c;
            m_ch.m_fg[l] = g;
            m_ch.m_fk[l] = fk;
            m_ch.m_fmk[l] = mk;
            m_ch.m_fmd[l] = mode;
            for ( int j = 0; j < 5; ++j ) m_ch.m_xw[j][l] = w[j];
        }
    }
}

template <class VT>
void Poly::processWith( float *L, float *R, int n )
{
    int done = 0;
    while ( done < n )
    {
        const int m = std::min( static_cast<int>( PolyBlock - m_pos % PolyBlock ), n - done );
        renderSegment<VT>( L + done, R + done, m );
        done += m;
    }
}

template void Poly::processWith<float>( float *, float *, int );
#if PARH_VEC_PATH != 0
template void Poly::processWith<VecF>( float *, float *, int );
#endif

void Poly::process( float *L, float *R, int n )
{
    processWith<VecF>( L, R, n );
}

}   // namespace parh
