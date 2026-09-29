/**
 * @file Corpus.cpp
 * @brief The variable-order step model and the constrained sampler (Corpus.h).
 */
#include "parh/compose/Corpus.h"
#include <algorithm>
#include <cmath>

namespace parh {

namespace {
constexpr int A = kCorpusAlpha;
}

StepModel::StepModel(CorpusRoleId role) : r_(kCorpusRoles[static_cast<int>(role)])
{
    // Order 0, smoothed (every step at least a little likely).
    double p0[A];
    double n0 = 0.0;
    for (int c = 0; c < A; ++c) n0 += r_.t0[c] + 0.5;
    for (int c = 0; c < A; ++c) p0[c] = (r_.t0[c] + 0.5) / n0;
    // Order 1 with Witten-Bell back-off: lambda = N / (N + T), T the distinct continuations.
    p1_.assign(static_cast<size_t>(A * A), 0.0f);
    for (int a = 0; a < A; ++a) {
        double n = 0.0, t = 0.0;
        for (int c = 0; c < A; ++c) { n += r_.t1[a][c]; if (r_.t1[a][c] > 0) t += 1.0; }
        const double lam = n > 0.0 ? n / (n + t) : 0.0;
        for (int c = 0; c < A; ++c)
            p1_[static_cast<size_t>(a * A + c)] = static_cast<float>((n > 0.0 ? lam * r_.t1[a][c] / n : 0.0) + (1.0 - lam) * p0[c]);
    }
    // Order 2, from the sparse counts, backed off to order 1.
    p2_.assign(static_cast<size_t>(A * A * A), 0.0f);
    std::vector<double> n2(static_cast<size_t>(A * A), 0.0), t2(static_cast<size_t>(A * A), 0.0);
    for (int i = 0; i < r_.t2Count; ++i) {
        const CorpusTriple& x = r_.t2[i];
        n2[static_cast<size_t>(x.a * A + x.b)] += x.n;
        t2[static_cast<size_t>(x.a * A + x.b)] += 1.0;
    }
    for (int a = 0; a < A; ++a)
        for (int b = 0; b < A; ++b) {
            const size_t ab = static_cast<size_t>(a * A + b);
            const double lam = n2[ab] > 0.0 ? n2[ab] / (n2[ab] + t2[ab]) : 0.0;
            for (int c = 0; c < A; ++c) p2_[ab * A + static_cast<size_t>(c)] = static_cast<float>((1.0 - lam) * p1_[static_cast<size_t>(b * A + c)]);
        }
    for (int i = 0; i < r_.t2Count; ++i) {
        const CorpusTriple& x = r_.t2[i];
        const size_t ab = static_cast<size_t>(x.a * A + x.b);
        const double lam = n2[ab] / (n2[ab] + t2[ab]);
        p2_[ab * A + x.c] += static_cast<float>(lam * x.n / n2[ab]);
    }
}

const StepModel& corpusModel(CorpusRoleId role)
{
    static const StepModel kModels[kCorpusRoleCount] = { StepModel(CorpusRoleId::Lead), StepModel(CorpusRoleId::Piano),
                                                      StepModel(CorpusRoleId::Acid), StepModel(CorpusRoleId::Bass) };
    return kModels[static_cast<int>(role)];
}

double StepModel::p(int a, int b, int c) const
{
    if (b < 0) {
        double n = 0.0;
        for (int k = 0; k < A; ++k) n += r_.t0[k] + 0.5;
        return (r_.t0[c] + 0.5) / n;
    }
    if (a < 0) return p1_[static_cast<size_t>(b * A + c)];
    return p2_[static_cast<size_t>((a * A + b) * A + c)];
}

double StepModel::onset(int step, bool prev) const
{
    const uint16_t* row = r_.onset[(step & 31) + (prev ? 32 : 0)];
    return (row[1] + 0.5) / (row[0] + row[1] + 1.0);
}
double StepModel::accent(int step) const
{
    const uint16_t* row = r_.accent[step & 15];
    return (row[1] + 0.5) / (row[0] + row[1] + 1.0);
}
double StepModel::slide(int step) const
{
    const uint16_t* row = r_.slide[step & 15];
    return (row[1] + 0.5) / (row[0] + row[1] + 1.0);
}

bool sampleConstrained(const StepModel& model, int n, const std::vector<std::array<bool, kCorpusAlpha>>& allowed,
                       const std::function<double(int, int, int, int)>& shape, Rng& r, std::vector<int>& out)
{
    out.clear();
    if (n <= 0 || static_cast<int>(allowed.size()) < n) return false;
    auto sh = [&](int i, int a, int b, int c) { return shape ? std::max(0.0, shape(i, a, b, c)) : 1.0; };
    // beta[i][a][b]: the weight of every completion from position i on, given steps a at i-1 and b at i (i >= 1).
    std::vector<std::vector<double>> beta(static_cast<size_t>(n), std::vector<double>(static_cast<size_t>(A * A), 0.0));
    for (int a = 0; a < A; ++a)
        for (int b = 0; b < A; ++b)
            beta[static_cast<size_t>(n - 1)][static_cast<size_t>(a * A + b)] = allowed[static_cast<size_t>(n - 1)][static_cast<size_t>(b)] ? 1.0 : 0.0;
    for (int i = n - 2; i >= 1; --i) {
        double mx = 0.0;
        for (int a = 0; a < A; ++a)
            for (int b = 0; b < A; ++b) {
                if (!allowed[static_cast<size_t>(i)][static_cast<size_t>(b)]) continue;
                double s = 0.0;
                for (int c = 0; c < A; ++c)
                    if (allowed[static_cast<size_t>(i + 1)][static_cast<size_t>(c)])
                        s += model.p(a, b, c) * sh(i + 1, a, b, c) * beta[static_cast<size_t>(i + 1)][static_cast<size_t>(b * A + c)];
                beta[static_cast<size_t>(i)][static_cast<size_t>(a * A + b)] = s;
                mx = std::max(mx, s);
            }
        if (mx <= 0.0) return false;
        for (double& v : beta[static_cast<size_t>(i)]) v /= mx;   // rescaled: the proportions within a position are what count
    }
    // Position 0, then forward.
    auto draw = [&](const std::vector<double>& w) {
        double sum = 0.0;
        for (double v : w) sum += v;
        if (sum <= 0.0) return -1;
        double x = static_cast<double>(r.uniform()) * sum;
        for (size_t k = 0; k < w.size(); ++k) { if (x < w[k]) return static_cast<int>(k); x -= w[k]; }
        for (size_t k = w.size(); k > 0; --k) if (w[k - 1] > 0.0) return static_cast<int>(k - 1);
        return -1;
    };
    std::vector<double> w(A, 0.0);
    for (int c = 0; c < A; ++c) {
        if (!allowed[0][static_cast<size_t>(c)]) continue;
        double tail = 1.0;
        if (n > 1) {
            tail = 0.0;
            for (int d = 0; d < A; ++d)
                if (allowed[1][static_cast<size_t>(d)]) tail += model.p(-1, c, d) * sh(1, -1, c, d) * beta[1][static_cast<size_t>(c * A + d)];
        }
        w[static_cast<size_t>(c)] = model.p(-1, -1, c) * sh(0, -1, -1, c) * tail;
    }
    int x0 = draw(w);
    if (x0 < 0) return false;
    out.push_back(x0);
    if (n == 1) return true;
    for (int d = 0; d < A; ++d)
        w[static_cast<size_t>(d)] = allowed[1][static_cast<size_t>(d)] ? model.p(-1, x0, d) * sh(1, -1, x0, d) * beta[1][static_cast<size_t>(x0 * A + d)] : 0.0;
    int x1 = draw(w);
    if (x1 < 0) return false;
    out.push_back(x1);
    for (int i = 2; i < n; ++i) {
        const int a = out[static_cast<size_t>(i - 2)], b = out[static_cast<size_t>(i - 1)];
        for (int c = 0; c < A; ++c)
            w[static_cast<size_t>(c)] = allowed[static_cast<size_t>(i)][static_cast<size_t>(c)]
                ? model.p(a, b, c) * sh(i, a, b, c) * beta[static_cast<size_t>(i)][static_cast<size_t>(b * A + c)] : 0.0;
        const int c = draw(w);
        if (c < 0) return false;
        out.push_back(c);
    }
    return true;
}

} // namespace parh
