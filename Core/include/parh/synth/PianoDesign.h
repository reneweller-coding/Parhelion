/**
 * @file PianoDesign.h
 * @brief The physical piano's design (PLAN 5.8): the virtual instrument as a data sheet, and everything computed from it
 *        before a note sounds -- the soundboard's modes, the bridge's admittance, the strings' coupled modes, the
 *        hammers. Pianoteq's principle (patent US 7,915,515 B2): the coupled mechanical system is solved offline into
 *        complex eigenvalues, and in real time only damped sinusoids run, excited by a nonlinear hammer.
 *
 * **The data sheet** (Conklin, "Piano strings and scale design", JASA 99(6), 1996; Chaigne and Askenfelt, JASA 95(2)
 * and 95(3), 1994). Per key the speaking length (a plain-wire law L ~ f^-0.9 that saturates at the case's longest string),
 * plain steel wire above the bass break and wound strings under it, a course of one, two or three strings, the core's
 * diameter; the tension follows from f0, L and the mass per length, the inharmonicity from Fletcher (1964):
 * B = pi^3 E d^4 / (64 T L^2) with the core's diameter d, the partials f_k = k f0 sqrt(1 + B k^2). The tuning is
 * stretched by the inharmonicity itself: above A4 each note's fundamental meets the second partial of the note an octave
 * under it, below A4 its second partial meets the fundamental an octave above (the Railsback curve, from B alone).
 *
 * **The soundboard** is a ribbed orthotropic spruce plate in the outline of the case, solved by Rayleigh-Ritz (the
 * offline finite model of Pianoteq, here as a variational one): a basis of sine products on the bounding rectangle,
 * the plate's bending energy (along the grain, across it with the ribs smeared in, the twist) integrated over the
 * outline only, the rim as a stiff elastic support along the outline, the bridges as curved beams (mass and bending
 * stiffness along their line). Cholesky and a symmetric QL solver give the modes up to about 1.2 kHz, mass-normalised;
 * their loss factor is spruce's (Ege, Boutillon and Rebillat, JSV 332, 2013: about 1 to 2 per cent). Above, the ribs
 * make the board a set of waveguides (Ege et al. 2013) whose mean point mobility is that of the infinite plate,
 * Y_inf = 1 / (8 sqrt(D m'')); Parhelion carries it as its real part there and as a bank of log-spaced resonators in
 * the sound (Bank, ICMC 2007: parallel second-order filters).
 *
 * **The strings of a course** (Weinreich, JASA 62(6), 1977). Per partial the N strings (1 to 3), slightly mistuned (the
 * unison width), end on one bridge point of admittance Y(f). A string's complex frequency is shifted by
 * -2 f0 Z0 Y (the string's impedance Z0 = sqrt(T mu)); the N strings share it, so the course is the matrix
 * S = diag(s_j) - kappa J (J all ones, kappa = 2 f0 Z0 Y), complex symmetric, a diagonal plus rank one: its eigenvalues
 * are the roots of prod(lambda - s_j) + kappa sum_j prod_(l != j)(lambda - s_l), found by Durand-Kerner, its
 * eigenvectors v_j = 1 / (s_j - lambda). The symmetric mode takes N times the bridge's losses (the prompt sound), the
 * others hardly any (the aftersound); with mistuning they mix, and the beating and the double decay come out of the
 * physics. The horizontal polarisation is a second course with a smaller admittance, excited a little by the hammer's
 * irregularity (Bank, Zambon and Fontana, IEEE TASLP 18(4), 2010: separate resonator banks).
 *
 * **Discretisation.** Every coupled mode is a complex one-pole driven by the real hammer force with a zero-order hold:
 * z[n+1] = e^(lambda T) z[n] + (e^(lambda T) - 1) / lambda F[n]; the bridge force, the displacement under the hammer and
 * the displacement amplitude (for the tension) are real parts of the state times complex coefficients. The hammer runs
 * on sub-steps of the same modes while it touches the strings, so the loop has no delay-free path and the top keys'
 * short contacts are resolved (Bank 2010 discretises the resonators impulse-invariantly for the same reason).
 *
 * **The hammer** (Stulov, JASA 97(4), 1995): F = Q0 [u^p - (eps / tau0) int u^p(xi) e^((xi - t) / tau0) dxi], the
 * integral a one-pole low pass over u^p; static stiffness K = Q0 (1 - eps) and exponent p after Chaigne and Askenfelt
 * (C2 4e8 N/m^2.3, C4 4.5e9 N/m^2.5, C7 1e11 N/m^3), hammer masses from about 11 g in the bass to 5 g at the top.
 *
 * Computing a design allocates and takes a fraction of a second: never on the audio thread. Designs are cached per
 * specification and rate.
 */
