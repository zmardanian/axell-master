#pragma once
// Axell Master - the whole signal chain, framework independent (no JUCE): the plugin wrapper only maps parameters.
//
//   in -> sanitize -> [dry ring: compare / bypass, aligned to the full latency]
//      -> input gain -> upsample 1x/2x/4x -> soft clipper -> downsample -> true-peak limiter (style, release, link)
//      -> dither -> [level-matched compare | bypass] -> structural fade -> out
//   meters: input + output loudness (BS.1770-4 / EBU R128), output true peak, limiter GR, clipper GR, display history.
//
// Real-time safety: prepare() allocates everything; process() never allocates, locks or throws.
#include <axell/dsp/Dither.h>
#include <axell/dsp/Filters.h>
#include <axell/dsp/History.h>
#include <axell/dsp/LatencyHandshake.h>
#include <axell/dsp/Loudness.h>
#include <axell/dsp/MasterLimiter.h>
#include <axell/dsp/VoicePresence.h>   // 3.0 VOICE HOLD: the suite's shared voice decision (Axell Gate's)
#include <axell/dsp/Oversampler.h>
#include <axell/dsp/TruePeak.h>

#include <array>
#include <atomic>
#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__)
 #include <xmmintrin.h>
 #define AXM_HAS_SSE 1
#endif

namespace axm
{
using namespace axell;

struct Params
{
    float inputDb = 0.0f;          // drive into the clipper / limiter
    float clipPct = 0.0f;          // 0 = clipper off; 100 = the clipper starts at the ceiling
    float clipShapePct = 30.0f;    // 0 = soft knee, 100 = hard clip
    float ceilingDb = -1.0f;       // dBTP
    float releaseMs = 120.0f;
    int   style = 0;               // 0 transparent, 1 punchy, 2 loud, 3 safe
    float lookMs = 2.0f;           // limiter lookahead 0.5 .. 5 ms
    float linkPct = 100.0f;        // stereo link
    int   osLog2 = 4;              // clipper oversampling 1x / 2x / 4x / 8x / 16x (16x default: aliasing < -99 dBc)
    int   dither = 0;              // 0 off, 1 24-bit, 2 16-bit TPDF, 3 16-bit noise-shaped
    bool  voiceHold = false;       // 3.0: in a vocal's pauses the limiter holds its gain (vocal-bus presets turn it on)
    bool  compare = false;         // the unprocessed input at the output's loudness
    bool  bypass = false;          // host bypass (latency-compensated)
};

struct DisplayFrame
{
    float inPeakDb = -120.0f;    // input after the gain (what the clipper/limiter are fed)
    float outPeakDb = -120.0f, limGrDb = 0.0f, clipGrDb = 0.0f, outShortLufs = -120.0f;
    float outPeakChDb[2] { -120.0f, -120.0f };
};

struct ScopedFtz
{
#if AXM_HAS_SSE
    ScopedFtz() noexcept : saved (_mm_getcsr()) { _mm_setcsr (saved | 0x8040u); }
    ~ScopedFtz() { _mm_setcsr (saved); }
    unsigned int saved;
#endif
};

class Engine
{
public:
    static constexpr int kChunk = 256;
    static constexpr double kMaxLookMs = 5.0, kMinLookMs = 0.5;

    struct Config
    {
        int osLog2 = 2, look = 96;
        bool operator== (const Config& o) const noexcept { return osLog2 == o.osLog2 && look == o.look; }
        bool operator!= (const Config& o) const noexcept { return ! (*this == o); }
    };

    void prepare (double sampleRate, int numChannels, int maxBlockSize = 0)
    {
        fs = sampleRate;
        numCh = std::clamp (numChannels, 1, kMaxCh);
        maxBlock = std::max (0, maxBlockSize);
        os.prepare (kChunk, fs, true); // band-limited down path: the true-peak guarantee holds for any input (M2)
        maxLook = (int) std::ceil (kMaxLookMs * 0.001 * fs);
        lim.prepare (fs, maxLook);
        inMeter.prepare (fs, numCh);
        outMeter.prepare (fs, numCh);
        outTp.prepare();
        dither.prepare (fs);
        kwDry.prepare (fs);
        kwWet.prepare (fs);
        int maxOs = 0;
        for (int k = 0; k <= Oversampler::kMaxFactorLog2; ++k)
            maxOs = std::max (maxOs, os.latencyFor (k));
        const int maxLat = maxOs + MasterLimiter::latencyFor (maxLook);
        int ring = 1;
        while (ring < maxLat + kChunk + 1)
            ring <<= 1;
        ringMask = ring - 1;
        for (auto& r : dryRing)
            r.assign ((size_t) ring, 0.0f);
        smoothK = 1.0f - onePole (0.02f, fs);
        switchK = 1.0f - onePole (0.01f, fs);
        fadeStep = (float) (1.0 / std::max (1.0, 0.003 * fs));
        loudK = 1.0f - onePole (0.4f, fs);
        cmpK = 1.0f - onePole (0.25f, fs);
        hopLen = std::max (1, (int) std::lround (0.01 * fs));
        voice.prepare (fs);
        pauseSpan = (int) (10.0 * fs);
        sinceVoice = pauseSpan;
        setParams (params);
        active = want;
        applyConfig();
        hs.prepared (latency, fs);
        reset();
    }

