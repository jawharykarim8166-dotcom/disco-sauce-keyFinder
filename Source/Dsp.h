#pragma once
// Disco Sauce Key Finder - analysis engine (no JUCE dependency, fully offline)
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>
#include <numeric>

namespace ds
{
constexpr double kPi = 3.14159265358979323846;
using StopFn = std::function<bool()>;

//==============================================================================
struct FFT
{
    explicit FFT (int n)
        : N (n), logN (0), cosT ((size_t) (n / 2)), sinT ((size_t) (n / 2)), rev ((size_t) n)
    {
        while ((1 << logN) < n) ++logN;
        for (int i = 0; i < N / 2; ++i)
        {
            cosT[(size_t) i] = (float) std::cos (2.0 * kPi * i / N);
            sinT[(size_t) i] = (float) -std::sin (2.0 * kPi * i / N);
        }
        for (int i = 0; i < N; ++i)
        {
            int r = 0;
            for (int b = 0; b < logN; ++b)
                if (i & (1 << b)) r |= 1 << (logN - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    void run (float* re, float* im) const   // forward only
    {
        for (int i = 0; i < N; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len >> 1, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosT[(size_t) (k * step)];
                    const float wi = sinT[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
        }
    }

    int N, logN;
    std::vector<float> cosT, sinT;
    std::vector<int> rev;
};

inline float medianOf (float* w, int n)
{
    std::nth_element (w, w + n / 2, w + n);
    return w[n / 2];
}

// triangular low-pass + decimate
inline std::vector<float> decimate (const std::vector<float>& x, int factor)
{
    if (factor <= 1) return x;
    std::vector<float> y (x.size() / (size_t) factor);
    const long n = (long) x.size();
    for (size_t i = 0; i < y.size(); ++i)
    {
        double s = 0.0, ws = 0.0;
        for (int k = -factor; k < factor; ++k)
        {
            const long idx = (long) i * factor + k + factor / 2;
            if (idx < 0 || idx >= n) continue;
            const double w = 1.0 - std::abs (k + 0.5) / factor;
            s += w * x[(size_t) idx]; ws += w;
        }
        y[i] = (float) (ws > 0.0 ? s / ws : 0.0);
    }
    return y;
}

//==============================================================================
struct Reading
{
    bool   valid = false;
    float  seconds = 0.f;
    double bpm = 0.0;
    float  bpmConf = 0.f;
    double bpmAlt[2] = { 0.0, 0.0 };     // half / double
    int    root = -1;
    bool   minor = false;
    float  keyConf = 0.f;
    int    relRoot = -1;
    double chroma[12] = {};
};

//==============================================================================
//  TEMPO
//==============================================================================
//  Onset strength: log-mel spectral flux (like the one used by professional beat trackers)
inline std::vector<float> onsetEnvelope (const std::vector<float>& x, double sr, int hop, const StopFn& stop)
{
    const int N = 1024, NB = 40, K = N / 2 + 1;
    std::vector<float> env;
    if ((int) x.size() < N * 8) return env;

    FFT fft (N);
    std::vector<float> win ((size_t) N), re ((size_t) N), im ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));

    auto mel  = [] (double f) { return 2595.0 * std::log10 (1.0 + f / 700.0); };
    auto imel = [] (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); };
    const double fLo = 30.0, fHi = std::min (10000.0, 0.45 * sr);
    std::vector<double> edges ((size_t) NB + 2);
    for (int i = 0; i < NB + 2; ++i) edges[(size_t) i] = imel (mel (fLo) + (mel (fHi) - mel (fLo)) * i / (NB + 1));
    std::vector<float> wgt ((size_t) NB * K, 0.f);
    for (int b = 0; b < NB; ++b)
        for (int k = 0; k < K; ++k)
        {
            const double f = k * sr / N, lo = edges[(size_t) b], mid = edges[(size_t) b + 1], hi = edges[(size_t) b + 2];
            double w = 0.0;
            if (f > lo && f <= mid) w = (f - lo) / (mid - lo);
            else if (f > mid && f < hi) w = (hi - f) / (hi - mid);
            wgt[(size_t) b * K + k] = (float) w;
        }

    const int frames = ((int) x.size() - N) / hop + 1;
    std::vector<float> Lm ((size_t) frames * NB);
    float mx = -1e9f;
    for (int t = 0; t < frames; ++t)
    {
        if ((t & 255) == 0 && stop()) return {};
        const float* p = x.data() + (size_t) t * (size_t) hop;
        for (int i = 0; i < N; ++i) { re[(size_t) i] = p[i] * win[(size_t) i]; im[(size_t) i] = 0.f; }
        fft.run (re.data(), im.data());
        for (int b = 0; b < NB; ++b)
        {
            float e = 0.f;
            const float* w = &wgt[(size_t) b * K];
            for (int k = 0; k < K; ++k) e += w[k] * (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
            const float db = 10.0f * std::log10 (e + 1e-3f);
            Lm[(size_t) t * NB + b] = db;
            mx = std::max (mx, db);
        }
    }
    const float floorDb = mx - 70.0f;
    for (auto& v : Lm) v = std::max (v, floorDb);

    env.assign ((size_t) frames, 0.f);
    for (int t = 1; t < frames; ++t)
    {
        float s = 0.f;
        for (int b = 0; b < NB; ++b)
            s += std::max (0.f, Lm[(size_t) t * NB + b] - Lm[(size_t) (t - 1) * NB + b]);
        env[(size_t) t] = s / (float) NB;
    }

    // detrend (0.4 s local mean), rectify, remove mean, light smoothing, unit variance
    std::vector<double> cs ((size_t) frames + 1, 0.0);
    for (int i = 0; i < frames; ++i) cs[(size_t) i + 1] = cs[(size_t) i] + env[(size_t) i];
    const int half = std::max (2, (int) (0.4 * sr / hop));
    std::vector<float> e2 ((size_t) frames, 0.f);
    for (int i = 0; i < frames; ++i)
    {
        const int a = std::max (0, i - half), b = std::min (frames, i + half + 1);
        const double loc = (cs[(size_t) b] - cs[(size_t) a]) / (double) (b - a);
        e2[(size_t) i] = (float) std::max (0.0, (double) env[(size_t) i] - loc);
    }
    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<float> t2 = e2;
        for (int i = 1; i + 1 < frames; ++i) t2[(size_t) i] = 0.25f * e2[(size_t) i - 1] + 0.5f * e2[(size_t) i] + 0.25f * e2[(size_t) i + 1];
        e2.swap (t2);
    }
    double mean = 0.0;
    for (auto v : e2) mean += v;
    mean /= (double) frames;
    double ss = 0.0;
    for (auto& v : e2) { v = (float) (v - mean); ss += (double) v * v; }
    const float inv = (float) (1.0 / (std::sqrt (ss / frames) + 1e-9));
    for (auto& v : e2) v *= inv;
    return e2;
}

struct TempoResult { double bpm = 0.0; float conf = 0.f; };

inline TempoResult estimateTempo (const std::vector<float>& env, double rate, const StopFn& stop)
{
    TempoResult res;
    const int N = (int) env.size();
    if (N < (int) (rate * 6.0)) return res;

    const int Wlen = std::min (N, (int) (10.0 * rate)), Whop = std::max (1, (int) (5.0 * rate));
    int maxLagWanted = (int) std::ceil (rate * 60.0 / 45.0 * 4.0) + 4;
    const int maxLag = std::min (maxLagWanted, (int) (Wlen * 0.65));
    if (maxLag < 20) return res;

    // ---- windowed, normalised autocorrelations (robust to intros / breakdowns)
    std::vector<std::vector<double>> wins;
    auto acf = [&] (int start) -> std::vector<double>
    {
        std::vector<double> a ((size_t) maxLag + 2, 0.0);
        double r0 = 0.0;
        for (int i = 0; i < Wlen; ++i) r0 += (double) env[(size_t) (start + i)] * env[(size_t) (start + i)];
        if (r0 < 1e-9) return {};
        r0 /= (double) Wlen;
        for (int lag = 0; lag <= maxLag; ++lag)
        {
            double s = 0.0;
            const int cnt = Wlen - lag;
            for (int i = 0; i < cnt; ++i) s += (double) env[(size_t) (start + i)] * env[(size_t) (start + i + lag)];
            a[(size_t) lag] = (s / (double) cnt) / r0;
        }
        return a;
    };
    for (int s = 0; s + Wlen <= N; s += Whop)
    {
        if (stop()) return res;
        auto a = acf (s);
        if (! a.empty()) wins.push_back (std::move (a));
    }
    if (N - Wlen > Whop / 2 && ((N - Wlen) % Whop) > (int) (rate * 2.0))
    {
        auto a = acf (N - Wlen);
        if (! a.empty()) wins.push_back (std::move (a));
    }
    if (wins.empty()) return res;

    std::vector<double> avg ((size_t) maxLag + 2, 0.0);
    for (auto& w : wins) for (size_t i = 0; i < avg.size(); ++i) avg[i] += w[i] / (double) wins.size();

    auto interp = [&] (const std::vector<double>& A, double lag) -> double
    {
        const int i = (int) lag; const double fr = lag - i;
        return A[(size_t) i] * (1.0 - fr) + A[(size_t) i + 1] * fr;
    };
    const double wts[4] = { 1.0, 0.8, 0.6, 0.5 };
    auto scoreAt = [&] (const std::vector<double>& A, double b) -> double
    {
        const double L = rate * 60.0 / b;
        double s = 0.0, ws = 0.0;
        for (int k = 1; k <= 4; ++k)
        {
            const double lag = k * L;
            if (lag >= (double) maxLag) break;
            s += wts[k - 1] * interp (A, lag); ws += wts[k - 1];
        }
        return ws > 0.0 ? s / ws : 0.0;
    };
    auto prior = [] (double b) { const double o = std::log2 (b / 120.0) / 0.8; return std::exp (-0.5 * o * o); };

    // ---- coarse: best tempo over the whole excerpt
    double bestV = -1e30, bestB = 0.0, sumS = 0.0; int cntS = 0;
    for (double b = 45.0; b <= 230.0; b += 0.1)
    {
        const double raw = scoreAt (avg, b);
        const double v = std::max (0.0, raw) * prior (b);
        if (b >= 60.0 && b <= 200.0) { sumS += std::max (0.0, raw); ++cntS; }
        if (v > bestV) { bestV = v; bestB = b; }
    }
    if (bestB <= 0.0) return res;
    // fold into the range where most DAW projects live (84..168 BPM); the other octave is shown as an alternative
    while (bestB < 84.0) bestB *= 2.0;
    while (bestB >= 168.0) bestB *= 0.5;

    // ---- per-window agreement (stability)
    int agree = 0;
    for (auto& w : wins)
    {
        double bv = -1e30, bb = 0.0;
        for (double b = 45.0; b <= 230.0; b += 0.25)
        {
            const double v = std::max (0.0, scoreAt (w, b)) * prior (b);
            if (v > bv) { bv = v; bb = b; }
        }
        const double ratio = bb / bestB;
        if (std::abs (ratio - 1.0) < 0.03) agree += 2;
        else if (std::abs (ratio - 2.0) < 0.06 || std::abs (ratio - 0.5) < 0.03) agree += 1;
    }
    const double agreeFrac = (double) agree / (2.0 * (double) wins.size());

    // ---- fine: pulse-train fit over the whole excerpt (phase optimised, sub-frame accurate)
    auto pulseScore = [&] (double b) -> double
    {
        const double P = rate * 60.0 / b;
        double best = -1e30;
        for (double ph = 0.0; ph < P; ph += 0.5)
        {
            double s = 0.0; int cnt = 0;
            for (double t = ph; t < (double) (N - 1); t += P)
            {
                const int i = (int) t; const double fr = t - i;
                s += (double) env[(size_t) i] * (1.0 - fr) + (double) env[(size_t) i + 1] * fr; ++cnt;
            }
            if (cnt > 0) best = std::max (best, s / cnt);
        }
        return best;
    };
    double fineB = bestB, fineS = -1e30;
    const double stepB = std::max (0.01, bestB * 0.0002);
    for (double b = bestB * 0.985; b <= bestB * 1.015; b += stepB)
    {
        if (stop()) return res;
        const double s = pulseScore (b);
        if (s > fineS) { fineS = s; fineB = b; }
    }
    // most DAW projects use whole or half tempos: snap when the fit is nearly as good
    const double ri = std::round (fineB), rh = std::round (fineB * 2.0) / 2.0;
    if (std::abs (fineB - ri) <= 0.30 && pulseScore (ri) >= 0.975 * fineS) fineB = ri;
    else if (std::abs (fineB - rh) <= 0.20 && pulseScore (rh) >= 0.975 * fineS) fineB = rh;

    const double peaky = (sumS > 0.0 && cntS > 0) ? scoreAt (avg, fineB) / (sumS / cntS + 1e-9) : 1.0;
    const double c1 = std::min (1.0, std::max (0.0, (peaky - 1.0) / 3.0));
    res.bpm = fineB;
    res.conf = (float) (30.0 + 69.0 * (0.5 * c1 + 0.5 * agreeFrac));
    return res;
}

//==============================================================================
//  KEY
//==============================================================================
inline double pearson (const double* a, const double* b)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; }
    ma /= 12.0; mb /= 12.0;
    double sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < 12; ++i)
    {
        const double da = a[i] - ma, db = b[i] - mb;
        sab += da * db; saa += da * da; sbb += db * db;
    }
    return sab / (std::sqrt (saa * sbb) + 1e-12);
}