#pragma once
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace parh {

constexpr int kPianoKeys = 88;           ///< A0 .. C8
constexpr int kPianoLowKey = 21;         ///< MIDI note of A0
constexpr int kPianoRegions = 16;        ///< points along the bridges where forces enter the board (Bank 2010: R = 8)
constexpr int kPianoMainRegions = 12;    ///< of them on the long bridge, the rest on the bass bridge
constexpr int kPianoLongModes = 6;       ///< longitudinal modes per key at most
constexpr int kPianoSymPartials = 16;    ///< partials of a sympathetically ringing string
constexpr int kPianoBassBreak = 45;      ///< the first plain-wire key (MIDI); wound strings under it

/** @brief The instruments (piano.instrument). */
enum class PianoInstrument : int { Grand = 0, BabyGrand, Upright, Soft, Count };
constexpr int kPianoInstruments = static_cast<int>(PianoInstrument::Count);
extern const char* const kPianoInstrumentNames[kPianoInstruments];

/** @brief The knobs that change the instrument (the design); the others act while it plays (Piano.h). */
struct PianoSpec {
    int instrument = 0;        ///< PianoInstrument
    float hardness = 1.0f;     ///< factor on the felt's stiffness
    float strike = 0.0f;       ///< the strike point, -1 (towards the agraffe) .. 1 (deeper into the string)
    float unison = 1.0f;       ///< the unison width, cents between the outer strings of a course
    float inharm = 1.0f;       ///< factor on the inharmonicity B
    float impedance = 1.0f;    ///< factor on the bridge's admittance (more: louder and shorter)
    float stretch = 1.0f;      ///< factor on the stretch of the tuning
    float condition = 0.0f;    ///< random mistuning of each string, cents (standard deviation)
    bool operator==(const PianoSpec&) const = default;
};

/** @brief Complex one-pole modes as a structure of arrays, padded with silent modes to a multiple of 8 (Vec.h). */
struct PianoModes {
    int count = 0;                 ///< modes that sound
    int padded = 0;                ///< count rounded up to 8
    std::vector<float> pr, pi;     ///< e^(lambda T)
    std::vector<float> gr, gi;     ///< (e^(lambda T) - 1) / lambda: the input's gain
    void resize(int n);
};

/** @brief One key: its strings' coupled modes, its hammer, where it meets the board. */
struct PianoKey {
    int midi = 60;
    double f0 = 261.6;           ///< the string's ideal fundamental (the first partial is f0 sqrt(1 + B))
    double length = 0.62, tension = 700.0, mu = 0.007, coreArea = 8.7e-7, B = 4e-4, z0 = 2.2, strikeRatio = 0.125;
    int strings = 3;
    bool wound = false, damper = true;
    // The hammer.
    double hammerMass = 0.0087, q0 = 3e10, hammerP = 2.5, hammerEps = 0.85, hammerTau = 1e-5;
    int oversample = 4;          ///< sub-steps per sample while the hammer touches
    // The coupled modes (vertical and horizontal courses).
    PianoModes modes;
    std::vector<float> spr, spi, sgr, sgi;   ///< pole and input gain per sub-step
    std::vector<float> br, bi;               ///< bridge force: Re(b z)
    std::vector<float> hr, hi;               ///< displacement under the hammer: Re(h z)
    std::vector<float> dr, di;               ///< the partial number times the course's mean displacement: Re(d z)
    std::vector<float> omegaT;               ///< the mode's angle per sample (the tension turns it)
    std::vector<float> damperSigma;          ///< the damper's added decay at full contact, 1/s
    int partials = 0;                        ///< partials per string
    // The tension (Kirchhoff-Carrier): the relative rise of every frequency is tensionGain * sum |d z|^2.
    double tensionGain = 0.0;
    // The longitudinal modes: excited by the square of the bridge force (the local stretch at the termination).
    int longCount = 0;
    float lpr[kPianoLongModes] = {}, lpi[kPianoLongModes] = {}, lgr[kPianoLongModes] = {}, lgi[kPianoLongModes] = {};
    float lcr[kPianoLongModes] = {}, lci[kPianoLongModes] = {};   ///< output: Re(c z), peak gain 1
    double longDrive = 0.0;                  ///< E S / (2 T^2): the longitudinal force per squared bridge force
    // The course as a sympathetic string: its first partials, driven by the bridge's acceleration.
    PianoModes sym;                          ///< input: the bridge's acceleration at the key's regions
    std::vector<float> symOutR, symOutI;     ///< bridge force: Re(c z)
    std::vector<float> symDamper;            ///< the damper's added decay, 1/s
    // The board.
    int region = 0;                          ///< the first of the two region points the force is shared between
    float regionW0 = 1.0f, regionW1 = 0.0f;
    float pan = 0.0f;                        ///< -1 bass .. 1 treble (the high bank's image)
};

/** @brief The soundboard: its low modes (driven at the region points) and the high bank. */
struct PianoBoard {
    PianoModes modes;
    std::vector<float> shape;          ///< [kPianoRegions][padded]: the mode shapes at the region points
    std::vector<float> micL, micC, micR;   ///< the shapes at three listening points, over the reference angular frequency
    std::vector<float> accR, accI;     ///< acceleration = Re(acc z): radiated (times the listening weights), and the
                                       ///< sympathetic strings' drive
    std::vector<double> freq, loss;    ///< Hz and loss factor per mode (the tests, the admittance)
    std::vector<float> keyShape;       ///< [kPianoKeys][count]: the shapes at every key's bridge point (the admittance)
    PianoModes high;                   ///< the high bank (log-spaced, above the modes)
    std::vector<float> highShape;      ///< [kPianoRegions][padded]
    std::vector<float> highL, highR;
    double yInf = 1e-3;                ///< the ribbed plate's characteristic mobility, s/kg
    double modeTop = 1200.0;           ///< the highest mode computed, Hz
};

/** @brief A whole instrument at one sample rate. */
struct PianoDesign {
    PianoSpec spec;
    double sampleRate = 48000.0;
    PianoBoard board;
    std::vector<PianoKey> keys;        ///< kPianoKeys
    float outGain = 1.0f;              ///< the radiated velocity to full scale
};

/** @brief The design for @p spec at @p sampleRate (cached; allocates, never on the audio thread). */
std::shared_ptr<const PianoDesign> designPiano(const PianoSpec& spec, double sampleRate);

/** @brief The bridge's admittance at key @p key (0 .. 87) and frequency @p hz, s/kg, as the design uses it. */
std::complex<double> pianoAdmittance(const PianoDesign& d, int key, double hz);

/**
 * @brief The soundboard alone, for the tests: the first @p count natural frequencies of a plain rectangular orthotropic
 *        plate (a x b, simply supported, no ribs, no bridge) as the Rayleigh-Ritz solver finds them.
 */
std::vector<double> pianoPlateTest(double a, double b, double dx, double dy, double d12, double d66, double massPerArea, int count);

} // namespace parh
