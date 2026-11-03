// BobaCore.h - JUCE-independent DSP engine of Boba (spectral resonance suppressor + spectral sidechain compressor).
//
// One STFT engine, two detectors:
//   * Resonance   : a bin is "a resonance" when it sticks out above the smoothed spectral envelope around it.
//   * Compressor  : a bin is reduced when it is above a (tilted) threshold - the original Boba behaviour.
// Everything after detection is shared: link, max-cut, focus, smoothing across frequency (no bin-to-bin
// gain jumps), attack/release per bin (with tilt), inverse FFT, overlap-add, mix/trim/out/bypass.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace boba
{
constexpr int    kMaxFFT = 4096;
constexpr int    kCurvePoints = 256;               // log-spaced 20 Hz .. 20 kHz, used by the UI
constexpr double kPi = 3.14159265358979323846;

// Level calibration for the Compressor detector (dB are relative to an unnormalised 2048-point Hann FFT).
constexpr float kFftMagnitudeScale = 1.0f;

struct Settings
{
    bool  resonanceMode = true;     // false = spectral sidechain compressor
    float thresholdDb   = 0.0f;     // Compressor mode only, -70 .. +30
    float depth         = 4.5f;     // 0 .. 10
    float detail        = 3.0f;     // 0 .. 10   (resonance width: low = broad areas, high = fine peaks)
    float sharpness     = 0.35f;    // 0 (soft) .. 1 (hard)
    float attackMs      = 10.0f;    // 0 .. 1200
    float releaseMs     = 80.0f;    // 0 .. 1200
    float tiltDetail    = 0.0f;     // -1 .. 1  (negative = more detail in the lows, positive = in the highs)
    float tiltAttack    = 0.0f;     // -1 .. 1  (positive = slower in the highs)
    float tiltRelease   = 0.0f;
    float maxCutDb      = 0.0f;     // 0 = off, otherwise maximum reduction in dB
    float rangeLoHz     = 20.0f;    // processing range
    float rangeHiHz     = 20000.0f;
    float linkPercent   = 100.0f;   // 0 .. 100
    float focus         = 0.0f;     // -1 .. 1 : bias the processing to channel 1 (L or M) or 2 (R or S)
    bool  midSide       = false;
    bool  externalSidechain = false;
    bool  solo          = false;    // listen to the (EQ'd) detection signal
    bool  delta         = false;    // listen to what is removed
    bool  bypass        = false;    // latency-compensated bypass
    float mix           = 1.0f;     // 0 .. 1
    float wetTrimDb     = 0.0f;
    float outDb         = 0.0f;

    struct Eq   // emphasis EQ applied to the detection signal only
    {
        float hpFreq = 20.0f, lpFreq = 20000.0f;
        float peakFreq[3]   = { 121.8675f, 548.0007f, 8307.348f };
        float peakGainDb[3] = { 0.0f, 0.0f, 0.0f };
        float peakQ[3]      = { 0.754704f, 0.728435f, 0.521149f };
        bool operator== (const Eq& o) const
        {
            if (hpFreq != o.hpFreq || lpFreq != o.lpFreq) return false;
            for (int i = 0; i < 3; ++i)
                if (peakFreq[i] != o.peakFreq[i] || peakGainDb[i] != o.peakGainDb[i] || peakQ[i] != o.peakQ[i]) return false;
            return true;
        }
    } eq;
};

//==============================================================================
class FFT
{
public:
    FFT() { tw.reserve (kMaxFFT / 2); rev.reserve (kMaxFFT); }

    void init (int n)
    {
        N = n;
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
            tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / n));
        rev.resize ((size_t) n);
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }
    void forward (std::complex<float>* d) const { run (d, false); }
    void inverse (std::complex<float>* d) const { run (d, true); } // includes 1/N