//  Spectral peaks of one frame -> callback(freqHz, relativeMagnitude)
template <class Fn>
inline void framePeaks (const float* h, int B, int kLo, double sr, int N, float relThresh, Fn cb)
{
    float fm = 0.f;
    for (int b = 0; b < B; ++b) fm = std::max (fm, h[b]);
    if (fm <= 1e-9f) return;
    for (int b = 1; b < B - 1; ++b)
    {
        const float v = h[b];
        if (v > h[b - 1] && v >= h[b + 1] && v > relThresh * fm)
        {
            const double a = std::log ((double) h[b - 1] + 1e-12), m = std::log ((double) v + 1e-12), c = std::log ((double) h[b + 1] + 1e-12);
            const double den = a - 2.0 * m + c;
            double d = std::abs (den) > 1e-9 ? 0.5 * (a - c) / den : 0.0;
            d = std::max (-0.5, std::min (0.5, d));
            cb (((double) (kLo + b) + d) * sr / N, (double) (v / fm));
        }
    }
}

inline void addPitch (double* fr, double pitch, double tuning, double w)
{
    const double p = pitch - tuning;
    const double lo = std::floor (p), frac = p - lo;
    const int pc = (((int) lo) % 12 + 12) % 12;
    fr[pc] += w * (1.0 - frac);
    fr[(pc + 1) % 12] += w * frac;
}

