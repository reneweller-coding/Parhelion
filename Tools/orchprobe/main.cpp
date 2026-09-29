/**
 * @file main.cpp
 * @brief parh_orchprobe: looks into the orchestra (PLAN 5.9) -- renders notes of the string section, the choir, the
 *        brass and the timpani as WAV, and measures what phase 4b asks of them: the pitch where it belongs, the
 *        bowed string's sawtooth (partials falling as 1/k), the cost.
 *
 *     parh_orchprobe --strings "45:0.7:3,64:0.7:3" --out strings.wav
 *     parh_orchprobe --measure
 */
#include "parh/Dsp.h"
#include "parh/Params.h"
#include "parh/WavWriter.h"
#include "parh/synth/Brass.h"
#include "parh/synth/Choir.h"
#include "parh/synth/Strings.h"
#include "parh/synth/Timpani.h"
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
        const std::complex<double> wl = std::polar(1.0, -2.0 * kPiD / static_cast<double>(len));
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

std::vector<double> spectrumDb(const std::vector<float>& x, size_t start, size_t n)
{
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) a[i] = (start + i < x.size() ? x[start + i] : 0.0f) * (0.5 - 0.5 * std::cos(2.0 * kPiD * i / (n - 1)));
    fft(a);
    std::vector<double> m(n / 2);
    for (size_t i = 0; i < n / 2; ++i) m[i] = 20.0 * std::log10(std::abs(a[i]) + 1e-12);
    return m;
}

/** @brief The strongest peak's frequency within f +- 3 %. */
double peakNear(const std::vector<double>& s, double f, size_t n)
{
    const double bin = kRate / n;
    size_t lo = static_cast<size_t>(f * 0.97 / bin), hi = static_cast<size_t>(f * 1.03 / bin), at = lo;
    for (size_t i = lo; i <= hi && i + 1 < s.size(); ++i) if (s[i] > s[at]) at = i;
    const double a = s[at - 1], b = s[at], c = s[at + 1];
    return (static_cast<double>(at) + 0.5 * (a - c) / (a - 2.0 * b + c)) * bin;
}

template <class Instr>
std::vector<float> renderOne(Instr& ins, int pitch, float vel, double hold, double secs)
{
    const int total = static_cast<int>(secs * kRate), off = static_cast<int>(hold * kRate);
    std::vector<float> out(static_cast<size_t>(total));
    float L[32], R[32];
    ins.noteOn(pitch, vel, false, 0.0);
    for (int i = 0; i < total; i += 32) {
        if (i <= off && off < i + 32) ins.noteOff(pitch);
        const int n = std::min(32, total - i);
        ins.process(L, R, n);
        for (int k = 0; k < n; ++k) out[static_cast<size_t>(i + k)] = 0.5f * (L[k] + R[k]);
    }
    return out;
}

/** @brief The period of @p x from @p start (autocorrelation over 4096 samples, lags for 30 .. 2000 Hz), in Hz. */
double acfPitch(const std::vector<float>& x, size_t start)
{
    const size_t n = 4096;
    double best = -1e30;
    int lag = 0;
    const int lo = static_cast<int>(kRate / 2000.0), hi = static_cast<int>(kRate / 30.0);
    std::vector<double> r(static_cast<size_t>(hi + 2));
    for (int l = lo; l <= hi + 1; ++l) {
        double s = 0.0, e = 0.0;
        for (size_t i = 0; i < n; ++i) { s += static_cast<double>(x[start + i]) * x[start + i + static_cast<size_t>(l)]; e += static_cast<double>(x[start + i + static_cast<size_t>(l)]) * x[start + i + static_cast<size_t>(l)]; }
        r[static_cast<size_t>(l)] = s / std::sqrt(e + 1e-30);
    }
    // The first lag whose correlation comes within 10 % of the best.
    for (int l = lo; l <= hi; ++l) best = std::max(best, r[static_cast<size_t>(l)]);
    for (int l = lo + 1; l <= hi; ++l)
        if (r[static_cast<size_t>(l)] > 0.9 * best && r[static_cast<size_t>(l)] >= r[static_cast<size_t>(l - 1)] && r[static_cast<size_t>(l)] >= r[static_cast<size_t>(l + 1)]) { lag = l; break; }
    if (lag == 0) return kRate / (std::max_element(r.begin() + lo, r.begin() + hi) - r.begin());
    const double a = r[static_cast<size_t>(lag - 1)], b = r[static_cast<size_t>(lag)], c = r[static_cast<size_t>(lag + 1)];
    return kRate / (lag + 0.5 * (a - c) / (a - 2.0 * b + c));
}

