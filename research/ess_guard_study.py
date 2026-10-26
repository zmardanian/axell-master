"""Axell Master 3.0 ESS GUARD study: the artist's own mix (vocal over beat) mastered loud with ESS GUARD off and on, at
equal integrated loudness. Esses are located on the session's acapella (Velvet 3.0's sibilance network, frame-centred).
Measure ("crunch"): in 10 ms frames of the 4-16 kHz band, the energy of the output that no gain applied to the input
explains (y - a x with the best a per frame), re the explained part - gain changes (limiting) are not distortion, the
clipper's squared-off noise is. usage: python3 ess_guard_study.py <mix.wav> <acapella.wav> <MasterRender> <essp> <work dir>"""
import os, sys, json, subprocess, numpy as np
from scipy import signal as sg
from scipy.io import wavfile

def read(p):
    fs, x = wavfile.read(p)
    x = x.astype(np.float64)
    if x.dtype.kind == "i" or np.max(np.abs(x)) > 2:
        x = x / {2: 32768.0, 4: 2147483648.0}.get(wavfile.read(p)[1].dtype.itemsize, 1.0)
    return fs, (x if x.ndim == 2 else np.stack([x, x], 1))

def kweight(fs):
    # BS.1770 K filter (pre-filter shelf + RLB high-pass), coefficients for any rate via the analog prototype
    import math
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = math.tan(math.pi * f0 / fs); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * f0 / fs)
    a2 = [1, 2 * (K * K - 1) / (1 + K / Q + K * K), (1 - K / Q + K * K) / (1 + K / Q + K * K)]
    return b1, a1, [1, -2, 1], a2

def lufs(y, fs):
    b1, a1, b2, a2 = kweight(fs)
    z = sg.lfilter(b2, a2, sg.lfilter(b1, a1, y, axis=0), axis=0)
    blk, hop = int(0.4 * fs), int(0.1 * fs)
    ms = np.array([np.mean(np.sum(z[i:i + blk] ** 2, 1)) for i in range(0, len(z) - blk, hop)])
    L = -0.691 + 10 * np.log10(ms + 1e-20)
    g = ms[L > -70]; L1 = -0.691 + 10 * np.log10(g.mean()); g2 = g[-0.691 + 10 * np.log10(g) > L1 - 10]
    return -0.691 + 10 * np.log10(g2.mean())

def master(x, fs, preset, gain, guard, tool, work):
    x.astype(np.float32).tofile(f"{work}/in.f32")
    r = subprocess.run([tool, f"{work}/in.f32", f"{work}/out.f32", str(fs), str(preset), f"{gain:.3f}", "--no-codecs",
                        "--ess-guard", str(guard)], check=True, capture_output=True, text=True)
    return np.fromfile(f"{work}/out.f32", np.float32).reshape(-1, 2).astype(np.float64)

def crunch(x, y, fs, mask):
    b, a = sg.butter(6, [4000, 16000], "bandpass", fs=fs)
    xb = sg.filtfilt(b, a, x.mean(1)); yb = sg.filtfilt(b, a, y.mean(1))
    fr = int(0.01 * fs); n = min(len(xb), len(yb)) // fr
    X = xb[:n * fr].reshape(n, fr); Y = yb[:n * fr].reshape(n, fr); m = mask[:n * fr].reshape(n, fr).mean(1) > 0.5
    a_ = np.sum(X * Y, 1) / (np.sum(X * X, 1) + 1e-20)
    res = np.sum((Y - a_[:, None] * X) ** 2, 1); exp = np.sum((a_[:, None] * X) ** 2, 1)
    loud = 10 * np.log10(exp + 1e-20) > np.percentile(10 * np.log10(exp + 1e-20), 30)
    sel = lambda k: 10 * np.log10(res[k].sum() / (exp[k].sum() + 1e-20))
    return sel(m & loud), sel(~m & loud)

if __name__ == "__main__":
    mix, aca, tool, essp, work = sys.argv[1:6]
    os.makedirs(work, exist_ok=True)
    fs, x = read(mix); fs2, v = read(aca); assert fs == fs2
    n = min(len(x), len(v)); x, v = x[:n], v[:n]
    wavfile.write(f"{work}/aca.wav", fs, v.astype(np.float32))
    subprocess.run([essp, f"{work}/aca.wav", f"{work}/p.f32"], check=True)
    p = np.fromfile(f"{work}/p.f32", np.float32)[:n]
    ess = p > 0.5
    print(f"{os.path.basename(mix)}: {n / fs:.0f} s, esses {100 * ess.mean():.1f} % of the time", flush=True)
    out = {}
    for preset, name, target in ((4, "Loud Rap / Trap", -9.0), (9, "Boom Bap Loud, Codec-Ready (-9)", -9.0), (1, "Streaming Master (-14)", -14.0)):
        # the drive that lands on the target with ESS GUARD off; then ESS GUARD's drive matched to that loudness
        g = target - lufs(x, fs)
        for _ in range(4):
            L = lufs(master(x, fs, preset, g, 0, tool, work), fs); g += target - L
        y0 = master(x, fs, preset, g, 0, tool, work); L0 = lufs(y0, fs)
        for guard in (50, 100):
            gg = g
            for _ in range(4):
                Lg = lufs(master(x, fs, preset, gg, guard, tool, work), fs); gg += L0 - Lg
            y1 = master(x, fs, preset, gg, guard, tool, work); L1 = lufs(y1, fs)
            c0e, c0o = crunch(x, y0, fs, ess); c1e, c1o = crunch(x, y1, fs, ess)
            print(f"  {name:34s} ESS GUARD {guard:3d} %: loudness {L0:6.2f} / {L1:6.2f} LUFS | crunch on esses {c0e:6.1f} -> {c1e:6.1f} dB"
                  f" | elsewhere {c0o:6.1f} -> {c1o:6.1f} dB | drive {g:+.2f} / {gg:+.2f} dB", flush=True)
            out[f"{name} {guard}"] = dict(L0=L0, L1=L1, ess0=c0e, ess1=c1e, other0=c0o, other1=c1o, drive0=g, drive1=gg)
    json.dump(out, open(f"{work}/ess_guard.json", "w"), indent=1)
