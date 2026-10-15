/**
 * @file Piano.h
 * @brief The physical piano in real time (PLAN 5.8): the design's modes (PianoDesign.h) excited by hammers, turned by
 *        the strings' tension, stopped by dampers, radiated by the soundboard, with the strings under raised dampers
 *        ringing along.
 *
 * **A note** is a voice on its key: the hammer, a mass flying at a speed from the velocity (0.4 e^(2.6 v) m/s, 0.5 m/s
 * pianissimo to 5 m/s fortissimo), compresses the felt against the course (Stulov's hysteretic law) and is thrown back;
 * while it touches, the key's modes run on sub-steps with the force as their input, so the contact is resolved and the
 * brightness over the dynamics comes from the felt's nonlinearity, not from a filter. A key struck again while it
 * sounds is the same voice: the hammer meets the strings where they are.
 *
 * **While it sounds**, every block of the raster:
 *  - the tension (Kirchhoff-Carrier, Bank 2010 eq. 26): the mean of sum k^2 q_k^2 raises every frequency by
 *    E S pi^2 / (16 L^2 T) of it; the poles turn by that angle -- the fortissimo's pitch glide;
 *  - the damper (Lehtonen et al., JASA 2007 and 2009): when the key is up and the pedal down only partly, the felt comes
 *    down over about 15 ms and adds its decay to every mode, less on the high partials; the pedal's middle is the
 *    half pedal; the keys above F6 have no damper;
 *  - the longitudinal modes: the square of the bridge force is the stretch at the termination (E S / (2 T^2) F^2), its
 *    sum and difference frequencies ring through the string's longitudinal resonances into the bridge -- the phantom
 *    partials and the bass's metal (Bank and Sujbert, JASA 117(4), 2005; the local term of the stretch, the spatial
 *    selection of their eq. 22 left out).
 *
 * **The board** takes the bridge forces at sixteen region points (a key's force shared between its two), rings its
 * modes and radiates at three listening points blended by the width (bass side, middle, treble side); the high bank
 * carries what lies above the modes. **The sympathetic strings**: the board's acceleration at the regions drives the
 * first sixteen partials of every key whose damper is up and which no voice plays (the pedal's bloom, silently held
 * keys); what they give back is radiated by a second copy of the board, so the coupling runs one way and cannot swing
 * up (Bank 2010: one-sided coupling). **The mechanics**: the key's thump on the keybed as a short force pulse into the
 * board (Askenfelt and Jansson 1990), the damper felt's noise as it lands.
 *
 * **Lanes** (Vec.h): the resonator banks -- the voices' strings, the board, the high bank, the sympathetic strings --
 * run eight modes side by side and sum them into eight fixed accumulators, added in order at the end, so AVX2, NEON and
 * the scalar path agree bit for bit. The hammer's contact and every coefficient are scalar.
 *
 * **Time.** The tension, the dampers, the end of a hammer's contact, a voice's end and the sympathetic strings' coming
 * and going are decided on an absolute grid of MaxBlock samples counted from the last reset (as Poly.h does): a call
 * is cut at the grid, so where a host splits its blocks changes no sample. setSpec() allocates (a new design): at
 * load, never on the audio thread.
 *
 * **Modulation** (Phase 5b, Modulation.h): a Modulator per voice, read at every cell: the pitch (a drift or a bend of
 * two semitones at most, the poles turned exactly with the tension's rise), the hammer's hardness (at the strike), the
 * voice's force on the bridge (the level) and where on the bridge it drives (the pan: its region moved by up to four of
 * the sixteen, so its image moves with it). No strike point: the action's hammer line is fixed, and the design keeps
 * the modes' response at that line only.
 * @author Rene Weller
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/synth/Modulation.h"
#include "parh/synth/PianoDesign.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace parh {

/** @brief The physical piano: hammers, strings, dampers, the soundboard and the sympathetic strings (see the file comment). */
class Piano {
public:
    static constexpr int Voices = 24;       ///< voices at most (the Quest: fewer)
    static constexpr int MaxBlock = 32;     ///< samples per call at most (the engine's raster)