    void reset() noexcept
    {
        // a configuration whose latency the host does not compensate yet waits for the handshake (control())
        const bool apply = ! hs.tracking() || latencyOf (want) == latencyOf (active);
        if (apply && want != active)
        {
            active = want;
            applyConfig();
        }
        os.reset();
        lim.reset();
        dither.reset();
        voice.reset();
        sinceVoice = pauseSpan;
        for (auto& r : dryRing)
            std::fill (r.begin(), r.end(), 0.0f);
        writePos = 0;
        inGain = inTarget;
        cmpAmt = cmpTarget;
        bypassAmt = bypassTarget;
        cmpGain = 1.0f;
        msDry = msWet = 0.0;
        kwDry.resetState();
        kwWet.resetState();
        fadeGain = fadeTgt = (want == active) ? 1.0f : 0.0f; // otherwise start muted until the switch
        warm = 0;
        hopCount = 0;
        hopIn = hopLim = hopClip = 0.0f;
        hopOutCh = { 0.0f, 0.0f };
    }

    // clipper threshold (linear): from 6 dB above the ceiling (CLIP 0 %) to the ceiling (100 %); knee half-width from
    // 50 % of the threshold (SHAPE soft) to 15 % (hard): a true hard clip aliases even at 4x (DECISIONS M3)
    static void clipShape (const Params& p, float& thr, float& knee) noexcept
    {
        const float ceil = std::pow (10.0f, std::clamp (p.ceilingDb, -24.0f, 0.0f) / 20.0f);
        const float pct = std::clamp (p.clipPct, 0.0f, 100.0f) * 0.01f;
        thr = ceil * std::pow (10.0f, 6.0f * (1.0f - pct) / 20.0f);
        knee = (0.5f - 0.35f * std::clamp (p.clipShapePct, 0.0f, 100.0f) * 0.01f) * thr;
    }
    // quadratic soft knee: identity below thr - knee, C1-continuous, flat at thr from thr + knee
    static float clipCurve (float x, float thr, float knee) noexcept
    {
        const float a = std::abs (x);
        const float lo = thr - knee;
        if (a <= lo)
            return x;
        float y;
        if (knee > 0.0f && a < thr + knee)
        {
            const float d = a - lo;
            y = a - d * d / (4.0f * knee);
        }
        else
            y = thr;
        return x < 0.0f ? -y : y;
    }

    void setParams (const Params& p) noexcept
    {
        params = p;
        inTarget = dbToGain (std::clamp (p.inputDb, -24.0f, 36.0f));
        clipOn = p.clipPct > 0.0f;
        clipShape (p, clipThr, clipKnee);
        lim.setParams (p.ceilingDb, p.releaseMs, (MasterLimiter::Style) std::clamp (p.style, 0, 3), std::clamp (p.linkPct, 0.0f, 100.0f) * 0.01f);
        switch (p.dither)
        {
            case 1: dither.set (24, Dither::Mode::tpdf); break;
            case 2: dither.set (16, Dither::Mode::tpdf); break;
            case 3: dither.set (16, Dither::Mode::shaped); break;
            default: dither.set (24, Dither::Mode::off); break;
        }
        cmpTarget = p.compare ? 1.0f : 0.0f;
        bypassTarget = p.bypass ? 1.0f : 0.0f;
        want.osLog2 = std::clamp (p.osLog2, 0, Oversampler::kMaxFactorLog2);
        want.look = std::clamp ((int) std::lround (std::clamp ((double) p.lookMs, kMinLookMs, kMaxLookMs) * 0.001 * fs),
                                MasterLimiter::kSmooth, std::max (MasterLimiter::kSmooth, maxLook));
    }

    int latencyOf (const Config& c) const noexcept { return os.latencyFor (c.osLog2) + MasterLimiter::latencyFor (c.look); }
    int getLatencySamples() const noexcept { return latencyPub.load (std::memory_order_acquire); }
    LatencyHandshake& handshake() noexcept { return hs; }

