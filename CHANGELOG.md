# Changelog

## 4.0.1 - 2026-10-26
- **Faster, bit-identical.** The pitch analysis inside the suite's sibilance detector (ESS HOLD) (YIN's difference function) now computes eight lags at a time in registers on a straight copy of the window; every lag still adds its terms in the same order, so the output is byte-for-byte the same as 4.0.0 (checked on four of the artist's takes through the installed 4.0.0 and the new build). CPU at 48 kHz, 128-sample blocks: vocal presets with VOICE HOLD + ESS HOLD 8.50 -> 7.59 % of one core (-10.8 %), other presets unchanged.

## 4.0.0 - 2026-10-26
- **ESS HOLD (new, for a vocal or vocal bus):** through each ess of the voice the limiter holds the gain of the word
  it belongs to instead of releasing, so the drive no longer pushes the S, SH and CH sounds above the vowels; after the
  ess it glides back (10 ms) to exactly what 3.0 does. On 28 of the artist's raw takes with the vocal presets driven
  +12 dB, the esses' lift over their word: Vocal Peak Catcher **+2.76 -> +0.40 dB**, Booth Rap Vocal Bus +2.29 -> +0.48,
  Vocal Upfront +2.88 -> +0.87, Ad-libs +3.73 -> +1.46; vowels -0.01..-0.08 dB, pauses and true peak unchanged. Uses the
  suite's sibilance detector (Axell Velvet's LIVE network, via axell-core); no added latency. On in the vocal presets,
  off by default and for projects saved before 4.0, which render bit-identically to 3.0.0 (DECISIONS M19).
- **Layout:** VOICE HOLD and ESS HOLD sit as two full-width switches in the OUTPUT column.
- Verified: DSP 391 checks at 44.1 / 48 / 96 kHz (new: ESS HOLD on a synthetic voice - lift removed, vowels 0.00 dB,
  ceiling held, latency unchanged; CPU with both holds on, 16x: 8.1 % of one core), plugin 66 (parameter 15 `essHold`,
  version hint 4; a 3.x project opens with it off), axell-core CoreTests 143/143 (flag 2: hold, return to the 3.0
  path within 30 ms, all-zero flags bit-identical), ASan + UBSan clean, independent audit 29/29 (new: 3.0.0 sessions
  render bit-identically to the 3.0.0 bundle; ESS HOLD on keeps latency, level and the ceiling), pluginval strictness
  10, Steinberg's VST3 validator 47/47.

## 3.0.0 - 2026-10-26
- **VOICE HOLD (new, for a vocal or vocal bus):** in the pauses of the voice the limiter holds its gain at the voice's
  level instead of releasing, so the drive no longer lifts the room, breaths and headphone bleed between phrases.
  On the artist's take with Vocal Peak Catcher driven +12 dB: pauses +8.1 dB re the voice without it, +2.7 dB with
  it; the voice limited exactly as before. Uses the suite's voice detector (Axell Gate's, via axell-core); no added
  latency. On in the vocal presets, off by default (DECISIONS M18).
- **Axell's Dojo look**: the family emblem, a brush-written title with Master's seal (匠, "master craftsman") and
  Master's own forest-green ground.
- Tried and not shipped: ESS GUARD (no net gain on the artist's mix, M18).
- Verified: DSP 378 checks, plugin 61, ASan + UBSan clean, independent audit 25/25, pluginval strictness 10,
  Steinberg's VST3 validator 47/47; axell-core CoreTests 133/133 (the limiter's hold included).

## 2.0.0 - 2026-10-17
Masters that survive streaming.

- **STREAMING CODECS (new):** the master runs through the real codecs while it plays, with the decoded true peak and
  clipped samples since the last change.
  - Opus 160 kbps (YouTube) and Vorbis 160 kbps (Spotify), the reference BSD-licensed libraries.
  - Each codec runs at four frame offsets, because where the frames fall moves the decoded peaks by up to 0.9 dB.
  - It runs on its own thread (about 15 % of one core), never on the audio thread.
- **Why:** measured on real rap masters at -9 LUFS, 1.0's -1 dBTP masters decoded at up to +1.1 dBTP with Vorbis
  and +0.6 with AAC (up to 68 clipped samples per song). The -0.5 dBTP Club preset clipped with Opus too.
- **SAFE (new):** lowers the ceiling until both codecs decode at -0.3 dBTP or below (play again; 1-3 presses).
  - Verified on 9 loud masters: 0 clipped samples with Opus, Vorbis 160 / 320, FDK AAC 256 and MP3 320, at 6
    frame offsets each.
  - The cost: 0.5-1.3 LU of loudness on a -9 LUFS master.
- **20 new presets (28 in all), grouped** MASTER, YOUR STUDIO (BOOM BAP), VOCAL BUS, BEAT / DRUM BUS. Each was checked on
  real material (masters on three rap mixes, vocal presets on dry and processed vocals, beat presets on beat stems).
  - Boom Bap Loud, Codec-Ready (-9): 0 clipped samples after Opus and Vorbis without SAFE.
  - Each preset's description shows as the tooltip of the preset name.
- **Club / CD preset:** ceiling -1.0 dBTP (was -0.5).
- **Unchanged:** the clipper, limiter, loudness meter and dither are bit-for-bit 1.0's. 1.0 projects open unchanged,
  with the codec check on.

## 1.0.0 - 2026-10-15
First release of Axell Master, the mastering limiter of the Axell suite.

- Oversampled soft clipper (1x / 2x / 4x / 8x / 16x, 16x default: aliasing below -99 dBc) with CLIP amount and
  SHAPE (soft to hard), live transfer curve.
- True-peak limiter: ceiling -24 to 0 dBTP, release 1-1000 ms, look-ahead 0.5-5 ms, stereo link, four
  program-dependent release styles (Transparent, Punchy, Loud, Safe) with a bass-aware hold (THD+N -139 dB on a
  50 Hz bass at 6 dB of limiting, Transparent). The ceiling holds for any input at every oversampling setting.
- Loudness meter to ITU-R BS.1770-4 / EBU R128: momentary, short-term, integrated, loudness range,
  true peak, PLR, input integrated; history display with peaks, gain reduction and short-term loudness.
- Delivery targets (-14 Spotify / YouTube / Tidal, -16 Apple Music, -9 club, -23 EBU R128, -24 ATSC A/85)
  with LEARN (sets GAIN from the measured loudness).
- Level-matched COMPARE, A/B slots, 8 factory presets, user presets.
- Dither for export: 24-bit, 16-bit TPDF, 16-bit with 9th-order F-weighted psychoacoustic noise shaping.
- Latency reported exactly and changed only after the host was told; host bypass latency-aligned and
  click-free.
