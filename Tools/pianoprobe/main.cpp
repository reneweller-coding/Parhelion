/**
 * @file main.cpp
 * @brief parh_pianoprobe: looks into the physical piano (PLAN 5.8) -- the design's numbers, single notes as WAV, and the
 *        measurements of phase 4a (inharmonicity, the two-stage decay, the spectral centroid over the velocity, the
 *        pitch glide, the cost).
 *
 *     parh_pianoprobe --info [--instrument n]
 *     parh_pianoprobe --notes "60:0.7:2,36:1.0:4" --out notes.wav [--pedal 1] [--instrument n]
 *     parh_pianoprobe --measure
 */
#include "parh/Dsp.h"
#include "parh/Params.h"
#include "parh/WavWriter.h"
#include "parh/synth/Piano.h"
#include "parh/synth/PianoDesign.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace parh;

namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr double kRate = 48000.0;

void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPiD / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** @brief Renders one note (held @p hold seconds) for @p secs seconds, mono (L + R). */
std::vector<float> renderNote(Piano& p, int midi, float vel, double hold, double secs, float pedal = 0.0f)
{
    ParamStore ps;
    std::vector<float> v(static_cast<size_t>(piano::Count));
    ps.readModule(Module::Piano, 0, v.data());
    v[piano::Pedal] = pedal;
    p.update(v.data());
    p.reset();
    const int total = static_cast<int>(secs * kRate), off = static_cast<int>(hold * kRate);
    std::vector<float> out(static_cast<size_t>(total));
    float L[Piano::kMaxBlock], R[Piano::kMaxBlock];
    p.noteOn(midi, vel, 0.0);
    bool released = false;
    for (int i = 0; i < total; i += Piano::kMaxBlock) {
        if (!released && i >= off) { p.noteOff(midi); released = true; }
        const int n = std::min(Piano::kMaxBlock, total - i);
        p.process(L, R, n);
        for (int k = 0; k < n; ++k) out[static_cast<size_t>(i + k)] = 0.5f * (L[k] + R[k]);
    }
    return out;
}

/** @brief The magnitude spectrum (dB) of @p x from @p start over @p n samples (Hann), and the bin width. */
std::vector<double> spectrum(const std::vector<float>& x, size_t start, size_t n)
{
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPiD * i / (n - 1));
        a[i] = start + i < x.size() ? x[start + i] * w : 0.0;
    }
    fft(a);
    std::vector<double> m(n / 2);
    for (size_t i = 0; i < n / 2; ++i) m[i] = 20.0 * std::log10(std::abs(a[i]) + 1e-12);
    return m;
}

/** @brief The level (dB RMS) of @p x over 50 ms windows. */
std::vector<double> envelope(const std::vector<float>& x)
{
    const size_t w = static_cast<size_t>(0.05 * kRate);
    std::vector<double> e;
    for (size_t i = 0; i + w <= x.size(); i += w) {
        double s = 0.0;
        for (size_t k = 0; k < w; ++k) s += static_cast<double>(x[i + k]) * x[i + k];
        e.push_back(10.0 * std::log10(s / w + 1e-20));
    }
    return e;
}

void info(int instrument)
{
    PianoSpec spec;
    spec.instrument = instrument;
    const auto t0 = std::chrono::steady_clock::now();
    auto d = designPiano(spec, kRate);
    const auto t1 = std::chrono::steady_clock::now();
    std::printf("design %s: %.2f s\n", kPianoInstrumentNames[instrument], std::chrono::duration<double>(t1 - t0).count());
    const PianoBoard& b = d->board;
    std::printf("board: %zu modes up to %.0f Hz, Y_inf %.2e s/kg; first:", b.freq.size(), b.modeTop, b.yInf);
    for (size_t n = 0; n < std::min<size_t>(12, b.freq.size()); ++n) std::printf(" %.1f", b.freq[n]);
    std::printf("\n");
    for (int midi : { 21, 28, 36, 44, 45, 48, 60, 72, 84, 96, 108 }) {
        const PianoKey& k = d->keys[static_cast<size_t>(midi - kPianoLowKey)];
        const double f1 = k.f0 * std::sqrt(1.0 + k.B);
        const std::complex<double> y = pianoAdmittance(*d, midi - kPianoLowKey, f1);
        // The slowest and the fastest decay among the first partial's coupled modes.
        double slow = 0.0, fast = 1e9;
        for (int m = 0; m < k.strings; ++m) {
            const double mag = std::hypot(k.modes.pr[static_cast<size_t>(m)], k.modes.pi[static_cast<size_t>(m)]);
            const double t60 = 6.91 / (-std::log(mag) * kRate);
            slow = std::max(slow, t60);
            fast = std::min(fast, t60);
        }
        std::printf("  %3d f1 %7.2f L %.3f T %6.0f B %.2e Z0 %5.2f N %d partials %3d modes %3d |Y| %.2e  T60 %.1f .. %.1f s  long %d (%.0f Hz)\n",
                    midi, f1, k.length, k.tension, k.B, k.z0, k.strings, k.partials, k.modes.count, std::abs(y), fast, slow,
                    k.longCount, k.longCount > 0 ? std::atan2(k.lpi[0], k.lpr[0]) * kRate / (2.0 * kPiD) : 0.0);
    }
}