    /**
     * @brief Sets the rate; the design follows with setSpec().
     * @param[in] sampleRate  the sample rate
     */
    void prepare(double sampleRate);
    /**
     * @brief Builds or fetches the design for @p spec (allocates) and silences everything.
     * @param[in] spec  the piano's specification
     */
    void setSpec(const PianoSpec& spec);
    /**
     * @brief The playing knobs (the piano:: parameters, in their order).
     * @param[in] v  the piano's knob values
     */
    void update(const float* v);
    /**
     * @brief Strikes @p pitch (transposed by octaves into A0 .. C8) at @p velocity, @p late samples ago (0 <= late <
     *        1).
     * @param[in] pitch     the MIDI note
     * @param[in] velocity  the velocity, 0..1
     * @param[in] late      samples ago, 0..1
     */
    void noteOn(int pitch, float velocity, double late);
    /**
     * @brief Releases @p pitch (the damper falls unless the pedal holds it).
     * @param[in] pitch  the MIDI note
     */
    void noteOff(int pitch);
    /** @brief Silence, every key up. */
    void reset();
    /**
     * @brief The Quest's limits: fewer voices.
     * @param[in] voices  the most voices
     */
    void setVoiceLimit(int voices) { m_voiceLimit = std::clamp(voices, 1, Voices); }
    /**
     * @brief Renders @p n samples, replacing @p L and @p R.
     * @param[out] L  the left channel
     * @param[out] R  the right channel
     * @param[in]  n  the block's samples
     */
    void process(float* L, float* R, int n);
    /**
     * @brief Renders with a chosen lane type (float: the scalar reference), for the vector tests.
     * @param[out] L  the left channel
     * @param[out] R  the right channel
     * @param[in]  n  the block's samples
     */
    template <class VT> void processWith(float* L, float* R, int n);
    /**
     * @brief Voices sounding.
     * @return the voices sounding
     */
    int activeVoices() const;
    /**
     * @brief The design (the tests).
     * @return the design
     */
    const PianoDesign* design() const { return m_design.get(); }
    /**
     * @brief A voice's relative frequency rise from the tension, the largest now sounding (the tests).
     * @return the largest relative frequency rise
     */
    float maxTensionRise() const;
    /**
     * @brief The deck's shared modulation sources (Modulation.h).
     * @param[in] g  the shared sources
     */
    void setModGlobals(const ModGlobals& g) { m_mod.setGlobals(g); }
    /**
     * @brief The beat now and the beats per sample (the synced LFOs).
     * @param[in] beat            the beat now
     * @param[in] beatsPerSample  the beats per sample
     */
    void setClock(double beat, double beatsPerSample) { m_mod.setClock(m_pos, beat, beatsPerSample); }

private:
    /** @brief One sounding key: its hammer, its modes, its damper, its mechanics, its modulation. */
    struct Voice {
        int m_key = -1;              ///< 0 .. 87, -1 free
        bool m_contact = false;      ///< the hammer touches or flies near
        double m_yh = 0.0;   ///< the hammer's displacement
        double m_vh = 0.0;   ///< the hammer's velocity
        double m_hyst = 0.0;   ///< the felt's hysteresis state
        int m_contactSteps = 0;   ///< sub-steps since the contact began
        int m_apart = 0;   ///< sub-steps the felt has been apart from the strings in a row
        float m_damper = 0.0f;       ///< the felt's contact, 0 .. 1
        float m_delta = 0.0f;        ///< the tension's relative frequency rise
        float m_peak = 0.0f;         ///< the bridge force's peak in this grid cell so far
        float m_lastPeak = 0.0f;     ///< and in the last whole one
        int m_quiet = 0;             ///< blocks under the threshold
        uint64_t m_age = 0;   ///< when it started, in voices (the oldest is taken)
        std::vector<float> m_zr;   ///< the strings' modes: state, real part
        std::vector<float> m_zi;   ///< ... imaginary part
        std::vector<float> m_pr;   ///< their poles this block (turned by the tension and the bend): real part
        std::vector<float> m_pi;   ///< ... imaginary part
        float m_lzr[PianoLongModes] = {};   ///< the longitudinal modes: state, real part
        float m_lzi[PianoLongModes] = {};   ///< ... imaginary part
        int m_thump = -1;            ///< samples into the key's thump, -1 none
        float m_thumpGain = 0.0f;   ///< the thump's level
        int m_damperNoise = -1;      ///< samples into the damper's noise, -1 none
        float m_damperNoiseGain = 0.0f;   ///< the damper noise's level
        Rng m_noise;                 ///< its own stream (a shared one would interleave by how the calls are cut)
        // The modulation's values (Phase 5b): the key's own where no slot reaches them.
        double m_bend = 0.0;         ///< semitones
        float m_hardMul = 1.0f;   ///< the hammer's hardness factor (the modulation)
        float m_modGain = 1.0f;   ///< the force on the bridge (the modulation's level)
        float m_velHard = 1.0f;      ///< the felt's stiffness at this note's velocity (PianoSpec::hardVelocity)
        int m_region = 0;            ///< the bridge region its force goes to (and the next)
        float m_w0 = 1.0f;   ///< its share in the region
        float m_w1 = 0.0f;   ///< its share in the next region
    };
    /**
     * @brief Voice @p i's modulation now; @p strike: its hammer is about to fly (the hardness).
     * @param[in] i       the voice
     * @param[in] strike  true when its hammer is about to fly
     */
    void modVoice(int i, bool strike);
    /** @brief The board's states: its modes and the high bank. */
    struct BoardState {
        std::vector<float> m_zr;   ///< the board's modes: state, real part
        std::vector<float> m_zi;   ///< ... imaginary part
        std::vector<float> m_hzr;   ///< the high bank: state, real part
        std::vector<float> m_hzi;   ///< ... imaginary part
    };

