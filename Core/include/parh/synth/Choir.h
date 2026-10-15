/**
 * @file Choir.h
 * @brief The choir (PLAN 5.9, Cinematic): singers from a glottal source and a vocal tract, "aah" to "uuh", no words.
 *
 * **The source** of a singer is the Liljencrants-Fant model of the glottal flow's derivative (Fant, Liljencrants and
 * Lin, "A four-parameter model of glottal flow", STL-QPSR 4, 1985): a growing sinusoid E0 e^(alpha t) sin(pi t / tp)
 * up to the main excitation at te, then an exponential return -(Ee / (eps ta)) (e^(-eps (t - te)) - e^(-eps (T0 - te)))
 * that closes the glottis by the period's end; eps from eps ta = 1 - e^(-eps (T0 - te)), alpha from a flow that
 * returns to zero over the period. Its shape follows one number, Rd (Fant, "The LF-model revisited", STL-QPSR 2-3,
 * 1995): Ra = (-1 + 4.8 Rd) / 100, Rk = (22.4 + 11.8 Rd) / 100, Rg = Rk / (4 (0.11 Rd / (0.5 + 1.2 Rk) - Ra)); lax
 * and breathy near 2.7, pressed near 0.8 (choir.tension). The model is solved once per Rd in time normalised to the
 * period (a table of 64) and scaled to each period; within the period it runs as a rotating phasor and a decaying
 * exponential, a few multiplications a sample.
 *
 * **The singer**: the period's length and amplitude perturbed from period to period (jitter about 0.4 %, shimmer about
 * 3 %), a vibrato of 4.8 to 5.8 Hz and 15 to 35 cents that sets in after a third of a second, a slow wander of the
 * pitch, an intonation and an onset of his own, aspiration noise in the open phase (choir.breath).
 *
 * **The vocal tract**: five formants in parallel as band passes of unit peak, their frequencies, bandwidths and levels
 * from the singers' tables of the formant-synthesis tradition (Csound's FOF tables after Peterson and Barney and
 * Sundberg) for bass, tenor, alto and soprano on "a", "o" and "u"; the vowel knob glides between them. A note's singers
 * share two tracts, one a little shorter and one a little longer (the section's spread, and its stereo).
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/synth/Modulation.h"
#include <cstdint>

namespace parh {

constexpr int ChoirNotes = 6;       ///< notes at once
constexpr int ChoirSingers = 6;     ///< singers on a note at most
constexpr int LfTable = 64;         ///< the LF shapes over Rd

/** @brief The choir: up to ChoirNotes notes, each sung by a section of singers through two vocal tracts. */
class Choir {
public:
    static constexpr int MaxBlock = 32;   ///< the longest block process() renders at once