//  Harmonic chroma (percussion removed by time-median filtering, tuning corrected,
//  harmonic-sum so bass notes without a strong fundamental still count).
inline bool tonalChroma (const std::vector<float>& x1, double s1, double* chroma, double& tuningOut, const StopFn& stop)
{
    const int N = 8192, HOP = 2048;
    if ((int) x1.size() < N * 3) return false;
    FFT fft (N);
    std::vector<float> win ((size_t) N), re ((size_t) N), im ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));
    const int F = ((int) x1.size() - N) / HOP + 1;
    const int kLo = (int) std::ceil (50.0 * N / s1), kHi = std::min (N / 2 - 2, (int) (4200.0 * N / s1)), B = kHi - kLo + 1;
    if (B < 100) return false;

    std::vector<float> M ((size_t) F * B), H ((size_t) F * B);
    for (int f = 0; f < F; ++f)
    {
        if ((f & 63) == 0 && stop()) return false;
        const float* p = x1.data() + (size_t) f * HOP;
        for (int i = 0; i < N; ++i) { re[(size_t) i] = p[i] * win[(size_t) i]; im[(size_t) i] = 0.f; }
        fft.run (re.data(), im.data());
        for (int k = kLo; k <= kHi; ++k)
            M[(size_t) f * B + (k - kLo)] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
    }
    float w[16];
    for (int b = 0; b < B; ++b)
    {
        if ((b & 255) == 0 && stop()) return false;
        for (int f = 0; f < F; ++f)
        {
            int n = 0;
            for (int j = -4; j <= 4; ++j) w[n++] = M[(size_t) std::min (F - 1, std::max (0, f + j)) * B + b];
            H[(size_t) f * B + b] = medianOf (w, n);
        }
    }
    std::vector<float> fmax ((size_t) F, 0.f);
    float gmax = 0.f;
    for (int f = 0; f < F; ++f)
    {
        for (int b = 0; b < B; ++b) fmax[(size_t) f] = std::max (fmax[(size_t) f], H[(size_t) f * B + b]);
        gmax = std::max (gmax, fmax[(size_t) f]);
    }
    if (gmax <= 1e-6f) return false;

    // ---- tuning offset (semitones, -0.5..0.5)
    double hist[50] = {};
    for (int f = 0; f < F; f += 2)
    {
        if (fmax[(size_t) f] < 0.05f * gmax) continue;
        framePeaks (&H[(size_t) f * B], B, kLo, s1, N, 0.1f, [&] (double fr, double wt)
        {
            if (fr < 80.0 || fr > 1800.0) return;
            const double pitch = 69.0 + 12.0 * std::log2 (fr / 440.0);
            const double dev = pitch - std::round (pitch);
            hist[std::max (0, std::min (49, (int) ((dev + 0.5) * 50.0)))] += wt;
        });
    }
    int bi = 0; double bv = -1.0;
    for (int i = 0; i < 50; ++i)
    {
        const double v = hist[(i + 49) % 50] + hist[i] + hist[(i + 1) % 50];
        if (v > bv) { bv = v; bi = i; }
    }
    const double tuning = ((double) bi + 0.5) / 50.0 - 0.5;
    tuningOut = tuning;

    // ---- chroma with harmonic summation
    static const double hw[4] = { 1.0, 0.55, 0.35, 0.25 };
    for (int i = 0; i < 12; ++i) chroma[i] = 0.0;
    for (int f = 0; f < F; ++f)
    {
        if ((f & 63) == 0 && stop()) return false;
        if (fmax[(size_t) f] < 0.03f * gmax) continue;
        double fr[12] = {};
        framePeaks (&H[(size_t) f * B], B, kLo, s1, N, 0.08f, [&] (double fq, double wt)
        {
            for (int h = 1; h <= 4; ++h)
            {
                const double f0 = fq / h;
                if (f0 < 55.0) break;
                addPitch (fr, 69.0 + 12.0 * std::log2 (f0 / 440.0), tuning, std::sqrt (wt) * hw[h - 1]);
            }
        });
        double s = 0.0;
        for (int i = 0; i < 12; ++i) s += fr[i];
        if (s > 1e-9) for (int i = 0; i < 12; ++i) chroma[i] += fr[i] / s;
    }
    double s = 0.0;
    for (int i = 0; i < 12; ++i) s += chroma[i];
    if (s <= 1e-9) return false;
    for (int i = 0; i < 12; ++i) chroma[i] /= s;
    return true;
}