/** @brief Seconds of work per second of audio for one note under a few settings. */
void cost()
{
    auto piano = std::make_unique<Piano>();
    piano->prepare(kRate);
    piano->setSpec(PianoSpec{});
    struct Case { const char* name; int midi; double hold; float sym; float pedal; };
    const Case cases[] = { { "no note (the board alone)", -1, 10.0, 0.0f, 0.0f }, { "C2 held, sympathetic", 36, 10.0, 1.0f, 0.0f }, { "C2 held, no sympathetic", 36, 10.0, 0.0f, 0.0f },
                           { "C2 released at 0.1 s", 36, 0.1, 0.0f, 0.0f }, { "C4 held, no sympathetic", 60, 10.0, 0.0f, 0.0f },
                           { "C4 held, pedal down", 60, 10.0, 1.0f, 1.0f } };
    for (const Case& c : cases) {
        ParamStore ps;
        std::vector<float> v(static_cast<size_t>(piano::Count));
        ps.readModule(Module::Piano, 0, v.data());
        v[piano::Sympathetic] = c.sym;
        v[piano::Pedal] = c.pedal;
        piano->update(v.data());
        piano->reset();
        if (c.midi >= 0) piano->noteOn(c.midi, 0.7f, 0.0);
        float L[Piano::kMaxBlock], R[Piano::kMaxBlock];
        const int total = static_cast<int>(5.0 * kRate), off = static_cast<int>(c.hold * kRate);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < total; i += Piano::kMaxBlock) {
            if (i <= off && off < i + Piano::kMaxBlock) piano->noteOff(c.midi);
            piano->process(L, R, Piano::kMaxBlock);
        }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %-28s %.2f %% of a core\n", c.name, 100.0 * s / 5.0);
    }
}

/** @brief The same notes rendered in calls of 32 and in calls of odd sizes: must be equal, sample for sample. */
void splits()
{
    auto a = std::make_unique<Piano>(), b = std::make_unique<Piano>();
    for (Piano* p : { a.get(), b.get() }) {
        p->prepare(kRate);
        p->setSpec(PianoSpec{});
        ParamStore ps;
        std::vector<float> v(static_cast<size_t>(piano::Count));
        ps.readModule(Module::Piano, 0, v.data());
        v[piano::Pedal] = 0.6f;
        p->update(v.data());
        p->reset();
    }
    const int total = static_cast<int>(3.0 * kRate);
    std::vector<float> xa(total), xb(total);
    float L[64], R[64];
    const int events[4] = { 0, 12345, 30001, 70000 };
    auto run = [&](Piano& p, std::vector<float>& x, bool odd) {
        int i = 0, e = 0, c = 0;
        while (i < total) {
            while (e < 4 && events[e] <= i) { p.noteOn(48 + 7 * e, 0.8f, 0.0); ++e; }
            int n = odd ? 1 + (c * 7919) % 31 : 32;
            if (e < 4) n = std::min(n, events[e] - i);
            n = std::min(n, total - i);
            p.process(L, R, n);
            for (int k = 0; k < n; ++k) x[static_cast<size_t>(i + k)] = L[k];
            i += n;
            ++c;
        }
    };
    run(*a, xa, false);
    run(*b, xb, true);
    int first = -1, count = 0;
    for (int i = 0; i < total; ++i) if (std::memcmp(&xa[static_cast<size_t>(i)], &xb[static_cast<size_t>(i)], 4) != 0) { if (first < 0) first = i; ++count; }
    std::printf("splits: %d samples differ, the first at %d\n", count, first);
}

