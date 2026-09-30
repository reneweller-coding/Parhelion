/**
 * @file PianoDesign.cpp
 * @brief The physical piano's design (PianoDesign.h): the data sheet, the soundboard by Rayleigh-Ritz, the bridge
 *        admittance, the coupled strings, the hammers.
 */
#include "parh/synth/PianoDesign.h"
#include "parh/Dsp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <tuple>

namespace parh {

const char* const kPianoInstrumentNames[kPianoInstruments] = { "Grand", "Baby Grand", "Upright", "Soft" };

void PianoModes::resize(int n)
{
    count = n;
    padded = (n + 7) & ~7;
    for (std::vector<float>* v : { &pr, &pi, &gr, &gi }) v->assign(static_cast<size_t>(padded), 0.0f);
}

namespace {

constexpr double kPiD = 3.14159265358979323846;
using cd = std::complex<double>;
using Pt = std::array<double, 2>;

// ------------------------------------------------------------------------------------------------ linear algebra

/** @brief Cholesky in place: the lower triangle of @p a (n x n, row-major) becomes L with A = L L^T. */
bool cholesky(std::vector<double>& a, int n)
{
    for (int j = 0; j < n; ++j) {
        double s = a[static_cast<size_t>(j * n + j)];
        for (int k = 0; k < j; ++k) s -= a[static_cast<size_t>(j * n + k)] * a[static_cast<size_t>(j * n + k)];
        if (s <= 0.0) return false;
        const double d = std::sqrt(s);
        a[static_cast<size_t>(j * n + j)] = d;
        for (int i = j + 1; i < n; ++i) {
            double t = a[static_cast<size_t>(i * n + j)];
            for (int k = 0; k < j; ++k) t -= a[static_cast<size_t>(i * n + k)] * a[static_cast<size_t>(j * n + k)];
            a[static_cast<size_t>(i * n + j)] = t / d;
        }
        for (int i = 0; i < j; ++i) a[static_cast<size_t>(i * n + j)] = 0.0;
    }
    return true;
}

/** @brief Solves L X = B for every column of @p b (n x n, row-major) in place, L lower triangular. */
void forwardSolve(const std::vector<double>& l, std::vector<double>& b, int n)
{
    for (int c = 0; c < n; ++c)
        for (int i = 0; i < n; ++i) {
            double t = b[static_cast<size_t>(i * n + c)];
            for (int k = 0; k < i; ++k) t -= l[static_cast<size_t>(i * n + k)] * b[static_cast<size_t>(k * n + c)];
            b[static_cast<size_t>(i * n + c)] = t / l[static_cast<size_t>(i * n + i)];
        }
}

/**
 * @brief Eigenvalues and eigenvectors of a symmetric matrix: Householder tridiagonalisation and the implicit QL
 *        method (the EISPACK routines tred2 and tql2 as JAMA gives them). @p v holds the matrix on entry (row-major)
 *        and the eigenvectors in its columns on exit; @p d the eigenvalues, ascending.
 */
void symmetricEigen(std::vector<double>& v, int n, std::vector<double>& d)
{
    auto V = [&](int i, int j) -> double& { return v[static_cast<size_t>(i * n + j)]; };
    d.assign(static_cast<size_t>(n), 0.0);
    std::vector<double> e(static_cast<size_t>(n), 0.0);
    for (int j = 0; j < n; ++j) d[static_cast<size_t>(j)] = V(n - 1, j);
    for (int i = n - 1; i > 0; --i) {
        double scale = 0.0, h = 0.0;
        for (int k = 0; k < i; ++k) scale += std::fabs(d[static_cast<size_t>(k)]);
        if (scale == 0.0) {
            e[static_cast<size_t>(i)] = d[static_cast<size_t>(i - 1)];
            for (int j = 0; j < i; ++j) { d[static_cast<size_t>(j)] = V(i - 1, j); V(i, j) = 0.0; V(j, i) = 0.0; }
        } else {
            for (int k = 0; k < i; ++k) { d[static_cast<size_t>(k)] /= scale; h += d[static_cast<size_t>(k)] * d[static_cast<size_t>(k)]; }
            double f = d[static_cast<size_t>(i - 1)];
            double g = std::sqrt(h);
            if (f > 0) g = -g;
            e[static_cast<size_t>(i)] = scale * g;
            h -= f * g;
            d[static_cast<size_t>(i - 1)] = f - g;
            for (int j = 0; j < i; ++j) e[static_cast<size_t>(j)] = 0.0;
            for (int j = 0; j < i; ++j) {
                f = d[static_cast<size_t>(j)];
                V(j, i) = f;
                g = e[static_cast<size_t>(j)] + V(j, j) * f;
                for (int k = j + 1; k <= i - 1; ++k) {
                    g += V(k, j) * d[static_cast<size_t>(k)];
                    e[static_cast<size_t>(k)] += V(k, j) * f;
                }
                e[static_cast<size_t>(j)] = g;
            }
            f = 0.0;
            for (int j = 0; j < i; ++j) { e[static_cast<size_t>(j)] /= h; f += e[static_cast<size_t>(j)] * d[static_cast<size_t>(j)]; }
            const double hh = f / (h + h);
            for (int j = 0; j < i; ++j) e[static_cast<size_t>(j)] -= hh * d[static_cast<size_t>(j)];
            for (int j = 0; j < i; ++j) {
                f = d[static_cast<size_t>(j)];
                g = e[static_cast<size_t>(j)];
                for (int k = j; k <= i - 1; ++k) V(k, j) -= (f * e[static_cast<size_t>(k)] + g * d[static_cast<size_t>(k)]);
                d[static_cast<size_t>(j)] = V(i - 1, j);
                V(i, j) = 0.0;
            }
        }
        d[static_cast<size_t>(i)] = h;
    }
    for (int i = 0; i < n - 1; ++i) {
        V(n - 1, i) = V(i, i);
        V(i, i) = 1.0;
        const double h = d[static_cast<size_t>(i + 1)];
        if (h != 0.0) {
            for (int k = 0; k <= i; ++k) d[static_cast<size_t>(k)] = V(k, i + 1) / h;
            for (int j = 0; j <= i; ++j) {
                double g = 0.0;
                for (int k = 0; k <= i; ++k) g += V(k, i + 1) * V(k, j);
                for (int k = 0; k <= i; ++k) V(k, j) -= g * d[static_cast<size_t>(k)];
            }
        }
        for (int k = 0; k <= i; ++k) V(k, i + 1) = 0.0;
    }
    for (int j = 0; j < n; ++j) { d[static_cast<size_t>(j)] = V(n - 1, j); V(n - 1, j) = 0.0; }
    V(n - 1, n - 1) = 1.0;
    e[0] = 0.0;
    // tql2
    for (int i = 1; i < n; ++i) e[static_cast<size_t>(i - 1)] = e[static_cast<size_t>(i)];
    e[static_cast<size_t>(n - 1)] = 0.0;
    double f = 0.0, tst1 = 0.0;
    const double eps = std::pow(2.0, -52.0);
    for (int l = 0; l < n; ++l) {
        tst1 = std::max(tst1, std::fabs(d[static_cast<size_t>(l)]) + std::fabs(e[static_cast<size_t>(l)]));
        int m = l;
        while (m < n) {
            if (std::fabs(e[static_cast<size_t>(m)]) <= eps * tst1) break;
            ++m;
        }
        if (m > l) {
            int iter = 0;
            do {
                ++iter;
                double g = d[static_cast<size_t>(l)];
                double p = (d[static_cast<size_t>(l + 1)] - g) / (2.0 * e[static_cast<size_t>(l)]);
                double r = std::hypot(p, 1.0);
                if (p < 0) r = -r;
                d[static_cast<size_t>(l)] = e[static_cast<size_t>(l)] / (p + r);
                d[static_cast<size_t>(l + 1)] = e[static_cast<size_t>(l)] * (p + r);
                const double dl1 = d[static_cast<size_t>(l + 1)];
                double h = g - d[static_cast<size_t>(l)];
                for (int i = l + 2; i < n; ++i) d[static_cast<size_t>(i)] -= h;
                f += h;
                p = d[static_cast<size_t>(m)];
                double c = 1.0, c2 = c, c3 = c;
                const double el1 = e[static_cast<size_t>(l + 1)];
                double s = 0.0, s2 = 0.0;
                for (int i = m - 1; i >= l; --i) {
                    c3 = c2;
                    c2 = c;
                    s2 = s;
                    g = c * e[static_cast<size_t>(i)];
                    h = c * p;
                    r = std::hypot(p, e[static_cast<size_t>(i)]);
                    e[static_cast<size_t>(i + 1)] = s * r;
                    s = e[static_cast<size_t>(i)] / r;
                    c = p / r;
                    p = c * d[static_cast<size_t>(i)] - s * g;
                    d[static_cast<size_t>(i + 1)] = h + s * (c * g + s * d[static_cast<size_t>(i)]);
                    for (int k = 0; k < n; ++k) {
                        h = V(k, i + 1);
                        V(k, i + 1) = s * V(k, i) + c * h;
                        V(k, i) = c * V(k, i) - s * h;
                    }
                }
                p = -s * s2 * c3 * el1 * e[static_cast<size_t>(l)] / dl1;
                e[static_cast<size_t>(l)] = s * p;
                d[static_cast<size_t>(l)] = c * p;
            } while (std::fabs(e[static_cast<size_t>(l)]) > eps * tst1 && iter < 60);
        }
        d[static_cast<size_t>(l)] += f;
        e[static_cast<size_t>(l)] = 0.0;
    }
    for (int i = 0; i < n - 1; ++i) {
        int k = i;
        double p = d[static_cast<size_t>(i)];
        for (int j = i + 1; j < n; ++j) if (d[static_cast<size_t>(j)] < p) { k = j; p = d[static_cast<size_t>(j)]; }
        if (k != i) {
            d[static_cast<size_t>(k)] = d[static_cast<size_t>(i)];
            d[static_cast<size_t>(i)] = p;
            for (int j = 0; j < n; ++j) std::swap(V(j, i), V(j, k));
        }
    }
}

// ------------------------------------------------------------------------------------------------ the plate

/** @brief A curved beam on the plate (a bridge): its polyline, bending stiffness EI and mass per length. */
struct Beam {
    std::vector<Pt> pts;
    double ei = 0.0, mass = 0.0;
};

/** @brief What the Rayleigh-Ritz solver needs. */
struct PlateSpec {
    double a = 1.0, b = 1.0;                 ///< the bounding rectangle, m (x across the grain, y along it)
    double dx = 1.0, dy = 1.0, d12 = 0.0, d66 = 0.0;   ///< bending stiffnesses, N m
    double m = 1.0;                          ///< mass per area, kg/m^2
    std::vector<Pt> outline;                 ///< the polygon (m); empty: the whole rectangle
    std::vector<Beam> beams;
    double rim = 0.0;                        ///< support stiffness per length along the outline, N/m^2
    int nx = 18, ny = 18;                    ///< basis functions per direction
    double grid = 0.015;                     ///< integration step, m
};

/** @brief The solved plate: frequencies (Hz, ascending) and the basis coefficients of each mass-normalised mode. */
struct PlateModes {
    int basis = 0;
    std::vector<double> freq;
    std::vector<std::vector<double>> coef;
    double a = 1.0, b = 1.0;
    int nx = 0, ny = 0;
    /** @brief Mode @p m's displacement at (x, y). */
    double shape(int m, double x, double y) const
    {
        double s = 0.0;
        for (int i = 0; i < nx; ++i) {
            const double si = std::sin((i + 1) * kPiD * x / a);
            for (int j = 0; j < ny; ++j) s += coef[static_cast<size_t>(m)][static_cast<size_t>(i * ny + j)] * si * std::sin((j + 1) * kPiD * y / b);
        }
        return s;
    }
};

bool insidePolygon(const std::vector<Pt>& poly, double x, double y)
{
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const double xi = poly[i][0], yi = poly[i][1], xj = poly[j][0], yj = poly[j][1];
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) in = !in;
    }
    return in;
}

