#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// Direct C++ port of planFromSpectrum() from the HTML version.
namespace chroma {
struct Zone { double f = 0, gain = 0, q = 1, score = 0; bool present = false; };
struct Tone { double f = 0, peak = 0, broad = 0; bool found = false; };
struct Plan { Zone mud, harsh; Tone warm, pres, air; double bWarm = 0, bPres = 0, bAir = 0; };

// avgDb: averaged power spectrum in dB, binHz: Hz per bin. th/cap come from the intensity setting.
inline Plan planFromSpectrum(const std::vector<double>& avgDb, double binHz, double th, double cap)
{
    constexpr int KPO = 12; constexpr double F0 = 40.0;
    const int K = (int) std::floor(std::log2(18000.0 / F0) * KPO) + 1;
    const int nb = (int) avgDb.size();
    auto kOf = [&](double f) { return std::max(0, std::min(K - 1, (int) std::floor(std::log2(f / F0) * KPO + 0.5))); };
    auto fOf = [&](double k) { return F0 * std::pow(2.0, k / KPO); };
    auto at = [&](double f) { double x = f / binHz; int i = (int) std::floor(x); double t = x - i;
        return avgDb[std::min(nb - 1, i)] * (1 - t) + avgDb[std::min(nb - 1, i + 1)] * t; };

    std::vector<double> g;
    for (int k = 0; k < K; ++k) {
        const double f = fOf(k), lo = f * std::pow(2.0, -1.0 / (2 * KPO)), hi = f * std::pow(2.0, 1.0 / (2 * KPO));
        const int b0 = std::max(1, (int) std::ceil(lo / binHz)), b1 = std::min(nb - 1, (int) std::floor(hi / binHz));
        if (b1 > b0) { double s = 0; for (int b = b0; b <= b1; ++b) s += std::pow(10.0, avgDb[b] / 10);
            g.push_back(10 * std::log10(s / (b1 - b0 + 1))); }
        else g.push_back(at(f));
    }
    std::vector<double> g3(K), med(K), p(K), e(K);
    for (int k = 0; k < K; ++k) { double s = 0; int c = 0;
        for (int j = k - 1; j <= k + 1; ++j) if (j >= 0 && j < K) { s += g[j]; ++c; }
        g3[k] = s / c; }
    for (int k = 0; k < K; ++k) { std::vector<double> a;
        for (int j = k - 6; j <= k + 6; ++j) if (j >= 0 && j < K) a.push_back(g3[j]);
        std::sort(a.begin(), a.end()); med[k] = a[a.size() >> 1]; p[k] = g3[k] - med[k]; }

    // robust tilt line across 100 Hz - 12 kHz
    std::vector<int> idx; for (int k = kOf(100); k <= kOf(12000); ++k) idx.push_back(k);
    double a = 0, b = 0;
    for (int it = 0; it < 4; ++it) {
        double mx = 0, my = 0; for (int k : idx) { mx += std::log2(fOf(k)); my += g3[k]; }
        mx /= idx.size(); my /= idx.size();
        double sxx = 0, sxy = 0; for (int k : idx) { double x = std::log2(fOf(k)) - mx; sxx += x * x; sxy += x * (g3[k] - my); }
        b = sxy / sxx; a = my - b * mx;
        std::vector<double> res; double ss = 0;
        for (int k : idx) { double r = g3[k] - (a + b * std::log2(fOf(k))); res.push_back(r); ss += r * r; }
        double sd = std::sqrt(ss / res.size()); if (sd == 0) sd = 1;
        std::vector<int> nidx; for (size_t i = 0; i < idx.size(); ++i) if (res[i] < 0.5 * sd) nidx.push_back(idx[i]);
        idx = nidx; if (idx.size() < 10) break;
    }
    for (int k = 0; k < K; ++k) e[k] = g3[k] - (a + b * std::log2(fOf(k)));

    struct Z { double f, score, peak, excess, broad; };
    auto zone = [&](double f1, double f2) { double best = -1e9; int bk = kOf(f1);
        for (int k = kOf(f1); k <= kOf(f2); ++k) { double s = 0.6 * p[k] + 0.35 * std::max(e[k], 0.0); if (s > best) { best = s; bk = k; } }
        double me = 0; int n = 0; for (int k = kOf(f1); k <= kOf(f2); ++k) { me += e[k]; ++n; }
        return Z { fOf(bk), best, p[bk], e[bk], me / n }; };
    auto mk = [&](const Z& z) { Zone o; o.f = z.f; o.score = z.score; o.present = z.score >= th;
        const double need = std::min(1.0, std::max(0.0, (z.score - th) / 6));
        const bool narrow = z.peak >= 2.5 && z.peak > z.broad;
        o.gain = o.present ? -std::round((1 + (cap - 1) * need) * 10) / 10 : 0;
        o.q = narrow ? std::min(4.0, 1.6 + z.peak * 0.25) : 1.0; return o; };
    auto tone = [&](double f1, double f2, double defHz) { int bk = -1; double bp = -1e9;
        for (int k = kOf(f1); k <= kOf(f2); ++k) if (p[k] > bp) { bp = p[k]; bk = k; }
        double me = 0; int n = 0; for (int k = kOf(f1); k <= kOf(f2); ++k) { me += e[k]; ++n; }
        Tone t; t.found = bp > 0.8; t.f = t.found ? fOf(bk) : defHz; t.peak = bp; t.broad = me / n; return t; };
    auto boost = [&](const Tone& t) { double raw = std::min(7.0, std::max(2.0, 3.5 - 0.6 * t.broad));
        return std::round((1 + (cap - 1) * (raw - 2) / 5) * 10) / 10; };

    Plan r;
    r.mud = mk(zone(200, 500)); r.harsh = mk(zone(2000, 6000));
    r.warm = tone(80, 250, 120); r.pres = tone(700, 2500, 1400); r.air = tone(8000, 16000, 10000);
    r.bWarm = boost(r.warm); r.bPres = boost(r.pres); r.bAir = boost(r.air);
    return r;
}
} // namespace chroma