private:
    void run (std::complex<float>* d, bool inv) const
    {
        for (int i = 0; i < N; ++i) { int j = rev[(size_t) i]; if (j > i) std::swap (d[i], d[j]); }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < half; ++k)
                {
                    auto w = tw[(size_t) (k * step)];
                    if (inv) w = std::conj (w);
                    const auto a = d[i + k], b = d[i + k + half] * w;
                    d[i + k] = a + b;
                    d[i + k + half] = a - b;
                }
        }
        if (inv) { const float s = 1.0f / (float) N; for (int i = 0; i < N; ++i) d[i] *= s; }
    }
    int N = 0;
    std::vector<std::complex<float>> tw;
    std::vector<int> rev;
};

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void reset() { x1 = x2 = y1 = y2 = 0; }
    double process (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
    // RBJ cookbook
    void setHighpass (double fs, double f, double q) { set (fs, f, q, true); }
    void setLowpass  (double fs, double f, double q) { set (fs, f, q, false); }
    void setPeak (double fs, double f, double q, double gainDb)
    {
        f = std::min (f, fs * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        const double a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = -2 * c / a0; a2 = (1 - al / A) / a0;
    }
private:
    void set (double fs, double f, double q, bool hp)
    {
        f = std::min (f, fs * 0.49);
        const double w = 2 * kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        if (hp) { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = (1 + c) / 2 / a0; }
        else    { b0 = (1 - c) / 2 / a0; b1 =  (1 - c) / a0; b2 = (1 - c) / 2 / a0; }
        a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
};

// y = x - onepole_lowpass(x, 18 Hz). Only used on the detection path (the audio path is left untouched).
struct DcBlock
{
    double z = 0, a = 0.0025;
    void setup (double fs) { a = 1.0 - std::exp (-2.0 * kPi * 18.0 / fs); }
    void reset() { z = 0; }
    double process (double x) { z += a * (x - z); return x - z; }
};

struct Ramp   // one-pole smoother for mix / trim / out / bypass (no zipper noise)
{
    double v = 0; bool init = false;
    void reset() { init = false; }
    double next (double target, double c) { if (! init) { v = target; init = true; } v += (target - v) * c; return v; }
};

//==============================================================================
class Processor
{
public:
    Processor() { allocate(); setFFT (2048, 8); }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : dcSc) d.setup (fs);
        eqValid = false; tablesValid = false;
        reset();
    }

    // A sample is only final once the newest frame covering it has been processed: N-1 samples.
    int latencySamples() const { return N - 1; }
    int fftSize() const { return N; }
    int overlap() const { return ov; }

    // Never allocates after construction (capacity is reserved for kMaxFFT), so it is safe to call from the audio thread.
    void setFFT (int fftSize, int overlapFactor)
    {
        fftSize = std::clamp (fftSize, 128, kMaxFFT);
        overlapFactor = std::clamp (overlapFactor, 2, 32);
        N = fftSize; ov = overlapFactor; hop = std::max (1, N / ov);
        const int half = N / 2; const size_t H = (size_t) half + 1;
        fft.init (N);
        win.assign ((size_t) N, 0.f);
        for (int i = 0; i < N; ++i) win[(size_t) i] = (float) (0.5 * (1.0 - std::cos (2.0 * kPi * i / N)));
        norm.assign ((size_t) hop, 0.f);
        for (int p = 0; p < hop; ++p)
        {
            double s = 0; for (int j = 0; j < ov; ++j) s += (double) win[(size_t) (p + j * hop)] * win[(size_t) (p + j * hop)];
            norm[(size_t) p] = (float) std::max (s, 1e-6);
        }
        // spectral tilt of the Compressor threshold ("PinkShape"): +20 dB (lowest bin) to -15 dB (top bin)
        pink.assign (H, 0.f);
        for (int k = 1; k < half; ++k)
            pink[(size_t) k] = (float) (20.0 + (double) (k - 1) * (-15.0 - 20.0) / (double) std::max (1, half - 1));
        pink[(size_t) half] = pink[(size_t) half - 1];

        A.assign ((size_t) N, {}); B.assign ((size_t) N, {});
        XL.assign (H, {}); XR.assign (H, {});
        for (int c = 0; c < 2; ++c)
        {
            pw[c].assign (H, 0.f); lvl[c].assign (H, -200.f); tgt[c].assign (H, 0.f);
            redState[c].assign (H, 0.f); gainOut[c].assign (H, 1.f);
        }
        cum.assign (H + 1, 0.f); tmp.assign (H, 0.f);
        winLo.assign (H, 0); winHi.assign (H, 0); dilR.assign (H, 0); blurR.assign (H, 0);
        rangeW.assign (H, 1.f); aAtk.assign (H, 1.f); aRel.assign (H, 1.f);
        tablesValid = false;
        reset();
    }

    void reset()
    {
        for (auto& r : ring) r.assign ((size_t) N, 0.f);
        for (auto& a : acc)  a.assign ((size_t) N, 0.f);
        for (auto& d : dryRing) d.assign ((size_t) N, 0.f);
        for (int c = 0; c < 2; ++c)
        {
            std::fill (pw[c].begin(), pw[c].end(), 0.f);
            std::fill (redState[c].begin(), redState[c].end(), 0.f);
            std::fill (gainOut[c].begin(), gainOut[c].end(), 1.f);
        }
        writePos = 0; hopCount = 0; t = 0;
        for (auto& d : dcSc) d.reset();
        for (auto& b : eqL) b.reset();
        for (auto& b : eqR) b.reset();
        mixS.reset(); trimS.reset(); outS.reset(); bypS.reset();
        for (auto& c : curve) c.store (0.f, std::memory_order_relaxed);
    }

    // Reduction (dB, positive) per log-spaced point 20 Hz..20 kHz, for the UI. Lock-free.
    void copyCurve (float* dst) const
    {
        for (int i = 0; i < kCurvePoints; ++i) dst[i] = curve[(size_t) i].load (std::memory_order_relaxed);
    }

    // scL/scR may be nullptr (no external sidechain). In-place (in == out) is allowed.
    void process (const float* inL, const float* inR, const float* scInL, const float* scInR,
                  float* outL, float* outR, int numSamples, const Settings& s)
    {
        updateEq (s.eq);
        updateTables (s);
        cur = s;
        const bool useExt = s.externalSidechain && scInL != nullptr && scInR != nullptr;
        const double rc = 1.0 - std::exp (-1.0 / (fs * 0.015));
        const double mixT = std::clamp ((double) s.mix, 0.0, 1.0);
        const double trimT = std::pow (10.0, s.wetTrimDb / 20.0), outT = std::pow (10.0, s.outDb / 20.0);
        const double bypT = s.bypass ? 1.0 : 0.0;
        const int mask = N - 1;

        for (int i = 0; i < numSamples; ++i)
        {
            const double dryInL = inL[i], dryInR = inR[i];
            // ---- detection path: select -> DC block -> emphasis EQ
            double aL = useExt ? scInL[i] : dryInL, aR = useExt ? scInR[i] : dryInR;
            aL = dcSc[0].process (aL); aR = dcSc[1].process (aR);
            for (int b = 0; b < 5; ++b) { aL = eqL[b].process (aL); aR = eqR[b].process (aR); }
            const float soloL = (float) aL, soloR = (float) aR;

            double mL = dryInL, mR = dryInR;
            if (s.midSide)
            {
                mL = (dryInL + dryInR) * 0.5; mR = (dryInL - dryInR) * 0.5;
                const double am = (aL + aR) * 0.5, as = (aL - aR) * 0.5; aL = am; aR = as;
            }

            // ---- STFT input
            ring[0][(size_t) writePos] = (float) mL; ring[1][(size_t) writePos] = (float) mR;
            ring[2][(size_t) writePos] = (float) aL; ring[3][(size_t) writePos] = (float) aR;
            const int dIdx = (int) (t & mask);
            const int dRead = (int) ((t - latencySamples()) & mask);
            const float dryL = dryRing[0][(size_t) dRead], dryR = dryRing[1][(size_t) dRead];   // delayed by the latency
            dryRing[0][(size_t) dIdx] = (float) dryInL; dryRing[1][(size_t) dIdx] = (float) dryInR;
            writePos = (writePos + 1) & mask;
            if (++hopCount == hop) { hopCount = 0; processFrame(); }

            float wL = 0, wR = 0;
            const int64_t tau = t - latencySamples();
            if (tau >= 0)
            {
                const size_t ai = (size_t) (tau & mask);
                const float nrm = norm[(size_t) (tau % hop)];
                wL = acc[0][ai] / nrm; wR = acc[1][ai] / nrm;
                acc[0][ai] = 0; acc[1][ai] = 0;
            }
            ++t;

            // ---- decode, mix, delta, out, bypass, solo
            double pL = wL, pR = wR;
            if (s.midSide) { pL = (double) wL + wR; pR = (double) wL - wR; }
            const double mixv = mixS.next (mixT, rc), trim = trimS.next (trimT, rc), outv = outS.next (outT, rc), by = bypS.next (bypT, rc);
            double oL, oR;
            if (s.delta) { oL = dryL - pL; oR = dryR - pR; }
            else { oL = dryL * (1.0 - mixv) + pL * trim * mixv; oR = dryR * (1.0 - mixv) + pR * trim * mixv; }
            oL *= outv; oR *= outv;
            if (by > 1e-6) { oL = oL * (1.0 - by) + dryL * by; oR = oR * (1.0 - by) + dryR * by; }
            if (s.solo) { oL = soloL; oR = soloR; }
            outL[i] = (float) oL; outR[i] = (float) oR;
        }
    }

private:
    using Cx = std::complex<float>;

    void allocate()
    {
        const size_t M = (size_t) kMaxFFT, H = (size_t) kMaxFFT / 2 + 2;
        win.reserve (M); norm.reserve (M); pink.reserve (H); A.reserve (M); B.reserve (M); XL.reserve (H); XR.reserve (H);
        for (auto& v : ring) v.reserve (M);
        for (auto& v : acc) v.reserve (M);
        for (auto& v : dryRing) v.reserve (M);
        for (int c = 0; c < 2; ++c) { pw[c].reserve (H); lvl[c].reserve (H); tgt[c].reserve (H); redState[c].reserve (H); gainOut[c].reserve (H); }
        cum.reserve (H + 1); tmp.reserve (H);
        winLo.reserve (H); winHi.reserve (H); dilR.reserve (H); blurR.reserve (H);
        rangeW.reserve (H); aAtk.reserve (H); aRel.reserve (H);
    }

    void updateEq (const Settings::Eq& e)
    {
        if (eqValid && e == eqCache) return;
        eqCache = e; eqValid = true;
        Biquad f[5];
        f[0].setHighpass (fs, e.hpFreq, 0.70710678);
        for (int i = 0; i < 3; ++i) f[1 + i].setPeak (fs, e.peakFreq[i], e.peakQ[i], e.peakGainDb[i]);
        f[4].setLowpass (fs, e.lpFreq, 0.849070);
        for (int b = 0; b < 5; ++b)
            for (auto* q : { &eqL[b], &eqR[b] }) { q->b0 = f[b].b0; q->b1 = f[b].b1; q->b2 = f[b].b2; q->a1 = f[b].a1; q->a2 = f[b].a2; }
    }

    static float lerp (float a, float b, float x) { return a + (b - a) * x; }
    static float sstep (float x) { x = std::clamp (x, 0.f, 1.f); return x * x * (3.f - 2.f * x); }

    // Everything that depends on frequency (per-bin tables). Recomputed only when a relevant setting changes.
    void updateTables (const Settings& s)
    {
        const std::array<float, 13> key = { (float) fs, (float) N, (float) ov, s.detail, s.tiltDetail, s.tiltAttack, s.tiltRelease,
                                            s.sharpness, s.attackMs, s.releaseMs, s.rangeLoHz, s.rangeHiHz, s.resonanceMode ? 1.f : 0.f };
        if (tablesValid && key == tableKey) return;
        tableKey = key; tablesValid = true;

        const int half = N / 2;
        const double binHz = fs / N, hopMs = 1000.0 * hop / fs, hopSec = (double) hop / fs;
        const float sharp = std::clamp (s.sharpness, 0.f, 1.f);
        const int cap = std::max (4, 12 * N / 2048);
        const float frac = lerp (0.045f, 0.002f, sharp);

        knee = s.resonanceMode ? lerp (10.f, 2.f, sharp) : lerp (8.f, 1.f, sharp);
        bias = s.resonanceMode ? knee * 0.5f + 2.5f : 0.f;            // resonance starts ~2.5 dB above the envelope (random noise peaks stay untouched)
        const double tRise = s.resonanceMode ? 0.008 : 0.002, tFall = s.resonanceMode ? 0.040 : 0.010;
        aRise = (float) (1.0 - std::exp (-hopSec / tRise)); aFall = (float) (1.0 - std::exp (-hopSec / tFall));

        const bool loOff = s.rangeLoHz <= 20.5f, hiOff = s.rangeHiHz >= 19990.f;
        for (int k = 0; k <= half; ++k)
        {
            const double f = std::max (k * binHz, 1.0);
            const float pos = (float) std::clamp (2.0 * std::log10 (std::max (f, 20.0) / 20.0) / 3.0 - 1.0, -1.0, 1.0);

            const float d = std::clamp (s.detail + s.tiltDetail * 5.0f * pos, 0.f, 10.f);
            const float wOct = 1.5f * std::pow (0.08f / 1.5f, d / 10.f);
            const float ratio = std::exp2 (wOct);
            int lo = (int) ((float) k / ratio), hi = (int) ((float) k * ratio + 0.5f);
            lo = std::max (0, std::min (lo, k - 2)); hi = std::min (half, std::max (hi, k + 2));
            winLo[(size_t) k] = lo; winHi[(size_t) k] = hi;

            const int dil = std::min (cap, (int) ((float) k * frac + 0.5f) + (sharp < 0.5f ? 1 : 0));
            dilR[(size_t) k] = dil; blurR[(size_t) k] = dil / 2;

            const double tauA = s.attackMs  * std::exp2 ((double) s.tiltAttack  * pos * 2.0);
            const double tauR = s.releaseMs * std::exp2 ((double) s.tiltRelease * pos * 2.0);
            aAtk[(size_t) k] = tauA < 0.01 ? 1.f : (float) (1.0 - std::exp (-hopMs / tauA));
            aRel[(size_t) k] = tauR < 0.01 ? 1.f : (float) (1.0 - std::exp (-hopMs / tauR));

            float w = 1.f;
            if (! loOff) w *= sstep ((float) std::log2 (f / s.rangeLoHz) * 1.5f + 0.5f);
            if (! hiOff) w *= sstep (0.5f - (float) std::log2 (f / s.rangeHiHz) * 1.5f);
            rangeW[(size_t) k] = w;
        }
        for (int i = 0; i < kCurvePoints; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / (kCurvePoints - 1));
            curveBin[(size_t) i] = std::clamp ((int) std::lround (f / binHz), 1, std::max (1, half - 1));
        }
    }

    // reduction target (dB, >= 0) for one channel, before link / max cut / smoothing
    void computeTarget (int ch, float strength)
    {
        const int half = N / 2;
        const float* L = lvl[ch].data(); float* tg = tgt[ch].data();
        tg[0] = 0.f; tg[half] = 0.f;
        if (cur.resonanceMode)
        {
            cum[0] = 0.f;
            for (int k = 0; k <= half; ++k) cum[(size_t) k + 1] = cum[(size_t) k] + L[k];
            for (int k = 0; k <= half; ++k)
            {
                const int lo = winLo[(size_t) k], hi = winHi[(size_t) k];
                const float env = (cum[(size_t) hi + 1] - cum[(size_t) lo]) / (float) (hi - lo + 1);
                tmp[(size_t) k] = std::min (L[k], env + 3.0f);            // peaks must not drag the envelope up
            }
            cum[0] = 0.f;
            for (int k = 0; k <= half; ++k) cum[(size_t) k + 1] = cum[(size_t) k] + tmp[(size_t) k];
            for (int k = 1; k < half; ++k)
            {
                const int lo = winLo[(size_t) k], hi = winHi[(size_t) k];
                const float env = (cum[(size_t) hi + 1] - cum[(size_t) lo]) / (float) (hi - lo + 1);
                const float x = L[k] - env - bias;
                const float gate = std::clamp ((L[k] + 30.f) * 0.1f, 0.f, 1.f);   // ignore the noise floor
                tg[k] = strength * softKnee (x, knee) * rangeW[(size_t) k] * gate;
            }
        }
        else
        {
            for (int k = 1; k < half; ++k)
                tg[k] = strength * softKnee (L[k] - (pink[(size_t) k] + cur.thresholdDb), knee) * rangeW[(size_t) k];
        }
    }

    static float softKnee (float x, float kn)
    {
        if (x <= -kn * 0.5f) return 0.f;
        if (x >= kn * 0.5f) return x;
        const float u = x + kn * 0.5f;
        return u * u / (2.f * kn);
    }

    // Smooth a reduction curve across frequency without losing its depth: dilate (max) then blur (box).
    void smoothCurve (float* t)
    {
        const int half = N / 2;
        for (int k = 0; k <= half; ++k)
        {
            const int r = dilR[(size_t) k];
            if (r <= 0) { tmp[(size_t) k] = t[k]; continue; }
            const int lo = std::max (0, k - r), hi = std::min (half, k + r);
            float m = t[lo]; for (int j = lo + 1; j <= hi; ++j) m = std::max (m, t[j]);
            tmp[(size_t) k] = m;
        }
        cum[0] = 0.f;
        for (int k = 0; k <= half; ++k) cum[(size_t) k + 1] = cum[(size_t) k] + tmp[(size_t) k];
        for (int k = 0; k <= half; ++k)
        {
            const int r = blurR[(size_t) k];
            const int lo = std::max (0, k - r), hi = std::min (half, k + r);
            t[k] = (cum[(size_t) hi + 1] - cum[(size_t) lo]) / (float) (hi - lo + 1);
        }
    }

    // One frame = 3 complex FFTs (two real signals packed per FFT).
    void processFrame()
    {
        const int half = N / 2, mask = N - 1;
        Cx* a = A.data(); Cx* b = B.data();
        for (int i = 0; i < N; ++i)
        {
            const size_t idx = (size_t) ((writePos + i) & mask); const float w = win[(size_t) i];
            a[i] = { ring[0][idx] * w, ring[1][idx] * w };   // main      L + jR
            b[i] = { ring[2][idx] * w, ring[3][idx] * w };   // detection L + jR
        }
        fft.forward (a); fft.forward (b);

        const float lvlOffset = 20.0f * std::log10 (kFftMagnitudeScale) - 20.0f * std::log10 ((float) N / 2048.0f);
        for (int k = 0; k <= half; ++k)
        {
            const int nk = (N - k) & mask;
            const Cx an = std::conj (a[nk]), bn = std::conj (b[nk]);
            XL[(size_t) k] = (a[k] + an) * 0.5f;            XR[(size_t) k] = Cx (0, -0.5f) * (a[k] - an);
            const Cx SL = (b[k] + bn) * 0.5f,               SR = Cx (0, -0.5f) * (b[k] - bn);
            const float p0 = std::norm (SL), p1 = std::norm (SR);
            float& q0 = pw[0][(size_t) k]; q0 += (p0 - q0) * (p0 > q0 ? aRise : aFall);   // fast rise, slow fall
            float& q1 = pw[1][(size_t) k]; q1 += (p1 - q1) * (p1 > q1 ? aRise : aFall);
            lvl[0][(size_t) k] = 10.0f * std::log10 (std::max (q0, 1e-18f)) + lvlOffset;
            lvl[1][(size_t) k] = 10.0f * std::log10 (std::max (q1, 1e-18f)) + lvlOffset;
        }

        const float strength = std::clamp (cur.depth * 0.1f, 0.f, 1.f);
        computeTarget (0, strength); computeTarget (1, strength);

        float* t0 = tgt[0].data(); float* t1 = tgt[1].data();
        const float link = std::clamp (cur.linkPercent * 0.01f, 0.f, 1.f);
        if (link > 0.f)
            for (int k = 0; k <= half; ++k) { const float m = std::max (t0[k], t1[k]); t0[k] += link * (m - t0[k]); t1[k] += link * (m - t1[k]); }
        if (cur.maxCutDb > 0.1f)
        {
            const float M = cur.maxCutDb;
            for (int k = 0; k <= half; ++k) { t0[k] = M * std::tanh (t0[k] / M); t1[k] = M * std::tanh (t1[k] / M); }
        }
        const float f = std::clamp (cur.focus, -1.f, 1.f);
        const float m0 = f > 0.f ? 1.f - f : 1.f, m1 = f < 0.f ? 1.f + f : 1.f;
        for (int k = 0; k <= half; ++k) { t0[k] *= m0; t1[k] *= m1; }

        for (int c = 0; c < 2; ++c)
        {
            smoothCurve (tgt[c].data());
            float* s = redState[c].data(); const float* tg = tgt[c].data(); float* g = gainOut[c].data();
            for (int k = 0; k <= half; ++k)
            {
                const float d = tg[k] - s[k];
                s[k] += d * (d > 0.f ? aAtk[(size_t) k] : aRel[(size_t) k]);   // attack = reduction growing
                g[k] = std::exp (-s[k] * 0.115129255f);                        // dB -> linear
            }
        }
        for (int i = 0; i < kCurvePoints; ++i)
        {
            const int k = curveBin[(size_t) i];
            curve[(size_t) i].store (std::max (redState[0][(size_t) k], redState[1][(size_t) k]), std::memory_order_relaxed);
        }

        // apply (gains are always <= 1) and pack the two channels back into one complex spectrum
        const float* g0 = gainOut[0].data(); const float* g1 = gainOut[1].data();
        for (int k = 0; k <= half; ++k)
        {
            const Cx YL = XL[(size_t) k] * g0[k], YR = XR[(size_t) k] * g1[k];
            a[k] = Cx (YL.real() - YR.imag(), YL.imag() + YR.real());
            if (k > 0 && k < half)
            {
                const Cx cl = std::conj (YL), cr = std::conj (YR);
                a[N - k] = Cx (cl.real() - cr.imag(), cl.imag() + cr.real());
            }
        }
        fft.inverse (a);

        const int64_t start = t - N + 1;
        for (int i = (int) std::max<int64_t> (0, -start); i < N; ++i)
        {
            const size_t ai = (size_t) ((start + i) & mask); const float w = win[(size_t) i];
            acc[0][ai] += a[i].real() * w; acc[1][ai] += a[i].imag() * w;
        }
    }

    double fs = 48000.0;
    int N = 2048, ov = 8, hop = 256;
    FFT fft;
    std::vector<float> win, norm, pink;
    std::vector<Cx> A, B, XL, XR;
    std::vector<float> ring[4], acc[2], dryRing[2];
    std::vector<float> pw[2], lvl[2], tgt[2], redState[2], gainOut[2], cum, tmp;
    std::vector<int> winLo, winHi, dilR, blurR;
    std::vector<float> rangeW, aAtk, aRel;
    std::array<int, kCurvePoints> curveBin {};
    std::array<float, 13> tableKey {}; bool tablesValid = false;
    float knee = 6.f, bias = 4.f, aRise = 0.5f, aFall = 0.1f;
    std::array<std::atomic<float>, kCurvePoints> curve {};
    int writePos = 0, hopCount = 0; int64_t t = 0;
    DcBlock dcSc[2];
    Biquad eqL[5], eqR[5];
    Settings::Eq eqCache; bool eqValid = false;
    Ramp mixS, trimS, outS, bypS;
    Settings cur;
};
} // namespace boba
