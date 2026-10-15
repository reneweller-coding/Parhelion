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
 * @author Rene Weller
 */
#pragma once
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace parh
{

constexpr int PianoKeys = 88;          ///< A0 .. C8
constexpr int PianoLowKey = 21;        ///< MIDI note of A0
constexpr int PianoRegions = 16;       ///< points along the bridges where forces enter the board (Bank 2010: R = 8)
constexpr int PianoMainRegions = 12;   ///< of them on the long bridge, the rest on the bass bridge
constexpr int PianoLongModes = 6;      ///< longitudinal modes per key at most
constexpr int PianoSymPartials = 16;   ///< partials of a sympathetically ringing string
constexpr int PianoBassBreak = 45;     ///< the first plain-wire key (MIDI); wound strings under it

/** @brief The instruments (piano.instrument). */
enum class PianoInstrumentE : int
{
    GRAND = 0,
    BABY_GRAND,
    UPRIGHT,
    SOFT,
    COUNT
};
constexpr int PianoInstruments = static_cast<int>( PianoInstrumentE::COUNT );   ///< how many instruments there are
extern const char *const PianoInstrumentNames[PianoInstruments];

/** @brief The knobs that change the instrument (the design); the others act while it plays (Piano.h). */
struct PianoSpec
{
    int   m_instrument = 0;     ///< PianoInstrument
    float m_hardness = 1.0f;    ///< factor on the felt's stiffness
    float m_strike = 0.0f;      ///< the strike point, -1 (towards the agraffe) .. 1 (deeper into the string)
    float m_unison = 1.0f;      ///< the unison width, cents between the outer strings of a course
    float m_inharm = 1.0f;      ///< factor on the inharmonicity B
    float m_impedance = 1.0f;   ///< factor on the bridge's admittance (more: louder and shorter)
    float m_stretch = 1.0f;     ///< factor on the stretch of the tuning
    float m_condition = 0.0f;   ///< random mistuning of each string, cents (standard deviation)
    /**
     * @name The voicing against a reference piano (30.09.2026; Tools/pianoref, the user: "Du kannst es gerne mit Pianoteq
     *       vergleichen und daran abstimmen")
     * Not knobs of the page: the design's own numbers, fitted note by note to Pianoteq 8's Steinway D (NY Steinway Model
     * D) -- 48 notes, C1 to C7 at four velocities, by Nelder-Mead on the level over the keyboard and the velocity, the
     * octave spectra per register, the brightness and how it grows with the blow, the two decays, the knock and the body
     * of the first 400 ms, the damper (Tools/pianoref/compare.py; PLAN, "Das Piano gegen Pianoteq"). The defaults are the
     * fit; the neutral values (no shelf, 0 dB, 25/s and 0.5, 0, 1, 100 Hz, 0 dB, 1, 1, 1) are the design before it.
     * @{ */
    float m_felt =
        0.7f;   ///< factor on every hammer's stiffness, under the page's hardness (the fit: 1.16; 0.7 for the brightness)
    float m_coupling =
        0.296f;   ///< factor on the bridge's admittance, under the page's impedance (a looser coupling: a slower prompt sound)
    float m_radiation =
        372.7f;   ///< Hz: the sound is the board's acceleration below, its velocity above (a shelf; 0 none)
    float m_radiationEnd = 4505.0f;   ///< Hz: where the shelf's fall stops (0: it falls on)
    float m_voiceBass = 3.64f;        ///< dB on the lowest key's bridge force, linear over the keys to none at C4
    float m_voiceTreble = 7.54f;      ///< dB on the highest key's, linear from C4
    float m_damperRate = 11.0f;       ///< the damper's added decay at full contact on a 0.6 m string, 1/s
    float m_damperLength = 0.2f;   ///< how the damper's decay goes with the string's length: (0.6 m / L)^damperLength
    ///< (0.2 by hand: the Steinway damps evenly, 20..30 dB in 0.3 s; the fit's 0.5: bass 15, treble 34)
    float m_hardVelocity = 0.646f;   ///< the felt's stiffness over the velocity: times (velocity / 0.75)^hardVelocity
    float m_knock = 100.2f;          ///< factor on the key's thump into the board (the action's knock)
    float m_bodyCorner = 50.9f;      ///< Hz: under it the board's modes are held back in the sound (little net volume)
    float m_highBank = 7.95f;        ///< dB on the high bank (the board above its computed modes) against the modes
    float m_boardLoss = 0.539f;      ///< factor on the board's loss factor (less: the body rings longer)
    /** @} */
    /**
     * @brief Whether two specs build the same design (the cache).
     * @return true when they do
     */
    bool operator==( const PianoSpec & ) const = default;
};

/** @brief Complex one-pole modes as a structure of arrays, padded with silent modes to a multiple of 8 (Vec.h). */
struct PianoModes
{
    int                m_count = 0;    ///< modes that sound
    int                m_padded = 0;   ///< count rounded up to 8
    std::vector<float> m_pr;           ///< e^(lambda T): real part
    std::vector<float> m_pi;           ///< ... imaginary part
    std::vector<float> m_gr;           ///< (e^(lambda T) - 1) / lambda, the input's gain: real part
    std::vector<float> m_gi;           ///< ... imaginary part
    /**
     * @brief Room for @p n modes, padded to a multiple of 8 with silent ones.
     * @param[in] n  the modes
     */
    void resize( int n );
};

/** @brief One key: its strings' coupled modes, its hammer, where it meets the board. */
struct PianoKey
{
    int    m_midi = 60;             ///< its MIDI note
    double m_f0 = 261.6;            ///< the string's ideal fundamental (the first partial is f0 sqrt(1 + B))
    double m_length = 0.62;         ///< the speaking length, m
    double m_tension = 700.0;       ///< the tension, N
    double m_mu = 0.007;            ///< the mass per length, kg/m
    double m_coreArea = 8.7e-7;     ///< the steel core's cross section, m^2
    double m_B = 4e-4;              ///< the inharmonicity
    double m_z0 = 2.2;              ///< the string's wave impedance, kg/s
    double m_strikeRatio = 0.125;   ///< where the hammer strikes, a share of the length
    int    m_strings = 3;           ///< strings in the course
    bool   m_wound = false;         ///< a wound string
    bool   m_damper = true;         ///< it has a damper (none above F6)
    // The hammer.
    double m_hammerMass = 0.0087;   ///< the hammer's mass, kg
    double m_q0 = 3e10;             ///< the felt's stiffness (Stulov)
    double m_hammerP = 2.5;         ///< the felt's exponent
    double m_hammerEps = 0.85;      ///< the felt's hysteresis
    double m_hammerTau = 1e-5;      ///< the felt's relaxation time, s
    int    m_oversample = 4;        ///< sub-steps per sample while the hammer touches
    // The coupled modes (vertical and horizontal courses).
    PianoModes         m_modes;          ///< the course's coupled modes
    std::vector<float> m_spr;            ///< per sub-step: pole, real part
    std::vector<float> m_spi;            ///< ... imaginary part
    std::vector<float> m_sgr;            ///< ... input gain, real part
    std::vector<float> m_sgi;            ///< ... imaginary part
    std::vector<float> m_br;             ///< the bridge force's weights: real part
    std::vector<float> m_bi;             ///< ... imaginary part (Re(b z))
    std::vector<float> m_hr;             ///< the displacement under the hammer's weights: real part
    std::vector<float> m_hi;             ///< ... imaginary part (Re(h z))
    std::vector<float> m_dr;             ///< the partial number times the course's mean displacement: real part
    std::vector<float> m_di;             ///< ... imaginary part (Re(d z))
    std::vector<float> m_omegaT;         ///< the mode's angle per sample (the tension turns it)
    std::vector<float> m_damperSigma;    ///< the damper's added decay at full contact, 1/s
    int                m_partials = 0;   ///< partials per string
    // The tension (Kirchhoff-Carrier): the relative rise of every frequency is tensionGain * sum |d z|^2.
    double m_tensionGain = 0.0;   ///< the relative rise of every frequency per sum |d z|^2
    /// The longitudinal modes: excited by the square of the bridge force (the local stretch at the termination).
    int    m_longCount = 0;
    float  m_lpr[PianoLongModes] = {};   ///< the longitudinal modes: pole, real part
    float  m_lpi[PianoLongModes] = {};   ///< ... imaginary part
    float  m_lgr[PianoLongModes] = {};   ///< ... input gain, real part
    float  m_lgi[PianoLongModes] = {};   ///< ... imaginary part
    float  m_lcr[PianoLongModes] = {};   ///< their output weights: real part
    float  m_lci[PianoLongModes] = {};   ///< ... imaginary part (Re(c z), peak gain 1)
    double m_longDrive = 0.0;            ///< E S / (2 T^2): the longitudinal force per squared bridge force
    // The course as a sympathetic string: its first partials, driven by the bridge's acceleration.
    PianoModes         m_sym;         ///< input: the bridge's acceleration at the key's regions
    std::vector<float> m_symOutR;     ///< the sympathetic partials' bridge force: real part
    std::vector<float> m_symOutI;     ///< ... imaginary part (Re(c z))
    std::vector<float> m_symDamper;   ///< the damper's added decay, 1/s
    // The board.
    int   m_region = 0;        ///< the first of the two region points the force is shared between
    float m_regionW0 = 1.0f;   ///< the force's share at the region
    float m_regionW1 = 0.0f;   ///< ... at the next
    float m_pan = 0.0f;        ///< -1 bass .. 1 treble (the high bank's image)
};

/** @brief The soundboard: its low modes (driven at the region points) and the high bank. */
struct PianoBoard
{
    PianoModes         m_modes;        ///< the board's low modes
    std::vector<float> m_shape;        ///< [PianoRegions][padded]: the mode shapes at the region points
    std::vector<float> m_micL;         ///< the shapes at the bass-side listening point
    std::vector<float> m_micC;         ///< ... at the middle
    std::vector<float> m_micR;         ///< ... at the treble side (over the reference angular frequency)
    std::vector<float> m_accR;         ///< the acceleration's weights: real part
    std::vector<float> m_accI;         ///< ... imaginary part (Re(acc z): radiated, and the sympathetic strings' drive)
                                       ///< sympathetic strings' drive
    std::vector<double> m_freq;        ///< per mode: its frequency, Hz
    std::vector<double> m_loss;        ///< per mode: its loss factor
    std::vector<float>  m_keyShape;    ///< [PianoKeys][count]: the shapes at every key's bridge point (the admittance)
    PianoModes          m_high;        ///< the high bank (log-spaced, above the modes)
    std::vector<float>  m_highShape;   ///< [PianoRegions][padded]
    std::vector<float>  m_highL;       ///< the high bank's radiation, left
    std::vector<float>  m_highR;       ///< ... right
    double              m_yInf = 1e-3;        ///< the ribbed plate's characteristic mobility, s/kg
    double              m_modeTop = 1200.0;   ///< the highest mode computed, Hz
};

/** @brief A whole instrument at one sample rate. */
struct PianoDesign
{
    PianoSpec             m_spec;                   ///< the spec it was built for
    double                m_sampleRate = 48000.0;   ///< the sample rate it was built for, Hz
    PianoBoard            m_board;                  ///< the soundboard
    std::vector<PianoKey> m_keys;                   ///< PianoKeys
    float                 m_outGain = 1.0f;         ///< the radiated velocity to full scale
};

/**
 * @brief The design for @p spec at @p sampleRate (cached; allocates, never on the audio thread).
 * @param[in] spec        the piano's specification
 * @param[in] sampleRate  the sample rate
 * @return the design
 */
std::shared_ptr<const PianoDesign> designPiano( const PianoSpec &spec, double sampleRate );

/**
 * @brief The bridge's admittance at key @p key (0 .. 87) and frequency @p hz, s/kg, as the design uses it.
 * @param[in] d    the design
 * @param[in] key  the key, 0..87
 * @param[in] hz   the frequency
 * @return the admittance in s/kg
 */
std::complex<double> pianoAdmittance( const PianoDesign &d, int key, double hz );

/**
 * @brief The soundboard alone, for the tests: the first @p count natural frequencies of a plain rectangular orthotropic
 *        plate (a x b, simply supported, no ribs, no bridge) as the Rayleigh-Ritz solver finds them.
 * @param[in] a            the plate's width in metres
 * @param[in] b            its depth in metres
 * @param[in] dx           the bending stiffness along x
 * @param[in] dy           the bending stiffness along y
 * @param[in] d12          the coupling stiffness
 * @param[in] d66          the torsional stiffness
 * @param[in] massPerArea  the mass per area
 * @param[in] count        the frequencies wanted
 * @return the natural frequencies in Hz
 */
std::vector<double> pianoPlateTest( double a, double b, double dx, double dy, double d12, double d66,
                                    double massPerArea, int count );

}   // namespace parh
