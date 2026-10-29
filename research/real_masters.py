"""Axell Master on the artist's real mixes (read-only Desktop files): for each preset and target, GAIN is found so the
master lands on the target loudness, then the master is measured - integrated loudness (BS.1770-4 gated), true peak
(16x FFT reconstruction), and what the streaming services' lossy encoders make of it (AAC, MP3, Opus: decoded true
peak and clipped samples). Analysis from textbook definitions; nothing uses the plugin's code.
usage: python3 real_masters.py [out.json]"""
import os, sys, json, wave, subprocess, tempfile, numpy as np
from scipy import signal as sg
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tests", "independent"))
src = open(os.path.join(ROOT, "tests/independent/master_audit.py")).read()
ns = {"__file__": os.path.join(ROOT, "tests/independent/master_audit.py")}
exec(src[:src.index("AUDIT_START")], ns)
Run, run_all, true_peak, k_weight = ns["Run"], ns["run_all"], ns["true_peak"], ns["k_weight"]

MIXES = {"suite": os.path.expanduser("~/Desktop/axell suite on.wav"), "dry": os.path.expanduser("~/Desktop/dry vocal+beat.wav"),
         "penta": os.path.expanduser("~/Desktop/axell penta eq vocal+beat.wav")}
FS = 48000

def read(p):
    w = wave.open(p); n = w.getnframes(); ch = w.getnchannels(); sw = w.getsampwidth(); raw = w.readframes(n)
    if sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / float(1 << 23)
    else:
        x = np.frombuffer(raw, np.int16) / 32768.0
    return w.getframerate(), x.reshape(-1, ch).astype(np.float64)

def integrated(y, fs):
    """BS.1770-4 gated integrated loudness: 400 ms blocks, 75 % overlap, -70 LUFS absolute and -10 LU relative gates"""
    (b1, a1), (b2, a2) = k_weight(fs)
    z = sg.lfilter(b2, a2, sg.lfilter(b1, a1, y, axis=0), axis=0)
    blk, hop = int(0.4 * fs), int(0.1 * fs)
    ms = np.array([np.sum(np.mean(z[i:i + blk] ** 2, axis=0)) for i in range(0, len(z) - blk + 1, hop)])
    L = -0.691 + 10 * np.log10(ms + 1e-30)
    g = ms[L > -70]; rel = -0.691 + 10 * np.log10(np.mean(g)) - 10
    g = ms[(L > -70) & (L > rel)]
    return -0.691 + 10 * np.log10(np.mean(g))

def short_term_max(y, fs):
    (b1, a1), (b2, a2) = k_weight(fs)
    z = sg.lfilter(b2, a2, sg.lfilter(b1, a1, y, axis=0), axis=0)
    blk, hop = int(3 * fs), int(0.1 * fs)
    return max(-0.691 + 10 * np.log10(np.sum(np.mean(z[i:i + blk] ** 2, axis=0)) + 1e-30) for i in range(0, len(z) - blk, hop))

CODECS = {"aac 256k": ["-c:a", "aac", "-b:a", "256k"], "fdk-aac 256k": ["-c:a", "libfdk_aac", "-b:a", "256k"],
          "mp3 320k": ["-c:a", "libmp3lame", "-b:a", "320k"], "opus 160k": ["-c:a", "libopus", "-b:a", "160k"],
          "vorbis 160k": ["-c:a", "libvorbis", "-b:a", "160k"], "vorbis 320k": ["-c:a", "libvorbis", "-b:a", "320k"]}
EXT = {"aac 256k": "m4a", "fdk-aac 256k": "m4a", "mp3 320k": "mp3", "opus 160k": "opus", "vorbis 160k": "ogg", "vorbis 320k": "ogg"}

def roundtrip(y, fs, name, tmp):
    src = f"{tmp}/in.f32"; enc = f"{tmp}/enc.{EXT[name]}"; dec = f"{tmp}/dec.f32"
    y.astype("<f4").tofile(src)
    common = ["ffmpeg", "-v", "error", "-y"]
    subprocess.run(common + ["-f", "f32le", "-ar", str(fs), "-ac", "2", "-i", src] + CODECS[name] + [enc], check=True)
    subprocess.run(common + ["-i", enc, "-f", "f32le", "-ar", str(fs), "-ac", "2", dec], check=True)
    z = np.fromfile(dec, "<f4").astype(np.float64).reshape(-1, 2)
    for f in (src, enc, dec):          # ~30 MB each: never let them pile up
        os.remove(f)
    return 20 * np.log10(true_peak(z, up=8, trim=2000)), 20 * np.log10(np.abs(z).max()), int(np.sum(np.abs(z) > 1.0))

PRESETS = [("Streaming Master (-14 LUFS)", -14.0), ("Boom Bap Punch", -14.0), ("Boom Bap Punch", -9.0),
           ("Loud Rap / Trap", -9.0), ("Club / CD (-9 LUFS)", -9.0)]
if os.environ.get("AXM_PRESETS"):            # e.g. AXM_PRESETS=3,4 for a subset
    PRESETS = [PRESETS[int(i)] for i in os.environ["AXM_PRESETS"].split(",")]

if __name__ == "__main__":
    out = {}
    tmp = tempfile.mkdtemp(prefix="axm-real-", dir=os.path.expanduser("~/axell-master-data"))
    mixes = {k: read(p)[1] for k, p in MIXES.items()}
    for k, x in mixes.items():
        print(f"{k}: {len(x) / FS:.0f} s, integrated {integrated(x, FS):.1f} LUFS, true peak {20 * np.log10(true_peak(x, 8, 2000)):.1f} dBTP", flush=True)
    for pi, (preset, target) in enumerate(PRESETS):
        gains = {k: target - integrated(x, FS) for k, x in mixes.items()}
        for it in range(3):   # GAIN iterations (limiting is non-linear)
            runs = [Run(f"p{pi}{k}{it}", FS, sets={"Program": preset, "Input": f"{gains[k]:.2f}"}) for k in mixes]
            res = run_all(runs, {r.name: mixes[k] for r, k in zip(runs, mixes)}, jobname=f"real{pi}_{it}")
            ys = {k: res[r.name][r.latency:r.latency + len(mixes[k])] for r, k in zip(runs, mixes)}
            for k in mixes:
                gains[k] += target - integrated(ys[k], FS)
        for k, y in ys.items():
            row = dict(preset=preset, target=target, gain=round(gains[k], 2), lufs=round(integrated(y, FS), 2),
                       tp=round(20 * np.log10(true_peak(y, 8, 2000)), 2), st_max=round(short_term_max(y, FS), 2))
            row["plr"] = round(row["tp"] - row["lufs"], 2)
            for c in CODECS:
                tpc, spc, nclip = roundtrip(y, FS, c, tmp)
                row[c] = dict(tp=round(tpc, 2), sp=round(spc, 2), clipped=nclip)
            out[f"{preset} @ {target} / {k}"] = row
            print(f"{preset:28s} {target:5.0f} {k:6s} gain {row['gain']:+5.1f}  LUFS {row['lufs']:6.2f}  TP {row['tp']:6.2f}  "
                  + "  ".join(f"{c}: TP {row[c]['tp']:+5.2f} clip {row[c]['clipped']}" for c in CODECS), flush=True)
    json.dump(out, open(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "real_masters_1.0.json"), "w"), indent=1)
