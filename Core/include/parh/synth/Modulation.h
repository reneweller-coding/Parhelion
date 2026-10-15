/**
 * @file Modulation.h
 * @brief A voice's own modulation (26.09.2026): a modulation envelope, four LFOs and an eight-slot modulation matrix,
 *        taken over from the BerlinSchoolGenerator (eph/synth/Modulation.h) on the user's request: "je eine
 *        vollständige ADSR-Hüllkurve für AMP und Filter sollte schon drin sein (vielleicht auch noch ein dritter, für
 *        weitere Modulationen?). Und drei, vier LFOs doch eigentlich auch, z.B. um durch das Wavetable zu
 *        interpolieren." The six melodic voices carry it per voice, the bass and the acid once.
 *
 * **Sources.** The four LFOs (bipolar, -1..1), the modulation envelope and the synth's filter envelope (0..1), the
 * note's velocity (0..1), its key position (-1..1 over four octaves around middle C) and a random value drawn per
 * note (-1..1). **Destinations.** Pitch, the second oscillator's pitch, pulse width, the place in a wavetable, the FM
 * index, cutoff, resonance, the filter's mode, the level and the pan. A slot's amount is -1..1 of the destination's
 * span (modDestSpan): 12 semitones for the pitch (the amount squared, so a vibrato of a few cents sits on the first
 * tenth of the knob), 24 for the second oscillator, 0.45 of the pulse width, the whole table, five octaves of cutoff,
 * the whole of the resonance and the mode, the FM index doubled or shut, the level doubled or shut, the pan from one
 * side to the other.
 *
 * **Determinism.** Everything runs on the synth's absolute sample count and the set's beat, never on the host's
 * blocks: the envelope per sample, the LFOs evaluated at the synth's control steps (the 16-sample grid of Poly). A
 * free LFO's phase advances by the samples between two evaluations; a synced one reads its phase off the beat, so a
 * one-bar sweep opens on every downbeat unless it restarts with each note (retrig). The random shapes draw from a
 * stream of their own.
 *
 * **Fade.** An LFO can come in over a few seconds after each note, as a player reaches for the wheel.
 *
 * **Every voice** (Phase 5b, 29.09.2026; the user: "genügend Modulationsmöglichkeiten in hinreichender Komplexität").
 * The block moves from the six melodic voices to the bass, the 303, the piano, the strings, the choir, the brass and the
 * timpani, each with the destinations its model has (its own list of targets, mapped onto ModDest: the bow's pressure,
 * speed and place, the choir's vowel and its voice's tension, the brass's breath and blare, the hammer's and the
 * mallet's hardness and where they strike ...). Three sources join the voice's own: the **wheel** and the **pressure**
 * (the player's hand: perform.wheel and perform.pressure, MIDI CC 1 and channel pressure) and the score's **energy**
 * (0 .. 10 as 0 .. 1, the section's, PLAN 6.4), so a sound can breathe with the track. The physical models have no
 * filter envelope: their block begins with the modulation envelope (readModCore, 48 knobs). NoteModulation holds a
 * Modulator per note of such an engine and runs it on the engine's own control grid.
 * @note Copied from Phosphene `Core/include/phos/Modulation.h` at 76f7100 (29.09.2026); namespace parh, prefix PARH_.
 * @author Rene Weller
 */