void measureStrings(int players, float vibrato, float pressure, float position)
{
    auto s = std::make_unique<StringSection>();
    s->prepare(kRate, 7);
    ParamStore ps;
    std::vector<float> v(static_cast<size_t>(strings::Count));
    ps.readModule(Module::Strings, 0, v.data());
    v[strings::Players] = static_cast<float>(players);
    v[strings::Vibrato] = vibrato;
    v[strings::Pressure] = pressure;
    v[strings::Position] = position;
    s->update(v.data());
    for (int pitch : { 31, 43, 50, 57, 64, 69, 76, 88 }) {
        s->reset();
        const std::vector<float> x = renderOne(*s, pitch, 0.7f, 3.0, 2.5);
        const size_t n = 32768;
        const std::vector<double> sp = spectrumDb(x, static_cast<size_t>(1.0 * kRate), n);
        const double f = midiToHz(pitch), found = peakNear(sp, f, n);
        // The partials' levels relative to the first (a sawtooth falls 6 dB per octave).
        const double p1 = sp[static_cast<size_t>(found / (kRate / n))];
        std::printf("strings %d (family %d): acf %+.1f, peak %+.1f cents, partials 2/4/8 at %+.1f / %+.1f / %+.1f dB",
                    pitch, StringSection::familyOf(pitch), 1200.0 * std::log2(acfPitch(x, static_cast<size_t>(1.5 * kRate)) / f), 1200.0 * std::log2(found / f),
                    sp[static_cast<size_t>(2 * found / (kRate / n))] - p1, sp[static_cast<size_t>(4 * found / (kRate / n))] - p1,
                    8 * found < 0.45 * kRate ? sp[static_cast<size_t>(8 * found / (kRate / n))] - p1 : 0.0);
        float peak = 0.0f;
        for (float y : x) peak = std::max(peak, std::fabs(y));
        std::printf(", peak %.1f dBFS\n", 20.0 * std::log10(peak + 1e-12));
    }
    // Cost: a four-note chord of six players each, ten seconds.
    s->reset();
    for (int p : { 43, 55, 62, 67 }) s->noteOn(p, 0.7f, false, 0.0);
    float L[32], R[32];
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < static_cast<int>(10.0 * kRate); i += 32) s->process(L, R, 32);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("strings cost: four notes, %d lanes, %.1f %% of a core\n", s->activeLanes(), 100.0 * secs / 10.0);
}

/** @brief Four chords (Am F C G) twice: strings, then choir, then brass with timpani, then all; written to @p path. */
void demo(const char* path)
{
    ParamStore ps;
    auto s = std::make_unique<StringSection>();
    auto c = std::make_unique<Choir>();
    auto b = std::make_unique<Brass>();
    auto tp = std::make_unique<Timpani>();
    s->prepare(kRate, 1); c->prepare(kRate, 2); b->prepare(kRate, 3); tp->prepare(kRate, 4);
    std::vector<float> v(64);
    ps.readModule(Module::Strings, 0, v.data()); s->update(v.data());
    ps.readModule(Module::Choir, 0, v.data()); c->update(v.data());
    ps.readModule(Module::Brass, 0, v.data()); b->update(v.data());
    ps.readModule(Module::Timpani, 0, v.data()); tp->update(v.data());
    static const int kChords[4][4] = { { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 48, 55, 60, 64 }, { 43, 55, 59, 62 } };
    WavWriter wav;
    if (!wav.open(path, static_cast<int>(kRate), 2, WavFormat::Float32)) return;
    const double chordSecs = 2.2;
    for (int part = 0; part < 4; ++part)
        for (int ch = 0; ch < 4; ++ch) {
            const int* k = kChords[ch];
            const bool strings = part == 0 || part == 3, choir = part == 1 || part == 3, brass = part >= 2;
            for (int j = 0; j < 4; ++j) {
                if (strings) s->noteOn(k[j] + (j == 0 ? -12 : 0), 0.65f, false, 0.0);
                if (choir) c->noteOn(k[j], 0.7f, false, 0.0);
                if (brass && j < 3) b->noteOn(k[j] - 12, 0.6f, false, 0.0);
            }
            if (brass) tp->noteOn(k[0] - 12 + (k[0] < 45 ? 12 : 0), 0.9f, false, 0.0);
            float L[32], R[32], l2[32], r2[32];
            const int total = static_cast<int>(chordSecs * kRate), off = static_cast<int>((chordSecs - 0.15) * kRate);
            for (int i = 0; i < total; i += 32) {
                if (i <= off && off < i + 32)
                    for (int j = 0; j < 4; ++j) { s->noteOff(k[j] + (j == 0 ? -12 : 0)); c->noteOff(k[j]); b->noteOff(k[j] - 12); }
                for (int q = 0; q < 32; ++q) L[q] = R[q] = 0.0f;
                s->process(l2, r2, 32); for (int q = 0; q < 32; ++q) { L[q] += l2[q]; R[q] += r2[q]; }
                c->process(l2, r2, 32); for (int q = 0; q < 32; ++q) { L[q] += l2[q]; R[q] += r2[q]; }
                b->process(l2, r2, 32); for (int q = 0; q < 32; ++q) { L[q] += l2[q]; R[q] += r2[q]; }
                tp->process(l2, r2, 32); for (int q = 0; q < 32; ++q) { L[q] += l2[q]; R[q] += r2[q]; }
                wav.write(L, R, 32);
            }
        }
    wav.close();
    std::printf("wrote %s\n", path);
}