/** @brief Points every @p ds along a polyline, with the unit tangent there. */
std::vector<std::array<double, 4>> samplePolyline(const std::vector<Pt>& pts, double ds, bool closed)
{
    std::vector<std::array<double, 4>> out;
    const size_t segs = closed ? pts.size() : pts.size() - 1;
    for (size_t s = 0; s < segs; ++s) {
        const Pt p0 = pts[s], p1 = pts[(s + 1) % pts.size()];
        const double dx = p1[0] - p0[0], dy = p1[1] - p0[1], len = std::hypot(dx, dy);
        if (len <= 0.0) continue;
        const int n = std::max(1, static_cast<int>(std::ceil(len / ds)));
        for (int k = 0; k < n; ++k) {
            const double t = (k + 0.5) / n;
            out.push_back({ p0[0] + t * dx, p0[1] + t * dy, dx / len, dy / len });
        }
    }
    return out;
}

PlateModes solvePlate(const PlateSpec& ps, int keep)
{
    const int nx = ps.nx, ny = ps.ny, N = nx * ny;
    std::vector<double> K(static_cast<size_t>(N * N), 0.0), M(static_cast<size_t>(N * N), 0.0);
    // The area: a midpoint grid on the rectangle, the points inside the outline.
    const int px = std::max(8, static_cast<int>(std::ceil(ps.a / ps.grid))), py = std::max(8, static_cast<int>(std::ceil(ps.b / ps.grid)));
    const double hx = ps.a / px, hy = ps.b / py, w = hx * hy;
    std::vector<double> S(static_cast<size_t>(nx * px)), S1(S.size()), S2(S.size());
    for (int i = 0; i < nx; ++i) {
        const double k = (i + 1) * kPiD / ps.a;
        for (int p = 0; p < px; ++p) {
            const double x = (p + 0.5) * hx;
            S[static_cast<size_t>(i * px + p)] = std::sin(k * x);
            S1[static_cast<size_t>(i * px + p)] = k * std::cos(k * x);
            S2[static_cast<size_t>(i * px + p)] = -k * k * std::sin(k * x);
        }
    }
    std::vector<double> X0(static_cast<size_t>(nx * nx)), X2(X0.size()), X20(X0.size()), X1(X0.size());
    std::vector<double> T(static_cast<size_t>(ny)), T1(T.size()), T2(T.size());
    std::vector<int> cols;
    for (int q = 0; q < py; ++q) {
        const double y = (q + 0.5) * hy;
        cols.clear();
        for (int p = 0; p < px; ++p)
            if (ps.outline.empty() || insidePolygon(ps.outline, (p + 0.5) * hx, y)) cols.push_back(p);
        if (cols.empty()) continue;
        for (int i = 0; i < nx; ++i)
            for (int k = 0; k < nx; ++k) {
                double a0 = 0.0, a2 = 0.0, a20 = 0.0, a1 = 0.0;
                for (int p : cols) {
                    const size_t ip = static_cast<size_t>(i * px + p), kp = static_cast<size_t>(k * px + p);
                    a0 += S[ip] * S[kp];
                    a2 += S2[ip] * S2[kp];
                    a20 += S2[ip] * S[kp];
                    a1 += S1[ip] * S1[kp];
                }
                X0[static_cast<size_t>(i * nx + k)] = a0;
                X2[static_cast<size_t>(i * nx + k)] = a2;
                X20[static_cast<size_t>(i * nx + k)] = a20;
                X1[static_cast<size_t>(i * nx + k)] = a1;
            }
        for (int j = 0; j < ny; ++j) {
            const double k = (j + 1) * kPiD / ps.b;
            T[static_cast<size_t>(j)] = std::sin(k * y);
            T1[static_cast<size_t>(j)] = k * std::cos(k * y);
            T2[static_cast<size_t>(j)] = -k * k * std::sin(k * y);
        }
        for (int i = 0; i < nx; ++i)
            for (int k = 0; k < nx; ++k) {
                const size_t ik = static_cast<size_t>(i * nx + k), ki = static_cast<size_t>(k * nx + i);
                const double x0 = X0[ik] * w, x2 = X2[ik] * w, x20 = X20[ik] * w, x02 = X20[ki] * w, x1 = X1[ik] * w;
                for (int j = 0; j < ny; ++j)
                    for (int l = 0; l < ny; ++l) {
                        const size_t r = static_cast<size_t>((i * ny + j) * N + (k * ny + l));
                        const double tt = T[static_cast<size_t>(j)] * T[static_cast<size_t>(l)];
                        K[r] += ps.dx * x2 * tt + ps.dy * x0 * T2[static_cast<size_t>(j)] * T2[static_cast<size_t>(l)]
                              + ps.d12 * (x20 * T[static_cast<size_t>(j)] * T2[static_cast<size_t>(l)] + x02 * T2[static_cast<size_t>(j)] * T[static_cast<size_t>(l)])
                              + 4.0 * ps.d66 * x1 * T1[static_cast<size_t>(j)] * T1[static_cast<size_t>(l)];
                        M[r] += ps.m * x0 * tt;
                    }
            }
    }
    // Lines: the bridges (mass and bending along their tangent) and the rim (a stiff support).
    std::vector<double> u(static_cast<size_t>(N)), c(static_cast<size_t>(N));
    auto basisAt = [&](double x, double y, double tx, double ty) {
        for (int i = 0; i < nx; ++i) {
            const double kx = (i + 1) * kPiD / ps.a, si = std::sin(kx * x), ci = std::cos(kx * x);
            for (int j = 0; j < ny; ++j) {
                const double ky = (j + 1) * kPiD / ps.b, sj = std::sin(ky * y), cj = std::cos(ky * y);
                u[static_cast<size_t>(i * ny + j)] = si * sj;
                const double pxx = -kx * kx * si * sj, pyy = -ky * ky * si * sj, pxy = kx * ky * ci * cj;
                c[static_cast<size_t>(i * ny + j)] = tx * tx * pxx + 2.0 * tx * ty * pxy + ty * ty * pyy;
            }
        }
    };
    auto rank1 = [&](std::vector<double>& A, const std::vector<double>& x, double s) {
        for (int r = 0; r < N; ++r) {
            const double xr = x[static_cast<size_t>(r)] * s;
            if (xr == 0.0) continue;
            double* row = &A[static_cast<size_t>(r * N)];
            for (int q = 0; q < N; ++q) row[q] += xr * x[static_cast<size_t>(q)];
        }
    };
    const double ds = 0.01;
    for (const Beam& bm : ps.beams)
        for (const auto& p : samplePolyline(bm.pts, ds, false)) {
            basisAt(p[0], p[1], p[2], p[3]);
            rank1(M, u, bm.mass * ds);
            rank1(K, c, bm.ei * ds);
        }
    if (!ps.outline.empty() && ps.rim > 0.0)
        for (const auto& p : samplePolyline(ps.outline, ds, true)) {
            if (p[0] < 1e-6 || p[1] < 1e-6 || p[0] > ps.a - 1e-6 || p[1] > ps.b - 1e-6) continue;   // the basis is 0 there
            basisAt(p[0], p[1], p[2], p[3]);
            rank1(K, u, ps.rim * ds);
        }
    // K v = w^2 M v:  M = L L^T,  C = L^-1 K L^-T,  C y = w^2 y,  v = L^-T y.
    PlateModes out;
    out.basis = N;
    out.a = ps.a;
    out.b = ps.b;
    out.nx = nx;
    out.ny = ny;
    if (!cholesky(M, N)) return out;
    forwardSolve(M, K, N);                          // X = L^-1 K
    std::vector<double> Xt(static_cast<size_t>(N * N));
    for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) Xt[static_cast<size_t>(j * N + i)] = K[static_cast<size_t>(i * N + j)];
    forwardSolve(M, Xt, N);                         // L^-1 X^T = (X L^-T)^T = C
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            const double s = 0.5 * (Xt[static_cast<size_t>(i * N + j)] + Xt[static_cast<size_t>(j * N + i)]);
            Xt[static_cast<size_t>(i * N + j)] = Xt[static_cast<size_t>(j * N + i)] = s;
        }
    std::vector<double> ev;
    symmetricEigen(Xt, N, ev);
    const int count = std::min(keep, N);
    for (int m = 0; m < count; ++m) {
        out.freq.push_back(std::sqrt(std::max(0.0, ev[static_cast<size_t>(m)])) / (2.0 * kPiD));
        // v = L^-T y: back substitution with L^T.
        std::vector<double> v(static_cast<size_t>(N));
        for (int i = N - 1; i >= 0; --i) {
            double t = Xt[static_cast<size_t>(i * N + m)];
            for (int k = i + 1; k < N; ++k) t -= M[static_cast<size_t>(k * N + i)] * v[static_cast<size_t>(k)];
            v[static_cast<size_t>(i)] = t / M[static_cast<size_t>(i * N + i)];
        }
        out.coef.push_back(std::move(v));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ the instruments

/** @brief One instrument: its case, its board, its strings' longest length, its felt. */
struct Instrument {
    double width, depth;               ///< the board's bounding rectangle, m
    std::vector<Pt> outline;           ///< normalised to the rectangle
    std::vector<Pt> mainBridge, bassBridge;   ///< normalised polylines, treble end first
    Pt micL, micC, micR;
    double lmax;                       ///< the longest string, m
    double m, dx, dy, d12, d66;        ///< the board
    double loss0, loss1;               ///< loss factor at 0 Hz and its rise per kHz
    double hardness, strikeShift, stringLoss;
};

const Instrument& instrument(int which)
{
    static const std::vector<Pt> kGrandOutline = { { 0, 0 }, { 1, 0 }, { 1, 0.12 }, { 0.93, 0.35 }, { 0.8, 0.55 }, { 0.62, 0.72 },
                                                   { 0.42, 0.86 }, { 0.22, 0.96 }, { 0.1, 1.0 }, { 0, 1.0 } };
    static const std::vector<Pt> kRect = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    static const Instrument kInstr[kPianoInstruments] = {
        { 1.45, 1.80, kGrandOutline, { { 0.88, 0.10 }, { 0.66, 0.33 }, { 0.46, 0.52 }, { 0.30, 0.66 }, { 0.19, 0.76 } },
          { { 0.10, 0.80 }, { 0.20, 0.92 } }, { 0.28, 0.62 }, { 0.5, 0.45 }, { 0.72, 0.28 },
          1.95, 5.2, 6600.0, 2100.0, 1050.0, 120.0, 0.02, 0.01, 1.0, 0.0, 1.0 },
        { 1.40, 1.40, kGrandOutline, { { 0.88, 0.10 }, { 0.66, 0.33 }, { 0.46, 0.52 }, { 0.30, 0.66 }, { 0.19, 0.76 } },
          { { 0.10, 0.80 }, { 0.20, 0.92 } }, { 0.28, 0.62 }, { 0.5, 0.45 }, { 0.72, 0.28 },
          1.50, 5.0, 6300.0, 2040.0, 1020.0, 120.0, 0.021, 0.01, 1.0, 0.0, 1.0 },
        { 1.42, 1.10, kRect, { { 0.86, 0.15 }, { 0.62, 0.40 }, { 0.40, 0.58 }, { 0.26, 0.70 } },
          { { 0.12, 0.72 }, { 0.20, 0.88 } }, { 0.28, 0.62 }, { 0.5, 0.45 }, { 0.72, 0.28 },
          1.22, 4.6, 5700.0, 1860.0, 960.0, 108.0, 0.022, 0.011, 1.0, 0.0, 1.0 },
        // The "leicht dumpf" piano of Dok. 5 (a K2000's): an upright, soft felt struck deeper, a board that lets go early.
        { 1.42, 1.10, kRect, { { 0.86, 0.15 }, { 0.62, 0.40 }, { 0.40, 0.58 }, { 0.26, 0.70 } },
          { { 0.12, 0.72 }, { 0.20, 0.88 } }, { 0.28, 0.62 }, { 0.5, 0.45 }, { 0.72, 0.28 },
          1.22, 4.6, 5700.0, 1860.0, 960.0, 108.0, 0.035, 0.02, 0.4, 0.3, 1.4 },
    };
    return kInstr[std::clamp(which, 0, kPianoInstruments - 1)];
}

std::vector<Pt> scaled(const std::vector<Pt>& p, double w, double d)
{
    std::vector<Pt> o;
    for (const Pt& q : p) o.push_back({ q[0] * w, q[1] * d });
    return o;
}

/** @brief The point at arc parameter @p t (0 .. 1) of a polyline. */
Pt alongPolyline(const std::vector<Pt>& p, double t)
{
    double total = 0.0;
    for (size_t i = 0; i + 1 < p.size(); ++i) total += std::hypot(p[i + 1][0] - p[i][0], p[i + 1][1] - p[i][1]);
    double want = std::clamp(t, 0.0, 1.0) * total;
    for (size_t i = 0; i + 1 < p.size(); ++i) {
        const double len = std::hypot(p[i + 1][0] - p[i][0], p[i + 1][1] - p[i][1]);
        if (want <= len || i + 2 == p.size()) {
            const double u = len > 0.0 ? std::min(1.0, want / len) : 0.0;
            return { p[i][0] + u * (p[i + 1][0] - p[i][0]), p[i][1] + u * (p[i + 1][1] - p[i][1]) };
        }
        want -= len;
    }
    return p.back();
}

double polylineLength(const std::vector<Pt>& p)
{
    double total = 0.0;
    for (size_t i = 0; i + 1 < p.size(); ++i) total += std::hypot(p[i + 1][0] - p[i][0], p[i + 1][1] - p[i][1]);
    return total;
}

/** @brief The board of an instrument: the plate's modes, cached (it does not depend on the knobs). */
const PlateModes& boardModes(int which)
{
    static std::mutex mtx;
    static std::map<int, PlateModes> cache;
    std::lock_guard<std::mutex> lock(mtx);
    auto it = cache.find(which);
    if (it != cache.end()) return it->second;
    const Instrument& ins = instrument(which);
    PlateSpec ps;
    ps.a = ins.width;
    ps.b = ins.depth;
    ps.dx = ins.dx;
    ps.dy = ins.dy;
    ps.d12 = ins.d12;
    ps.d66 = ins.d66;
    ps.m = ins.m;
    ps.outline = scaled(ins.outline, ins.width, ins.depth);
    ps.beams.push_back(Beam{ scaled(ins.mainBridge, ins.width, ins.depth), 470.0, 0.53 });
    ps.beams.push_back(Beam{ scaled(ins.bassBridge, ins.width, ins.depth), 300.0, 0.45 });
    ps.rim = 2e7;
    ps.nx = 18;
    ps.ny = 18;
    ps.grid = 0.015;
    return cache.emplace(which, solvePlate(ps, 200)).first->second;
}

// ------------------------------------------------------------------------------------------------ the scale

/** @brief Piecewise-linear interpolation over key indices (0 .. 87). */
double overKeys(double key, std::initializer_list<std::pair<double, double>> pts)
{
    const auto* b = pts.begin();
    const auto* e = pts.end();
    if (key <= b->first) {
        const auto& p0 = b[0];
        const auto& p1 = b[1];
        return p0.second + (key - p0.first) * (p1.second - p0.second) / (p1.first - p0.first);
    }
    for (const auto* p = b; p + 1 != e; ++p)
        if (key <= p[1].first) return p[0].second + (key - p[0].first) * (p[1].second - p[0].second) / (p[1].first - p[0].first);
    const auto& p0 = e[-2];
    const auto& p1 = e[-1];
    return p0.second + (key - p0.first) * (p1.second - p0.second) / (p1.first - p0.first);
}

constexpr double kSteelE = 2.0e11, kSteelRho = 7850.0;

/** @brief The strings' data of every key (length, wire, tension, B) before the tuning. */
void scaleStrings(PianoDesign& d, const Instrument& ins)
{
    for (int k = 0; k < kPianoKeys; ++k) {
        PianoKey& key = d.keys[static_cast<size_t>(k)];
        key.midi = kPianoLowKey + k;
        const double f = midiToHz(key.midi);
        // Length: the plain-wire law L ~ f^-0.9 from C8's 5.2 cm, saturating at the case's longest string.
        const double lpw = 0.052 * std::pow(4186.0 / f, 0.9);
        key.length = std::pow(std::pow(lpw, -3.0) + std::pow(ins.lmax, -3.0), -1.0 / 3.0);
        key.wound = key.midi < kPianoBassBreak;
        key.strings = key.midi < 29 ? 1 : key.wound ? 2 : 3;
        double core;
        if (!key.wound) {
            // Plain wire: 0.775 mm at C8 to 1.2 mm at the break; the tension follows (about 700 to 800 N).
            const double x = std::pow((108.0 - key.midi) / (108.0 - kPianoBassBreak), 1.3);
            core = (0.775 + 0.425 * x) * 1e-3;
            key.mu = kSteelRho * kPiD * core * core / 4.0;
            key.tension = key.mu * std::pow(2.0 * key.length * f, 2.0);
        } else {
            // Wound: the tension rises from 1100 N at the break to 1700 N at A0, the core from 1.25 to 1.6 mm; the
            // winding brings the mass the pitch needs.
            const double x = (kPianoBassBreak - 1.0 - key.midi) / (kPianoBassBreak - 1.0 - kPianoLowKey);
            key.tension = 1100.0 + 600.0 * x;
            core = (1.25 + 0.35 * x) * 1e-3;
            key.mu = key.tension / std::pow(2.0 * key.length * f, 2.0);
        }
        key.coreArea = kPiD * core * core / 4.0;
        key.B = std::pow(kPiD, 3.0) * kSteelE * std::pow(core, 4.0) / (64.0 * key.tension * key.length * key.length) * d.spec.inharm;
        key.z0 = std::sqrt(key.tension * key.mu);
        key.damper = key.midi <= 88;
        // The strike point: an eighth of the length, towards a sixteenth in the top octave and a half.
        key.strikeRatio = (key.midi < 88 ? 0.122 : 0.122 - (key.midi - 88) * (0.122 - 0.065) / 20.0)
                        * (1.0 + 0.2 * std::clamp(static_cast<double>(d.spec.strike) + ins.strikeShift, -1.5, 1.5));
        // The hammer (Chaigne and Askenfelt 1994): the effective mass their stiffnesses were fitted with (C2 4.9 g, C4
        // 2.97 g, C7 2.2 g; the head's own mass is larger, the shank's flexibility takes part of it), stiffness and
        // exponent over the keyboard.
        const double kk = k;
        key.hammerMass = std::exp(overKeys(kk, { { 15, std::log(0.0049) }, { 39, std::log(0.00297) }, { 75, std::log(0.0022) } }));
        key.hammerP = overKeys(kk, { { 0, 2.3 }, { 39, 2.5 }, { 87, 3.2 } });
        const double logK = overKeys(kk, { { 15, 8.6 }, { 39, 9.65 }, { 75, 11.0 } });
        key.hammerEps = 0.85;
        key.q0 = std::pow(10.0, logK) * d.spec.hardness * d.spec.felt * ins.hardness / (1.0 - key.hammerEps);
        key.hammerTau = std::exp(overKeys(kk, { { 0, std::log(15e-6) }, { 87, std::log(4e-6) } }));
        key.oversample = key.midi >= 84 ? 8 : 4;
    }
}

/**
 * @brief The stretched tuning from the inharmonicity (PianoDesign.h): the fundamentals f0 of the ideal strings. A3 to A4
 *        equal-tempered; above, a note's first partial on the second of the note an octave under it; below, a note's
 *        fourth partial on the second of the note an octave above it (the sixth on the third in the lowest octave).
 */
void tune(PianoDesign& d)
{
    auto partial = [&](const PianoKey& k, int n, double f0) { return n * f0 * std::sqrt(1.0 + k.B * n * n); };
    std::vector<double> f1(kPianoKeys);
    for (int m = 57; m <= 69; ++m) f1[static_cast<size_t>(m - kPianoLowKey)] = midiToHz(m);
    for (int m = 70; m <= 108; ++m) {
        const PianoKey& lo = d.keys[static_cast<size_t>(m - 12 - kPianoLowKey)];
        const PianoKey& hi = d.keys[static_cast<size_t>(m - kPianoLowKey)];
        const double loF0 = f1[static_cast<size_t>(m - 12 - kPianoLowKey)] / std::sqrt(1.0 + lo.B);
        const double target = partial(lo, 2, loF0);                    // the lower note's second partial
        const double et = 2.0 * f1[static_cast<size_t>(m - 12 - kPianoLowKey)];
        f1[static_cast<size_t>(m - kPianoLowKey)] = et * std::pow(target / et, static_cast<double>(d.spec.stretch));
        (void)hi;
    }
    for (int m = 56; m >= kPianoLowKey; --m) {
        const PianoKey& lo = d.keys[static_cast<size_t>(m - kPianoLowKey)];
        const PianoKey& hi = d.keys[static_cast<size_t>(m + 12 - kPianoLowKey)];
        const int a = m < 33 ? 6 : 4, b = a / 2;
        const double hiF0 = f1[static_cast<size_t>(m + 12 - kPianoLowKey)] / std::sqrt(1.0 + hi.B);
        // a f0 sqrt(1 + B a^2) = the upper note's partial b
        const double loF0 = partial(hi, b, hiF0) / (a * std::sqrt(1.0 + lo.B * a * a));
        const double exact = loF0 * std::sqrt(1.0 + lo.B);
        const double et = 0.5 * f1[static_cast<size_t>(m + 12 - kPianoLowKey)];
        f1[static_cast<size_t>(m - kPianoLowKey)] = et * std::pow(exact / et, static_cast<double>(d.spec.stretch));
    }
    for (int k = 0; k < kPianoKeys; ++k) {
        PianoKey& key = d.keys[static_cast<size_t>(k)];
        key.f0 = f1[static_cast<size_t>(k)] / std::sqrt(1.0 + key.B);
        // The tension that tunes it (the length and the wire stay).
        key.tension = key.mu * std::pow(2.0 * key.length * key.f0, 2.0);
        key.z0 = std::sqrt(key.tension * key.mu);
    }
}

// ------------------------------------------------------------------------------------------------ the board

/** @brief The admittance at a point given its mode shapes (the modal sum plus the ribbed plate above the modes). */
cd admittanceAt(const PianoBoard& b, const float* shape, double hz)
{
    const double w = 2.0 * kPiD * hz;
    cd y = 0.0;
    for (size_t n = 0; n < b.freq.size(); ++n) {
        const double wn = 2.0 * kPiD * b.freq[n];
        const double phi = shape[n];
        y += cd(0.0, w) * phi * phi / cd(wn * wn - w * w, b.loss[n] * wn * w);
    }
    const double fc = 0.85 * b.modeTop;
    y += b.yInf / (1.0 + std::pow(fc / std::max(hz, 1.0), 4.0));
    return y;
}

void designBoard(PianoDesign& d, const Instrument& ins, const PlateModes& pm, uint64_t seed)
{
    PianoBoard& b = d.board;
    const double T = 1.0 / d.sampleRate;
    int count = 0;
    while (count < static_cast<int>(pm.freq.size()) && pm.freq[static_cast<size_t>(count)] < 1200.0) ++count;
    count = std::max(count, 1);
    b.modeTop = pm.freq[static_cast<size_t>(count - 1)];
    b.modes.resize(count);
    const int P = b.modes.padded;
    b.freq.assign(static_cast<size_t>(count), 0.0);
    b.loss.assign(static_cast<size_t>(count), 0.0);
    b.shape.assign(static_cast<size_t>(kPianoRegions * P), 0.0f);
    b.micL.assign(static_cast<size_t>(P), 0.0f);
    b.micC.assign(static_cast<size_t>(P), 0.0f);
    b.micR.assign(static_cast<size_t>(P), 0.0f);
    b.accR.assign(static_cast<size_t>(P), 0.0f);
    b.accI.assign(static_cast<size_t>(P), 0.0f);
    b.keyShape.assign(static_cast<size_t>(kPianoKeys * count), 0.0f);
    const std::vector<Pt> mainB = scaled(ins.mainBridge, ins.width, ins.depth), bassB = scaled(ins.bassBridge, ins.width, ins.depth);
    // The region points: twelve along the long bridge, four along the bass bridge.
    Pt region[kPianoRegions];
    for (int r = 0; r < kPianoMainRegions; ++r) region[r] = alongPolyline(mainB, r / (kPianoMainRegions - 1.0));
    for (int r = kPianoMainRegions; r < kPianoRegions; ++r)
        region[r] = alongPolyline(bassB, (r - kPianoMainRegions) / (kPianoRegions - kPianoMainRegions - 1.0));
    const Pt mics[3] = { { ins.micL[0] * ins.width, ins.micL[1] * ins.depth }, { ins.micC[0] * ins.width, ins.micC[1] * ins.depth },
                         { ins.micR[0] * ins.width, ins.micR[1] * ins.depth } };
    for (int n = 0; n < count; ++n) {
        const double f = pm.freq[static_cast<size_t>(n)], w = 2.0 * kPiD * f;
        const double eta = (ins.loss0 + ins.loss1 * f / 1000.0) * d.spec.boardLoss;
        b.freq[static_cast<size_t>(n)] = f;
        b.loss[static_cast<size_t>(n)] = eta;
        const double sigma = 0.5 * eta * w;
        const cd lam(-sigma, std::sqrt(std::max(0.0, w * w - sigma * sigma)));
        const cd p = std::exp(lam * T), g = (p - 1.0) / lam;
        b.modes.pr[static_cast<size_t>(n)] = static_cast<float>(p.real());
        b.modes.pi[static_cast<size_t>(n)] = static_cast<float>(p.imag());
        b.modes.gr[static_cast<size_t>(n)] = static_cast<float>(g.real());
        b.modes.gi[static_cast<size_t>(n)] = static_cast<float>(g.imag());
        for (int r = 0; r < kPianoRegions; ++r)
            b.shape[static_cast<size_t>(r * P + n)] = static_cast<float>(pm.shape(n, region[r][0], region[r][1]));
        // Radiation: the far field of a baffled plate is its acceleration, heard here at three listening points
        // (referred to 500 Hz), the lowest modes (little net volume) held back.
        const double corner = static_cast<double>(d.spec.bodyCorner);
        const double rad = f * f / (f * f + corner * corner) / (2.0 * kPiD * 500.0);
        b.micL[static_cast<size_t>(n)] = static_cast<float>(pm.shape(n, mics[0][0], mics[0][1]) * rad);
        b.micC[static_cast<size_t>(n)] = static_cast<float>(pm.shape(n, mics[1][0], mics[1][1]) * rad);
        b.micR[static_cast<size_t>(n)] = static_cast<float>(pm.shape(n, mics[2][0], mics[2][1]) * rad);
        b.accR[static_cast<size_t>(n)] = static_cast<float>(-2.0 * sigma);
        b.accI[static_cast<size_t>(n)] = static_cast<float>((w * w - sigma * sigma) / w);
    }
    // Every key's own bridge point (for its admittance).
    for (int k = 0; k < kPianoKeys; ++k) {
        const int midi = kPianoLowKey + k;
        const Pt at = midi >= kPianoBassBreak ? alongPolyline(mainB, (108.0 - midi) / (108.0 - kPianoBassBreak))
                                              : alongPolyline(bassB, (kPianoBassBreak - 1.0 - midi) / (kPianoBassBreak - 1.0 - kPianoLowKey));
        for (int n = 0; n < count; ++n) b.keyShape[static_cast<size_t>(k * count + n)] = static_cast<float>(pm.shape(n, at[0], at[1]));
    }
    // The ribbed plate's characteristic mobility, and the high bank above the modes: log-spaced resonators whose shapes
    // along the bridges are waves of the plate's bending wavenumber (the rib waveguides of Ege et al. 2013).
    const double dEq = std::sqrt(ins.dx * ins.dy);
    b.yInf = 1.0 / (8.0 * std::sqrt(dEq * ins.m));
    const double fLo = 0.85 * b.modeTop, fHi = std::min(0.45 * d.sampleRate, 18000.0);
    const int H = std::max(1, static_cast<int>(std::ceil(std::log2(fHi / fLo) * 8.5)));
    b.high.resize(H);
    const int HP = b.high.padded;
    b.highShape.assign(static_cast<size_t>(kPianoRegions * HP), 0.0f);
    b.highL.assign(static_cast<size_t>(HP), 0.0f);
    b.highR.assign(static_cast<size_t>(HP), 0.0f);
    Rng r;
    r.seed(mixSeed(seed, 0x48494748ull));   // "HIGH"
    const double mainLen = polylineLength(mainB);
    double pos[kPianoRegions];
    for (int q = 0; q < kPianoMainRegions; ++q) pos[q] = mainLen * q / (kPianoMainRegions - 1.0);
    for (int q = kPianoMainRegions; q < kPianoRegions; ++q)
        pos[q] = mainLen + 0.3 + polylineLength(bassB) * (q - kPianoMainRegions) / (kPianoRegions - kPianoMainRegions - 1.0);
    const double Q = 1.0 / (std::pow(2.0, 1.0 / 8.5) - 1.0);
    for (int j = 0; j < H; ++j) {
        const double f = fLo * std::pow(fHi / fLo, (j + 0.5) / H), w = 2.0 * kPiD * f;
        const double sigma = w / (2.0 * Q);
        const cd lam(-sigma, w);
        const cd p = std::exp(lam * T), g = (p - 1.0) / lam;
        b.high.pr[static_cast<size_t>(j)] = static_cast<float>(p.real());
        b.high.pi[static_cast<size_t>(j)] = static_cast<float>(p.imag());
        b.high.gr[static_cast<size_t>(j)] = static_cast<float>(g.real());
        b.high.gi[static_cast<size_t>(j)] = static_cast<float>(g.imag());
        const double kb = std::pow(w * w * ins.m / dEq, 0.25) / (2.0 * kPiD);
        const double psi = r.uniform();
        for (int q = 0; q < kPianoRegions; ++q)
            b.highShape[static_cast<size_t>(q * HP + j)] = static_cast<float>(std::cos(2.0 * kPiD * (kb * pos[q] + psi)));
        // A resonator's peak on a unit force is 1 / (2 sigma) of its state: 2 sigma Y_inf makes the bank's mean the
        // plate's mobility; the listening points see it with their own signs.
        const double c = 2.0 * sigma * b.yInf * f / 500.0 * std::pow(10.0, d.spec.highBank / 20.0);   // and the acceleration, as the modes
        b.highL[static_cast<size_t>(j)] = static_cast<float>(c * (0.5 + 0.5 * r.uniform()) * (r.uniform() < 0.5f ? -1.0 : 1.0));
        b.highR[static_cast<size_t>(j)] = static_cast<float>(c * (0.5 + 0.5 * r.uniform()) * (r.uniform() < 0.5f ? -1.0 : 1.0));
    }
}

// ------------------------------------------------------------------------------------------------ the strings

/** @brief The roots of prod(z - s_j) + kappa sum_j prod_(l != j)(z - s_l) (Durand-Kerner), n <= 3. */
void courseEigen(const cd* s, int n, cd kappa, cd* roots)
{
    auto poly = [&](cd z) {
        cd prod = 1.0, sum = 0.0;
        for (int j = 0; j < n; ++j) {
            prod *= (z - s[j]);
            cd p = 1.0;
            for (int l = 0; l < n; ++l) if (l != j) p *= (z - s[l]);
            sum += p;
        }
        return prod + kappa * sum;
    };
    for (int m = 0; m < n; ++m) roots[m] = s[m] - kappa + cd(0.0, 1e-3 * (m + 1));
    for (int it = 0; it < 200; ++it) {
        double moved = 0.0, scale = 1e-300;
        for (int m = 0; m < n; ++m) {
            cd den = 1.0;
            for (int l = 0; l < n; ++l) if (l != m) den *= (roots[m] - roots[l]);
            if (std::abs(den) < 1e-300) den = 1e-300;
            const cd step = poly(roots[m]) / den;
            roots[m] -= step;
            moved = std::max(moved, std::abs(step));
            scale = std::max(scale, std::abs(roots[m]));
        }
        if (moved < 1e-13 * scale) break;
    }
}

double sinc(double x) { return std::fabs(x) < 1e-9 ? 1.0 : std::sin(x) / x; }

/**
 * @brief The coupling kappa = 2 f0 Z0 Y, bounded where a partial meets a board resonance: the perturbation that gives it
 *        holds for weak coupling; where the board's mobility peaks, string and board share the energy instead, and the
 *        partial cannot lose it faster than the board's own mode does. The course's share of the bridge's losses
 *        (N Re kappa) is held under half the board's modal decay rate at that frequency, the phase kept.
 */
cd boundedCoupling(cd kappa, int strings, double hz, const Instrument& ins, double lossFactor)
{
    const double boardSigma = 0.5 * (ins.loss0 + ins.loss1 * hz / 1000.0) * lossFactor * 2.0 * kPiD * hz;
    const double cap = 0.5 * boardSigma / strings;
    // The same bound on the frequency's shift: near a board mode the string's partial is pulled, but by no more than
    // the coupling can carry.
    return cd(std::min(kappa.real(), cap), std::clamp(kappa.imag(), -0.5 * cap, 0.5 * cap));
}

void designKey(PianoDesign& d, const Instrument& ins, int k, Rng& r)
{
    PianoKey& key = d.keys[static_cast<size_t>(k)];
    const double fs = d.sampleRate, T = 1.0 / fs;
    const int N = key.strings;
    // The course: the strings' mistuning (the unison width plus the condition), never quite zero.
    double cents[3] = { 0.0, 0.0, 0.0 };
    const double u = d.spec.unison;
    if (N == 2) { cents[0] = -0.5 * u; cents[1] = 0.5 * u; }
    if (N == 3) { cents[0] = -0.5 * u; cents[1] = 0.0; cents[2] = 0.5 * u; }
    for (int j = 0; j < N; ++j) cents[j] += d.spec.condition * r.gaussian() * 0.5 + 0.03 * j;
    double irregular[3];
    for (int j = 0; j < N; ++j) irregular[j] = 1.0 + 0.03 * r.bipolar();
    // The partials up to 12 kHz (or 0.45 fs), at most 140.
    const double fTop = std::min(0.45 * fs, 12000.0);
    int K = 0;
    while (K < 140 && (K + 1) * key.f0 * std::sqrt(1.0 + key.B * (K + 1) * (K + 1)) < fTop) ++K;
    key.partials = std::max(K, 1);
    const int KH = std::min(key.partials, 20);   // the horizontal course: the partials that carry its aftersound
    const int total = N * key.partials + N * KH;
    key.modes.resize(total);
    const int P = key.modes.padded;
    for (std::vector<float>* v : { &key.spr, &key.spi, &key.sgr, &key.sgi, &key.br, &key.bi, &key.hr, &key.hi, &key.dr, &key.di,
                                   &key.omegaT, &key.damperSigma })
        v->assign(static_cast<size_t>(P), 0.0f);
    const float* shape = &d.board.keyShape[static_cast<size_t>(k * static_cast<int>(d.board.freq.size()))];
    const double b1 = key.wound ? 0.4 : 0.2, b3 = (key.wound ? 1e-8 : 5e-9) * ins.stringLoss;
    const double hammerWidth = key.midi >= 84 ? 0.008 : 0.012;
    const double Ts = T / key.oversample;
    int mode = 0;
    for (int pol = 0; pol < 2; ++pol) {
        const int parts = pol == 0 ? key.partials : KH;
        for (int kk = 1; kk <= parts; ++kk) {
            const double fk = kk * key.f0 * std::sqrt(1.0 + key.B * kk * kk), wk = 2.0 * kPiD * fk;
            const cd Y = admittanceAt(d.board, shape, fk) * static_cast<double>(d.spec.impedance) * static_cast<double>(d.spec.coupling) * (pol == 0 ? 1.0 : 0.25);
            const cd kappa = boundedCoupling(2.0 * key.f0 * key.z0 * Y, N, fk, ins, d.spec.boardLoss);
            const double sigmaInt = b1 + b3 * wk * wk;
            cd s[3], lam[3];
            for (int j = 0; j < N; ++j) s[j] = cd(-sigmaInt, wk * std::pow(2.0, cents[j] / 1200.0));
            courseEigen(s, N, kappa, lam);
            // The hammer's force on a string, per unit force on the course, in the partial's modal equation.
            const double spatial = std::sin(kk * kPiD * key.strikeRatio) * sinc(kk * kPiD * hammerWidth / (2.0 * key.length));
            const double common = 2.0 / (key.mu * key.length) * spatial / N * (pol == 0 ? 1.0 : 0.05);
            const double wOut = ((kk & 1) ? -1.0 : 1.0) * kk * kPiD * key.tension / key.length * (pol == 0 ? 1.0 : 0.3);
            const cd c(0.0, -1.0 / wk);
            for (int m = 0; m < N; ++m) {
                cd v[3], sum = 0.0, vv = 0.0, vb = 0.0;
                for (int j = 0; j < N; ++j) {
                    v[j] = 1.0 / (s[j] - lam[m]);
                    sum += v[j];
                    vv += v[j] * v[j];
                    vb += v[j] * (common * irregular[j]);
                }
                const cd gamma = vb / vv;
                const cd bridge = wOut * c * sum * gamma;
                const cd hammer = pol == 0 ? spatial * c * (sum / static_cast<double>(N)) * gamma : cd(0.0);
                const cd disp = static_cast<double>(kk) * c * (sum / static_cast<double>(N)) * gamma;
                const cd p = std::exp(lam[m] * T), g = (p - 1.0) / lam[m];
                const cd ps = std::exp(lam[m] * Ts), gs = (ps - 1.0) / lam[m];
                const size_t q = static_cast<size_t>(mode++);
                key.modes.pr[q] = static_cast<float>(p.real());
                key.modes.pi[q] = static_cast<float>(p.imag());
                key.modes.gr[q] = static_cast<float>(g.real());
                key.modes.gi[q] = static_cast<float>(g.imag());
                key.spr[q] = static_cast<float>(ps.real());
                key.spi[q] = static_cast<float>(ps.imag());
                key.sgr[q] = static_cast<float>(gs.real());
                key.sgi[q] = static_cast<float>(gs.imag());
                key.br[q] = static_cast<float>(bridge.real());
                key.bi[q] = static_cast<float>(bridge.imag());
                key.hr[q] = static_cast<float>(hammer.real());
                key.hi[q] = static_cast<float>(hammer.imag());
                key.dr[q] = static_cast<float>(disp.real());
                key.di[q] = static_cast<float>(disp.imag());
                key.omegaT[q] = static_cast<float>(lam[m].imag() * T);
                // The damper (Lehtonen et al. 2007): tens of dB per second, less on the high partials and the long strings.
                key.damperSigma[q] = key.damper ? static_cast<float>(d.spec.damperRate * std::pow(0.6 / key.length, static_cast<double>(d.spec.damperLength))
                                                                     / (1.0 + fk / 2500.0)) : 0.0f;
            }
        }
    }
    // The tension (Kirchhoff-Carrier, Bank 2010 eq. 26): sum over the partials of k^2 q_k^2, time-averaged.
    key.tensionGain = kSteelE * key.coreArea * kPiD * kPiD / (16.0 * key.length * key.length * key.tension);
    // The longitudinal modes: c_L from the core's stiffness over the whole mass.
    const double cL = std::sqrt(kSteelE * key.coreArea / key.mu);
    key.longCount = 0;
    for (int n = 1; n <= kPianoLongModes; ++n) {
        const double f = n * cL / (2.0 * key.length);
        if (f > std::min(0.4 * fs, 9000.0)) break;
        const double w = 2.0 * kPiD * f, sigma = w / (2.0 * 80.0);
        const cd lam(-sigma, w), p = std::exp(lam * T), g = (p - 1.0) / lam;
        const int i = key.longCount++;
        key.lpr[i] = static_cast<float>(p.real());
        key.lpi[i] = static_cast<float>(p.imag());
        key.lgr[i] = static_cast<float>(g.real());
        key.lgi[i] = static_cast<float>(g.imag());
        key.lcr[i] = static_cast<float>(2.0 * sigma);   // peak gain 1 (the positive frequency's half resonates)
        key.lci[i] = 0.0f;
    }
    key.longDrive = kSteelE * key.coreArea / (2.0 * key.tension * key.tension);
    // The sympathetic string: the course's symmetric mode of its first partials, driven by the bridge's acceleration;
    // its bridge force is Re(-i 2 N T / (L w_k) z) (the end's motion projected on the partial, PianoDesign.h).
    const int S = std::min(kPianoSymPartials, key.partials);
    key.sym.resize(S);
    key.symOutR.assign(static_cast<size_t>(key.sym.padded), 0.0f);
    key.symOutI.assign(static_cast<size_t>(key.sym.padded), 0.0f);
    key.symDamper.assign(static_cast<size_t>(key.sym.padded), 0.0f);
    for (int kk = 1; kk <= S; ++kk) {
        const double fk = kk * key.f0 * std::sqrt(1.0 + key.B * kk * kk), wk = 2.0 * kPiD * fk;
        const cd Y = admittanceAt(d.board, shape, fk) * static_cast<double>(d.spec.impedance) * static_cast<double>(d.spec.coupling);
        const cd lam = cd(-(b1 + b3 * wk * wk), wk) - static_cast<double>(N) * boundedCoupling(2.0 * key.f0 * key.z0 * Y, N, fk, ins, d.spec.boardLoss);
        const cd p = std::exp(lam * T), g = (p - 1.0) / lam;
        const size_t q = static_cast<size_t>(kk - 1);
        key.sym.pr[q] = static_cast<float>(p.real());
        key.sym.pi[q] = static_cast<float>(p.imag());
        key.sym.gr[q] = static_cast<float>(g.real());
        key.sym.gi[q] = static_cast<float>(g.imag());
        key.symOutI[q] = static_cast<float>(-2.0 * N * key.tension / (key.length * wk));
        key.symDamper[q] = key.damper ? static_cast<float>(d.spec.damperRate * std::pow(0.6 / key.length, static_cast<double>(d.spec.damperLength))
                                                           / (1.0 + fk / 2500.0)) : 0.0f;
    }
    // Where the key meets the board: between two region points of its bridge.
    if (key.midi >= kPianoBassBreak) {
        const double t = (108.0 - key.midi) / (108.0 - kPianoBassBreak) * (kPianoMainRegions - 1);
        key.region = std::min(static_cast<int>(t), kPianoMainRegions - 2);
        key.regionW1 = static_cast<float>(t - key.region);
    } else {
        const double t = (kPianoBassBreak - 1.0 - key.midi) / (kPianoBassBreak - 1.0 - kPianoLowKey) * (kPianoRegions - kPianoMainRegions - 1);
        key.region = kPianoMainRegions + std::min(static_cast<int>(t), kPianoRegions - kPianoMainRegions - 2);
        key.regionW1 = static_cast<float>(t - (key.region - kPianoMainRegions));
    }
    key.regionW0 = 1.0f - key.regionW1;
    key.pan = static_cast<float>((key.midi - 64.5) / 43.5);
}

std::shared_ptr<PianoDesign> build(const PianoSpec& spec, double sampleRate)
{
    auto d = std::make_shared<PianoDesign>();
    d->spec = spec;
    d->sampleRate = sampleRate;
    d->keys.resize(kPianoKeys);
    const Instrument& ins = instrument(spec.instrument);
    scaleStrings(*d, ins);
    tune(*d);
    const uint64_t seed = 0x5041524850494E4Full + static_cast<uint64_t>(spec.instrument);   // "PARHPINO"
    designBoard(*d, ins, boardModes(spec.instrument), seed);
    Rng r;
    r.seed(mixSeed(seed, 0x4B455953ull));   // "KEYS"
    for (int k = 0; k < kPianoKeys; ++k) designKey(*d, ins, k, r);
    // The voicing (PianoSpec::voiceBass, voiceTreble): the force a key's strings put into the bridge, in dB linear over
    // the keys from C4 to either end -- what a technician does with the felt and the regulation, here on the output.
    for (int k = 0; k < kPianoKeys; ++k) {
        PianoKey& key = d->keys[static_cast<size_t>(k)];
        const double db = key.midi < 60 ? d->spec.voiceBass * (60.0 - key.midi) / (60.0 - kPianoLowKey)
                                        : d->spec.voiceTreble * (key.midi - 60.0) / (108.0 - 60.0);
        if (db == 0.0) continue;
        const float g = static_cast<float>(std::pow(10.0, db / 20.0));
        for (float& x : key.br) x *= g;
        for (float& x : key.bi) x *= g;
    }
    d->outGain = 50.0f;   // a mezzo-forte C4 at about -13 dBFS with the level at -6 dB
    return d;
}

} // namespace