void measure()
{
    PianoSpec spec;
    auto piano = std::make_unique<Piano>();
    piano->prepare(kRate);
    piano->setSpec(spec);
    // Cost: ten seconds of a chord of six notes.
    {
        ParamStore ps;
        std::vector<float> v(static_cast<size_t>(piano::Count));
        ps.readModule(Module::Piano, 0, v.data());
        piano->update(v.data());
        piano->reset();
        for (int m : { 36, 48, 55, 60, 64, 67 }) piano->noteOn(m, 0.7f, 0.0);
        float L[Piano::kMaxBlock], R[Piano::kMaxBlock];
        const auto t0 = std::chrono::steady_clock::now();
        const int total = static_cast<int>(10.0 * kRate);
        for (int i = 0; i < total; i += Piano::kMaxBlock) piano->process(L, R, Piano::kMaxBlock);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("cost: six notes, 10 s of audio in %.2f s (%.1f %% of a core)\n", s, 100.0 * s / 10.0);
    }
    // Inharmonicity: the partials of C2 and C4 against k f0 sqrt(1 + B k^2).
    for (int midi : { 36, 60 }) {
        const std::vector<float> x = renderNote(*piano, midi, 0.7f, 3.0, 1.5);
        const size_t n = 32768;
        const std::vector<double> s = spectrum(x, static_cast<size_t>(0.2 * kRate), n);
        const PianoKey& k = piano->design()->keys[static_cast<size_t>(midi - kPianoLowKey)];
        const double bin = kRate / n;
        std::printf("partials of %d (B %.2e):", midi, k.B);
        double worst = 0.0;
        for (int p = 1; p <= 12; ++p) {
            const double f = p * k.f0 * std::sqrt(1.0 + k.B * p * p);
            size_t lo = static_cast<size_t>((f * 0.985) / bin), hi = static_cast<size_t>((f * 1.015) / bin), at = lo;
            for (size_t i = lo; i <= hi && i < s.size(); ++i) if (s[i] > s[at]) at = i;
            // Parabolic interpolation of the peak.
            const double a = s[at - 1], b = s[at], c = s[at + 1];
            const double off = 0.5 * (a - c) / (a - 2.0 * b + c);
            const double found = (static_cast<double>(at) + off) * bin;
            const double cents = 1200.0 * std::log2(found / f);
            worst = std::max(worst, std::fabs(cents));
            std::printf(" %d:%+.1f", p, cents);
        }
        std::printf("  (cents off the model, worst %.1f)\n", worst);
    }
    // Two-stage decay: C4, the level's slope over 0.2 .. 1.2 s and over 4 .. 8 s.
    {
        const std::vector<double> e = envelope(renderNote(*piano, 60, 0.7f, 12.0, 10.0));
        auto slope = [&](double a, double b) {
            const size_t i = static_cast<size_t>(a / 0.05), j = static_cast<size_t>(b / 0.05);
            return (e[j] - e[i]) / (b - a);
        };
        std::printf("decay of C4: %.1f dB/s early (0.2..1.2 s), %.1f dB/s late (4..8 s)\n", slope(0.2, 1.2), slope(4.0, 8.0));
        const std::vector<double> e2 = envelope(renderNote(*piano, 60, 0.7f, 1.0, 3.0));
        std::printf("released after 1 s: level at 1.0 s %.1f dB, at 1.5 s %.1f dB\n", e2[20], e2[30]);
    }
    // The spectral centroid over the velocity (C4), and the peak level.
    for (float vel : { 0.2f, 0.5f, 0.8f, 1.0f }) {
        const std::vector<float> x = renderNote(*piano, 60, vel, 2.0, 0.6);
        const size_t n = 16384;
        const std::vector<double> s = spectrum(x, 0, n);
        double num = 0.0, den = 0.0;
        for (size_t i = 1; i < s.size(); ++i) {
            const double p = std::pow(10.0, s[i] / 10.0);
            num += p * i * (kRate / n);
            den += p;
        }
        float peak = 0.0f;
        for (float y : x) peak = std::max(peak, std::fabs(y));
        std::printf("velocity %.1f: centroid %.0f Hz, peak %.1f dBFS\n", vel, num / den, 20.0 * std::log10(peak + 1e-12));
    }
    // The pitch glide: the fundamental of A1 fortissimo in its first 0.2 s against 3 .. 3.4 s.
    {
        const std::vector<float> x = renderNote(*piano, 33, 1.0f, 6.0, 4.0);
        const PianoKey& k = piano->design()->keys[static_cast<size_t>(33 - kPianoLowKey)];
        auto peakHz = [&](double t) {
            const size_t n = 32768;
            const std::vector<double> s = spectrum(x, static_cast<size_t>(t * kRate), n);
            const double bin = kRate / n, f = 2.0 * k.f0 * std::sqrt(1.0 + 4.0 * k.B);
            size_t lo = static_cast<size_t>(f * 0.97 / bin), hi = static_cast<size_t>(f * 1.03 / bin), at = lo;
            for (size_t i = lo; i <= hi; ++i) if (s[i] > s[at]) at = i;
            const double a = s[at - 1], b = s[at], c = s[at + 1];
            return (static_cast<double>(at) + 0.5 * (a - c) / (a - 2.0 * b + c)) * bin;
        };
        std::printf("pitch glide of A1 ff (2nd partial): %.2f cents from the first 0.7 s to 3 .. 3.7 s\n",
                    1200.0 * std::log2(peakHz(0.0) / peakHz(3.0)));
    }
}

} // namespace

