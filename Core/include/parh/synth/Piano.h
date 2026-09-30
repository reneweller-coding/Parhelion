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
 * and going are decided on an absolute grid of kMaxBlock samples counted from the last reset (as Poly.h does): a call
 * is cut at the grid, so where a host splits its blocks changes no sample. setSpec() allocates (a new design): at
 * load, never on the audio thread.
 *
 * **Modulation** (Phase 5b, Modulation.h): a Modulator per voice, read at every cell: the pitch (a drift or a bend of
 * two semitones at most, the poles turned exactly with the tension's rise), the hammer's hardness (at the strike), the
 * voice's force on the bridge (the level) and where on the bridge it drives (the pan: its region moved by up to four of
 * the sixteen, so its image moves with it). No strike point: the action's hammer line is fixed, and the design keeps
 * the modes' response at that line only.
 */
#pragma once
#include "parh/Dsp.h"
#include "parh/synth/Modulation.h"
#include "parh/synth/PianoDesign.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace parh {

class Piano {
public:
    static constexpr int kVoices = 24;       ///< voices at most (the Quest: fewer)
    static constexpr int kMaxBlock = 32;     ///< samples per call at most (the engine's raster)

    /** @brief Sets the rate; the design follows with setSpec(). */
    void prepare(double sampleRate);
    /** @brief Builds or fetches the design for @p spec (allocates) and silences everything. */
    void setSpec(const PianoSpec& spec);
    /** @brief The playing knobs (the piano:: parameters, in their order). */
    void update(const float* v);
    /** @brief Strikes @p pitch (transposed by octaves into A0 .. C8) at @p velocity, @p late samples ago (0 <= late < 1). */
    void noteOn(int pitch, float velocity, double late);
    /** @brief Releases @p pitch (the damper falls unless the pedal holds it). */
    void noteOff(int pitch);
    /** @brief Silence, every key up. */
    void reset();
    /** @brief The Quest's limits: fewer voices. */
    void setVoiceLimit(int voices) { voiceLimit_ = std::clamp(voices, 1, kVoices); }
    /** @brief Renders @p n samples, replacing @p L and @p R. */
    void process(float* L, float* R, int n);
    /** @brief Renders with a chosen lane type (float: the scalar reference), for the vector tests. */
    template <class V> void processWith(float* L, float* R, int n);
    /** @brief Voices sounding. */
    int activeVoices() const;
    /** @brief The design (the tests). */
    const PianoDesign* design() const { return design_.get(); }
    /** @brief A voice's relative frequency rise from the tension, the largest now sounding (the tests). */
    float maxTensionRise() const;
    /** @brief The deck's shared modulation sources (Modulation.h). */
    void setModGlobals(const ModGlobals& g) { mod_.setGlobals(g); }
    /** @brief The beat now and the beats per sample (the synced LFOs). */
    void setClock(double beat, double beatsPerSample) { mod_.setClock(pos_, beat, beatsPerSample); }

private:
    struct Voice {
        int key = -1;              ///< 0 .. 87, -1 free
        bool contact = false;      ///< the hammer touches or flies near
        double yh = 0.0, vh = 0.0, hyst = 0.0;   ///< hammer displacement, velocity, hysteresis state
        int contactSteps = 0, apart = 0;
        float damper = 0.0f;       ///< the felt's contact, 0 .. 1
        float delta = 0.0f;        ///< the tension's relative frequency rise
        float peak = 0.0f;         ///< the bridge force's peak in this grid cell so far
        float lastPeak = 0.0f;     ///< and in the last whole one
        int quiet = 0;             ///< blocks under the threshold
        uint64_t age = 0;
        std::vector<float> zr, zi, pr, pi;
        float lzr[kPianoLongModes] = {}, lzi[kPianoLongModes] = {};
        int thump = -1;            ///< samples into the key's thump, -1 none
        float thumpGain = 0.0f;
        int damperNoise = -1;      ///< samples into the damper's noise, -1 none
        float damperNoiseGain = 0.0f;
        Rng noise;                 ///< its own stream (a shared one would interleave by how the calls are cut)
        // The modulation's values (Phase 5b): the key's own where no slot reaches them.
        double bend = 0.0;         ///< semitones
        float hardMul = 1.0f, modGain = 1.0f;
        float velHard = 1.0f;      ///< the felt's stiffness at this note's velocity (PianoSpec::hardVelocity)
        int region = 0;            ///< the bridge region its force goes to (and the next)
        float w0 = 1.0f, w1 = 0.0f;
    };
    /** @brief Voice @p i's modulation now; @p strike: its hammer is about to fly (the hardness). */
    void modVoice(int i, bool strike);
    /** @brief The board's states: its modes and the high bank. */
    struct BoardState {
        std::vector<float> zr, zi, hzr, hzi;
    };

    int voiceFor(int key);
    void startVoice(Voice& v, int key, float velocity, double late);
    void blockCoefficients(Voice& v, int n);
    void contactBlock(Voice& v, float* force, int n);
    template <class V> void stringBlock(Voice& v, float* force, int n);
    template <class V> void renderSegment(float* L, float* R, int n, bool cellStart, bool cellEnd);
    template <class V> void boardBlock(BoardState& s, const float (*region)[kMaxBlock], float* L, float* R, bool wantAccel, int n);
    template <class V> void symBlock(int n, bool sounding, bool cellStart, bool cellEnd);

    std::shared_ptr<const PianoDesign> design_;
    double sampleRate_ = 48000.0;
    PianoSpec spec_{};
    bool specSet_ = false;
    Voice voices_[kVoices];
    int voiceLimit_ = kVoices;
    uint64_t clock_ = 0;
    int64_t pos_ = 0;                    ///< samples since the last reset (the grid)
    int held_[kPianoKeys] = {};          ///< notes down per key
    // The playing knobs.
    float level_ = 1.0f, pedal_ = 0.0f, sympathetic_ = 1.0f, phantom_ = 1.0f, damperNoise_ = 0.3f, mechanics_ = 0.3f;
    float width_ = 0.7f;
    Svf lowCut_[2];
    /** @brief The radiation shelf (PianoSpec::radiation): first order, per channel. */
    float radB0_ = 1.0f, radB1_ = 0.0f, radA1_ = 0.0f, radX_[2] = {}, radY_[2] = {};
    float lowCutHz_ = 80.0f;
    std::vector<float> micL_, micR_;     ///< the board's listening weights for the width
    // The board and its copy for the sympathetic strings.
    BoardState board_, symBoard_;
    // The sympathetic strings of every key.
    std::vector<float> symZr_, symZi_, symPr_, symPi_;   ///< [kPianoKeys][sym padded 16]
    float symDamper_[kPianoKeys] = {};
    float symPeak_[kPianoKeys] = {};
    bool symOn_[kPianoKeys] = {};
    bool symBoardOn_ = false;           ///< the second board carries something
    // Per block.
    float regionF_[kPianoRegions][kMaxBlock] = {};
    float symRegionF_[kPianoRegions][kMaxBlock] = {};
    float accel_[kPianoRegions][kMaxBlock] = {};
    float force_[kMaxBlock] = {};
    float acc_[kMaxBlock * 8] = {};
    float accL_[kMaxBlock * 8] = {}, accR_[kMaxBlock * 8] = {};
    float accA_[kPianoRegions][kMaxBlock * 8] = {};
    NoteModulation<kVoices> mod_;        ///< the modulation, a Modulator per voice (Phase 5b)
};

} // namespace parh