#pragma once
#include "parh/Dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace parh
{

constexpr int Lfos = 4;       ///< LFOs per voice
constexpr int ModSlots = 8;   ///< slots of the modulation matrix

/** @brief The LFOs' shapes (LfoShapeNames). */
enum class LfoShapeE : int
{
    SINE,
    TRIANGLE,
    SAW_UP,
    SAW_DOWN,
    SQUARE,
    SAMPLE_HOLD,
    SMOOTH_RANDOM,
    COUNT
};
/** @brief The matrix's sources (ModSourceNames); the last three are the same for every voice of a deck. */
enum class ModSourceE : int
{
    OFF,
    LFO1,
    LFO2,
    LFO3,
    LFO4,
    MOD_ENV,
    FILTER_ENV,
    VELOCITY,
    KEY,
    RANDOM,
    WHEEL,      ///< perform.wheel (MIDI CC 1), 0..1
    PRESSURE,   ///< perform.pressure (channel pressure), 0..1
    ENERGY,     ///< the score's energy at the beat, 0..1 (Score::sections)
    COUNT
};
/**
 * @brief The matrix's destinations. The polyphonic voice offers Off .. Detune in this order (ModDestNames); every
 *        other engine a list of its own mapped onto these (SynthModDests ... TimpaniModDests).
 */
enum class ModDestE : int
{
    OFF,
    PITCH,
    OSC2_PITCH,
    PULSE_WIDTH,
    TABLE_POS,
    FM_INDEX,
    CUTOFF,
    RESONANCE,
    FILTER_MODE,
    LEVEL,
    PAN,
    DETUNE,       ///< the unison's spread, doubled or shut
    DRIVE,        ///< the mono synth's drive, 0..1 added
    ENV_AMOUNT,   ///< the mono synth's filter envelope, octaves added
    PRESSURE,     ///< the bow's force, the breath: a factor 2^x
    SPEED,        ///< the bow's speed: a factor 2^x
    POSITION,     ///< the bow's place, the hammer's or the mallet's strike point: added (the knob's -1..1 or 0..1)
    VIB_DEPTH,    ///< the vibrato's depth: a factor 1 + x
    VIB_RATE,     ///< the vibrato's rate: a factor 2^x
    HARDNESS,     ///< the hammer's or the mallet's felt: a factor 2^x
    VOWEL,        ///< the choir's vowel, added
    TENSION,      ///< the voice's quality (Rd), added
    BREATH,       ///< the aspiration, added
    FORMANT,      ///< the formants moved, a factor 2^(x/2) (a tract shorter or longer)
    BRASSINESS,   ///< the blare, added
    DECAY,        ///< the modes' decay: a factor 2^x
    COUNT
};
constexpr int ModSources = static_cast<int>( ModSourceE::COUNT );   ///< sources including Off
constexpr int ModDests = static_cast<int>( ModDestE::COUNT );       ///< destinations including Off
constexpr int LfoSyncs = 10;                                        ///< entries of LfoSyncNames

static_assert( ModDests <= 32, "a slot's destination is a bit of an unsigned mask" );

/** @brief The sources every voice of a deck shares (Deck::updateCell). */
struct ModGlobals
{
    float m_wheel = 0.0f;      ///< perform.wheel
    float m_pressure = 0.0f;   ///< perform.pressure
    float m_energy = 0.0f;     ///< the score's energy / 10
};

/**
 * @brief Cycles per beat of an LFO's sync division (LfoSyncNames: free, 4, 2, 1 bars, 1/2 .. 1/16, 1/4T, 1/8T); 0:
 *        free.
 * @param[in] sync  the sync division
 * @return the cycles per beat, 0 for free
 */
inline double lfoCyclesPerBeat( int sync )
{
    static const double k[LfoSyncs] = { 0.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 1.5, 3.0 };
    return k[std::clamp( sync, 0, LfoSyncs - 1 )];
}

/** @brief One LFO's settings. */
struct LfoSettings
{
    float m_rateHz = 1.0f;   ///< the free rate
    int   m_shape = 0;       ///< LfoShape
    int   m_sync = 0;        ///< tempo division (lfoCyclesPerBeat), 0 free
    int   m_retrig = 0;      ///< 1: every note restarts it
    float m_fadeS = 0.0f;    ///< it comes in over this long after each note
};
/** @brief One slot of the matrix. */
struct ModSlot
{
    int   m_src = 0;         ///< ModSource
    int   m_dst = 0;         ///< ModDest
    float m_amount = 0.0f;   ///< -1..1 of the destination's span
};
/** @brief A voice's modulation settings: the modulation envelope, the LFOs, the matrix. */
struct ModSettings
{
    float       m_attackMs = 10.0f;     ///< the modulation envelope's attack, ms
    float       m_decayMs = 500.0f;     ///< its decay, ms
    float       m_sustain = 0.0f;       ///< its sustain, 0..1
    float       m_releaseMs = 300.0f;   ///< its release, ms
    LfoSettings m_lfo[Lfos];            ///< the four LFOs
    ModSlot     m_slot[ModSlots];       ///< the matrix
};

/**
 * @brief The modulation block's layout in a module's parameter table (Params.h; 26.09.2026): the filter envelope's
 *        attack, sustain and release, the modulation envelope's four times, four LFOs of five knobs (rate, shape,
 *        sync, retrig, fade) and eight slots of three (source, target, amount) -- 51 knobs from the block's first.
 */
constexpr int ModBlockMenv = 3;     ///< the modulation envelope's first knob in the block
constexpr int ModBlockLfo = 7;      ///< the first LFO's first knob
constexpr int ModBlockSlots = 27;   ///< the first slot's first knob
constexpr int ModBlockSize = 51;    ///< knobs in the block
/** @brief The block without the filter envelope (every engine but Poly): from the modulation envelope's attack, 48 knobs. */
constexpr int ModCoreSize = ModBlockSize - ModBlockMenv;

/**
 * @brief The ModSettings a module's knobs ask for, from the modulation envelope's first knob on (the block's core).
 * @param[in] m         the core's first knob (MenvAttack) in the module's effective values
 * @param[in] dstMap    where a synth offers its own destinations: its target list's index -> ModDest, or null
 * @param[in] dstCount  entries of @p dstMap
 * @return the modulation settings
 */
inline ModSettings readModCore( const float *m, const int *dstMap = nullptr, int dstCount = 0 )
{
    const float *b = m - ModBlockMenv;   // (the offsets below count from the filter envelope's first knob)
    ModSettings  ms;
    ms.m_attackMs = b[ModBlockMenv];
    ms.m_decayMs = b[ModBlockMenv + 1];
    ms.m_sustain = b[ModBlockMenv + 2];
    ms.m_releaseMs = b[ModBlockMenv + 3];
    for ( int l = 0; l < Lfos; ++l )
    {
        const float *f = b + ModBlockLfo + 5 * l;
        ms.m_lfo[l].m_rateHz = f[0];
        ms.m_lfo[l].m_shape = static_cast<int>( std::lround( f[1] ) );
        ms.m_lfo[l].m_sync = static_cast<int>( std::lround( f[2] ) );
        ms.m_lfo[l].m_retrig = f[3] >= 0.5f ? 1 : 0;
        ms.m_lfo[l].m_fadeS = f[4];
    }
    for ( int k = 0; k < ModSlots; ++k )
    {
        const float *f = b + ModBlockSlots + 3 * k;
        ms.m_slot[k].m_src = static_cast<int>( std::lround( f[0] ) );
        const int d = static_cast<int>( std::lround( f[1] ) );
        ms.m_slot[k].m_dst = dstMap == nullptr ? d : dstMap[std::clamp( d, 0, dstCount - 1 )];
        ms.m_slot[k].m_amount = f[2];
    }
    return ms;
}

/**
 * @brief The same from the whole block's first knob (Poly's FiltAttack).
 * @param[in] b         the block's first knob in the module's effective values
 * @param[in] dstMap    the synth's own destinations, or null
 * @param[in] dstCount  their count
 * @return the modulation settings
 */
inline ModSettings readModBlock( const float *b, const int *dstMap = nullptr, int dstCount = 0 )
{
    return readModCore( b + ModBlockMenv, dstMap, dstCount );
}

/**
 * @brief The span of each destination for an amount of 1 (see the file comment); the pitch's amount is squared.
 * @param[in] d  the destination
 * @return its span for an amount of 1
 */
inline float modDestSpan( ModDestE d )
{
    switch ( d )
    {
    case ModDestE::PITCH: return 12.0f;
    case ModDestE::OSC2_PITCH: return 24.0f;
    case ModDestE::PULSE_WIDTH: return 0.45f;
    case ModDestE::CUTOFF: return 5.0f;
    case ModDestE::ENV_AMOUNT: return 3.0f;
    case ModDestE::OFF: return 0.0f;
    default: return 1.0f;
    }
}

/**
 * @brief One voice's modulation: the envelope, the LFOs, the matrix (see the file comment).
 *
 * Use: set() when the settings change; noteOn() / noteOff() with the voice's notes; tick() every sample (the
 * envelope); evaluate() at the voice's control steps, which returns the sums per destination.
 */
class Modulator
{
public:
    /**
     * @brief The sample rate and the seed of the random shapes' stream.
     * @param[in] sampleRate  the sample rate in Hz
     * @param[in] seed        the seed of the random shapes
     */
    void prepare( double sampleRate, uint64_t seed )
    {
        m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        m_env.setSampleRate( m_sr );
        m_env.kill();
        for ( int l = 0; l < Lfos; ++l )
        {
            m_lfo[l] = LfoState{};
            m_lfo[l].m_rng.seed( mixSeed( seed, 0x4C464F00ull + static_cast<uint64_t>( l ) ) );
        }
        m_lastAt = 0;
        m_noteAt = std::numeric_limits<int64_t>::min() / 2;
        set( m_ );
    }
    /**
     * @brief Takes new settings; the LFOs run on.
     * @param[in] m  the settings
     */
    void set( const ModSettings &m )
    {
        m_ = m;
        m_env.setTimes( m.m_attackMs * 0.001f, m.m_decayMs * 0.001f, std::clamp( m.m_sustain, 0.0f, 1.0f ),
                        m.m_releaseMs * 0.001f );
        m_lfoUsed = 0u;
        m_dstMask = 0u;
        m_envUsed = false;
        m_live = 0;
        for ( const ModSlot &s : m.m_slot )
        {
            const int src = std::clamp( s.m_src, 0, ModSources - 1 ), dst = std::clamp( s.m_dst, 0, ModDests - 1 );
            if ( src == 0 || dst == 0 || s.m_amount == 0.0f ) continue;
            const ModDestE d = static_cast<ModDestE>( dst );
            const float    a = std::clamp( s.m_amount, -1.0f, 1.0f );
            Live          &l = m_liveSlots[m_live++];
            l.m_src = src;
            l.m_dst = dst;
            l.m_amount = ( d == ModDestE::PITCH ? a * std::fabs( a ) : a ) * modDestSpan( d );
            m_dstMask |= 1u << dst;
            if ( src >= static_cast<int>( ModSourceE::LFO1 ) && src <= static_cast<int>( ModSourceE::LFO4 ) )
                m_lfoUsed |= 1u << ( src - static_cast<int>( ModSourceE::LFO1 ) );
            m_envUsed = m_envUsed || src == static_cast<int>( ModSourceE::MOD_ENV );
        }
        for ( int l = 0; l < Lfos; ++l )
        {
            m_lfo[l].m_inc = std::clamp( static_cast<double>( m.m_lfo[l].m_rateHz ), 0.0, 100.0 ) / m_sr;
            m_lfo[l].m_cpb = lfoCyclesPerBeat( m.m_lfo[l].m_sync );
        }
    }
    /**
     * @brief Whether any slot is live.
     * @return true when any slot is live
     */
    bool active() const { return m_live > 0; }
    /**
     * @brief Whether a live slot reaches @p d.
     * @param[in] d  the destination
     * @return true when a live slot reaches it
     */
    bool targets( ModDestE d ) const { return ( ( m_dstMask >> static_cast<int>( d ) ) & 1u ) != 0u; }
    /**
     * @brief A new note at sample @p at, beat @p beat: the envelope's attack, the retriggered LFOs from their start,
     *        the fades from 0.
     * @param[in] at    the note's sample
     * @param[in] beat  its beat
     */
    void noteOn( int64_t at, double beat )
    {
        m_env.noteOn();
        m_noteAt = at;
        advance( at );
        for ( int l = 0; l < Lfos; ++l )
        {
            if ( m_.m_lfo[l].m_retrig == 0 ) continue;
            LfoState &s = m_lfo[l];
            // The next whole cycle begins here (a new cycle also draws a new random value).
            if ( s.m_cpb > 0.0 )
                s.m_offset = beat * s.m_cpb - ( std::floor( beat * s.m_cpb - s.m_offset ) + 1.0 );
            else
                s.m_cyc = std::floor( s.m_cyc ) + 1.0;
        }
    }
    /** @brief The note's end: the envelope's release. */
    void noteOff() { m_env.noteOff(); }
    /** @brief Silences the envelope. */
    void kill() { m_env.kill(); }
    /** @brief One sample of the envelope (only when a slot reads it). */
    void tick()
    {
        if ( m_envUsed ) m_env.process();
    }
    /**
     * @brief @p n samples of the envelope at once (the voice's 16-sample grid).
     * @param[in] n  the samples
     */
    void tick( int n )
    {
        if ( m_envUsed )
            for ( int i = 0; i < n; ++i ) m_env.process();
    }
    /**
     * @brief The sums per destination at sample @p at, beat @p beat.
     * @param[in]     at    the synth's absolute sample count;
     * @param[in]     beat  the set's beat there
     * @param[in]     ext   the voice's own sources, indexed by ModSource (FilterEnv, Velocity, Key, Random are read)
     * @param[in,out] out   ModDests sums, overwritten
     */
    void evaluate( int64_t at, double beat, const float *ext, float *out )
    {
        float src[ModSources] = {};
        for ( int i = 0; i < ModSources; ++i ) src[i] = ext[i];
        src[0] = 0.0f;
        src[static_cast<int>( ModSourceE::MOD_ENV )] = m_env.level();
        advance( at );
        for ( int l = 0; l < Lfos; ++l )
            if ( ( ( m_lfoUsed >> l ) & 1u ) != 0u )
                src[static_cast<int>( ModSourceE::LFO1 ) + l] = value( l, at, beat );
        for ( int d = 0; d < ModDests; ++d ) out[d] = 0.0f;
        for ( int i = 0; i < m_live; ++i )
            out[m_liveSlots[i].m_dst] += src[m_liveSlots[i].m_src] * m_liveSlots[i].m_amount;
    }
    /**
     * @brief The envelope's level (for tests).
     * @return the envelope's level
     */
    float envLevel() const { return m_env.level(); }
    /**
     * @brief @p sum of destination @p d where a slot reaches it, else nothing (for the synths' per-destination reads).
     * @param[in] sums  the sums per destination
     * @param[in] d     the destination
     * @return its sum where a slot reaches it, else 0
     */
    float offset( const float *sums, ModDestE d ) const
    {
        return active() && targets( d ) ? sums[static_cast<int>( d )] : 0.0f;
    }

private:
    /** @brief One LFO's running state. */
    struct LfoState
    {
        double  m_cyc = 0.0;      ///< a free LFO's cycles
        double  m_offset = 0.0;   ///< a synced one's shift against the beat (retrig)
        double  m_inc = 0.0;      ///< cycles per sample, free
        double  m_cpb = 0.0;      ///< cycles per beat, synced (0: free)
        int64_t m_cycle =
            std::numeric_limits<int64_t>::min();   ///< the whole cycle last read (the random shapes draw on a new one)
        float m_from = 0.0f;                       ///< the random shapes' last value
        float m_to = 0.0f;                         ///< ... and the next
        Rng   m_rng;                               ///< the random shapes' stream
    };
    /** @brief A slot that reaches something, with its amount already scaled by the span. */
    struct Live
    {
        int   m_src = 0;         ///< its source (ModSource)
        int   m_dst = 0;         ///< its destination (ModDest)
        float m_amount = 0.0f;   ///< its amount, scaled by the destination's span
    };

    /**
     * @brief The free LFOs run on to sample @p at (read or not).
     * @param[in] at  the sample to run on to
     */
    void advance( int64_t at )
    {
        const double n = static_cast<double>( at - m_lastAt );
        for ( LfoState &s : m_lfo )
            if ( s.m_cpb <= 0.0 ) s.m_cyc += n * s.m_inc;
        m_lastAt = at;
    }
    /**
     * @brief LFO @p l's value at sample @p at, beat @p beat.
     * @param[in] l     the LFO
     * @param[in] at    the sample
     * @param[in] beat  the beat
     * @return its value
     */
    float value( int l, int64_t at, double beat )
    {
        LfoState     &s = m_lfo[l];
        const double  c = s.m_cpb > 0.0 ? beat * s.m_cpb - s.m_offset : s.m_cyc;
        const double  whole = std::floor( c );
        const float   p = static_cast<float>( c - whole );
        const int64_t w = static_cast<int64_t>( whole );
        if ( w != s.m_cycle )
        {
            s.m_cycle = w;
            s.m_from = s.m_to;
            s.m_to = s.m_rng.bipolar();
        }
        float v = 0.0f;
        switch (
            static_cast<LfoShapeE>( std::clamp( m_.m_lfo[l].m_shape, 0, static_cast<int>( LfoShapeE::COUNT ) - 1 ) ) )
        {
        case LfoShapeE::SINE: v = sin01( p ); break;
        case LfoShapeE::TRIANGLE: v = p < 0.25f ? 4.0f * p : ( p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f ); break;
        case LfoShapeE::SAW_UP: v = 2.0f * p - 1.0f; break;
        case LfoShapeE::SAW_DOWN: v = 1.0f - 2.0f * p; break;
        case LfoShapeE::SQUARE: v = p < 0.5f ? 1.0f : -1.0f; break;
        case LfoShapeE::SAMPLE_HOLD: v = s.m_to; break;
        case LfoShapeE::SMOOTH_RANDOM: v = s.m_from + ( s.m_to - s.m_from ) * p * p * ( 3.0f - 2.0f * p ); break;
        default: break;
        }
        const float fade = m_.m_lfo[l].m_fadeS;
        if ( fade > 0.0f )
        {
            const double t = static_cast<double>( at - m_noteAt ) / ( static_cast<double>( fade ) * m_sr );
            if ( t < 1.0 ) v *= static_cast<float>( std::max( 0.0, t ) );
        }
        return v;
    }

    double      m_sr = 48000.0;          ///< sample rate
    ModSettings m_;                      ///< the settings
    Envelope    m_env;                   ///< the modulation envelope
    LfoState    m_lfo[Lfos];             ///< the LFOs
    Live        m_liveSlots[ModSlots];   ///< the slots that reach something
    int         m_live = 0;              ///< how many
    unsigned    m_lfoUsed = 0u;          ///< which LFOs a slot reads (bit l)
    unsigned    m_dstMask = 0u;          ///< which destinations a slot reaches (bit d)
    bool        m_envUsed = false;       ///< a slot reads the envelope
    int64_t     m_lastAt = 0;            ///< the sample the free LFOs ran to
    int64_t     m_noteAt = 0;            ///< the last note's sample (the fades)
};

/**
 * @brief The modulation of an engine's @p N notes (Phase 5b): a Modulator per note, the sums per destination, the
 *        note's own sources and the deck's shared ones, the beat on the engine's own sample count.
 *
 * Use: prepare(); set() from update(); setClock() from the deck's cell (the engine's sample count and the beat
 * there); noteOn()/noteOff() with the engine's notes; evaluate() for a sounding note at the engine's control grid
 * (its absolute samples, so every block size gives the same sums); then read with get().
 */
template <int N>
class NoteModulation
{
public:
    /**
     * @brief Sets the sample rate and the seed of every note's LFOs and of the random source; clears the sums.
     * @param[in] sampleRate  the sample rate
     * @param[in] seed        the seed of the LFOs and the random source
     */
    void prepare( double sampleRate, uint64_t seed )
    {
        m_sr = sampleRate;
        m_seed = seed;
        for ( int i = 0; i < N; ++i )
        {
            m_mod[i].prepare( sampleRate, mixSeed( seed, 0x4E4F5445ull + static_cast<uint64_t>( i ) ) );   // "NOTE"
            m_lastAt[i] = 0;
            for ( float &s : m_sum[i] ) s = 0.0f;
        }
        m_rng.seed( mixSeed( seed, 0x524E44ull ) );   // "RND"
        m_clockAt = 0;
        m_clockBeat = 0.0;
    }
    /** @brief Back to the start (the engine's sample count starts again): the LFOs, the envelopes, the random stream; the settings stay. */
    void reset() { prepare( m_sr, m_seed ); }
    /**
     * @brief New settings (every note the same).
     * @param[in] ms  the settings
     */
    void set( const ModSettings &ms )
    {
        for ( Modulator &m : m_mod ) m.set( ms );
        m_on = m_mod[0].active();
    }
    /**
     * @brief Whether any slot is live.
     * @return true when a slot is live
     */
    bool on() const { return m_on; }
    /**
     * @brief The deck's shared sources.
     * @param[in] g  the shared sources
     */
    void setGlobals( const ModGlobals &g ) { m_glob = g; }
    /**
     * @brief The beat at the engine's sample @p at, and the tempo in beats per sample.
     * @param[in] at              the engine's sample
     * @param[in] beat            the beat there
     * @param[in] beatsPerSample  the beats per sample
     */
    void setClock( int64_t at, double beat, double beatsPerSample )
    {
        m_clockAt = at;
        m_clockBeat = beat;
        m_bps = beatsPerSample;
    }
    /**
     * @brief Note @p i begins at sample @p at.
     * @param[in] i         the note's slot
     * @param[in] at        the engine's sample
     * @param[in] pitch     the MIDI note
     * @param[in] velocity  the velocity, 0..1
     */
    void noteOn( int i, int64_t at, int pitch, float velocity )
    {
        m_vel[i] = velocity;
        m_key[i] = std::clamp( static_cast<float>( pitch - 60 ) / 24.0f, -1.0f, 1.0f );
        m_rnd[i] = m_rng.bipolar();   // (drawn for every note, live or not: the stream stays put)
        m_lastAt[i] = at;
        m_mod[i].noteOn( at, beatAt( at ) );
        for ( float &s : m_sum[i] ) s = 0.0f;
    }
    /**
     * @brief Note @p i is released.
     * @param[in] i  the note's slot
     */
    void noteOff( int i ) { m_mod[i].noteOff(); }
    /**
     * @brief The sums of note @p i at sample @p at (its envelope run on to there).
     * @param[in] i          the note's slot
     * @param[in] at         the engine's sample
     * @param[in] filterEnv  the filter envelope's level
     */
    void evaluate( int i, int64_t at, float filterEnv = 0.0f )
    {
        if ( !m_on ) return;
        const int64_t n = at - m_lastAt[i];
        if ( n > 0 ) m_mod[i].tick( static_cast<int>( std::min<int64_t>( n, 1 << 20 ) ) );
        m_lastAt[i] = at;
        float ext[ModSources] = {};
        ext[static_cast<int>( ModSourceE::FILTER_ENV )] = filterEnv;
        ext[static_cast<int>( ModSourceE::VELOCITY )] = m_vel[i];
        ext[static_cast<int>( ModSourceE::KEY )] = m_key[i];
        ext[static_cast<int>( ModSourceE::RANDOM )] = m_rnd[i];
        ext[static_cast<int>( ModSourceE::WHEEL )] = m_glob.m_wheel;
        ext[static_cast<int>( ModSourceE::PRESSURE )] = m_glob.m_pressure;
        ext[static_cast<int>( ModSourceE::ENERGY )] = m_glob.m_energy;
        m_mod[i].evaluate( at, beatAt( at ), ext, m_sum[i] );
    }
    /**
     * @brief Note @p i's sum for @p d (0 where no slot reaches it).
     * @param[in] i  the note's slot
     * @param[in] d  the destination
     * @return the sum
     */
    float get( int i, ModDestE d ) const
    {
        return m_on && m_mod[i].targets( d ) ? m_sum[i][static_cast<int>( d )] : 0.0f;
    }
    /**
     * @brief Whether a live slot reaches @p d.
     * @param[in] d  the destination
     * @return true when a live slot reaches it
     */
    bool targets( ModDestE d ) const { return m_on && m_mod[0].targets( d ); }
    /**
     * @brief The beat at sample @p at.
     * @param[in] at  the engine's sample
     * @return the beat
     */
    double beatAt( int64_t at ) const { return m_clockBeat + static_cast<double>( at - m_clockAt ) * m_bps; }

private:
    double     m_sr = 48000.0;            ///< the sample rate, Hz
    uint64_t   m_seed = 1;                ///< the seed
    Modulator  m_mod[N];                  ///< a Modulator per note
    float      m_sum[N][ModDests] = {};   ///< per note: the sums per destination
    float      m_vel[N] = {};             ///< per note: its velocity (a source)
    float      m_key[N] = {};             ///< per note: its key around C4, -1 .. 1 (a source)
    float      m_rnd[N] = {};             ///< per note: its random value, -1 .. 1 (a source)
    int64_t    m_lastAt[N] = {};          ///< per note: the sample its envelope ran to
    ModGlobals m_glob;                    ///< the deck's shared sources
    Rng        m_rng;                     ///< the random source's stream
    bool       m_on = false;              ///< a slot is live
    int64_t    m_clockAt = 0;             ///< the engine sample of the last setClock()
    double     m_clockBeat = 0.0;         ///< the beat there
    double     m_bps = 0.0;               ///< beats per sample
};

/**
 * @brief Fills the shared sources into a voice's own (Poly::applyModulation).
 * @param[out] ext  the voice's sources
 * @param[in]  g    the shared sources
 */
inline void fillGlobals( float *ext, const ModGlobals &g )
{
    ext[static_cast<int>( ModSourceE::WHEEL )] = g.m_wheel;
    ext[static_cast<int>( ModSourceE::PRESSURE )] = g.m_pressure;
    ext[static_cast<int>( ModSourceE::ENERGY )] = g.m_energy;
}

/** @name The engines' target lists (their knobs' choices, Params.cpp) mapped onto ModDest
 *  @{ */
inline constexpr int SynthModDests[] = {
    0, 1, 3, 6, 7, 13, 12, 9, 10
};   ///< Off Pitch PW Cutoff Res EnvAmt Drive Level Pan
inline constexpr int PianoModDests[] = { 0, 1, 19, 9, 10 };   ///< Off Pitch Hardness Level Pan
inline constexpr int StringsModDests[] = {
    0, 1, 14, 15, 16, 17, 18, 9, 10
};   ///< Off Pitch Pressure Speed Position Vib VibRate Level Pan
inline constexpr int ChoirModDests[] = {
    0, 1, 20, 21, 22, 17, 23, 9, 10
};   ///< Off Pitch Vowel Tension Breath Vib Formant Level Pan
inline constexpr int BrassModDests[] = { 0, 1, 14, 24, 17, 9, 10 };     ///< Off Pitch Pressure Brassiness Vib Level Pan
inline constexpr int TimpaniModDests[] = { 0, 1, 19, 16, 25, 9, 10 };   ///< Off Pitch Hardness Strike Decay Level Pan
/** @} */
static_assert( static_cast<int>( ModDestE::ENV_AMOUNT ) == 13 && static_cast<int>( ModDestE::DRIVE ) == 12 &&
                   static_cast<int>( ModDestE::PRESSURE ) == 14 && static_cast<int>( ModDestE::HARDNESS ) == 19 &&
                   static_cast<int>( ModDestE::FORMANT ) == 23 && static_cast<int>( ModDestE::DECAY ) == 25,
               "the target lists' numbers" );

}   // namespace parh
