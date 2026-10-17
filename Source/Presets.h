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
    int target;            // index into kTargets
    const char* category;  // the preset menu's group
};

// preset menu groups, in menu order
constexpr const char* kCategories[] = { "MASTER", "YOUR STUDIO (BOOM BAP)", "VOCAL BUS", "BEAT / DRUM BUS" };

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

// 1.0's eight keep their program numbers 0-7 (projects and host automation refer to them); 2.0 adds 20 after them.
// Every value of the 2.0 presets was checked on real material (research/presets_study.py, DECISIONS M17).
// Targets: 0 off, 1 -14, 2 -16, 3 -9, 4 -23, 5 -24. Styles: 0 transparent, 1 punchy, 2 loud, 3 safe.
inline std::array<FactoryPreset, 28> factoryPresets()
{
    auto P = [] (float clip, float shape, float ceil, float rel, int style, float look, float link)
    {
        Params p;
        p.clipPct = clip; p.clipShapePct = shape; p.ceilingDb = ceil; p.releaseMs = rel;
        p.style = style; p.lookMs = look; p.linkPct = link; p.osLog2 = 4; p.dither = 0;
        return p;
    };
    constexpr const char* M = "MASTER";
    constexpr const char* Y = "YOUR STUDIO (BOOM BAP)";
    constexpr const char* V = "VOCAL BUS";
    constexpr const char* B = "BEAT / DRUM BUS";
    return { {
        // ---- 1.0 ------------------------------------------------------------------------------------------------------
        { "Init", "Transparent limiting to -1 dBTP, no clipper.", P (0, 30, -1.0f, 120, 0, 2.0f, 100), 1, M },
        { "Streaming Master (-14 LUFS)", "Clean and open: light clipping catches the fastest peaks, the transparent limiter does the rest. Target -14 LUFS.",
          P (20, 30, -1.0f, 150, 0, 2.0f, 100), 1, M },
        { "Apple Music (-16 LUFS)", "Most dynamic of the streaming targets: transparent limiting, no clipper.", P (0, 30, -1.0f, 180, 0, 2.5f, 100), 2, M },
        { "Boom Bap Punch", "Drums keep their snap: clipper on the transients, punchy release.", P (35, 40, -1.0f, 90, 1, 1.5f, 100), 1, M },
        { "Loud Rap / Trap", "Maximum density for loud releases: more clipping, loud style, fast release. Watch the 808s with the Safe style if they distort.",
          P (55, 60, -1.0f, 60, 2, 1.5f, 90), 3, M },
        { "Club / CD (-9 LUFS)", "Loud and solid. 2.0: ceiling -1 dBTP (was -0.5: at -9 LUFS it clipped after Opus and Vorbis on every "
          "test master); check the streaming codecs and press SAFE if needed.", P (45, 50, -1.0f, 80, 2, 2.0f, 100), 3, M },
        { "Safe / Acoustic", "Lowest distortion: long program-dependent release, no clipper. For acoustic, jazz, vocals.", P (0, 30, -1.0f, 300, 3, 3.0f, 100), 2, M },
        { "Broadcast (EBU R128)", "Peaks only: transparent limiting to -1 dBTP for -23 LUFS delivery.", P (0, 30, -1.0f, 200, 0, 3.0f, 100), 4, M },

        // ---- 2.0: your studio - underground boom bap, rapped vocals from a booth, streaming releases, beats -------------
        { "Boom Bap Streaming Master (-14)", "Your mixes already sit near -14 LUFS: the clipper takes only the snare and kick tips, the punchy "
          "release keeps the knock, and the limiter barely works. For the streaming release.", P (30, 45, -1.0f, 90, 1, 1.5f, 100), 1, Y },
        { "Boom Bap Loud, Codec-Ready (-9)", "A loud boom bap master that survives streaming without SAFE: on your own mixes at "
          "-9 LUFS this ceiling (-3 dBTP) decoded at -0.26 dBTP or lower with Opus and Vorbis, 0 clipped samples (at -2 dBTP "
          "they still clipped). LEARN lands near -9.3 LUFS: the limiter is at its density limit there.", P (45, 50, -3.0f, 80, 1, 1.5f, 100), 3, Y },
        { "Booth Rap Vocal Bus", "On the vocal bus of booth takes: catches plosives and shouted syllables (a few dB) with a soft "
          "clip, so the vocal compressor after it works on the performance, not on spikes. GAIN until the loudest words just "
          "touch the ceiling.", P (15, 25, -3.0f, 45, 1, 1.0f, 100), 0, Y },
        { "Boom Bap Drum Bus Knock", "On the drum bus: the clipper shaves the tops of kicks and snares, so the drums get denser and "
          "louder in the mix without losing their knock. 2 dB of headroom stays for the mix.", P (60, 35, -2.0f, 70, 1, 1.0f, 100), 0, Y },
        { "Beat Tape / Instrumental Master (-14)", "For instrumentals and beat tapes: a little wider (link 90 %) and more open than a "
          "vocal master, punchy release for the drums.", P (35, 40, -1.0f, 110, 1, 2.0f, 90), 1, Y },

        // ---- 2.0: masters ---------------------------------------------------------------------------------------------
        { "Hip-Hop / R&B Streaming (-14)", "Smooth rap and R&B masters: moderate clipping, transparent release, everything locked to "
          "the centre.", P (25, 40, -1.0f, 110, 0, 2.0f, 100), 1, M },
        { "Pop / Modern Master", "Bright, dense and clean: medium clipping with a firmer knee, transparent limiting. Drive by ear "
          "(modern pop masters sit around -11 to -9 LUFS) or LEARN to -14.", P (30, 50, -1.0f, 100, 0, 2.0f, 100), 1, M },
        { "EDM / Electronic Loud (-9)", "Maximum loudness for electronic music: harder clipping, loud style, fast release, link 80 % "
          "for width. Check the streaming codecs: at -9 LUFS SAFE is usually needed.", P (60, 70, -1.0f, 50, 2, 1.0f, 80), 3, M },
        { "Rock / Band Master", "Guitars, live drums: gentle clipping of the snare, slower transparent release so cymbals and "
          "guitars do not pump.", P (25, 55, -1.0f, 150, 0, 2.5f, 100), 1, M },
        { "Lo-Fi / Chill Master (-14)", "Soft, warm and dynamic: barely any clipping, the slow Safe style, long lookahead.",
          P (10, 15, -1.0f, 250, 3, 3.0f, 100), 1, M },
        { "Podcast / Spoken Word (-16)", "Speech for podcast platforms: -16 LUFS, -1.5 dBTP, no clipping, transparent limiting "
          "that only rounds the loudest words.", P (0, 30, -1.5f, 150, 0, 3.0f, 100), 2, M },
        { "Mix Bus Safety (peaks only)", "While mixing: stops overs without changing the sound (no clipper, transparent, fast "
          "recovery). Not a master: no loudness target.", P (0, 30, -1.0f, 50, 0, 1.0f, 100), 0, M },

        // ---- 2.0: vocal bus -------------------------------------------------------------------------------------------
        { "Vocal Peak Catcher", "Only the loudest syllables: -6 dBTP ceiling, transparent, fast release. Set GAIN so it works "
          "2-4 dB on the peaks.", P (0, 30, -6.0f, 40, 0, 1.0f, 100), 0, V },
        { "Vocal Upfront (Dense)", "A lead vocal that sits on top of a dense beat: light soft clipping, punchy and fast, so "
          "every word comes through at the same level.", P (25, 30, -4.0f, 30, 1, 0.7f, 100), 0, V },
        { "Ad-libs / Background Vocals", "Ad-libs, doubles and stacks pushed back and even: more clipping, loud style, link 60 % "
          "so wide doubles keep their width.", P (35, 40, -6.0f, 25, 2, 0.5f, 60), 0, V },
        { "Sung Vocal (Smooth)", "Sung hooks and long notes: no clipping, the slow Safe style and a long lookahead, so held "
          "notes are not pumped or distorted.", P (0, 30, -4.0f, 150, 3, 3.0f, 100), 0, V },

        // ---- 2.0: beat / drum bus ---------------------------------------------------------------------------------------
        { "Drum Bus Punch", "Drums denser and louder with their attack kept: strong clipping of the transients, punchy "
          "release, 2 dB of headroom.", P (65, 40, -2.0f, 60, 1, 1.0f, 100), 0, B },
        { "808 / Sub Control", "Long 808s and sub bass held even without distortion: the Safe style's bass-aware hold, the "
          "longest lookahead, no clipping.", P (0, 30, -2.0f, 200, 3, 5.0f, 100), 0, B },
        { "Trap Beat Hard Clip", "The hard-clipped trap sound on the whole beat: nearly all peak reduction by a hard clipper, "
          "loud style.", P (90, 90, -1.0f, 40, 2, 0.8f, 100), 0, B },
        { "Sample-Based Beat Glue", "Sample-based beats glued together: a soft clip, a transparent and unhurried limiter, "
          "link 90 % to keep the sample's stereo.", P (30, 20, -1.5f, 120, 0, 2.0f, 90), 0, B },
    } };
}
} // namespace axm
