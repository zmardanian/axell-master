#pragma once
// Axell Master 2.0 - streaming codec check (framework independent, no JUCE).
//
// A master at -1 dBTP can still clip once a streaming service has encoded it: a lossy codec does not keep the waveform,
// only its spectrum, and the decoded peaks of a loud master land higher. Measured on real rap masters at -9 LUFS
// (research/real_masters.py): decoded true peaks up to +1.1 dBTP with Vorbis 160 kbps and +0.3 with Opus 160 kbps.
// So the master is encoded and decoded here with the reference libraries the services use:
//   Opus 160 kbps (YouTube), Vorbis 160 kbps (Spotify's "high" quality)
// at 44.1 / 48 kHz as the host runs (Opus: 48 kHz only, other rates resampled); hosts at 88.2 / 96 kHz are resampled to
// 48 kHz for both (no service streams at 96 kHz, and libvorbis has no 160 kbps mode there).
// and the decoded signal's true peak (the core's 8x meter) and clipped samples (|x| > 1) are kept since the last reset.
// Where a codec's frames fall decides its peaks: shifting the same master by a few samples moved the decoded true peak
// by up to 0.9 dB (Vorbis) and 0.7 dB (Opus). The service's framing is unknown, so each codec runs kOffsets times at
// different frame offsets and the worst is kept.
// safeCeilingChangeDb() is how much to lower the limiter's ceiling so the worse codec decodes at kTargetDb.
//
// Threads: push() is called by the audio thread and only copies into a lock-free single-producer / single-consumer
// ring (no allocation, no lock). The codecs run on their own worker thread (they allocate). If the worker cannot keep
// up (an offline render far faster than real time), push() drops audio and the result is flagged incomplete.
// Tests and tools can run it without the thread (prepare (..., false), then drain()).
#include <axell/dsp/TruePeak.h>
#include <opus.h>
#include <vorbis/vorbisenc.h>

#include "Resampler.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

namespace axm
{
class CodecCheck
{
public:
    enum { kOpus = 0, kVorbis = 1, kCodecs = 2 };
    static constexpr int kBitrate = 160000;
    static constexpr float kTargetDb = -0.3f;    // the worse codec's decoded true peak after SAFE (dBTP)
    static constexpr double kRingSeconds = 30.0;

    static const char* name (int c) { return c == kOpus ? "OPUS 160" : "VORBIS 160"; }

    CodecCheck() = default;
    ~CodecCheck() { stop(); }

    // message thread: (re)starts everything for a sample rate (allocates)
    void prepare (double sampleRate, bool withThread = true)
    {
        stop();
        fs = (int) std::lround (sampleRate);
        cap = (size_t) (kRingSeconds * fs);
        ring.assign (2 * cap, 0.0f);
        wpos.store (0);
        rpos.store (0);
        resetReq.store (true);
        threaded = withThread;
        if (withThread)
        {
            running.store (true);
            worker = std::thread ([this] { loop(); });
        }
    }

    void stop()
    {
        running.store (false);
        if (worker.joinable())
            worker.join();
        closeCodecs();
    }