    /** @brief Sets the sample rate and the seed of the singers' streams, solves the LF table, falls silent. */
    void prepare(double sampleRate, uint64_t seed);
    /** @brief Reads the effective parameter values (indexed by choir::). */
    void update(const float* v);
    /** @brief Starts @p pitch at @p velocity (0..1), @p late samples after its ideal start; @p unused is not read. */
    void noteOn(int pitch, float velocity, bool unused, double late);
    /** @brief Releases the note of @p pitch. */
    void noteOff(int pitch);
    /** @brief Silence: every note and singer off. */
    void reset();
    /** @brief Singers on a note (1 .. ChoirSingers, at most the quality's limit): the knob, through update(). */
    void setSingers(int n) { m_singers = n < 1 ? 1 : (n > m_singerLimit ? m_singerLimit : n); }
    /** @brief The quality's limit on the singers (Deck::setQuest), as StringSection::setPlayerLimit. */
    void setSingerLimit(int n) { m_singerLimit = n < 1 ? 1 : (n > ChoirSingers ? ChoirSingers : n); }
    /** @brief Renders @p n samples, replacing @p L and @p R (scalar: nothing to split into lanes). */
    void process(float* L, float* R, int n);
    /** @brief How many singers sing now (for the tests and the meters). */
    int activeSingers() const;
    /** @brief The voice a pitch is sung by: 0 bass, 1 tenor, 2 alto, 3 soprano. */
    static int voiceOf(int pitch);
    /** @brief One period of the LF source at @p rd, @p n samples (normalised to Ee = 1; for the tests). */
    void lfPeriod(double rd, int n, float* out) const;
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { m_mod.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { m_mod.setClock(m_pos, beat, beatsPerSample); }

private:
    /** @brief The LF model's shape of one period. */
    struct LfShape {
        double m_tp;      ///< the flow's peak
        double m_te;      ///< the main excitation
        double m_ta;      ///< the return phase's time constant
        double m_alpha;   ///< the growth of the open phase
        double m_eps;     ///< the return's decay
        double m_e0;      ///< the open phase's amplitude (Ee = 1)
    };
    /** @brief One singer of a note: his period, his source, his perturbations. */
    struct Singer {
        bool m_on = false;   ///< he sings
        int m_note = -1;   ///< the note he sings, -1 none
        int m_group = 0;   ///< the tract he sings through (0 the shorter, 1 the longer)
        double m_t = 0.0;   ///< seconds into the period
        double m_T0 = 0.005;   ///< the period's length, seconds
        double m_start = 0.0;          ///< seconds until the singer sets in
        LfShape m_s{};                 ///< this period's, in seconds
        double m_w = 0.0;              ///< pi / tp
        double m_zr = 0.0;   ///< e^((alpha + i w) t): real part
        double m_zi = 0.0;   ///< ... imaginary part
        double m_sr = 1.0;   ///< its step per sample: real part
        double m_si = 0.0;   ///< ... imaginary part
        double m_ret = 0.0;   ///< the return phase's e^(-eps (t - te))
        double m_retStep = 1.0;   ///< its factor per sample
        double m_retEnd = 0.0;   ///< its value at the period's end, e^(-eps (T0 - te))
        bool m_returning = false;   ///< the period is past te: the return phase
        double m_amp = 1.0;   ///< this period's amplitude (shimmer)
        double m_cents = 0.0;   ///< his intonation, cents
        double m_vibHz = 5.3;   ///< his vibrato's rate, Hz
        double m_vibCents = 25.0;   ///< his vibrato's depth, cents
        double m_vibPhase = 0.0;   ///< his vibrato's phase, radians
        double m_wander = 0.0;   ///< the slow wander of his pitch, cents
        double m_wanderTo = 0.0;   ///< where it wanders to
        double m_age = 0.0;   ///< seconds since the note began
        Rng m_rng;   ///< his own random stream
    };
    /** @brief One sung note: its pitch and voice, its envelope, its two tracts and its modulation. */
    struct Note {
        bool m_on = false;   ///< it sounds
        bool m_held = false;   ///< its key is held
        int m_pitch = 60;   ///< MIDI note
        int m_voice = 0;   ///< the voice that sings it: 0 bass, 1 tenor, 2 alto, 3 soprano
        float m_velocity = 0.7f;   ///< its velocity, 0..1
        double m_t = 0.0;   ///< seconds since it began
        double m_released = -1.0;   ///< seconds since its release, -1 while held
        float m_env = 0.0f;   ///< its envelope
        Svf m_formant[2][5];   ///< the five formants of each of the two tracts
        float m_gain[5] = {};   ///< the formants' levels
        int m_quiet = 0;   ///< cells it has been silent (it ends after a few)
        // The modulation's values for this note (Phase 5b); the knobs' where no slot reaches them.
        float m_vowel = 0.0f;   ///< the vowel, 0 "a" .. 2 "u"
        float m_formantMul = 1.0f;   ///< the formants' frequency factor
        float m_tension = 0.4f;   ///< the tension: Rd from lax to pressed
        float m_breath = 0.25f;   ///< the aspiration noise
        float m_vib = 1.0f;   ///< the vibrato's depth factor
        float m_level = 1.0f;   ///< the level factor
        float m_panL = 1.0f;   ///< the pan's gain, left
        float m_panR = 1.0f;   ///< the pan's gain, right
        double m_pitchSt = 0.0;   ///< the pitch offset, semitones
    };
    /** @brief Starts singer @p g's next period of note @p n, @p tInto seconds into it: its length, amplitude and LF shape. */
    void startPeriod(Singer& g, const Note& n, double tInto);
    /** @brief Sets note @p n's formants for its voice, vowel and formant factor. */
    void setFormants(Note& n);
    /** @brief Every sounding note's modulation at this cell (every 32 samples of the choir's own count). */
    void modCell();
    /** @brief Note @p s's modulation now. */
    void modNote(int s);
    /** @brief The LF shape for @p rd, in time normalised to the period. */
    static LfShape solveLf(double rd);

    double m_sr = 48000.0;   ///< the sample rate, Hz
    uint64_t m_seed = 1;   ///< the singers' streams' seed
    int m_singers = ChoirSingers;   ///< singers on a note
    int m_singerLimit = ChoirSingers;   ///< setSingerLimit()
    uint32_t m_counter = 0;   ///< counts the notes started (each singer's stream)
    LfShape m_lf[LfTable];   ///< the LF shapes over Rd
    float m_level = 1.0f;   ///< choir.level, linear
    float m_vowel = 0.0f;   ///< the vowel knob
    float m_vibrato = 1.0f;   ///< the vibrato's depth knob
    float m_breath = 0.25f;   ///< the breath knob
    float m_tension = 0.4f;   ///< the tension knob
    float m_attackMs = 250.0f;   ///< the attack, ms
    float m_releaseMs = 400.0f;   ///< the release, ms
    float m_width = 0.8f;   ///< the width
    float m_lowCutHz = 90.0f;   ///< the low cut, Hz
    Svf m_lowCut[2];   ///< the low cut, per channel
    Singer m_singer[ChoirNotes * ChoirSingers];   ///< the singers, ChoirSingers per note
    Note m_note[ChoirNotes];   ///< the notes
    NoteModulation<ChoirNotes> m_mod;   ///< the modulation, a Modulator per note (Phase 5b)
    int64_t m_pos = 0;                   ///< the choir's absolute sample count
};

} // namespace parh
