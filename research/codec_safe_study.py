"""Calibrates and verifies SAFE: each real mix is mastered loud (-9 LUFS presets), SAFE lowers the ceiling from the
in-plugin codec check (tools/MasterRender, the shipping code) to a target, and the result goes through the real
encoders (ffmpeg: AAC native + FDK 256k, MP3 320k, Opus 160k, Vorbis 160k + 320k) at 6 start offsets. Reported: the
worst decoded true peak, clipped samples, and the loudness given up. usage: python3 codec_safe_study.py T1 [T2 ...]"""
import os, sys, json, subprocess, tempfile, numpy as np
from concurrent.futures import ThreadPoolExecutor
HERE = os.path.dirname(os.path.abspath(__file__))
targets = [float(a) for a in sys.argv[1:]] or [-0.3]
sys.argv = ["x"]; exec(open(os.path.join(HERE, "real_masters.py")).read().split("if __name__")[0])
TOOL = os.path.join(os.path.dirname(HERE), "build", "MasterRender")
CASES = [(k, 3, g) for k, g in (("suite", 5.7), ("dry", 5.9), ("penta", 6.0))] + \
        [(k, 4, g) for k, g in (("suite", 5.5), ("dry", 5.7), ("penta", 5.8))] + \
        [(k, 5, g) for k, g in (("suite", 5.3), ("dry", 5.5), ("penta", 5.5))]
OFFSETS = (0, 37, 128, 333, 700, 1500)
os.makedirs(os.path.expanduser("~/axell-master-data"), exist_ok=True)
tmp = tempfile.mkdtemp(prefix="axm-safe-", dir=os.path.expanduser("~/axell-master-data"))   # on disk: /tmp is RAM
mixes = {}
for k in ("suite", "dry", "penta"):
    mixes[k] = f"{tmp}/{k}.f32"; read(MIXES[k])[1].astype("<f4").tofile(mixes[k])

def case(args):
    (k, preset, gain), T = args
    out = f"{tmp}/{k}{preset}{T}.f32"
    r = json.loads(subprocess.run([TOOL, mixes[k], out, "48000", str(preset), str(gain), "--safe", "--target", str(T)],
                                  capture_output=True, text=True, check=True).stdout)
    y = np.fromfile(out, "<f4").astype(np.float64).reshape(-1, 2)
    worst, clips = {}, {}
    for c in CODECS:
        tps, cl = [], 0
        for o in OFFSETS:
            d = f"{tmp}/{k}{preset}{T}{c[:3]}{o}"; os.makedirs(d, exist_ok=True)
            tp, sp, n = roundtrip(np.concatenate([np.zeros((o, 2)), y]), FS, c, d)
            os.rmdir(d)
            tps.append(tp); cl += n
        worst[c], clips[c] = max(tps), cl
    return dict(passes=r["passes"], mix=k, preset=preset, target=T, lufs=integrated(y, FS), ceiling=r["ceiling_db"], change=r["safe_change_db"],
                measured_before=max(r["opus_db"], r["vorbis_db"]), measured_after=max(r["after_opus_db"], r["after_vorbis_db"]),
                worst=worst, clips=clips)

if __name__ == "__main__":
    jobs = [(c, T) for T in targets for c in CASES]
    with ThreadPoolExecutor(6) as ex:
        res = list(ex.map(case, jobs))
    for T in targets:
        rs = [r for r in res if r["target"] == T]
        print(f"=== target {T:+.1f} dBTP")
        for r in rs:
            print(f"{r['mix']:6s} p{r['preset']} {r['passes']} pass ceiling {r['ceiling']:+.2f} (change {r['change']:+.2f}) LUFS {r['lufs']:.2f} | "
                  f"check {r['measured_before']:+.2f} -> {r['measured_after']:+.2f} | " +
                  " ".join(f"{c.split()[0]} {r['worst'][c]:+.2f}/{r['clips'][c]}" for c in CODECS), flush=True)
        allw = max(max(r["worst"].values()) for r in rs); allc = sum(sum(r["clips"].values()) for r in rs)
        print(f"   worst decoded true peak over everything {allw:+.2f} dBTP, clipped samples {allc}, mean ceiling change {np.mean([r['change'] for r in rs]):+.2f} dB")
    json.dump(res, open(os.path.join(HERE, "codec_safe_study.json"), "w"), indent=1)
    import shutil
    shutil.rmtree(tmp)