    void setEnabled (bool on) noexcept { enabled.store (on, std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return enabled.load (std::memory_order_relaxed); }

    // audio thread: stereo (a mono master is checked as dual mono). Never allocates, locks or waits.
    void push (const float* const* x, int numChannels, int n) noexcept
    {
        if (! enabled.load (std::memory_order_relaxed) || cap == 0 || n <= 0)
            return;
        const size_t w = wpos.load (std::memory_order_relaxed), r = rpos.load (std::memory_order_acquire);
        if (cap - (w - r) < (size_t) n)
        {
            dropped.store (true, std::memory_order_relaxed);
            return;
        }
        const float* l = x[0];
        const float* rr = x[numChannels > 1 ? 1 : 0];
        for (int i = 0; i < n; ++i)
        {
            const size_t k = ((w + (size_t) i) % cap) * 2;
            ring[k] = l[i];
            ring[k + 1] = rr[i];
        }
        wpos.store (w + (size_t) n, std::memory_order_release);
    }

    // any thread: start a new measurement (everything pushed so far is discarded)
    void requestReset() noexcept { resetReq.store (true, std::memory_order_release); }

    // results since the last reset (any thread)
    float worstDb (int c) const noexcept { return worst[c].load (std::memory_order_relaxed); }   // decoded dBTP
    int clippedSamples (int c) const noexcept { return clips[c].load (std::memory_order_relaxed); }
    double measuredSeconds() const noexcept { return seconds.load (std::memory_order_relaxed); }
    bool incomplete() const noexcept { return dropped.load (std::memory_order_relaxed); }
    bool available (int c) const noexcept { return ok[c].load (std::memory_order_relaxed); }

    // the ceiling change (dB, <= 0) that brings the worse codec's decoded true peak down to kTargetDb
    float safeCeilingChangeDb() const noexcept { return safeCeilingChangeDb (kTargetDb); }
    float safeCeilingChangeDb (float target) const noexcept
    {
        float w = -200.0f;
        for (int c = 0; c < kCodecs; ++c)
            if (available (c))
                w = std::max (w, worstDb (c));
        return w > target ? target - w : 0.0f;
    }

    // without the thread: process everything pushed so far (tools, tests)
    void drain()
    {
        if (resetReq.exchange (false))
            restart();
        while (step (true)) {}
    }

private:
    void loop()
    {
        while (running.load (std::memory_order_relaxed))
        {
            if (resetReq.exchange (false, std::memory_order_acq_rel))
                restart();
            if (! step (false))
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
    }

    // processes one chunk from the ring; false when there was not enough to do
    bool step (bool flush)
    {
        const size_t r = rpos.load (std::memory_order_relaxed), w = wpos.load (std::memory_order_acquire);
        const size_t avail = w - r;
        const size_t chunk = (size_t) kChunk;
        if (avail == 0 || (! flush && avail < chunk))
            return false;
        const int n = (int) std::min (avail, chunk);
        for (int i = 0; i < n; ++i)
        {
            const size_t k = ((r + (size_t) i) % cap) * 2;
            in[0][(size_t) i] = ring[k];
            in[1][(size_t) i] = ring[k + 1];
        }
        rpos.store (r + (size_t) n, std::memory_order_release);
        // one resampled copy at 48 kHz when the host is not there (Opus always, Vorbis above 48 kHz)
        int n48 = n;
        const float* s48[2] { in[0].data(), in[1].data() };
        if (fs != 48000)
        {
            for (int c = 0; c < 2; ++c)
                n48 = toOpus[(size_t) c].process (in[(size_t) c].data(), n, in48[(size_t) c].data(), (int) in48[(size_t) c].size());
            s48[0] = in48[0].data();
            s48[1] = in48[1].data();
        }
        if (ok[kOpus].load())
        {
            for (auto& o : opus)
                o.feed (s48[0], s48[1], n48);
            publish (kOpus);
        }
        if (ok[kVorbis].load())
        {
            const bool at48 = vorbisFs() != fs;
            for (auto& v : vorbis)
                v.feed (at48 ? s48[0] : in[0].data(), at48 ? s48[1] : in[1].data(), at48 ? n48 : n);
            publish (kVorbis);
        }
        frames += n;
        seconds.store ((double) frames / fs, std::memory_order_relaxed);
        return true;
    }

    // ---- setup / teardown (worker thread, or the caller when unthreaded) --------------------------------------------
    void restart()
    {
        closeCodecs();
        rpos.store (wpos.load (std::memory_order_acquire), std::memory_order_release);   // discard what came before
        frames = 0;
        seconds.store (0.0);
        dropped.store (false);
        for (int c = 0; c < kCodecs; ++c)
        {
            worst[c].store (-200.0f);
            clips[c].store (0);
        }
        for (auto& v : in)
            v.assign ((size_t) kChunk, 0.0f);
        const int maxIn48 = (int) std::ceil (kChunk * 48000.0 / fs) + 64;
        for (auto& rs : toOpus)
            rs.prepare (fs, 48000, kChunk);
        for (auto& v : in48)
            v.assign ((size_t) maxIn48, 0.0f);
        for (auto& v : deint)
            v.assign ((size_t) std::max (kOpusFrame, kChunk), 0.0f);
        bool allOpus = true, allVorbis = true;
        for (int k = 0; k < kOffsets; ++k)
        {
            allOpus = opus[k].open (kOffset[k]) && allOpus;
            allVorbis = vorbis[k].open (vorbisFs(), kOffset[k]) && allVorbis;
        }
        ok[kOpus].store (allOpus);
        ok[kVorbis].store (allVorbis);
    }

    void closeCodecs()
    {
        ok[kOpus].store (false);
        ok[kVorbis].store (false);
        for (auto& o : opus)
            o.close();
        for (auto& v : vorbis)
            v.close();
    }

    // ---- the codecs ---------------------------------------------------------------------------------------------------
    // one stream: clipped samples and true peak; a codec's result is the worst of its streams
    struct Meter
    {
        axell::TruePeakMeter tp;
        int clipped = 0;
        void reset() { tp.prepare(); clipped = 0; }
        void add (float* const* x, int n)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    clipped += std::abs (x[ch][i]) > 1.0f ? 1 : 0;
            tp.process (x, 2, n);
        }
    };

    struct OpusStream
    {
        OpusEncoder* enc = nullptr;
        OpusDecoder* dec = nullptr;
        std::vector<float> buf, out, d0, d1;
        int fill = 0;
        unsigned char packet[4000] {};
        Meter m;
        bool open (int offset)
        {
            close();
            int err = 0;
            enc = opus_encoder_create (48000, 2, OPUS_APPLICATION_AUDIO, &err);
            if (err != OPUS_OK)
                return false;
            dec = opus_decoder_create (48000, 2, &err);
            if (err != OPUS_OK)
                return false;
            opus_encoder_ctl (enc, OPUS_SET_BITRATE (kBitrate));
            opus_encoder_ctl (enc, OPUS_SET_COMPLEXITY (10));
            opus_encoder_ctl (enc, OPUS_SET_SIGNAL (OPUS_SIGNAL_MUSIC));
            buf.assign (2 * kOpusFrame, 0.0f);
            out.assign (2 * kOpusFrame, 0.0f);
            d0.assign (kOpusFrame, 0.0f);
            d1.assign (kOpusFrame, 0.0f);
            fill = offset % kOpusFrame;   // the stream starts with this many zeros: its frames sit elsewhere
            m.reset();
            return true;
        }
        void close()
        {
            if (enc != nullptr)
                opus_encoder_destroy (enc);
            if (dec != nullptr)
                opus_decoder_destroy (dec);
            enc = nullptr;
            dec = nullptr;
        }
        void feed (const float* l, const float* r, int n)
        {
            for (int i = 0; i < n; ++i)
            {
                buf[(size_t) (2 * fill)] = l[i];
                buf[(size_t) (2 * fill + 1)] = r[i];
                if (++fill < kOpusFrame)
                    continue;
                fill = 0;
                const int bytes = opus_encode_float (enc, buf.data(), kOpusFrame, packet, (int) sizeof (packet));
                const int got = bytes > 0 ? opus_decode_float (dec, packet, bytes, out.data(), kOpusFrame, 0) : 0;
                for (int k = 0; k < got; ++k)
                {
                    d0[(size_t) k] = out[(size_t) (2 * k)];
                    d1[(size_t) k] = out[(size_t) (2 * k + 1)];
                }
                float* d[2] { d0.data(), d1.data() };
                if (got > 0)
                    m.add (d, got);
            }
        }
    };

    struct VorbisStream
    {
        vorbis_info vi {}, dvi {};
        vorbis_comment vc {}, dvc {};
        vorbis_dsp_state vd {}, dvd {};
        vorbis_block vb {}, dvb {};
        bool viInit = false, encOpen = false, dviInit = false, decOpen = false;
        Meter m;
        // average bitrate 160 kbps without slow rate management (what ffmpeg -b:a does), at the host rate
        bool open (int rate, int offset)
        {
            close();
            vorbis_info_init (&vi);
            viInit = true;
            bool v = vorbis_encode_setup_managed (&vi, 2, rate, -1, kBitrate, -1) == 0
                     && vorbis_encode_ctl (&vi, OV_ECTL_RATEMANAGE2_SET, nullptr) == 0 && vorbis_encode_setup_init (&vi) == 0;
            if (! v)
                return false;
            vorbis_comment_init (&vc);
            vorbis_analysis_init (&vd, &vi);
            vorbis_block_init (&vd, &vb);
            encOpen = true;
            ogg_packet h[3];
            vorbis_analysis_headerout (&vd, &vc, &h[0], &h[1], &h[2]);
            vorbis_info_init (&dvi);
            vorbis_comment_init (&dvc);
            dviInit = true;
            for (auto& p : h)
                v = v && vorbis_synthesis_headerin (&dvi, &dvc, &p) == 0;
            if (! v)
                return false;
            vorbis_synthesis_init (&dvd, &dvi);
            vorbis_block_init (&dvd, &dvb);
            decOpen = true;
            m.reset();
            if (offset > 0)   // the stream starts with this many zeros: its blocks sit elsewhere
            {
                float** b = vorbis_analysis_buffer (&vd, offset);
                std::memset (b[0], 0, sizeof (float) * (size_t) offset);
                std::memset (b[1], 0, sizeof (float) * (size_t) offset);
                vorbis_analysis_wrote (&vd, offset);
            }
            return true;
        }
        void close()
        {
            if (decOpen)
            {
                vorbis_block_clear (&dvb);
                vorbis_dsp_clear (&dvd);
            }
            if (dviInit)
            {
                vorbis_comment_clear (&dvc);
                vorbis_info_clear (&dvi);
            }
            if (encOpen)
            {
                vorbis_block_clear (&vb);
                vorbis_dsp_clear (&vd);
                vorbis_comment_clear (&vc);
            }
            if (viInit)
                vorbis_info_clear (&vi);
            viInit = encOpen = dviInit = decOpen = false;
        }
        void feed (const float* l, const float* r, int n)
        {
            float** b = vorbis_analysis_buffer (&vd, n);
            std::memcpy (b[0], l, sizeof (float) * (size_t) n);
            std::memcpy (b[1], r, sizeof (float) * (size_t) n);
            vorbis_analysis_wrote (&vd, n);
            while (vorbis_analysis_blockout (&vd, &vb) == 1)
            {
                vorbis_analysis (&vb, nullptr);
                vorbis_bitrate_addblock (&vb);
                ogg_packet op;
                while (vorbis_bitrate_flushpacket (&vd, &op) == 1)
                {
                    if (vorbis_synthesis (&dvb, &op) == 0)
                        vorbis_synthesis_blockin (&dvd, &dvb);
                    float** pcm = nullptr;
                    int k = 0;
                    while ((k = vorbis_synthesis_pcmout (&dvd, &pcm)) > 0)
                    {
                        m.add (pcm, k);
                        vorbis_synthesis_read (&dvd, k);
                    }
                }
            }
        }
    };

    void publish (int c)
    {
        float tp = 0.0f;
        int cl = 0;
        for (int k = 0; k < kOffsets; ++k)
        {
            const Meter& m = c == kOpus ? opus[k].m : vorbis[k].m;
            tp = std::max (tp, m.tp.maxTruePeak());
            cl = std::max (cl, m.clipped);
        }
        worst[c].store (tp > 0.0f ? 20.0f * std::log10 (tp) : -200.0f, std::memory_order_relaxed);
        clips[c].store (cl, std::memory_order_relaxed);
    }

    int vorbisFs() const noexcept { return fs == 44100 || fs == 48000 ? fs : 48000; }

    static constexpr int kChunk = 4096, kOpusFrame = 960;   // 20 ms Opus frames

    int fs = 48000;
    size_t cap = 0;
    std::vector<float> ring;
    std::atomic<size_t> wpos { 0 }, rpos { 0 };
    std::atomic<bool> enabled { true }, resetReq { true }, dropped { false }, running { false };
    std::atomic<bool> ok[kCodecs] { { false }, { false } };
    std::atomic<float> worst[kCodecs] { { -200.0f }, { -200.0f } };
    std::atomic<int> clips[kCodecs] { { 0 }, { 0 } };
    std::atomic<double> seconds { 0.0 };
    bool threaded = false;
    std::thread worker;
    long long frames = 0;

    static constexpr int kOffsets = 4;
    static constexpr int kOffset[kOffsets] { 0, 211, 523, 857 };
    std::vector<float> in[2], in48[2], deint[2];
    Resampler toOpus[2];
    OpusStream opus[kOffsets];
    VorbisStream vorbis[kOffsets];
};
} // namespace axm
