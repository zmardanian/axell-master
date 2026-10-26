// Axell Master - DSP suite (JUCE-free: Source/Engine.h exactly as it ships). Exit code = failed checks.
#include "CodecCheck.h"
#include "Engine.h"
#include "Presets.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace axm;
static int fails = 0, checks = 0;
static thread_local bool countAllocs = false;
static long allocCount = 0;
void* operator new (std::size_t n)
{
    if (countAllocs)
        ++allocCount;
    if (void* p = std::malloc (n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static void check (const std::string& name, double v, double lo, double hi, const char* unit)
{
    ++checks;
    const bool ok = v >= lo && v <= hi;
    fails += ok ? 0 : 1;
    std::printf ("%s  %-100s %11.4f %s  [%g .. %g]\n", ok ? "PASS" : "FAIL", name.c_str(), v, unit, lo, hi);
}
static void info (const std::string& name, double v, const char* unit) { std::printf ("INFO  %-100s %11.4f %s\n", name.c_str(), v, unit); }
static double db (double a) { return 20.0 * std::log10 (std::max (a, 1e-30)); }

using Buf = std::vector<std::vector<float>>;
static Buf run (Engine& e, Buf x, int block = 512, bool count = false)
{
    const int n = (int) x[0].size(), nc = (int) x.size();
    for (int p = 0; p < n; p += block)
    {
        float* io[2] {};
        for (int c = 0; c < nc; ++c)
            io[c] = x[(size_t) c].data() + p;
        countAllocs = count;
        e.process (io, nc, std::min (block, n - p));
        countAllocs = false;
    }
    return x;
}
static std::unique_ptr<Engine> make (double fs, const Params& p, int nc = 2)
{
    auto e = std::make_unique<Engine>();
    e->prepare (fs, nc, 512);
    e->setParams (p);
    e->reset();
    return e;
}
// band-limit to fc * fs with a +-H tap Blackman-windowed sinc
static void bandlimit (Buf& b, double fc, int H = 200)
{
    std::vector<double> h ((size_t) (2 * H + 1));
    for (int k = -H; k <= H; ++k)
        h[(size_t) (k + H)] = (k == 0 ? 2 * fc : std::sin (2 * kPi * fc * k) / (kPi * k)) * (0.42 + 0.5 * std::cos (kPi * k / H) + 0.08 * std::cos (2 * kPi * k / H));
    for (auto& ch : b)
    {
        std::vector<float> o (ch.size());
        for (long i = 0; i < (long) ch.size(); ++i)
        {
            double a = 0;
            for (int k = -H; k <= H; ++k)
                if (i - k >= 0 && i - k < (long) ch.size())
                    a += ch[(size_t) (i - k)] * h[(size_t) (k + H)];
            o[(size_t) i] = (float) a;
        }
        ch = o;
    }
}
// drum-bus / master-like program: kick, snare, hats, bass, pad (stereo)
static Buf program (double fs, double secs, double gain, unsigned seed = 3)
{
    Buf b (2, std::vector<float> ((size_t) (secs * fs)));
    std::mt19937 g (seed);
    std::normal_distribution<double> nd;
    for (size_t i = 0; i < b[0].size(); ++i)
    {
        const double t = (double) i / fs, beat = std::fmod (t, 0.5);
        const double kick = std::exp (-beat * 12) * std::sin (2 * kPi * (48 + 90 * std::exp (-beat * 35)) * beat);
        const double sn = std::exp (-std::fmod (t + 0.25, 0.5) * 22) * nd (g) * 0.45;
        const double hat = std::exp (-std::fmod (t, 0.125) * 70) * nd (g) * 0.15;
        const double bass = 0.35 * std::sin (2 * kPi * 55 * t) * (0.6 + 0.4 * std::sin (2 * kPi * 0.5 * t));
        const double pad = 0.12 * (std::sin (2 * kPi * 220 * t) + std::sin (2 * kPi * 277.2 * t + 1) + std::sin (2 * kPi * 329.6 * t + 2));
        b[0][i] = (float) (gain * (kick + sn + hat + bass + pad));
        b[1][i] = (float) (gain * (kick + 0.85 * sn + 1.1 * hat + bass + 0.9 * pad));
    }
    return b;
}
// independent true-peak reference: Hann-windowed sinc +-512 taps, 16 points per sample around the largest samples
static double truePeakRef (const std::vector<float>& x, size_t from, size_t to)
{
    const long H = 512;
    double amax = 0;
    for (size_t i = from; i < to; ++i)
        amax = std::max (amax, (double) std::abs (x[i]));
    double best = amax;
    for (size_t i = from; i < to; ++i)
    {
        if (std::abs (x[i]) < amax * 0.8)
            continue;
        for (int p = 1; p < 16; ++p)
        {
            const double t = (double) i + p / 16.0;
            double s = 0;
            for (long k = (long) i - H; k <= (long) i + H + 1; ++k)
                if (k >= 0 && k < (long) x.size())
                {
                    const double d = t - (double) k;
                    s += x[(size_t) k] * std::sin (kPi * d) / (kPi * d) * (0.5 + 0.5 * std::cos (kPi * d / (H + 2)));
                }
            best = std::max (best, std::abs (s));
        }
    }
    return best;
}
static double thdnDb (const float* y, int n, double f0, double fs)
{
    double ss = 0, cc = 0, sc = 0, xs = 0, xc = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 2 * kPi * f0 * i / fs, s = std::sin (w), c = std::cos (w);
        ss += s * s; cc += c * c; sc += s * c; xs += y[i] * s; xc += y[i] * c;
    }
    const double det = ss * cc - sc * sc, a = (xs * cc - xc * sc) / det, b = (xc * ss - xs * sc) / det;
    double res = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 2 * kPi * f0 * i / fs, e = y[i] - (a * std::sin (w) + b * std::cos (w));
        res += e * e;
    }
    return db (std::sqrt (res / n) / (std::hypot (a, b) / std::sqrt (2.0)));
}

// synthetic voice (Axell Gate's test voice): vowels through formants and sibilants, 1.6 s phrases, 0.8 s pauses
static std::vector<float> voiceTake (double fs, double secs, double gainDb, double noiseDb)
{
    const size_t n = (size_t) (secs * fs);
    std::vector<float> x (n);
    std::mt19937 g (5);
    std::normal_distribution<double> nd;
    const double gain = std::pow (10.0, gainDb / 20.0), ng = std::pow (10.0, noiseDb / 20.0), pi = 3.14159265358979323846;
    double ph = 0.0, b0 = 0, b1 = 0, s1 = 0, s2 = 0;
    const double w = 2 * pi * 7000 / fs, rr = std::exp (-pi * 1500 / fs);
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / fs, phrase = std::fmod (t, 2.4), syl = std::fmod (phrase, 0.32);
        double y = 0.0;
        if (phrase > 0.2 && phrase < 1.8)
        {
            const double f0 = 140.0 + 25.0 * std::sin (2 * pi * 0.6 * t);
            ph += 2 * pi * f0 / fs;
            if (syl < 0.22)
            {
                const double env = std::min (1.0, std::min (syl, 0.22 - syl) / 0.015);
                double s = 0;
                for (int h = 1; h * f0 < std::min (0.45 * fs, 8000.0); ++h)
                {
                    const double f = h * f0;
                    s += (std::exp (-std::pow ((f - 650) / 250, 2)) + 0.6 * std::exp (-std::pow ((f - 1150) / 300, 2))
                          + 0.2 * std::exp (-std::pow ((f - 2600) / 500, 2))) * std::sin (h * ph) / std::sqrt ((double) h);
                }
                y = 0.35 * env * s;
            }
            else
            {
                const double a = (1 - rr) * nd (g) + 2 * rr * std::cos (w) * s1 - rr * rr * s2;
                s2 = s1;
                s1 = a;
                y = 0.8 * a * std::sin (pi * (syl - 0.22) / 0.1);
            }
        }
        const double wn = nd (g);
        b0 = 0.997 * b0 + 0.03 * wn;
        b1 = 0.6 * b1 + 0.4 * wn;
        x[i] = (float) (gain * y + ng * (b0 + 0.3 * b1));
    }
    return x;
}
static double cycleRmsDb (const std::vector<float>& x, double fs, double lo, double hi)
{
    double e = 0.0;
    size_t k = 0;
    for (size_t i = (size_t) (2.4 * fs); i < x.size(); ++i)
    {
        const double ph = std::fmod ((double) i / fs, 2.4);
        if (ph >= lo && ph < hi)
        {
            e += (double) x[i] * x[i];
            ++k;
        }
    }
    return 10.0 * std::log10 (e / std::max<size_t> (k, 1) + 1.0e-30);
}