std::shared_ptr<const PianoDesign> designPiano(const PianoSpec& spec, double sampleRate)
{
    static std::mutex mtx;
    static std::vector<std::shared_ptr<const PianoDesign>> cache;
    {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& c : cache) if (c->spec == spec && c->sampleRate == sampleRate) return c;
    }
    std::shared_ptr<const PianoDesign> d = build(spec, sampleRate);
    std::lock_guard<std::mutex> lock(mtx);
    cache.push_back(d);
    if (cache.size() > 6) cache.erase(cache.begin());
    return d;
}

std::complex<double> pianoAdmittance(const PianoDesign& d, int key, double hz)
{
    const int count = static_cast<int>(d.board.freq.size());
    return admittanceAt(d.board, &d.board.keyShape[static_cast<size_t>(std::clamp(key, 0, kPianoKeys - 1) * count)], hz)
         * static_cast<double>(d.spec.impedance) * static_cast<double>(d.spec.coupling);
}

std::vector<double> pianoPlateTest(double a, double b, double dx, double dy, double d12, double d66, double massPerArea, int count)
{
    PlateSpec ps;
    ps.a = a;
    ps.b = b;
    ps.dx = dx;
    ps.dy = dy;
    ps.d12 = d12;
    ps.d66 = d66;
    ps.m = massPerArea;
    ps.nx = 10;
    ps.ny = 10;
    ps.grid = std::min(a, b) / 60.0;
    return solvePlate(ps, count).freq;
}

} // namespace parh
