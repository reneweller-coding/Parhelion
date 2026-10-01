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

constexpr int kChoirNotes = 6;       ///< notes at once
constexpr int kChoirSingers = 6;     ///< singers on a note at most
constexpr int kLfTable = 64;         ///< the LF shapes over Rd

/** @brief The choir: up to kChoirNotes notes, each sung by a section of singers through two vocal tracts. */
class Choir {
public:
    static constexpr int kMaxBlock = 32;   ///< the longest block process() renders at once

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
    /** @brief Singers on a note (1 .. kChoirSingers, at most the quality's limit): the knob, through update(). */
    void setSingers(int n) { singers_ = n < 1 ? 1 : (n > singerLimit_ ? singerLimit_ : n); }
    /** @brief The quality's limit on the singers (Deck::setQuest), as StringSection::setPlayerLimit. */
    void setSingerLimit(int n) { singerLimit_ = n < 1 ? 1 : (n > kChoirSingers ? kChoirSingers : n); }
    /** @brief Renders @p n samples, replacing @p L and @p R (scalar: nothing to split into lanes). */
    void process(float* L, float* R, int n);
    /** @brief How many singers sing now (for the tests and the meters). */
    int activeSingers() const;
    /** @brief The voice a pitch is sung by: 0 bass, 1 tenor, 2 alto, 3 soprano. */
    static int voiceOf(int pitch);
    /** @brief One period of the LF source at @p rd, @p n samples (normalised to Ee = 1; for the tests). */
    void lfPeriod(double rd, int n, float* out) const;
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { mod_.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { mod_.setClock(pos_, beat, beatsPerSample); }

private:
    /** @brief The LF model's shape of one period. */
    struct LfShape {
        double tp;      ///< the flow's peak
        double te;      ///< the main excitation
        double ta;      ///< the return phase's time constant
        double alpha;   ///< the growth of the open phase
        double eps;     ///< the return's decay
        double e0;      ///< the open phase's amplitude (Ee = 1)
    };
    /** @brief One singer of a note: his period, his source, his perturbations. */
    struct Singer {
        bool on = false;   ///< he sings
        int note = -1;   ///< the note he sings, -1 none
        int group = 0;   ///< the tract he sings through (0 the shorter, 1 the longer)
        double t = 0.0;   ///< seconds into the period
        double T0 = 0.005;   ///< the period's length, seconds
        double start = 0.0;          ///< seconds until the singer sets in
        LfShape s{};                 ///< this period's, in seconds
        double w = 0.0;              ///< pi / tp
        double zr = 0.0;   ///< e^((alpha + i w) t): real part
        double zi = 0.0;   ///< ... imaginary part
        double sr = 1.0;   ///< its step per sample: real part
        double si = 0.0;   ///< ... imaginary part
        double ret = 0.0;   ///< the return phase's e^(-eps (t - te))
        double retStep = 1.0;   ///< its factor per sample
        double retEnd = 0.0;   ///< its value at the period's end, e^(-eps (T0 - te))
        bool returning = false;   ///< the period is past te: the return phase
        double amp = 1.0;   ///< this period's amplitude (shimmer)
        double cents = 0.0;   ///< his intonation, cents
        double vibHz = 5.3;   ///< his vibrato's rate, Hz
        double vibCents = 25.0;   ///< his vibrato's depth, cents
        double vibPhase = 0.0;   ///< his vibrato's phase, radians
        double wander = 0.0;   ///< the slow wander of his pitch, cents
        double wanderTo = 0.0;   ///< where it wanders to
        double age = 0.0;   ///< seconds since the note began
        Rng rng;   ///< his own random stream
    };
    /** @brief One sung note: its pitch and voice, its envelope, its two tracts and its modulation. */
    struct Note {
        bool on = false;   ///< it sounds
        bool held = false;   ///< its key is held
        int pitch = 60;   ///< MIDI note
        int voice = 0;   ///< the voice that sings it: 0 bass, 1 tenor, 2 alto, 3 soprano
        float velocity = 0.7f;   ///< its velocity, 0..1
        double t = 0.0;   ///< seconds since it began
        double released = -1.0;   ///< seconds since its release, -1 while held
        float env = 0.0f;   ///< its envelope
        Svf formant[2][5];   ///< the five formants of each of the two tracts
        float gain[5] = {};   ///< the formants' levels
        int quiet = 0;   ///< cells it has been silent (it ends after a few)
        // The modulation's values for this note (Phase 5b); the knobs' where no slot reaches them.
        float vowel = 0.0f;   ///< the vowel, 0 "a" .. 2 "u"
        float formantMul = 1.0f;   ///< the formants' frequency factor
        float tension = 0.4f;   ///< the tension: Rd from lax to pressed
        float breath = 0.25f;   ///< the aspiration noise
        float vib = 1.0f;   ///< the vibrato's depth factor
        float level = 1.0f;   ///< the level factor
        float panL = 1.0f;   ///< the pan's gain, left
        float panR = 1.0f;   ///< the pan's gain, right
        double pitchSt = 0.0;   ///< the pitch offset, semitones
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

    double sr_ = 48000.0;   ///< the sample rate, Hz
    uint64_t seed_ = 1;   ///< the singers' streams' seed
    int singers_ = kChoirSingers;   ///< singers on a note
    int singerLimit_ = kChoirSingers;   ///< setSingerLimit()
    uint32_t counter_ = 0;   ///< counts the notes started (each singer's stream)
    LfShape lf_[kLfTable];   ///< the LF shapes over Rd
    float level_ = 1.0f;   ///< choir.level, linear
    float vowel_ = 0.0f;   ///< the vowel knob
    float vibrato_ = 1.0f;   ///< the vibrato's depth knob
    float breath_ = 0.25f;   ///< the breath knob
    float tension_ = 0.4f;   ///< the tension knob
    float attackMs_ = 250.0f;   ///< the attack, ms
    float releaseMs_ = 400.0f;   ///< the release, ms
    float width_ = 0.8f;   ///< the width
    float lowCutHz_ = 90.0f;   ///< the low cut, Hz
    Svf lowCut_[2];   ///< the low cut, per channel
    Singer singer_[kChoirNotes * kChoirSingers];   ///< the singers, kChoirSingers per note
    Note note_[kChoirNotes];   ///< the notes
    NoteModulation<kChoirNotes> mod_;   ///< the modulation, a Modulator per note (Phase 5b)
    int64_t pos_ = 0;                   ///< the choir's absolute sample count
};

} // namespace parh
