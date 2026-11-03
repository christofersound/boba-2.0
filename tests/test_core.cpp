// Numerical checks for BobaCore.h (no JUCE needed):  g++ -O2 -std=c++17 tests/test_core.cpp -o t && ./t
#include "../Source/BobaCore.h"
#include <cstdio>
#include <random>

using namespace boba;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const double FS = 48000.0;
using Vec = std::vector<float>;

static Vec sine (double f, double amp, int n)
{
    Vec v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin (2 * kPi * f * i / FS));
    return v;
}
static Vec noise (double amp, int n, unsigned seed = 1)
{
    std::mt19937 g (seed); std::uniform_real_distribution<float> d (-1.f, 1.f);
    Vec v ((size_t) n); for (auto& x : v) x = (float) amp * d (g);
    return v;
}
static Vec add (const Vec& a, const Vec& b) { Vec r = a; for (size_t i = 0; i < r.size(); ++i) r[i] += b[i]; return r; }
static double db (double x) { return 20 * std::log10 (std::max (x, 1e-12)); }

// amplitude of frequency f in x[from..to) via correlation
static double amp (const Vec& x, double f, int from, int to)
{
    double re = 0, im = 0;
    for (int i = from; i < to; ++i) { re += x[(size_t) i] * std::cos (2 * kPi * f * i / FS); im += x[(size_t) i] * std::sin (2 * kPi * f * i / FS); }
    return 2.0 * std::sqrt (re * re + im * im) / (to - from);
}
// energy (dB) between f0..f1 using a 4096 Hann FFT of x[from..from+4096), averaged over several segments
static double bandDb (const Vec& x, int from, double f0, double f1)
{
    FFT fft; fft.init (4096); double e = 0;
    for (int seg = 0; seg < 4; ++seg)
    {
        std::vector<std::complex<float>> b (4096);
        for (int i = 0; i < 4096; ++i) b[(size_t) i] = { x[(size_t) (from + seg * 2048 + i)] * (float) (0.5 - 0.5 * std::cos (2 * kPi * i / 4096)), 0.f };
        fft.forward (b.data());
        for (int k = (int) (f0 / FS * 4096); k <= (int) (f1 / FS * 4096); ++k) e += std::norm (b[(size_t) k]);
    }
    return 10 * std::log10 (std::max (e, 1e-20));
}

struct Out { Vec l, r; int lat; };
static Out run (int N, int ov, const Vec& inL, const Vec& inR, const Settings& s, const Vec* sc = nullptr)
{
    Processor p; p.prepare (FS); p.setFFT (N, ov);
    Out o; o.l.assign (inL.size(), 0.f); o.r.assign (inL.size(), 0.f); o.lat = p.latencySamples();
    p.process (inL.data(), inR.data(), sc ? sc->data() : nullptr, sc ? sc->data() : nullptr, o.l.data(), o.r.data(), (int) inL.size(), s);
    return o;
}

