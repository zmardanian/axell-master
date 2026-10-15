# Changelog

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
