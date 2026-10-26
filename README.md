# Axell Master (VST3, Linux)

The mastering limiter of the Axell suite: an oversampled soft clipper into a true-peak limiter, with a loudness
meter to the broadcast standard, delivery targets with LEARN, a level-matched COMPARE, psychoacoustically shaped
dither for the export, and (2.0) a check of the master through the real streaming codecs, with SAFE to keep a loud
master from clipping after YouTube or Spotify encode it. Built for rap, trap and boom-bap masters, and transparent enough for acoustic music.

> **Part of Axell's Dojo** - the Axell Signature suite for rap vocals, beats and masters on Linux:
> [Penta EQ](https://github.com/zmardanian/axell-penta-eq) · [Duo Compressor](https://github.com/zmardanian/axell-duo-compressor) ·
> [Gate](https://github.com/zmardanian/axell-gate) · [De-Reverb](https://github.com/zmardanian/axell-dereverb) ·
> [Velvet](https://github.com/zmardanian/axell-velvet) · [Master](https://github.com/zmardanian/axell-master), installed,
> updated and tuned to your voice by the [Axell Signature Dojo](https://github.com/zmardanian/axell-signature-dojo).
> Since 3.0 the plugins share one core library (`external/axell-core`): a breakthrough in one plugin becomes a
> feature of the next.

![Axell Master](docs/screenshots/master-100pct.png)

## What's new in 4.0

- **ESS HOLD (for a vocal or vocal bus)**: driving a vocal into a limiter lifts its esses - between a vowel and its ess
  the limiter starts to release, and the S, SH and CH sounds come up against their own word. With ESS HOLD the
  suite's sibilance detector (Axell Velvet's, zero latency) marks the esses and the limiter holds the word's gain
  through each one, then glides back (10 ms) to exactly what 3.0 does - Axell Duo Compressor 4.0's method, built into
  the shared core limiter (attack and ceiling untouched). On 28 of the artist's raw takes, Vocal Peak Catcher driven
  +12 dB: esses over their word **+2.76 -> +0.40 dB** (Booth Rap Vocal Bus +2.29 -> +0.48, Vocal Upfront +2.88 -> +0.87,
  Ad-libs +3.73 -> +1.46); vowels within 0.1 dB, pauses and peaks unchanged. On in the vocal presets, off by default;
  projects from before 4.0 sound exactly as before.

## What was new in 3.0

- **VOICE HOLD (for a vocal or vocal bus)**: driving a vocal into the limiter lifts the room, breaths and headphone
  bleed between phrases by as much as the words are limited. With VOICE HOLD the suite's voice detector (Axell Gate's)
  marks the pauses and the limiter holds its gain at the voice's level there instead of releasing - Axell Duo
  Compressor 3.0's method, built into the shared core limiter (attack and ceiling untouched). On a real take,
  Vocal Peak Catcher driven +12 dB: pauses **+8.1 -> +2.7 dB** re the voice, the voice limited exactly as before. On in
  the vocal presets, off by default (on a mix or a beat the detector would take drum hits for a voice).
- **Axell's Dojo look**: the family emblem, a brush-written title with Master's seal (匠, "master craftsman"), a
  forest-green ground.

## What it does

**GAIN** drives the mix into the processing. **LEARN** sets it for you: play the song, press LEARN, then APPLY.
GAIN moves to hit the chosen target. A second pass lands within a few tenths of an LU.

**CLIPPER** shaves the shortest peaks (kicks, snares) before the limiter, so the limiter works less and pumps less.
- CLIP sets how much of the peak reduction is clipping.
- SHAPE runs from soft (rounded) to hard (flat).
- 16x oversampling is the default (1x / 2x / 4x / 8x selectable). Even a hard clip of a bright mix stays clean:
  aliasing is below -99 dBc.
- The live curve shows the clipper's transfer and your loudest recent peak.

**LIMITER** is a true-peak limiter (ceiling -24 to 0 dBTP, release 1-1000 ms, look-ahead 0.5-5 ms, stereo link).
It has four program-dependent release styles:
- **TRANSPARENT** (default): natural.
- **PUNCHY**: drums keep their snap.
- **LOUD**: the densest.
- **SAFE**: the slowest.

A lone peak recovers fast and dense passages slowly. A bass-aware hold keeps the gain steady through a bass note's
cycle: 40-50 Hz bass at 6 dB of limiting measures -139 dB THD+N in Transparent and Safe.

**LOUDNESS** follows ITU-R BS.1770-4 / EBU R128. It shows:
- integrated (large), short-term, momentary;
- loudness range (EBU Tech 3342);
- true peak (8x, twice the standard's minimum);
- PLR;
- the input's integrated loudness.

Targets: -14 LUFS (Spotify / YouTube / Tidal), -16 (Apple Music), -9 (club / CD), -23 (EBU R128), -24 (ATSC A/85).
The history shows the last seconds: peaks in and out, short-term loudness against the target, and clipper and
limiter gain reduction.

**OUTPUT**:
- **VOICE HOLD** (3.0, for a vocal or vocal bus): in the voice's pauses the limiter holds its gain instead of
  releasing, so the drive does not lift the room and the breaths (Vocal Peak Catcher +12 dB on a real take: pauses
  +8.1 -> +2.7 dB re the voice). On in the vocal presets; leave it off on a mix or a beat.
- **ESS HOLD** (4.0, for a vocal or vocal bus): through each ess the limiter holds the word's gain instead of
  releasing, so the drive does not push the esses above the vowels (Vocal Peak Catcher +12 dB, 28 real takes: esses
  over their word +2.76 -> +0.40 dB, vowels within 0.02 dB). On in the vocal presets; leave it off on a mix or a beat.
- **COMPARE** plays the unprocessed mix at the master's loudness, so you hear what the processing does and not just
  "louder". In the audit it matched within 0.03 LU.
- **DITHER** for the final export: 24-bit or 16-bit TPDF, or 16-bit shaped. The shaped mode uses the 9th-order
  F-weighted minimally audible filter (Wannamaker, JAES 1992), which is 22 dB quieter than plain TPDF at 2-5 kHz.

**STREAMING CODECS** (2.0). A -1 dBTP ceiling protects the file, but the services re-encode it, and a lossy codec's
decoded peaks land higher than the master's. Measured on real rap masters at -9 LUFS, 1.0's -1 dBTP masters decoded
at up to +1.1 dBTP (Vorbis 160, Spotify) and +0.6 (AAC), with clipped samples in every song.
- **The check:** while the master plays, it is encoded and decoded with the real Opus 160 kbps (YouTube) and
  Vorbis 160 kbps (Spotify) libraries, each at four frame offsets. Where the frames fall moves the decoded peak by up
  to 0.9 dB, and the service's framing is unknown.
- **Display:** the decoded true peak and clipped samples since the last change. Green = safe, amber = within
  0.3 dB of full scale, red = clips.
- **Threading:** it runs on its own thread and never touches the audio. On / off with the STREAMING CODECS switch.
- **SAFE:** lowers the ceiling until both codecs decode at -0.3 dBTP or below. Play the song again and press again
  if offered: a lower ceiling means denser limiting, so it usually takes 1-3 presses.
  - Verified on 9 loud masters: 0 clipped samples with Opus, Vorbis 160 / 320, FDK AAC 256 and MP3 320 at 6 frame
    offsets each.
  - It costs 0.5-1.3 LU on a -9 LUFS master. That is the price of a loud master that survives streaming, and you
  choose when to pay it.

There are 28 factory presets (2.0 added 20), grouped in the menu:
- **MASTER:** streaming, Apple Music, hip-hop / R&B, pop, EDM, rock, lo-fi, club, acoustic, podcast, broadcast,
  mix-bus safety.
- **YOUR STUDIO (BOOM BAP):** boom bap streaming master, a loud master that survives the codecs without SAFE, booth
  rap vocal bus, drum bus knock, beat tape master.
- **VOCAL BUS:** peak catcher, upfront, ad-libs / doubles, sung vocal.
- **BEAT / DRUM BUS:** drum bus punch, 808 / sub control, trap hard clip, sample-based glue.

Each 2.0 preset was checked on real material (DECISIONS M17), and its description shows as the tooltip of the preset
name. User presets and A/B too. Presets never change GAIN: how hard to drive depends on the mix, not the genre.

## Measured quality

| | |
|---|---|
| Streaming codecs after SAFE (9 loud rap masters, 6 frame offsets per codec) | 0 clipped samples with Opus 160, Vorbis 160 / 320, FDK AAC 256, MP3 320; worst decoded -0.08 dBTP |
| In-plugin codec check vs ffmpeg's encoders | Opus within 0.16 dB, Vorbis within 0.1 dB (same framing) |
| Codec check on vs off | bit-identical audio (it only listens, on its own thread) |
| Silence in | exactly 0 out |
| Below the ceiling | output = input delayed by the reported latency: error -138 dBFS at 1x, -125 dBFS at 16x |
| True-peak ceiling, -1 dBTP, mixes driven +12 to +14 dB | worst -1.017 dBTP (independent reconstruction), every style, link, clip and oversampling setting, 44.1 / 48 / 96 kHz, full-band input |
| Clipper aliasing, 5 kHz hard-clipped 6 dB | 16x -106 dBc, 8x -94 dBc, 4x -76 dBc (worst of 44.1 / 48 / 96 kHz) |
| Bass distortion, 40-50 Hz at ~6 dB of limiting | Transparent / Safe -139 dB, Punchy -83 dB, Loud -57 dB THD+N |
| Loudness meter | EBU Tech 3341 cases 1-5 and Tech 3342 cases 1-4 at 44.1 / 48 / 96 kHz |
| Live latency changes | the host is told first; output always aligned (0 misaligned windows through a real-time host) |
| Host block sizes 1-4096, everything on | identical output |
| CPU (48 kHz stereo, one core) | 4.7 % at defaults (16x), 3.4 % at 4x; the codec check adds ~15 % of one core on its own thread (switchable) |
| Real-time safety | no allocation, lock or I/O in the audio path (allocation counter in the test suite) |
| VOICE HOLD, a real take (vocal presets driven +18 dB) | pauses re the voice: Peak Catcher +13.9 -> +4.6 dB, Sung Vocal +6.8 -> +0.7, Booth Rap +6.1 -> +2.5; the voice's limiting within 0.2 dB |
| VOICE HOLD, synthetic voice (all rates) | pause lift 6.7 -> 0.1 dB, voice level unchanged, the ceiling held |
| ESS HOLD, 28 real takes (vocal presets driven +12 dB) | esses over their word: Peak Catcher +2.76 -> +0.40 dB, Booth Rap +2.29 -> +0.48, Upfront +2.88 -> +0.87, Ad-libs +3.73 -> +1.46; vowels within 0.1 dB, pauses and true peak unchanged |

All of these come from:
- the automated suites: `tests/`, 378 DSP checks at 44.1 / 48 / 96 kHz (the codec check also at 88.2 kHz) plus 61
  plugin checks; the core adds 143 checks (the limiter's VOICE HOLD and ESS HOLD included);
- an independent black-box audit through a VST3 host (`tests/independent/master_audit.py`, 25 checks, analysis from
  textbook definitions);
- the studies in `research/`: real masters through the streaming codecs, SAFE's calibration, every preset on real
  material;
- AddressSanitizer + UBSan;
- pluginval at strictness 10;
- Steinberg's VST3 validator (47/47).

The reports are in `tests/`; the reasoning behind every design choice is in [docs/DECISIONS.md](docs/DECISIONS.md).

## Engineering progress

| Version | Date | Engineering | Measured |
|---|---|---|---|
| **1.0.0** | 2026-10-15 | Oversampled soft clipper (16x default) into a true-peak limiter with four program-dependent release styles and a bass-aware hold; BS.1770-4 / EBU R128 meter; targets with LEARN; level-matched COMPARE; 9th-order F-weighted dither | Ceiling within 0.02 dB of -1 dBTP on mixes driven +12-14 dB (independent reconstruction); clipper aliasing -106 dBc at 16x; 40-50 Hz bass at 6 dB of limiting -139 dB THD+N; EBU Tech 3341/3342 conformance |
| **2.0.0** | 2026-10-17 | STREAMING CODECS: the master through real Opus and Vorbis encoders while it plays (4 frame offsets, own thread); SAFE lowers the ceiling until both decode at -0.3 dBTP; 20 presets checked on real material | 1.0's -1 dBTP masters decoded at up to +1.1 dBTP (Vorbis); after SAFE: 0 clipped samples on 9 loud masters with Opus, Vorbis 160/320, FDK AAC 256, MP3 320 |
| **3.0.0** | 2026-10-26 | VOICE HOLD for a vocal bus (Gate's voice detector + Duo 3.0's method in the shared core limiter); ESS GUARD studied and rejected (no net gain on the artist's mix, DECISIONS M18); Axell's Dojo look | Real take: pauses +8.1 -> +2.7 dB (+12 dB drive), voice unchanged; 378 DSP + 61 plugin checks, ASan clean, audit 25/25, pluginval L10, validator 47/47 |
| **4.0.0** | 2026-10-26 | ESS HOLD for a vocal bus (Velvet's sibilance detector + Duo 4.0's method in the shared core limiter: the word's gain through each ess, a shadow of the 3.0 path to return to) | 28 takes, +12 dB: esses over their word +2.76 -> +0.40 dB (Peak Catcher; +2.29..+3.73 -> +0.40..+1.46 over four vocal presets), vowels within 0.1 dB; 391 DSP + 66 plugin + 143 core checks, ASan clean, audit 29/29 (3.0 sessions bit-identical), pluginval L10, validator 47/47 |

Every design decision with its measurement: [docs/DECISIONS.md](docs/DECISIONS.md); all changes: [CHANGELOG.md](CHANGELOG.md).

## Install (Linux)

Download `AxellMaster-4.0.0-linux-x86_64.tar.gz` from the
[latest release](https://github.com/zmardanian/axell-master/releases/latest) (optional: `sha256sum -c SHA256SUMS.txt`), then:

    tar xzf AxellMaster-4.0.0-linux-x86_64.tar.gz
    cd AxellMaster-4.0.0-linux-x86_64
    ./install.sh

The installer copies `Axell Master.vst3` to `~/.vst3`. Then in REAPER: Options > Preferences > Plug-ins > VST >
Re-scan. Put it last on the master bus. Or let the [Axell Signature Dojo](https://github.com/zmardanian/axell-signature-dojo)
install and update it.

## Build (Bazzite / any Linux, inside a distrobox)

    bash scripts/setup-toolbox.sh      # creates the juce-build distrobox (Ubuntu 24.04) once
    bash scripts/build.sh
    bash scripts/test.sh               # DSP + plugin suites (--quick, --asan)
    bash scripts/pluginval.sh
    bash scripts/install.sh

- JUCE 8.0.15 is fetched by CMake.
- The DSP lives in JUCE-free headers: `Source/Engine.h` and the shared Axell core in `external/axell-core` (or
  `../axell-core`). It is tested with plain g++.

## Contact

Axell Yobag - axellpetre@protonmail.com (questions, bug reports, feedback). Issues on GitHub are welcome too.

Licence: GNU AGPLv3 (`LICENSE`); third-party components (JUCE, and in 2.0 the Opus, libvorbis and libogg
reference libraries, BSD-licensed) in `THIRD_PARTY_NOTICES.md`. No code, data or graphics of any
commercial product are used.