int main(int argc, char** argv)
{
    const DenormalGuard guard;   // as the engine runs: decayed partials are zero, not subnormal
    int instrument = 0;
    std::string notes, out = "piano.wav";
    float pedal = 0.0f;
    bool doInfo = false, doMeasure = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--info")) doInfo = true;
        else if (!std::strcmp(argv[i], "--measure")) doMeasure = true;
        else if (!std::strcmp(argv[i], "--cost")) cost();
        else if (!std::strcmp(argv[i], "--splits")) splits();
        else if (!std::strcmp(argv[i], "--instrument") && i + 1 < argc) instrument = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--notes") && i + 1 < argc) notes = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--pedal") && i + 1 < argc) pedal = static_cast<float>(std::atof(argv[++i]));
    }
    if (doInfo) info(instrument);
    if (doMeasure) measure();
    if (!notes.empty()) {
        PianoSpec spec;
        spec.instrument = instrument;
        auto piano = std::make_unique<Piano>();
        piano->prepare(kRate);
        piano->setSpec(spec);
        ParamStore ps;
        std::vector<float> v(static_cast<size_t>(piano::Count));
        ps.readModule(Module::Piano, 0, v.data());
        v[piano::Pedal] = pedal;
        piano->update(v.data());
        WavWriter wav;
        if (!wav.open(out.c_str(), static_cast<int>(kRate), 2, WavFormat::Float32)) return 1;
        // "midi:velocity:seconds" one after another, each held for its seconds, then a second of silence.
        size_t pos = 0;
        while (pos < notes.size()) {
            const size_t end = notes.find(',', pos);
            const std::string item = notes.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? notes.size() : end + 1;
            int midi = 60;
            float vel = 0.7f, secs = 2.0f;
            std::sscanf(item.c_str(), "%d:%f:%f", &midi, &vel, &secs);
            piano->noteOn(midi, vel, 0.0);
            float L[Piano::kMaxBlock], R[Piano::kMaxBlock];
            const int total = static_cast<int>((secs + 1.0f) * kRate), off = static_cast<int>(secs * kRate);
            for (int i = 0; i < total; i += Piano::kMaxBlock) {
                if (i <= off && off < i + Piano::kMaxBlock) piano->noteOff(midi);
                piano->process(L, R, Piano::kMaxBlock);
                wav.write(L, R, Piano::kMaxBlock);
            }
        }
        wav.close();
        std::printf("wrote %s\n", out.c_str());
    }
    return 0;
}
