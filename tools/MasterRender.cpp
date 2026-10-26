// Axell Master - offline renderer: the engine and the codec check exactly as they ship (no JUCE, no host).
// usage: MasterRender in.f32 out.f32 rate preset gainDb [--safe] [--ceiling dBTP]
//   in / out: stereo interleaved float32. preset: index into factoryPresets(). The output is aligned (latency removed).
//   --safe renders, measures the decoded codec peaks, lowers the ceiling by safeCeilingChangeDb() and renders again
//   (what pressing SAFE after playing the song does). Prints one JSON line with the measurements.
#include "CodecCheck.h"
#include "Engine.h"
#include "Presets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static std::vector<float> render (const std::vector<float>& x, int fs, const axm::Params& p, axm::CodecCheck& cc)
{
    const size_t n = x.size() / 2;
    axm::Engine e;
    e.prepare (fs, 2, 512);
    e.setParams (p);
    e.reset();
    const int L = e.getLatencySamples();
    std::vector<float> l (n + (size_t) L, 0.0f), r (n + (size_t) L, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        l[i] = x[2 * i];
        r[i] = x[2 * i + 1];
    }
    cc.requestReset();
    cc.drain();
    for (size_t pos = 0; pos < l.size(); pos += 512)
    {
        const int b = (int) std::min<size_t> (512, l.size() - pos);
        float* io[2] { l.data() + pos, r.data() + pos };
        e.process (io, 2, b);
        // only the aligned master is checked, from its first sample (the latency's leading zeros are not)
        const size_t from = std::max (pos, (size_t) L);
        if (from < pos + (size_t) b)
        {
            const float* a[2] { l.data() + from, r.data() + from };
            cc.push (a, 2, (int) (pos + (size_t) b - from));
        }
        cc.drain();
    }
    std::vector<float> y (2 * n);
    for (size_t i = 0; i < n; ++i)
    {
        y[2 * i] = l[i + (size_t) L];
        y[2 * i + 1] = r[i + (size_t) L];
    }
    return y;
}

int main (int argc, char** argv)
{
    if (argc < 6)
    {
        std::fprintf (stderr, "usage: MasterRender in.f32 out.f32 rate preset gainDb [--safe] [--ceiling dBTP] [--target dBTP] [--no-codecs] [--clip %] [--voice-hold 0|1] [--ess-hold 0|1]\n");
        return 2;
    }
    std::ifstream f (argv[1], std::ios::binary | std::ios::ate);
    const size_t bytes = (size_t) f.tellg();
    f.seekg (0);
    std::vector<float> x (bytes / 4);
    f.read ((char*) x.data(), (std::streamsize) bytes);
    const int fs = std::atoi (argv[3]);
    const auto presets = axm::factoryPresets();
    axm::Params p = presets[(size_t) std::atoi (argv[4])].params;
    p.inputDb = (float) std::atof (argv[5]);
    bool safe = false, codecs = true;
    float target = axm::CodecCheck::kTargetDb;
    for (int a = 6; a < argc; ++a)
    {
        if (std::strcmp (argv[a], "--safe") == 0)
            safe = true;
        else if (std::strcmp (argv[a], "--no-codecs") == 0)
            codecs = false;
        else if (std::strcmp (argv[a], "--clip") == 0 && a + 1 < argc)
            p.clipPct = (float) std::atof (argv[++a]);
        else if (std::strcmp (argv[a], "--voice-hold") == 0 && a + 1 < argc)
            p.voiceHold = std::atoi (argv[++a]) != 0;
        else if (std::strcmp (argv[a], "--ess-hold") == 0 && a + 1 < argc)
            p.essHold = std::atoi (argv[++a]) != 0;
        else if (std::strcmp (argv[a], "--target") == 0 && a + 1 < argc)
            target = (float) std::atof (argv[++a]);
        else if (std::strcmp (argv[a], "--ceiling") == 0 && a + 1 < argc)
            p.ceilingDb = (float) std::atof (argv[++a]);
    }
    axm::CodecCheck cc;
    cc.prepare (fs, false);
    cc.setEnabled (codecs);
    auto y = render (x, fs, p, cc);
    const float w0[2] { cc.worstDb (0), cc.worstDb (1) };
    const int c0[2] { cc.clippedSamples (0), cc.clippedSamples (1) };
    const float change = cc.safeCeilingChangeDb (target);
    // SAFE as the plugin does it: the ceiling down in its 0.1 dB steps, then the song again, until the check is on
    // target (a lower ceiling means denser limiting, so one step rarely lands exactly)
    int passes = 0;
    for (float d = change; safe && d < 0.0f && passes < 6; d = cc.safeCeilingChangeDb (target))
    {
        p.ceilingDb = std::max (-24.0f, std::floor ((p.ceilingDb + d) * 10.0f - 1.0e-3f) / 10.0f);
        y = render (x, fs, p, cc);
        ++passes;
    }
    std::ofstream o (argv[2], std::ios::binary);
    o.write ((const char*) y.data(), (std::streamsize) (y.size() * 4));
    std::printf ("{\"opus_db\": %.3f, \"vorbis_db\": %.3f, \"opus_clips\": %d, \"vorbis_clips\": %d, \"safe_change_db\": %.3f, "
                 "\"ceiling_db\": %.3f, \"after_opus_db\": %.3f, \"after_vorbis_db\": %.3f, \"after_opus_clips\": %d, "
                 "\"after_vorbis_clips\": %d, \"seconds\": %.2f, \"incomplete\": %s, \"passes\": %d}\n",
                 w0[0], w0[1], c0[0], c0[1], change, p.ceilingDb, cc.worstDb (0), cc.worstDb (1), cc.clippedSamples (0),
                 cc.clippedSamples (1), cc.measuredSeconds(), cc.incomplete() ? "true" : "false", passes);
    return 0;
}