//  Bass-note distribution (30..260 Hz) at high frequency resolution
inline bool bassChroma (const std::vector<float>& x3, double s3, double tuning, double* out, const StopFn& stop)
{
    const int N = 8192, HOP = 2048;
    for (int i = 0; i < 12; ++i) out[i] = 0.0;
    if ((int) x3.size() < N * 2) return false;
    FFT fft (N);
    std::vector<float> win ((size_t) N), re ((size_t) N), im ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));
    const int F = ((int) x3.size() - N) / HOP + 1;
    const int kLo = (int) std::ceil (30.0 * N / s3), kHi = std::min (N / 2 - 2, (int) (260.0 * N / s3)), B = kHi - kLo + 1;
    if (B < 20) return false;
    std::vector<float> M ((size_t) F * B);
    float gmax = 0.f;
    for (int f = 0; f < F; ++f)
    {
        if (stop()) return false;
        const float* p = x3.data() + (size_t) f * HOP;
        for (int i = 0; i < N; ++i) { re[(size_t) i] = p[i] * win[(size_t) i]; im[(size_t) i] = 0.f; }
        fft.run (re.data(), im.data());
        for (int k = kLo; k <= kHi; ++k)
        {
            const float m = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
            M[(size_t) f * B + (k - kLo)] = m; gmax = std::max (gmax, m);
        }
    }
    if (gmax <= 1e-6f) return false;
    for (int f = 0; f < F; ++f)
    {
        float fm = 0.f;
        for (int b = 0; b < B; ++b) fm = std::max (fm, M[(size_t) f * B + b]);
        if (fm < 0.08f * gmax) continue;
        double fr[12] = {};
        framePeaks (&M[(size_t) f * B], B, kLo, s3, N, 0.25f, [&] (double fq, double wt)
        {
            if (fq < 30.0) return;
            addPitch (fr, 69.0 + 12.0 * std::log2 (fq / 440.0), tuning, std::sqrt (wt));
        });
        double s = 0.0;
        for (int i = 0; i < 12; ++i) s += fr[i];
        if (s > 1e-9) for (int i = 0; i < 12; ++i) out[i] += fr[i] / s;
    }
    double s = 0.0;
    for (int i = 0; i < 12; ++i) s += out[i];
    if (s <= 1e-9) return false;
    for (int i = 0; i < 12; ++i) out[i] /= s;
    return true;
}