int main (int argc, char** argv)
{
    const bool quick = argc > 1 && std::string (argv[1]) == "--quick";
    const auto t0 = std::chrono::steady_clock::now();
    for (double fs : quick ? std::vector<double> { 48000.0 } : std::vector<double> { 44100.0, 48000.0, 96000.0 })
    {
        const std::string r = " @" + std::to_string ((int) fs);
        Params def;
        // 0. 3.0 VOICE HOLD (a vocal bus driven ~10 dB into the limiter): pauses no longer lifted, the voice the same
        {
            const auto v = voiceTake (fs, 12.0, -10.0, -56.0);
            Params p;
            p.inputDb = 24.0f;
            p.osLog2 = 2;
            auto eOff = make (fs, p);
            const auto off = run (*eOff, Buf { v, v });
            p.voiceHold = true;
            auto eOn = make (fs, p);
            const auto on = run (*eOn, Buf { v, v });
            const int L = eOn->getLatencySamples();
            auto al = [L] (std::vector<float> y) { y.erase (y.begin(), y.begin() + L); return y; };
            const auto yo = al (off[0]), yn = al (on[0]);
            const double liftOff = (cycleRmsDb (yo, fs, 1.9, 2.35) - cycleRmsDb (yo, fs, 0.3, 1.7)) - (cycleRmsDb (v, fs, 1.9, 2.35) - cycleRmsDb (v, fs, 0.3, 1.7));
            const double liftOn = (cycleRmsDb (yn, fs, 1.9, 2.35) - cycleRmsDb (yn, fs, 0.3, 1.7)) - (cycleRmsDb (v, fs, 1.9, 2.35) - cycleRmsDb (v, fs, 0.3, 1.7));
            info ("VOICE HOLD off: pauses re voice (lift)" + r, liftOff, "dB");
            info ("VOICE HOLD on: pauses re voice (lift)" + r, liftOn, "dB");
            check ("VOICE HOLD lowers the pauses' lift by" + r, liftOff - liftOn, 6.0, 60.0, "dB");
            check ("VOICE HOLD: voice level unchanged" + r, cycleRmsDb (yn, fs, 0.3, 1.7) - cycleRmsDb (yo, fs, 0.3, 1.7), -0.75, 0.75, "dB");
            double tp = 0.0;
            for (float x : yn)
                tp = std::max (tp, (double) std::abs (x));
            check ("VOICE HOLD: sample peak under the ceiling" + r, db (tp), -60.0, -1.0, "dBFS");
            check ("VOICE HOLD: latency unchanged" + r, eOn->getLatencySamples() - eOff->getLatencySamples(), 0, 0, "smp");
        }
        // 0b. 4.0 ESS HOLD (the same vocal bus, VOICE HOLD on as in the vocal presets): esses no longer lifted, vowels the same
        {
            const auto v = voiceTake (fs, 12.0, -10.0, -70.0);
            Params p;
            p.inputDb = 24.0f;
            p.osLog2 = 2;
            p.voiceHold = true;
            auto eOff = make (fs, p);
            const auto off = run (*eOff, Buf { v, v });
            p.essHold = true;
            auto eOn = make (fs, p);
            const auto on = run (*eOn, Buf { v, v });
            const int L = eOn->getLatencySamples();
            auto al = [L] (std::vector<float> y) { y.erase (y.begin(), y.begin() + L); return y; };
            const auto yo = al (off[0]), yn = al (on[0]);
            // per 0.32 s syllable of each phrase: vowel 0.02-0.20 s, ess 0.24-0.31 s
            auto part = [fs] (const std::vector<float>& x, double lo, double hi)
            {
                double e = 0.0;
                size_t k = 0;
                for (size_t i = (size_t) (2.4 * fs); i < x.size(); ++i)
                {
                    const double ph = std::fmod ((double) i / fs, 2.4);
                    if (ph < 0.2 || ph >= 1.8)
                        continue;
                    const double syl = std::fmod (ph, 0.32);
                    if (syl >= lo && syl < hi)
                    {
                        e += (double) x[i] * x[i];
                        ++k;
                    }
                }
                return 10.0 * std::log10 (e / std::max<size_t> (k, 1) + 1.0e-30);
            };
            const double in = part (v, 0.24, 0.31) - part (v, 0.02, 0.20);
            const double liftOff = part (yo, 0.24, 0.31) - part (yo, 0.02, 0.20) - in, liftOn = part (yn, 0.24, 0.31) - part (yn, 0.02, 0.20) - in;
            info ("ESS HOLD off: esses re vowels (lift)" + r, liftOff, "dB");
            info ("ESS HOLD on: esses re vowels (lift)" + r, liftOn, "dB");
            check ("ESS HOLD removes >= 60 % of the esses' lift (margin, dB)" + r, (liftOff - liftOn) - 0.6 * std::max (liftOff, 0.0), 0.0, 60.0, "dB");
            check ("ESS HOLD: vowel level unchanged" + r, part (yn, 0.02, 0.20) - part (yo, 0.02, 0.20), -0.75, 0.75, "dB");
            double tp = 0.0;
            for (float x : yn)
                tp = std::max (tp, (double) std::abs (x));
            check ("ESS HOLD: sample peak under the ceiling" + r, db (tp), -60.0, -1.0, "dBFS");
            check ("ESS HOLD: latency unchanged" + r, eOn->getLatencySamples() - eOff->getLatencySamples(), 0, 0, "smp");
        }
        // 1. silence, transparency, latency
        {
            auto e = make (fs, def);
            auto y = run (*e, Buf (2, std::vector<float> ((size_t) fs, 0.0f)));
            double m = 0;
            for (auto& ch : y)
                for (float v : ch)
                    m = std::max (m, (double) std::abs (v));
            check ("silence in -> exactly 0 out (defaults)" + r, m, 0, 0, "");
            for (int os = 0; os <= 4; ++os)
                for (float lk : { 0.5f, 2.0f, 5.0f })
                {
                    Params p = def;
                    p.osLog2 = os;
                    p.lookMs = lk;
                    auto e2 = make (fs, p);
                    Buf x = program (fs, 1.0, 0.05);
                    bandlimit (x, 0.4, 100);
                    auto y2 = run (*e2, x);
                    const int L = e2->getLatencySamples();
                    double err = 0;
                    for (size_t c = 0; c < 2; ++c)
                        for (size_t i = (size_t) L + 2000; i < x[0].size() - 2000; ++i)
                            err = std::max (err, (double) std::abs (y2[c][i] - x[c][i - (size_t) L]));
                    const int expect = e2->latencyOf ({ os, (int) std::lround (lk * 0.001 * fs) });
                    check ("below the ceiling, os " + std::to_string (1 << os) + "x, lookahead " + std::to_string (lk).substr (0, 3) +
                           " ms: output = input delayed by the latency (< 0.4 fs)" + r, db (err), -600, -95.0, "dBFS");
                    check ("latency reported = oversampler + 64 + lookahead (" + std::to_string (1 << os) + "x, " + std::to_string (lk).substr (0, 3) + " ms)" + r,
                           L, expect, expect, "samples");
                }
        }
        // 2. ceiling: every style, clip 0 / 50 / 100 %, link 100 / 0 %, -1 and -0.3 dBTP
        {
            Buf base = program (fs, 3.0, 1.6);
            bandlimit (base, 0.47);
            const char* st[] = { "transparent", "punchy", "loud", "safe" };
            double worstTp = -100, worstSp = -100;
            for (int s = 0; s < 4; ++s)
                for (float cl : { 0.0f, 50.0f, 100.0f })
                    for (float lk : { 100.0f, 0.0f })
                    {
                        Params p = def;
                        p.style = s; p.clipPct = cl; p.linkPct = lk; p.inputDb = 6.0f;
                        auto e = make (fs, p);
                        auto y = run (*e, base);
                        const size_t from = (size_t) (0.5 * fs);
                        double sp = 0, tp = 0;
                        for (auto& ch : y)
                        {
                            for (size_t i = from; i < ch.size(); ++i)
                                sp = std::max (sp, (double) std::abs (ch[i]));
                            if (s == 0 || cl == 100.0f)
                                tp = std::max (tp, truePeakRef (ch, from, ch.size() - 1000));
                        }
                        worstSp = std::max (worstSp, db (sp));
                        if (tp > 0)
                            worstTp = std::max (worstTp, db (tp));
                        (void) st;
                    }
            check ("ceiling -1 dBTP, program +~12 dB (band-limited), all styles / clip / link: worst sample peak" + r, worstSp, -60, -1.0, "dBFS");
            check ("ceiling -1 dBTP, same: worst true peak (independent long-sinc reference)" + r, worstTp, -60, -1.0, "dBTP");
            Params p = def;
            p.ceilingDb = -0.3f; p.inputDb = 8.0f; p.clipPct = 60.0f;
            auto e = make (fs, p);
            auto y = run (*e, base);
            double tp = 0;
            for (auto& ch : y)
                tp = std::max (tp, truePeakRef (ch, (size_t) (0.5 * fs), ch.size() - 1000));
            check ("ceiling -0.3 dBTP, clip 60 %, +14 dB: true peak (reference)" + r, db (tp), -60, -0.3, "dBTP");
            check ("engine's own true-peak meter agrees with the reference (difference)" + r, e->outputTruePeakDb() - db (tp), -0.06, 0.06, "dB");
            // full-band input (noise snare and hats up to Nyquist), clipper at every oversampling factor incl. 1x
            Buf full = program (fs, 3.0, 1.6, 11);
            double worstFull = -100;
            for (int os = 0; os <= 4; ++os)
            {
                Params q = def;
                q.osLog2 = os; q.clipPct = 60.0f; q.inputDb = 8.0f;
                auto e2 = make (fs, q);
                auto y2 = run (*e2, full);
                double t2 = 0;
                for (auto& ch : y2)
                    t2 = std::max (t2, truePeakRef (ch, (size_t) (0.5 * fs), ch.size() - 1000));
                worstFull = std::max (worstFull, db (t2));
            }
            check ("full-band input up to Nyquist, clip 60 %, 1x..16x: worst true peak (reference), ceiling -1" + r, worstFull, -60, -1.0, "dBTP");
        }
        // 3. clipper aliasing: 5 kHz sine hard-clipped 6 dB, worst non-harmonic component relative to the fundamental
        for (int os = 2; os <= 4; ++os)
        {
            Params p = def;
            p.clipPct = 100.0f; p.clipShapePct = 100.0f; p.inputDb = 6.0f; p.ceilingDb = -6.0f; p.osLog2 = os;
            auto e = make (fs, p, 1);
            Buf x (1, std::vector<float> ((size_t) (1.5 * fs)));
            for (size_t i = 0; i < x[0].size(); ++i)
                x[0][i] = (float) (0.5 * std::sin (2 * kPi * 5000.0 * (double) i / fs));
            auto y = run (*e, x);
            const int N = 32768;
            std::vector<double> re (N), im (N, 0.0);
            for (int i = 0; i < N; ++i)
            {
                const double w = 0.35875 - 0.48829 * std::cos (2 * kPi * i / N) + 0.14128 * std::cos (4 * kPi * i / N) - 0.01168 * std::cos (6 * kPi * i / N);
                re[(size_t) i] = y[0][y[0].size() - (size_t) N + (size_t) i] * w;
            }
            for (int i = 1, j = 0; i < N; ++i)
            {
                int bit = N >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
            }
            for (int len = 2; len <= N; len <<= 1)
                for (int i = 0; i < N; i += len)
                    for (int k = 0; k < len / 2; ++k)
                    {
                        const double wr = std::cos (-2 * kPi * k / len), wi = std::sin (-2 * kPi * k / len);
                        const size_t u = (size_t) (i + k), v = (size_t) (i + k + len / 2);
                        const double vr = re[v] * wr - im[v] * wi, vi = re[v] * wi + im[v] * wr;
                        re[v] = re[u] - vr; im[v] = im[u] - vi;
                        re[u] += vr; im[u] += vi;
                    }
            double worst = 0, fund = 0;
            for (int k = 1; k < N / 2; ++k)
            {
                const double f = k * fs / N, mag = std::hypot (re[(size_t) k], im[(size_t) k]);
                if (std::abs (f - 5000.0) < 30.0)
                    fund = std::max (fund, mag);
                bool harm = false;
                for (int h = 1; h * 5000.0 < fs / 2 + 100; ++h)
                    harm = harm || std::abs (f - h * 5000.0) < 30.0;
                if (! harm && f > 20 && f < std::min (20000.0, fs / 2))
                    worst = std::max (worst, mag);
            }
            const double lim = os == 2 ? -75.0 : os == 3 ? -90.0 : -99.0; // worst of 44.1 / 48 / 96 kHz (folding depends on the rate)
            check ("clipper " + std::to_string (1 << os) + "x, 5 kHz hard-clipped 6 dB: worst alias below 20 kHz re fundamental" + r,
                   db (worst / fund), -400, lim, "dBc");
        }
        // 4. distortion per style at the same loudness: 50 Hz sine (bass) and the program
        {
            const char* st[] = { "transparent", "punchy", "loud", "safe" };
            for (int s = 0; s < 4; ++s)
            {
                Params p = def;
                p.style = s; p.inputDb = 6.0f;
                auto e = make (fs, p, 1);
                Buf x (1, std::vector<float> ((size_t) (3.0 * fs)));
                for (size_t i = 0; i < x[0].size(); ++i)
                    x[0][i] = (float) (0.89 * std::sin (2 * kPi * 50.0 * (double) i / fs) * (i % (size_t) (0.5 * fs) < (size_t) (0.25 * fs) ? 1.0 : 0.5));
                auto y = run (*e, x);
                // steady 50 Hz at 6 dB GR: THD+N over the last loud quarter-second
                const size_t a = (size_t) (2.5 * fs) + (size_t) (0.05 * fs), n = (size_t) (0.18 * fs);
                const double lim[] = { -100.0, -75.0, -50.0, -100.0 }; // bass-aware hold (DECISIONS M5)
                check (std::string ("bass 50 Hz, ~6 dB GR, bursts: THD+N, style ") + st[s] + r, thdnDb (y[0].data() + a, (int) n, 50.0, fs),
                       -400, lim[s], "dB");
            }
        }
        // 5. loudness through the engine (EBU 3341 case 1 levels, below the ceiling)
        {
            Params p = def;
            p.ceilingDb = 0.0f;
            auto e = make (fs, p);
            Buf x (2, std::vector<float> ((size_t) (12 * fs)));
            for (size_t i = 0; i < x[0].size(); ++i)
                x[0][i] = x[1][i] = (float) (std::pow (10.0, -23.0 / 20.0) * std::sin (2 * kPi * 1000.0 * (double) i / fs));
            run (*e, x);
            check ("engine meters: -23 dBFS stereo 1 kHz -> output integrated" + r, e->outputLoudness().integratedLufs(), -23.1, -22.9, "LUFS");
            check ("engine meters: input integrated" + r, e->inputLoudness().integratedLufs(), -23.1, -22.9, "LUFS");
        }
        // 6. compare: the unprocessed input at the output's loudness
        {
            Params p = def;
            p.inputDb = 9.0f; p.clipPct = 40.0f;
            auto e0 = make (fs, p);
            Buf x = program (fs, 4.0, 0.25);
            auto y0 = run (*e0, x);
            p.compare = true;
            auto e1 = make (fs, p);
            auto y1 = run (*e1, x);
            auto rms = [&] (const Buf& b) { double s = 0; size_t n = 0; for (auto& ch : b) for (size_t i = (size_t) (2.5 * fs); i < ch.size(); ++i) { s += (double) ch[i] * ch[i]; ++n; } return 10 * std::log10 (s / n); };
            info ("compare: master louder than the input by" + r, rms (y0) - rms (x), "dB");
            check ("compare: unprocessed at the master's loudness (difference)" + r, rms (y1) - rms (y0), -1.0, 1.0, "dB");
        }
        // 7. dither on the 16-bit grid
        {
            Params p = def;
            p.dither = 2;
            auto e = make (fs, p);
            auto y = run (*e, program (fs, 1.0, 0.1));
            double off = 0;
            for (size_t i = (size_t) (0.2 * fs); i < y[0].size(); ++i)
                off = std::max (off, std::abs (y[0][i] * 32768.0 - std::round (y[0][i] * 32768.0)));
            check ("dither 16-bit: every output sample on the 16-bit grid (distance)" + r, off, 0, 1e-3, "LSB");
        }
        // 8. block sizes, allocations
        {
            Params p = def;
            p.clipPct = 40.0f; p.inputDb = 8.0f; p.style = 1; p.linkPct = 60.0f; p.dither = 3;
            Buf x = program (fs, 2.0, 0.3);
            auto e1 = make (fs, p), e2 = make (fs, p);
            auto a = run (*e1, x, 512);
            allocCount = 0;
            auto b = run (*e2, x, 37, true);
            double d = 0;
            for (size_t c = 0; c < 2; ++c)
                for (size_t i = 0; i < a[0].size(); ++i)
                    d = std::max (d, (double) std::abs (a[c][i] - b[c][i]));
            check ("host block size 512 vs 37 (clip, punchy, link 60 %, shaped dither): difference" + r, db (d), -600, -140, "dBFS");
            check ("heap allocations inside process()" + r, (double) allocCount, 0, 0, "");
        }
        // 9. latency handshake: a lookahead change waits for the host
        {
            Engine e;
            e.prepare (fs, 2, 512);
            e.setParams (def);
            e.reset();
            e.handshake().setHostLatency (e.getLatencySamples());
            const int L0 = e.getLatencySamples();
            Params p = def;
            p.lookMs = 4.0f;
            e.setParams (p);
            Buf x = program (fs, 0.3, 0.2);
            auto y = run (e, x);
            check ("handshake: unacknowledged change keeps the old latency" + r, e.getLatencySamples(), L0, L0, "samples");
            double late = 0;
            for (size_t i = (size_t) (0.05 * fs); i < y[0].size(); ++i)
                late = std::max (late, (double) std::abs (y[0][i]));
            check ("handshake: muted while waiting for the host" + r, db (late), -600, -120, "dBFS");
            int lat = 0;
            const bool took = e.handshake().take (lat);
            check ("handshake: request carries the new latency" + r, took ? lat : -1, L0 + (int) std::lround (0.002 * fs), L0 + (int) std::lround (0.002 * fs), "samples");
            e.handshake().ack();
            run (e, program (fs, 0.3, 0.2));
            check ("handshake: after the acknowledgement the new latency plays" + r, e.getLatencySamples(), lat, lat, "samples");
        }
        // 10. presets: ceiling held, no NaN
        for (const auto& fp : factoryPresets())
        {
            auto e = make (fs, fp.params);
            Buf x = program (fs, 2.0, 0.6);
            bandlimit (x, 0.47, 100);
            auto y = run (*e, x);
            double sp = 0;
            bool finite = true;
            for (auto& ch : y)
                for (size_t i = (size_t) (0.3 * fs); i < ch.size(); ++i)
                {
                    finite = finite && std::isfinite (ch[i]);
                    sp = std::max (sp, (double) std::abs (ch[i]));
                }
            check (std::string ("preset '") + fp.name + "': sample peak <= its ceiling" + r, db (sp), -60, fp.params.ceilingDb, "dBFS");
            check (std::string ("preset '") + fp.name + "': output finite" + r, finite ? 1 : 0, 1, 1, "");
        }
    }
    // 2.0: the streaming codec check (Opus / Vorbis encode-decode) - the codecs as they ship, unthreaded and threaded
    {
        auto feed = [] (CodecCheck& cc, const Buf& x, bool count)
        {
            const int n = (int) x[0].size();
            long allocs = 0;
            for (int p = 0; p < n; p += 512)
            {
                const float* io[2] { x[0].data() + p, x[1].data() + p };
                countAllocs = count;
                const long before = allocCount;
                cc.push (io, 2, std::min (512, n - p));
                allocs += allocCount - before;
                countAllocs = false;
            }
            return allocs;
        };
        auto sine = [] (double fs, double secs, double dbfs)
        {
            Buf x (2, std::vector<float> ((size_t) (secs * fs)));
            for (size_t i = 0; i < x[0].size(); ++i)
                x[0][i] = x[1][i] = (float) (std::pow (10.0, dbfs / 20.0) * std::sin (2 * 3.14159265358979 * 997.0 * (double) i / fs));
            return x;
        };
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0 })
        {
            const std::string r = " @ " + std::to_string ((int) fs) + " Hz";
            CodecCheck cc;
            cc.prepare (fs, false);
            cc.drain();
            check ("codec check: Opus and Vorbis open" + r, cc.available (0) && cc.available (1) ? 1 : 0, 1, 1, "");
            const long allocs = feed (cc, sine (fs, 4.0, -6.0), true);
            check ("codec check: push() never allocates (audio thread)" + r, (double) allocs, 0, 0, "allocs");
            cc.drain();
            check ("codec check: 1 kHz sine at -6 dBFS -> Opus decodes at -6 dBTP" + r, cc.worstDb (0), -6.6, -5.4, "dBTP");
            check ("codec check: 1 kHz sine at -6 dBFS -> Vorbis decodes at -6 dBTP" + r, cc.worstDb (1), -6.6, -5.2, "dBTP");
            check ("codec check: no clipped samples at -6 dBFS" + r, cc.clippedSamples (0) + cc.clippedSamples (1), 0, 0, "");
            check ("codec check: the whole signal was measured" + r, cc.measuredSeconds(), 3.9, 4.01, "s");
            check ("codec check: SAFE has nothing to do at -6 dBTP" + r, cc.safeCeilingChangeDb(), 0, 0, "dB");
        }
        {
            CodecCheck cc;
            cc.prepare (48000.0, false);
            cc.drain();
            feed (cc, sine (48000.0, 2.0, 1.0), false);   // +1 dBFS: clips after decoding
            cc.drain();
            const float w = std::max (cc.worstDb (0), cc.worstDb (1));
            check ("codec check: a +1 dBFS sine decodes above full scale and clips", w, 0.5, 1.6, "dBTP");
            check ("codec check: ... with clipped samples counted", cc.clippedSamples (0) + cc.clippedSamples (1), 1, 1e9, "");
            check ("codec check: SAFE lowers the ceiling to bring the worse codec to the target",
                   cc.safeCeilingChangeDb(), CodecCheck::kTargetDb - w - 1e-4, CodecCheck::kTargetDb - w + 1e-4, "dB");
            cc.requestReset();
            cc.drain();
            check ("codec check: reset clears the measurement", cc.measuredSeconds() + cc.clippedSamples (0) + cc.clippedSamples (1)
                   + (cc.worstDb (0) > -150.0f ? 1 : 0), 0, 0, "");
            feed (cc, Buf (2, std::vector<float> (48000 * 2)), false);   // digital silence
            cc.drain();
            check ("codec check: silence decodes to silence (no peak, no clip)", std::max (cc.worstDb (0), cc.worstDb (1)), -200, -150, "dBTP");
            // a render far faster than the codecs: more than the 30 s ring without draining -> flagged, never blocking
            cc.requestReset();
            cc.drain();
            feed (cc, sine (48000.0, 31.0, -6.0), false);
            check ("codec check: audio it could not keep up with is flagged incomplete", cc.incomplete() ? 1 : 0, 1, 1, "");
            cc.drain();
        }
        {
            // threaded, as in the plugin: the worker keeps up with real-time-sized pushes
            CodecCheck cc;
            cc.prepare (48000.0, true);
            const auto x = sine (48000.0, 5.0, -3.0);
            for (int p = 0; p < (int) x[0].size(); p += 4800)
            {
                const float* io[2] { x[0].data() + p, x[1].data() + p };
                cc.push (io, 2, std::min (4800, (int) x[0].size() - p));
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            }
            for (int k = 0; k < 3000 && cc.measuredSeconds() < 4.9; ++k)   // up to 30 s: slow under sanitizers
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            check ("codec check (worker thread): measured the whole 5 s", cc.measuredSeconds(), 4.9, 5.01, "s");
            // (Vorbis decodes a pure tone ~0.5 dB hot)
            check ("codec check (worker thread): -3 dBFS sine decodes at -3 dBTP", std::max (cc.worstDb (0), cc.worstDb (1)), -3.6, -2.2, "dBTP");
            check ("codec check (worker thread): nothing dropped", cc.incomplete() ? 1 : 0, 0, 0, "");
            cc.stop();
        }
    }
    // CPU: 48 kHz stereo, 512 blocks, defaults (4x clipper path) and everything on
    if (! quick)
        for (int mode = 0; mode < 3; ++mode)
        {
            Params p;
            if (mode == 1) { p.clipPct = 50.0f; p.inputDb = 8.0f; p.dither = 3; p.linkPct = 50.0f; }
            if (mode == 2) { p.inputDb = 8.0f; p.voiceHold = true; p.essHold = true; }   // 4.0: a vocal preset's detectors
            auto e = make (48000.0, p);
            Buf x = program (48000.0, 20.0, 0.3);
            const auto a = std::chrono::steady_clock::now();
            run (*e, x);
            const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count();
            check (std::string ("CPU, 48 kHz stereo, ") + (mode == 2 ? "VOICE HOLD + ESS HOLD on (16x)" : mode ? "clip 50 % + shaped dither + link 50 %" : "defaults (16x)")
                       + ": % of one core", s / 20.0 * 100.0, 0, mode == 2 ? 9.0 : 6.0, "%");
        }
    std::printf ("%s: %d of %d checks passed (%.1f s)\n", fails == 0 ? "ALL PASS" : "FAILURES", checks - fails, checks,
                 std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
    return std::min (fails, 250);
}