    /**
     * @brief The voice for @p key: the one already on it, else a free one, else the oldest.
     * @param[in] key  the key
     * @return the voice's index
     */
    int voiceFor(int key);
    /**
     * @brief Throws the hammer of voice @p v at @p key with @p velocity, @p late samples ago.
     * @param[in,out] v         the voice
     * @param[in]     key       the key
     * @param[in]     velocity  the velocity, 0..1
     * @param[in]     late      samples ago
     */
    void startVoice(Voice& v, int key, float velocity, double late);
    /**
     * @brief Voice @p v's poles for the next @p n samples: the tension's rise, the bend, the damper.
     * @param[in,out] v  the voice
     * @param[in]     n  the samples
     */
    void blockCoefficients(Voice& v, int n);
    /**
     * @brief @p n samples of voice @p v while the hammer touches: its modes on sub-steps, the bridge force into @p
     *        force.
     * @param[in,out] v      the voice
     * @param[out]    force  the bridge force
     * @param[in]     n      the samples
     */
    void contactBlock(Voice& v, float* force, int n);
    /**
     * @brief @p n samples of voice @p v's free strings, the bridge force into @p force.
     * @param[in,out] v      the voice
     * @param[in,out] force  the bridge force, added to
     * @param[in]     n      the samples
     */
    template <class VT> void stringBlock(Voice& v, float* force, int n);
    /**
     * @brief @p n samples within one grid cell (@p cellStart, @p cellEnd: it begins or ends one) into @p L and @p R.
     * @param[in,out] L          the left channel, added to
     * @param[in,out] R          the right channel, added to
     * @param[in]     n          the samples
     * @param[in]     cellStart  true when the segment begins a cell
     * @param[in]     cellEnd    true when it ends one
     */
    template <class VT> void renderSegment(float* L, float* R, int n, bool cellStart, bool cellEnd);
    /**
     * @brief @p n samples of board @p s driven by the @p region forces into @p L and @p R; @p wantAccel: its acceleration
     *        too.
     * @param[in,out] s          the board's state
     * @param[in]     region     the forces per region
     * @param[in,out] L          the left channel, added to
     * @param[in,out] R          the right channel, added to
     * @param[in]     wantAccel  true for the acceleration too
     * @param[in]     n          the samples
     */
    template <class VT> void boardBlock(BoardState& s, const float (*region)[MaxBlock], float* L, float* R, bool wantAccel, int n);
    /**
     * @brief @p n samples of the sympathetic strings (@p sounding: anything drives them) within a grid cell.
     * @param[in] n          the samples
     * @param[in] sounding   true when anything drives them
     * @param[in] cellStart  true when the segment begins a cell
     * @param[in] cellEnd    true when it ends one
     */
    template <class VT> void symBlock(int n, bool sounding, bool cellStart, bool cellEnd);