inline void keyFromChroma (const double* c, const double* bass, bool haveBass, Reading& r)
{
    static const double P[3][2][12] = {
        { { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 },       // Krumhansl
          { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 } },
        { { 5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0 },                   // Temperley
          { 5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0 } },
        { { 17.7661, 0.145624, 14.9265, 0.160186, 19.8049, 11.3587, 0.291248, 22.062, 0.145624, 8.15494, 0.232998, 18.6691 },   // Aarden
          { 18.2648, 0.737619, 14.0499, 16.8599, 0.702494, 14.4362, 0.702494, 18.6161, 4.56621, 1.93186, 7.37619, 1.75623 } } };

    double sc[24];
    for (int root = 0; root < 12; ++root)
        for (int mode = 0; mode < 2; ++mode)
        {
            double tot = 0.0;
            for (int p = 0; p < 3; ++p)
            {
                double rot[12];
                for (int i = 0; i < 12; ++i) rot[i] = P[p][mode][(i - root + 12) % 12];
                tot += pearson (c, rot);
            }
            tot /= 3.0;
            if (haveBass)
                tot += 0.30 * (bass[root] - 1.0 / 12.0) + 0.10 * (bass[(root + 7) % 12] - 1.0 / 12.0);
            sc[mode * 12 + root] = tot;
        }

    int bi = 0;
    for (int i = 1; i < 24; ++i) if (sc[i] > sc[bi]) bi = i;
    const int root = bi % 12; const bool minor = bi >= 12;
    const int relIdx = minor ? (root + 3) % 12 : 12 + (root + 9) % 12;
    double second = -9.0;
    for (int i = 0; i < 24; ++i) if (i != bi && i != relIdx && sc[i] > second) second = sc[i];
    const double margin = sc[bi] - second;
    r.root = root; r.minor = minor;
    r.relRoot = minor ? (root + 3) % 12 : (root + 9) % 12;
    r.keyConf = (float) std::min (99.0, std::max (30.0, 38.0 + 55.0 * sc[bi] + 160.0 * margin));
    for (int i = 0; i < 12; ++i) r.chroma[i] = c[i];
}

