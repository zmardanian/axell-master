#pragma once
// Axell Master - factory presets (framework independent: the test suite checks them). Input gain stays 0 dB in every
// preset (it depends on the mix): set it by ear or with LEARN. Names describe the use; no third-party branding.
#include "Engine.h"

#include <array>

namespace axm
{
struct FactoryPreset
{
    const char* name;
    const char* description;
    Params params;
    int target; // index into kTargets
};

// loudness targets (display + LEARN): integrated LUFS
struct Target
{
    const char* name;
    float lufs;
};
constexpr Target kTargets[] = { { "Off", 0.0f },
                                { "-14 LUFS  Spotify / YouTube / Tidal", -14.0f },
                                { "-16 LUFS  Apple Music", -16.0f },
                                { "-9 LUFS  Club / CD loud", -9.0f },
                                { "-23 LUFS  EBU R128 broadcast", -23.0f },
                                { "-24 LKFS  ATSC A/85", -24.0f } };
constexpr int kNumTargets = (int) (sizeof (kTargets) / sizeof (kTargets[0]));

inline std::array<FactoryPreset, 8> factoryPresets()
{
    auto P = [] (float clip, float shape, float ceil, float rel, int style, float look, float link)
    {
        Params p;
        p.clipPct = clip; p.clipShapePct = shape; p.ceilingDb = ceil; p.releaseMs = rel;
        p.style = style; p.lookMs = look; p.linkPct = link; p.osLog2 = 4; p.dither = 0;
        return p;
    };
    return { {
        { "Init", "Transparent limiting to -1 dBTP, no clipper.", P (0, 30, -1.0f, 120, 0, 2.0f, 100), 1 },
        { "Streaming Master (-14 LUFS)", "Clean and open: light clipping catches the fastest peaks, the transparent limiter does the rest. Target -14 LUFS.",
          P (20, 30, -1.0f, 150, 0, 2.0f, 100), 1 },
        { "Apple Music (-16 LUFS)", "Most dynamic of the streaming targets: transparent limiting, no clipper.", P (0, 30, -1.0f, 180, 0, 2.5f, 100), 2 },
        { "Boom Bap Punch", "Drums keep their snap: clipper on the transients, punchy release.", P (35, 40, -1.0f, 90, 1, 1.5f, 100), 1 },
        { "Loud Rap / Trap", "Maximum density for loud releases: more clipping, loud style, fast release. Watch the 808s with the Safe style if they distort.",
          P (55, 60, -1.0f, 60, 2, 1.5f, 90), 3 },
        { "Club / CD (-9 LUFS)", "Loud and solid with a -0.5 dBTP ceiling.", P (45, 50, -0.5f, 80, 2, 2.0f, 100), 3 },
        { "Safe / Acoustic", "Lowest distortion: long program-dependent release, no clipper. For acoustic, jazz, vocals.", P (0, 30, -1.0f, 300, 3, 3.0f, 100), 2 },
        { "Broadcast (EBU R128)", "Peaks only: transparent limiting to -1 dBTP for -23 LUFS delivery.", P (0, 30, -1.0f, 200, 0, 3.0f, 100), 4 },
    } };
}
} // namespace axm