    // meters (any thread)
    const LoudnessMeter& inputLoudness() const noexcept { return inMeter; }
    const LoudnessMeter& outputLoudness() const noexcept { return outMeter; }
    float outputTruePeakDb() const noexcept { const float v = outTp.maxTruePeak(); return v > 0.0f ? 20.0f * std::log10 (v) : -120.0f; }
    bool popDisplay (DisplayFrame& f) noexcept { return display.pop (f); }
    // GUI: restart the loudness / true-peak measurement (applied on the audio thread at the next block)
    void requestMeterReset() noexcept { meterResetReq.store (true, std::memory_order_release); }
    // audio measured by the output meters since their last reset (LEARN waits for enough of it)
    double measuredSeconds() const noexcept { return (double) measured.load (std::memory_order_relaxed) / fs; }

    void process (float* const* io, int numChannels, int numSamples) noexcept
    {
        ScopedFtz ftz;
        const int nCh = std::clamp (numChannels, 0, numCh);
        if (nCh == 0)
            return;
        if (meterResetReq.exchange (false, std::memory_order_acq_rel))
        {
            inMeter.reset();
            outMeter.reset();
            outTp.reset();
            measured.store (0, std::memory_order_relaxed);
        }
        for (int off = 0; off < numSamples; off += kChunk)
        {
            const int n = std::min (kChunk, numSamples - off);
            float* p[kMaxCh] {};
            for (int c = 0; c < nCh; ++c)
                p[c] = io[c] + off;
            chunk (p, nCh, n);
        }
    }

private:
    void applyConfig() noexcept
    {
        os.reset();
        lim.setLookahead (active.look);
        lim.reset();
        latency = latencyOf (active);
        latencyPub.store (latency, std::memory_order_release);
    }

    void control (int n) noexcept
    {
        const bool ready = hs.ready (latencyOf (hs.isHolding() ? active : want), fadeGain <= 0.0f, n);
        const Config tgt = hs.isHolding() ? active : want;
        if (tgt != active)
        {
            fadeTgt = 0.0f;
            if (fadeGain <= 0.0f && ready)
            {
                active = tgt;
                applyConfig();
                warm = latency + (hs.tracking() ? maxBlock : 0);
            }
        }
        else
            fadeTgt = (! ready || warm > 0) ? 0.0f : 1.0f;
        if (warm > 0)
            warm = std::max (0, warm - n);
    }

    float clip (float x) const noexcept { return clipCurve (x, clipThr, clipKnee); }