/** @brief Choir, brass and timpani: the pitch where it belongs, the level, the cost. */
void measureOthers()
{
    ParamStore ps;
    {
        auto c = std::make_unique<Choir>();
        c->prepare(kRate, 3);
        std::vector<float> v(static_cast<size_t>(choir::Count));
        ps.readModule(Module::Choir, 0, v.data());
        v[choir::Vibrato] = 0.0f;
        c->update(v.data());
        for (int pitch : { 43, 50, 57, 64, 69, 76 }) {
            c->reset();
            const std::vector<float> x = renderOne(*c, pitch, 0.7f, 3.0, 2.5);
            float peak = 0.0f;
            for (float y : x) peak = std::max(peak, std::fabs(y));
            std::printf("choir %d (voice %d): %+.1f cents, peak %.1f dBFS\n", pitch, Choir::voiceOf(pitch),
                        1200.0 * std::log2(acfPitch(x, static_cast<size_t>(1.2 * kRate)) / midiToHz(pitch)), 20.0 * std::log10(peak + 1e-12));
        }
        // The LF source: its flow returns to zero over a period (the derivative sums to 0).
        for (double rd : { 0.5, 1.0, 1.7, 2.6 }) {
            std::vector<float> per(2000);
            c->lfPeriod(rd, 2000, per.data());
            double sum = 0.0, lo = 0.0;
            for (float y : per) { sum += y; lo = std::min(lo, static_cast<double>(y)); }
            std::printf("  LF Rd %.1f: area %.2e (of %.2e), minimum %.2f\n", rd, sum / 2000.0, 1.0, lo);
        }
        c->reset();
        for (int p : { 45, 52, 57, 64, 69 }) c->noteOn(p, 0.7f, false, 0.0);
        float L[32], R[32];
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < static_cast<int>(10.0 * kRate); i += 32) c->process(L, R, 32);
        std::printf("choir cost: five notes, %d singers, %.1f %% of a core\n", c->activeSingers(),
                    10.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    {
        auto b = std::make_unique<Brass>();
        b->prepare(kRate, 5);
        std::vector<float> v(static_cast<size_t>(brass::Count));
        ps.readModule(Module::Brass, 0, v.data());
        v[brass::Players] = 1.0f;
        v[brass::Vibrato] = 0.0f;
        b->update(v.data());
        for (int pitch : { 34, 41, 46, 53, 58, 65, 70 })
            for (float vel : { 0.3f, 0.9f }) {
                b->reset();
                const std::vector<float> x = renderOne(*b, pitch, vel, 2.0, 1.5);
                float peak = 0.0f;
                for (size_t i = static_cast<size_t>(0.5 * kRate); i < x.size(); ++i) peak = std::max(peak, std::fabs(x[i]));
                const size_t n = 16384;
                const std::vector<double> sp = spectrumDb(x, static_cast<size_t>(0.5 * kRate), n);
                double num = 0.0, den = 0.0;
                for (size_t k = 1; k < sp.size(); ++k) { const double pw = std::pow(10.0, sp[k] / 10.0); num += pw * k * kRate / n; den += pw; }
                std::printf("brass %d vel %.1f: %+.1f cents, centroid %.0f Hz, peak %.1f dBFS\n", pitch, vel,
                            1200.0 * std::log2(acfPitch(x, static_cast<size_t>(0.8 * kRate)) / midiToHz(pitch)), num / den,
                            20.0 * std::log10(peak + 1e-12));
            }
    }
    {
        auto t = std::make_unique<Timpani>();
        t->prepare(kRate, 9);
        std::vector<float> v(static_cast<size_t>(timpani::Count));
        ps.readModule(Module::Timpani, 0, v.data());
        t->update(v.data());
        for (int pitch : { 40, 45, 50 })
            for (float vel : { 0.3f, 1.0f }) {
                t->reset();
                const std::vector<float> x = renderOne(*t, pitch, vel, 0.1, 3.0);
                const size_t n = 32768;
                const std::vector<double> sp = spectrumDb(x, static_cast<size_t>(0.3 * kRate), n);
                const double f = peakNear(sp, midiToHz(pitch), n);
                float peak = 0.0f;
                for (float y : x) peak = std::max(peak, std::fabs(y));
                double num = 0.0, den = 0.0;
                const std::vector<double> s0 = spectrumDb(x, 0, 4096);
                for (size_t k = 1; k < s0.size(); ++k) { const double pw = std::pow(10.0, s0[k] / 10.0); num += pw * k * kRate / 4096; den += pw; }
                std::printf("timpani %d vel %.1f: (1,1) at %+.1f cents, attack centroid %.0f Hz, peak %.1f dBFS\n", pitch, vel,
                            1200.0 * std::log2(f / midiToHz(pitch)), num / den, 20.0 * std::log10(peak + 1e-12));
            }
    }
}

} // namespace