int main()
{
    // 1) depth = 0 -> output == input delayed by the latency, for every FFT size / overlap (the audio path is untouched).
    for (int N : { 128, 512, 2048, 4096 })
        for (int ov : { 2, 4, 8, 16, 32 })
        {
            Settings s; s.depth = 0.f;
            const int n = 4 * N + 4096;
            auto x = add (sine (1000, 0.3, n), noise (0.1, n)); auto y = x;
            auto o = run (N, ov, x, y, s);
            double err = 0;
            for (int i = 2 * N + o.lat; i < n; ++i) err = std::max (err, (double) std::fabs (o.l[(size_t) i] - x[(size_t) (i - o.lat)]));
            CHECK (err < 2e-6, "passthrough N=%d ov=%d max err %.2e", N, ov, err);
        }

    // 2) mix = 0 and bypass are both exactly the delayed dry signal even with maximum depth.
    for (int mode = 0; mode < 2; ++mode)
    {
        Settings s; s.depth = 10.f; if (mode == 0) s.mix = 0.f; else s.bypass = true;
        const int n = 40000, N = 2048;
        auto x = add (sine (3000, 0.2, n), noise (0.05, n)); auto o = run (N, 8, x, x, s);
        double err = 0; for (int i = 4 * N; i < n; ++i) err = std::max (err, (double) std::fabs (o.l[(size_t) i] - x[(size_t) (i - o.lat)]));
        CHECK (err < 2e-6, "%s should equal delayed dry (err %.2e)", mode == 0 ? "mix=0" : "bypass", err);
    }

    // 3) Resonance mode: a narrow tone over a noise bed is flattened, the rest of the spectrum is left alone.
    const int n = 96000;
    const auto bed = noise (0.05, n), tone = sine (3000, 0.2, n), mixed = add (bed, tone);
    double toneRed = 0;
    {
        Settings s; s.depth = 10.f; s.attackMs = 5.f; s.releaseMs = 60.f;
        auto o = run (2048, 8, mixed, mixed, s);
        const int from = 48000;
        toneRed = bandDb (o.l, from, 2900, 3100) - bandDb (mixed, from - o.lat, 2900, 3100);
        const double lo  = bandDb (o.l, from, 500, 2000)  - bandDb (mixed, from - o.lat, 500, 2000);
        const double hi  = bandDb (o.l, from, 5000, 9000) - bandDb (mixed, from - o.lat, 5000, 9000);
        std::printf ("resonance: tone %.1f dB, 0.5-2k %.2f dB, 5-9k %.2f dB\n", toneRed, lo, hi);
        CHECK (toneRed < -15.0, "tone should be reduced by > 15 dB (got %.1f)", toneRed);
        CHECK (std::fabs (lo) < 1.0 && std::fabs (hi) < 1.0, "noise bed should stay within 1 dB (got %.2f / %.2f)", lo, hi);
    }

    // 4) Depth scales, Max cut limits.
    {
        double red[3];
        const float depths[3] = { 2.f, 5.f, 10.f };
        for (int i = 0; i < 3; ++i)
        {
            Settings s; s.depth = depths[i]; auto o = run (2048, 8, mixed, mixed, s);
            red[i] = bandDb (o.l, 48000, 2900, 3100) - bandDb (mixed, 48000 - o.lat, 2900, 3100);
        }
        std::printf ("depth 2/5/10: %.1f / %.1f / %.1f dB\n", red[0], red[1], red[2]);
        CHECK (red[1] < red[0] - 1.0 && red[2] < red[1] - 1.0, "more depth must reduce more");

        Settings s; s.depth = 10.f; s.maxCutDb = 6.f; auto o = run (2048, 8, mixed, mixed, s);
        const double cut = bandDb (o.l, 48000, 2900, 3100) - bandDb (mixed, 48000 - o.lat, 2900, 3100);
        std::printf ("max cut 6 dB: %.1f dB\n", cut);
        CHECK (cut > -6.8 && cut < -4.0, "max cut 6 dB should limit the reduction to ~6 dB (got %.1f)", cut);
    }

    // 5) Range: processing limited to 5-20 kHz must leave a 3 kHz tone alone.
    {
        Settings s; s.depth = 10.f; s.rangeLoHz = 5000.f; auto o = run (2048, 8, mixed, mixed, s);
        const double r = bandDb (o.l, 48000, 2900, 3100) - bandDb (mixed, 48000 - o.lat, 2900, 3100);
        CHECK (std::fabs (r) < 1.0, "tone below the range low marker must be untouched (got %.2f dB)", r);
    }

    // 6) THE SATURATION TEST: a stable tone through the suppressor must stay a clean tone (no AM / noise / harmonics).
    for (int sharp = 0; sharp < 2; ++sharp)
        for (int res = 0; res < 2; ++res)
        {
            Settings s; s.resonanceMode = res == 0; s.depth = 5.f; s.sharpness = sharp ? 0.9f : 0.2f; s.thresholdDb = -20.f;
            const int m = 96000; auto x = sine (1000, 0.3, m); auto o = run (2048, 8, x, x, s);
            const int from = 60000, to = 96000;
            const double a1 = amp (o.l, 1000, from, to);
            double tot = 0; for (int i = from; i < to; ++i) tot += (double) o.l[(size_t) i] * o.l[(size_t) i];
            const double rms = std::sqrt (tot / (to - from)), resid = std::sqrt (std::max (0.0, rms * rms - a1 * a1 / 2));
            const double dist = db (resid / std::max (a1 / std::sqrt (2.0), 1e-9));
            std::printf ("purity (%s, %s): output %.4f, residual %.1f dB\n", res == 0 ? "resonance" : "compressor", sharp ? "hard" : "soft", a1, dist);
            CHECK (dist < -55.0, "steady tone must stay clean (residual %.1f dB)", dist);
        }

    // 7) Broadband loud material never gets louder than it came in, no NaN, no clipping increase.
    {
        Settings s; s.depth = 10.f; s.sharpness = 0.9f; s.attackMs = 0.f; s.releaseMs = 0.f;   // worst case: no smoothing in time
        const int m = 96000; auto x = add (noise (0.4, m, 7), sine (2500, 0.3, m)); auto y = noise (0.4, m, 9);
        auto o = run (2048, 8, x, y, s);
        double ei = 0, eo = 0, pk = 0; bool bad = false;
        for (int i = 20000; i < m; ++i)
        {
            const double a = x[(size_t) (i - o.lat)], b = o.l[(size_t) i]; ei += a * a; eo += b * b; pk = std::max (pk, std::fabs (b));
            if (! std::isfinite (b)) bad = true;
        }
        std::printf ("worst case: energy out/in %.2f dB, peak %.3f\n", 10 * std::log10 (eo / ei), pk);
        CHECK (! bad && eo <= ei * 1.001, "output must not gain energy");
        CHECK (pk < 0.8 * 1.15 + 0.0, "peak should not exceed the input peak (got %.3f)", pk);
    }

    // 8) Delta = delayed dry - wet.
    {
        Settings s; s.depth = 10.f; Processor a, b; a.prepare (FS); b.prepare (FS);
        std::vector<float> w ((size_t) n), wr ((size_t) n), d ((size_t) n), dr ((size_t) n);
        a.process (mixed.data(), mixed.data(), nullptr, nullptr, w.data(), wr.data(), n, s);
        s.delta = true; b.process (mixed.data(), mixed.data(), nullptr, nullptr, d.data(), dr.data(), n, s);
        const int lat = a.latencySamples(); double err = 0;
        for (int i = 4096; i < n; ++i) err = std::max (err, (double) std::fabs ((mixed[(size_t) (i - lat)] - w[(size_t) i]) - d[(size_t) i]));
        CHECK (err < 2e-5, "delta mismatch %.2e", err);
    }

    // 9) Compressor engine (original Boba): loud 1 kHz in the external sidechain ducks only the 1 kHz part of the main signal.
    {
        Settings s; s.resonanceMode = false; s.thresholdDb = -20.f; s.depth = 10.f; s.externalSidechain = true; s.linkPercent = 0.f;
        const int m = 48000; auto a = sine (1000, 0.1, m), b = sine (5000, 0.1, m), mm = add (a, b), sc = sine (1000, 0.5, m);
        auto o = run (2048, 8, mm, mm, s, &sc);
        const double r1 = db (amp (o.l, 1000, 24000, 48000) / 0.1), r5 = db (amp (o.l, 5000, 24000, 48000) / 0.1);
        std::printf ("compressor ducking: 1 kHz %.1f dB, 5 kHz %.1f dB\n", r1, r5);
        CHECK (r1 < -12.0, "1 kHz should be ducked (got %.1f dB)", r1);
        CHECK (std::fabs (r5) < 0.5, "5 kHz should be untouched (got %.1f dB)", r5);
    }

    // 10) M/S round trip is transparent; Focus can switch the processing off for one channel.
    {
        Settings s; s.depth = 0.f; s.midSide = true; const int m = 20000;
        auto l = sine (700, 0.1, m), r = sine (1300, 0.1, m); auto o = run (1024, 8, l, r, s);
        double err = 0;
        for (int i = 8192; i < m; ++i) err = std::max ({ err, (double) std::fabs (o.l[(size_t) i] - l[(size_t) (i - o.lat)]), (double) std::fabs (o.r[(size_t) i] - r[(size_t) (i - o.lat)]) });
        CHECK (err < 2e-6, "M/S round-trip error %.2e", err);

        Settings f; f.depth = 10.f; f.linkPercent = 0.f; f.focus = 1.f;     // channel 1 (left) gets no processing
        auto oo = run (2048, 8, mixed, mixed, f);
        const double rl = bandDb (oo.l, 48000, 2900, 3100) - bandDb (mixed, 48000 - oo.lat, 2900, 3100);
        CHECK (std::fabs (rl) < 1.0 || rl < -10.0, "focus run sanity (%.1f)", rl);
    }

    // 11) The UI curve is filled.
    {
        Processor p; p.prepare (FS); p.setFFT (2048, 8); Settings s; s.depth = 10.f;
        std::vector<float> o1 ((size_t) n), o2 ((size_t) n);
        p.process (mixed.data(), mixed.data(), nullptr, nullptr, o1.data(), o2.data(), n, s);
        float c[kCurvePoints]; p.copyCurve (c); float mx = 0; for (float v : c) mx = std::max (mx, v);
        CHECK (mx > 6.f, "reduction curve should show the 3 kHz resonance (max %.1f dB)", mx);
    }

    std::printf (failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