    void chunk (float* const* io, int nCh, int n) noexcept
    {
        // 0. sanitize, input meters, dry ring
        for (int c = 0; c < nCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                float v = io[c][i];
                v = std::isfinite (v) ? std::clamp (v, -1000.0f, 1000.0f) : 0.0f;
                io[c][i] = v;
                dryRing[(size_t) c][(size_t) ((writePos + i) & ringMask)] = v;
            }
        inMeter.process (io, nCh, n);
        control (n);

        // 0b. 3.0 VOICE HOLD: the shared voice decision on the input (causal, ~10 ms); a pause only within 10 s of a voice
        if (params.voiceHold)
            for (int i = 0; i < n; ++i)
            {
                float m = 0.0f;
                for (int c = 0; c < nCh; ++c)
                    m += io[c][i];
                voice.push (m / (float) nCh);
                if (voice.voice())
                    sinceVoice = 0;
                else if (sinceVoice < pauseSpan)
                    ++sinceVoice;
                pauseFlags[(size_t) i] = (! voice.voice() && sinceVoice < pauseSpan) ? 1 : 0;
            }

        // 1. input gain, clipper (oversampled)
        const int log2 = active.osLog2, factor = 1 << log2;
        float preMax = 0.0f, postMax = 0.0f;
        for (int c = 0; c < nCh; ++c)
        {
            float g = inGain;
            for (int i = 0; i < n; ++i)
            {
                g += (inTarget - g) * smoothK;
                io[c][i] *= g;
            }
            if (c == nCh - 1)
                inGain = g;
            float* hi = os.up (io[c], n, c, log2);
            if (clipOn)
                for (int s = 0; s < n * factor; ++s)
                {
                    preMax = std::max (preMax, std::abs (hi[s]));
                    hi[s] = clip (hi[s]);
                    postMax = std::max (postMax, std::abs (hi[s]));
                }
            os.down (io[c], n, c, log2);
        }
        if (clipOn && postMax > 0.0f)
            hopClip = std::max (hopClip, 20.0f * std::log10 (std::max (preMax, postMax) / postMax));

        // 2. limiter, dither
        hopLim = std::max (hopLim, lim.process (io, nCh, n, params.voiceHold ? pauseFlags.data() : nullptr));
        dither.process (io, nCh, n);

        // 3. compare / bypass / fade (aligned to the full latency); meters on the final output
        const int L = latency;
        for (int i = 0; i < n; ++i)
        {
            const int at = (writePos + i - L) & ringMask;
            double pd = 0.0, pw = 0.0;
            for (int c = 0; c < nCh; ++c)
            {
                const float dry = dryRing[(size_t) c][(size_t) at];
                const double d = kwDry.process (dry, c), w = kwWet.process (io[c][i], c);
                pd += d * d;
                pw += w * w;
                hopIn = std::max (hopIn, std::abs (dry) * inGain);
            }
            msDry += (pd - msDry) * loudK;
            msWet += (pw - msWet) * loudK;
            if (msDry > 1.0e-7)
                cmpGain += ((float) std::clamp (std::sqrt (msWet / msDry), 0.0631, 15.85) - cmpGain) * cmpK;
            cmpAmt += (cmpTarget - cmpAmt) * switchK;
            bypassAmt += (bypassTarget - bypassAmt) * switchK;
            if (std::abs (cmpAmt - cmpTarget) < 1.0e-6f) cmpAmt = cmpTarget;
            if (std::abs (bypassAmt - bypassTarget) < 1.0e-6f) bypassAmt = bypassTarget;
            fadeGain = fadeTgt > fadeGain ? std::min (fadeTgt, fadeGain + fadeStep) : std::max (fadeTgt, fadeGain - fadeStep);
            for (int c = 0; c < nCh; ++c)
            {
                const float dry = dryRing[(size_t) c][(size_t) at];
                float y = io[c][i];
                if (cmpAmt > 0.0f)
                    y += (dry * cmpGain - y) * cmpAmt;
                if (bypassAmt > 0.0f)
                    y += (dry - y) * bypassAmt;
                y *= fadeGain;
                io[c][i] = y;
                hopOutCh[(size_t) c] = std::max (hopOutCh[(size_t) c], std::abs (y));
            }
            if (++hopCount >= hopLen)
            {
                auto db = [] (float a) { return a > 1.0e-6f ? 20.0f * std::log10 (a) : -120.0f; };
                DisplayFrame f;
                f.inPeakDb = db (hopIn);
                f.outPeakDb = db (std::max (hopOutCh[0], hopOutCh[1]));
                f.limGrDb = hopLim;
                f.clipGrDb = hopClip;
                f.outShortLufs = outMeter.shortTermLufs();
                f.outPeakChDb[0] = db (hopOutCh[0]);
                f.outPeakChDb[1] = db (nCh > 1 ? hopOutCh[1] : hopOutCh[0]);
                display.push (f);
                hopCount = 0;
                hopIn = hopLim = hopClip = 0.0f;
                hopOutCh = { 0.0f, 0.0f };
            }
        }
        outMeter.process (io, nCh, n);
        outTp.process (io, nCh, n);
        measured.store (measured.load (std::memory_order_relaxed) + n, std::memory_order_relaxed);
        writePos = (writePos + n) & ringMask;
    }

    double fs = 48000.0;
    int numCh = 2, maxBlock = 0, maxLook = 240, latency = 0, warm = 0, ringMask = 0, writePos = 0, hopLen = 480, hopCount = 0;
    Params params;
    Config active, want;
    Oversampler os;
    MasterLimiter lim;
    Dither dither;
    LoudnessMeter inMeter, outMeter;
    TruePeakMeter outTp;
    KWeighting kwDry, kwWet;
    LatencyHandshake hs;
    HistoryFifo<DisplayFrame> display;
    std::atomic<int> latencyPub { 0 };
    std::atomic<bool> meterResetReq { false };
    std::atomic<int64_t> measured { 0 };
    std::array<std::vector<float>, kMaxCh> dryRing;
    bool clipOn = false;
    float clipThr = 1.0f, clipKnee = 0.0f, inGain = 1.0f, inTarget = 1.0f, smoothK = 0.001f, switchK = 0.002f;
    axell::voice::VoicePresence voice;
    std::array<uint8_t, kChunk> pauseFlags {};
    int pauseSpan = 480000, sinceVoice = 480000;
    float cmpAmt = 0.0f, cmpTarget = 0.0f, cmpGain = 1.0f, cmpK = 0.001f, loudK = 0.001f, bypassAmt = 0.0f, bypassTarget = 0.0f;
    float fadeGain = 1.0f, fadeTgt = 1.0f, fadeStep = 0.01f;
    double msDry = 0.0, msWet = 0.0;
    float hopIn = 0.0f, hopLim = 0.0f, hopClip = 0.0f;
    std::array<float, 2> hopOutCh { 0.0f, 0.0f };
};
} // namespace axm