int main(int argc, char** argv)
{
    const DenormalGuard guard;
    std::string strings, out = "orchestra.wav";
    int players = 6;
    float vibrato = 1.0f, pressure = 1.0f, position = 0.0f;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--measure")) measureStrings(players, vibrato, pressure, position);
        else if (!std::strcmp(argv[i], "--others")) measureOthers();
        else if (!std::strcmp(argv[i], "--brassdebug")) {
            auto b = std::make_unique<Brass>();
            b->prepare(kRate, 5);
            ParamStore ps;
            std::vector<float> v(static_cast<size_t>(brass::Count));
            ps.readModule(Module::Brass, 0, v.data());
            b->update(v.data());
            b->noteOn(46, 0.7f, false, 0.0);
            float L[32], R[32];
            for (int blk = 0; blk < 200; ++blk) {
                b->process(L, R, 32);
                if (blk % 20 == 0) std::printf("block %d: %g %g active %d\n", blk, L[0], L[31], b->activePlayers());
            }
        }
        else if (!std::strcmp(argv[i], "--demo") && i + 1 < argc) demo(argv[++i]);
        else if (!std::strcmp(argv[i], "--players") && i + 1 < argc) players = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--vibrato") && i + 1 < argc) vibrato = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--pressure") && i + 1 < argc) pressure = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--position") && i + 1 < argc) position = static_cast<float>(std::atof(argv[++i]));
        else if (!std::strcmp(argv[i], "--strings") && i + 1 < argc) strings = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
    }
    if (!strings.empty()) {
        auto s = std::make_unique<StringSection>();
        s->prepare(kRate, 7);
        ParamStore ps;
        std::vector<float> v(static_cast<size_t>(strings::Count));
        ps.readModule(Module::Strings, 0, v.data());
        s->update(v.data());
        WavWriter wav;
        if (!wav.open(out.c_str(), static_cast<int>(kRate), 2, WavFormat::Float32)) return 1;
        // "midi:velocity:seconds[:s]" -- a chord is several items with seconds 0 but the last.
        size_t pos = 0;
        std::vector<int> chord;
        while (pos < strings.size()) {
            const size_t end = strings.find(',', pos);
            const std::string item = strings.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            pos = end == std::string::npos ? strings.size() : end + 1;
            int midi = 60;
            float vel = 0.7f, secs = 2.0f;
            char mode = 'l';
            std::sscanf(item.c_str(), "%d:%f:%f:%c", &midi, &vel, &secs, &mode);
            s->noteOn(midi, vel, mode == 's', 0.0);
            chord.push_back(midi);
            if (secs <= 0.0f) continue;
            float L[32], R[32];
            const int total = static_cast<int>((secs + 1.0f) * kRate), off = static_cast<int>(secs * kRate);
            for (int i = 0; i < total; i += 32) {
                if (i <= off && off < i + 32) for (int m : chord) s->noteOff(m);
                s->process(L, R, 32);
                wav.write(L, R, 32);
            }
            chord.clear();
        }
        wav.close();
        std::printf("wrote %s\n", out.c_str());
    }
    return 0;
}