    std::shared_ptr<const PianoDesign> m_design;   ///< the design: the modes of every key and of the board
    double m_sampleRate = 48000.0;   ///< the sample rate, Hz
    PianoSpec m_spec{};   ///< the spec the design was built for
    bool m_specSet = false;   ///< setSpec() was called
    Voice m_voices[Voices];   ///< the voices
    int m_voiceLimit = Voices;   ///< voices at most (the Quest: fewer)
    uint64_t m_clock = 0;   ///< counts the voices started
    int64_t m_pos = 0;                    ///< samples since the last reset (the grid)
    int m_held[PianoKeys] = {};          ///< notes down per key
    // The playing knobs.
    float m_level = 1.0f;   ///< piano.level, linear
    float m_pedal = 0.0f;   ///< the sustain pedal, 0 up .. 1 down
    float m_sympathetic = 1.0f;   ///< the sympathetic strings' amount
    float m_phantom = 1.0f;   ///< the phantom partials' amount
    float m_damperNoise = 0.3f;   ///< the damper noise's amount
    float m_mechanics = 0.3f;   ///< the thump's amount
    float m_width = 0.7f;   ///< the width: the listening points blended
    Svf m_lowCut[2];   ///< the low cut, per channel
    /** @brief The radiation shelf (PianoSpec::radiation), first order, per channel: its b0. */
    float m_radB0 = 1.0f;
    float m_radB1 = 0.0f;   ///< the shelf's b1
    float m_radA1 = 0.0f;   ///< the shelf's a1
    float m_radX[2] = {};   ///< the shelf's last input, per channel
    float m_radY[2] = {};   ///< the shelf's last output, per channel
    float m_lowCutHz = 80.0f;   ///< the low cut, Hz
    std::vector<float> m_micL;   ///< the board's listening weights for the left
    std::vector<float> m_micR;   ///< ... for the right
    // The board and its copy for the sympathetic strings.
    BoardState m_board;   ///< the board the voices drive
    BoardState m_symBoard;   ///< its copy the sympathetic strings drive
    // The sympathetic strings of every key.
    std::vector<float> m_symZr;   ///< the sympathetic strings' modes: state, real part
    std::vector<float> m_symZi;   ///< ... imaginary part
    std::vector<float> m_symPr;   ///< their poles: real part
    std::vector<float> m_symPi;   ///< ... imaginary part
    float m_symDamper[PianoKeys] = {};   ///< per key: its damper's contact
    float m_symPeak[PianoKeys] = {};   ///< per key: its sympathetic strings' peak in this grid cell
    bool m_symOn[PianoKeys] = {};   ///< per key: its sympathetic strings ring
    bool m_symBoardOn = false;           ///< the second board carries something
    // Per block.
    float m_regionF[PianoRegions][MaxBlock] = {};   ///< per region: the voices' bridge force this block
    float m_symRegionF[PianoRegions][MaxBlock] = {};   ///< per region: the sympathetic strings' force
    float m_accel[PianoRegions][MaxBlock] = {};   ///< per region: the board's acceleration (what drives the sympathetic strings)
    float m_force[MaxBlock] = {};   ///< one voice's bridge force this block
    float m_acc[MaxBlock * 8] = {};   ///< eight accumulators per sample (the lanes summed in order)
    float m_accL[MaxBlock * 8] = {};   ///< the board's left output: eight accumulators per sample
    float m_accR[MaxBlock * 8] = {};   ///< ... right
    float m_accA[PianoRegions][MaxBlock * 8] = {};   ///< per region: the acceleration's eight accumulators per sample
    NoteModulation<Voices> m_mod;        ///< the modulation, a Modulator per voice (Phase 5b)
};

} // namespace parh