//==============================================================================
//  Full analysis of a mono excerpt
inline Reading analyze (const std::vector<float>& mono, double sr, const StopFn& stop)
{
    Reading r;
    r.seconds = (float) ((double) mono.size() / sr);
    if (r.seconds < 6.0f) return r;

    // ---- tempo
    {
        const int f = std::max (1, (int) std::lround (sr / 22050.0));
        const auto x = decimate (mono, f);
        const double s2 = sr / f;
        const int hop = 256;
        const auto env = onsetEnvelope (x, s2, hop, stop);
        if (! env.empty())
        {
            const auto t = estimateTempo (env, s2 / hop, stop);
            r.bpm = t.bpm; r.bpmConf = t.conf;
            r.bpmAlt[0] = (t.bpm / 2.0 >= 40.0) ? t.bpm / 2.0 : 0.0;
            r.bpmAlt[1] = (t.bpm * 2.0 <= 260.0) ? t.bpm * 2.0 : 0.0;
        }
    }
    if (stop()) return r;

    // ---- key
    {
        const int f1 = std::max (1, (int) std::lround (sr / 11025.0));
        const auto x1 = decimate (mono, f1);
        double chroma[12], tuning = 0.0, bass[12];
        if (tonalChroma (x1, sr / f1, chroma, tuning, stop))
        {
            const int f3 = std::max (1, (int) std::lround (sr / 2756.0));
            const auto x3 = decimate (mono, f3);
            const bool hb = bassChroma (x3, sr / f3, tuning, bass, stop);
            keyFromChroma (chroma, bass, hb, r);
        }
    }
    r.valid = (r.bpm > 0.0 || r.root >= 0);
    return r;
}
} // namespace ds
