"""Independent black-box audit of the built Axell Master VST3 through a real VST3 host (MasterAuditHost, JUCE's VST3
hosting code), with analysis written here from textbook definitions (numpy/scipy) - nothing uses the plugin's own code.
Exit code = number of failures. Environment: AXELL_AUDIT_HOST, AXELL_VST3, AXELL_AUDIT_WORK (see axell-core/tools/lib.py)."""
import os, re, sys
import numpy as np
from scipy import signal as sg

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
os.environ.setdefault("AXELL_AUDIT_HOST", os.path.join(ROOT, "build/MasterAuditHost_artefacts/Release/MasterAuditHost"))
os.environ.setdefault("AXELL_VST3", os.path.join(ROOT, "build/AxellMaster_artefacts/Release/VST3/Axell Master.vst3"))
os.environ.setdefault("AXELL_AUDIT_WORK", os.path.expanduser("~/.cache/axell-master-audit"))
_vendored = os.path.join(ROOT, "external", "axell-core")
CORE = os.environ.get("AXELL_CORE", _vendored if os.path.isdir(_vendored) else os.path.join(os.path.dirname(ROOT), "axell-core"))
sys.path.insert(0, os.path.join(CORE, "tools"))
from lib import Run, run_all  # noqa: E402

results = []
def report(name, ok, detail):
    results.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}", flush=True)

def db(a):
    return 20 * np.log10(max(float(a), 1e-30))

def fir_lowpass(x, fs, fc, taps=4001):
    """linear-phase Kaiser FIR (beta 12: > 110 dB stopband), zero-phase by centring"""
    h = sg.firwin(taps, fc, fs=fs, window=("kaiser", 12.0))
    y = sg.fftconvolve(x, h[:, None], axes=0)
    d = (taps - 1) // 2
    return y[d:d + len(x)]

def beat(fs, secs, gain, seed=3, ch=2):
    """drum-and-bass-like program: kick, noise snare, hats, sub bass, pad"""
    rng = np.random.default_rng(seed)
    t = np.arange(int(fs * secs)) / fs
    b = np.mod(t, 0.5)
    kick = np.exp(-b * 12) * np.sin(2 * np.pi * (48 + 90 * np.exp(-b * 35)) * b)
    sn = np.exp(-np.mod(t + 0.25, 0.5) * 22) * rng.standard_normal(len(t)) * 0.45
    hat = np.exp(-np.mod(t, 0.125) * 70) * rng.standard_normal(len(t)) * 0.15
    bass = 0.35 * np.sin(2 * np.pi * 55 * t)
    pad = 0.12 * (np.sin(2 * np.pi * 220 * t) + np.sin(2 * np.pi * 329.6 * t + 2))
    left = kick + sn + hat + bass + pad
    right = kick + 0.85 * sn + 1.1 * hat + bass + 0.9 * pad
    x = gain * np.stack([left, right], 1)
    return x if ch == 2 else x[:, :1]

def true_peak(y, up=16, trim=4000):
    """ideal band-limited reconstruction (FFT resampling) of each channel; the ends are trimmed (circular wrap)"""
    m = 0.0
    for c in range(y.shape[1]):
        z = sg.resample(y[:, c], len(y) * up)
        m = max(m, float(np.abs(z[trim * up:-trim * up]).max()))
    return m

def k_weight(fs):
    """ITU-R BS.1770-4 K-weighting (shelf + RLB high-pass), coefficients derived for any rate from the analog prototype
    parameters given in the standard's reference implementation"""
    import math
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = math.tan(math.pi * f0 / fs); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * f0 / fs)
    a2 = [1, 2 * (K * K - 1) / (1 + K / Q + K * K), (1 - K / Q + K * K) / (1 + K / Q + K * K)]
    b2 = [1, -2, 1]
    return (b1, a1), (b2, a2)

def lufs(y, fs):
    """ungated loudness of the whole signal (steady programs): -0.691 + 10 log10(sum of channel mean squares)"""
    (b1, a1), (b2, a2) = k_weight(fs)
    z = sg.lfilter(b2, a2, sg.lfilter(b1, a1, y, axis=0), axis=0)
    return -0.691 + 10 * np.log10(np.sum(np.mean(z ** 2, axis=0)) + 1e-30)

AUDIT_START = __import__("time").time()

# ---------------------------------------------------------------- 1. silence, transparency below the ceiling, latency
fs = 48000
runs, ins = [Run("sil", fs)], {"sil": np.zeros((fs, 2))}
quiet = fir_lowpass(np.random.default_rng(3).uniform(-0.2, 0.2, (fs * 2, 2)), fs, 0.4 * fs)
for os_ in ["1x", "4x", "16x"]:
    runs.append(Run(f"tr{os_}", fs, sets={"Oversampling": os_}))
    ins[f"tr{os_}"] = quiet
out = run_all(runs, ins, jobname="a1")
report("silence in -> exactly silence out (defaults)", np.abs(out["sil"]).max() == 0.0, f"max |y| {np.abs(out['sil']).max():.3g}")
for r in runs[1:]:
    x, y, L = ins[r.name], out[r.name], r.latency
    e = np.abs(y[L + 4000:len(x) - 4000] - x[4000:len(x) - L - 4000]).max()
    lim = -95
    report(f"below the ceiling, {r.name[2:]}: output = input delayed by the reported latency ({L} samples)", db(e) < lim,
           f"max error {db(e):.1f} dBFS (< 0.4 fs content, limit {lim})")

# ---------------------------------------------------------------- 2. true-peak ceiling, independent reconstruction
cases = [("defaults, +12 dB", 48000, {"Input": "12"}, -1.0),
         ("clip 100 % hard, 16x, +14 dB", 48000, {"Input": "14", "Clip": "100", "Clip Shape": "100", "Oversampling": "16x"}, -1.0),
         ("clip 60 %, 1x, +14 dB", 48000, {"Input": "14", "Clip": "60", "Oversampling": "1x"}, -1.0),
         ("Loud, link 0 %, fast release, +14 dB", 48000, {"Input": "14", "Style": "Loud", "Stereo Link": "0", "Release": "10"}, -1.0),
         ("Punchy, ceiling -0.3, 0.5 ms lookahead, +14 dB", 48000, {"Input": "14", "Style": "Punchy", "Ceiling": "-0.3", "Lookahead": "0.5"}, -0.3),
         ("44.1 kHz, clip 50 %, +12 dB", 44100, {"Input": "12", "Clip": "50"}, -1.0),
         ("96 kHz, Safe, +12 dB", 96000, {"Input": "12", "Style": "Safe"}, -1.0)]
runs, ins = [], {}
for i, (name, rate, sets, ceil) in enumerate(cases):
    r = Run(f"tp{i}", rate, sets=sets)
    runs.append(r)
    # full-band program (noise snare and hats reach Nyquist): the hardest case for inter-sample peaks
    ins[r.name] = beat(rate, 4.0, 0.5, seed=10 + i)
out = run_all(runs, ins, jobname="tp")
for (name, rate, sets, ceil), r in zip(cases, runs):
    tp = db(true_peak(out[r.name][rate // 2:]))
    report(f"true peak <= ceiling ({name})", tp <= ceil + 0.005, f"{tp:.3f} dBTP, ceiling {ceil} dBTP (16x FFT reconstruction)")

# ---------------------------------------------------------------- 3. clipper aliasing (5 kHz hard-clipped 6 dB)
fs = 48000
t = np.arange(int(fs * 1.5)) / fs
sine = (0.5 * np.sin(2 * np.pi * 5000 * t))[:, None]
runs, ins = [], {}
for os_ in ["4x", "8x", "16x"]:
    r = Run(f"al{os_}", fs, ch=1, sets={"Clip": "100", "Clip Shape": "100", "Input": "6", "Ceiling": "-6", "Oversampling": os_})
    runs.append(r); ins[r.name] = sine
out = run_all(runs, ins, jobname="al")
for r, lim in zip(runs, [-75, -90, -99]):
    y = out[r.name][-32768:, 0]
    w = sg.windows.blackmanharris(len(y))
    Y = np.abs(np.fft.rfft(y * w)); f = np.fft.rfftfreq(len(y), 1 / fs)
    harm = np.zeros_like(f, bool)
    for h in range(1, 10):
        harm |= np.abs(f - 5000 * h) < 30
    fund = Y[np.abs(f - 5000) < 30].max()
    worst = Y[~harm & (f > 20) & (f < 20000)].max()
    report(f"clipper {r.name[2:]}: worst alias below 20 kHz re fundamental", db(worst / fund) < lim,
           f"{db(worst / fund):.1f} dBc (limit {lim})")

# ---------------------------------------------------------------- 4. release styles: recovery after a short peak
fs = 48000
n = int(fs * 1.5)
t = np.arange(n) / fs
x = 0.1 * np.sin(2 * np.pi * 1000 * t)                               # -20 dBFS tone
burst = (t >= 0.5) & (t < 0.51)
x[burst] = 1.6 * np.sin(2 * np.pi * 1000 * t[burst])                  # 10 ms at +4 dBFS: ~5 dB of limiting
x = np.repeat(x[:, None], 2, 1)
styles = ["Transparent", "Punchy", "Loud", "Safe"]
runs = [Run(f"st{s}", fs, sets={"Style": s, "Release": "120"}) for s in styles]
out = run_all(runs, {r.name: x for r in runs}, jobname="st")
rec = {}
for s, r in zip(styles, runs):
    y = out[r.name][:, 0]
    L = r.latency
    env = np.array([np.abs(y[i:i + 48]).max() for i in range(0, n - 48, 48)])     # 1 ms peak envelope
    ref = np.array([np.abs(x[max(0, i - L):max(0, i - L) + 48, 0]).max() for i in range(0, n - 48, 48)])
    g = 20 * np.log10(np.maximum(env, 1e-9) / np.maximum(ref, 1e-9))
    after = int((0.51 * fs + L) / 48) + 1
    k = next((i for i in range(after, len(g)) if g[i] > -1.0), len(g))
    rec[s] = (k - after) * 1.0
report("release styles recover in the order the names promise (Punchy < Loud < Transparent < Safe, ms to within 1 dB)",
       rec["Punchy"] < rec["Transparent"] and rec["Loud"] < rec["Transparent"] < rec["Safe"],
       ", ".join(f"{s} {v:.0f} ms" for s, v in rec.items()))

# ---------------------------------------------------------------- 4b. bass distortion under sustained limiting
def thdn_db(y, fs, f0):
    t = np.arange(len(y)) / fs
    A = np.stack([np.sin(2 * np.pi * f0 * t), np.cos(2 * np.pi * f0 * t), np.ones_like(t)], 1)
    c, *_ = np.linalg.lstsq(A, y, rcond=None)
    r = y - A @ c
    return 20 * np.log10(np.sqrt(np.mean(r ** 2)) / (np.hypot(c[0], c[1]) / np.sqrt(2)) + 1e-30)
fs = 48000
t = np.arange(fs * 3) / fs
bass = np.repeat((0.89 * np.sin(2 * np.pi * 40 * t))[:, None], 2, 1)       # 40 Hz at -1 dBFS, Input +6 -> ~6 dB of limiting
lims = {"Transparent": -90, "Punchy": -70, "Loud": -50, "Safe": -90}
runs = [Run(f"bs{s}", fs, sets={"Style": s, "Input": "6"}) for s in lims]
out = run_all(runs, {r.name: bass for r in runs}, jobname="bs")
res = {s: thdn_db(out[r.name][fs * 2:fs * 2 + fs // 2, 0], fs, 40) for s, r in zip(lims, runs)}
report("40 Hz bass under ~6 dB of steady limiting: THD+N per style (bass-aware hold)", all(res[s] < lims[s] for s in lims),
       ", ".join(f"{s} {v:.1f} dB (limit {lims[s]})" for s, v in res.items()))

# ---------------------------------------------------------------- 5. level-matched compare (independent BS.1770 loudness)
fs = 48000
prog = beat(fs, 6.0, 0.25, seed=5)
runs = [Run("cm0", fs, sets={"Input": "8", "Clip": "40"}), Run("cm1", fs, sets={"Input": "8", "Clip": "40", "Level-Matched Compare": "On"})]
out = run_all(runs, {r.name: prog for r in runs}, jobname="cm")
l0, l1, lin = lufs(out["cm0"][fs * 3:], fs), lufs(out["cm1"][fs * 3:], fs), lufs(prog[fs * 3:], fs)
report("COMPARE plays the input at the master's loudness (BS.1770, ungated)", abs(l1 - l0) < 1.0,
       f"master {l0:.2f} LUFS (input {lin:.2f}), compare {l1:.2f} LUFS, difference {l1 - l0:+.2f} LU")

# ---------------------------------------------------------------- 6. dither: 16-bit grid, spectrum of the shaped noise
fs = 48000
low = np.repeat((1e-3 * np.sin(2 * np.pi * 1000 * np.arange(fs * 2) / fs))[:, None], 2, 1)
runs = [Run("dt", fs, sets={"Dither": "16-bit"}), Run("ds", fs, sets={"Dither": "16-bit shaped"})]
out = run_all(runs, {r.name: low for r in runs}, jobname="dt")
for r in runs:
    q = out[r.name][fs // 2:] * 32768.0
    off = np.abs(q - np.round(q)).max()
    report(f"dither {r.name}: every sample on the 16-bit grid", off < 1e-3, f"max distance {off:.2e} LSB")
def band_noise(y, lo, hi):
    f, P = sg.welch(y[fs // 2:, 0] - 1e-3 * np.sin(2 * np.pi * 1000 * (np.arange(len(y) - fs // 2) + fs // 2 - runs[0].latency) / fs), fs, nperseg=8192)
    m = (f > lo) & (f < hi)
    return 10 * np.log10(np.mean(P[m]))
mid_t, mid_s = band_noise(out["dt"], 2000, 5000), band_noise(out["ds"], 2000, 5000)
report("16-bit shaped dither is quieter than TPDF in the 2-5 kHz band (where hearing is most sensitive)", mid_s < mid_t - 15,
       f"TPDF {mid_t:.1f} dB/Hz, shaped {mid_s:.1f} dB/Hz")

# ---------------------------------------------------------------- 7. host block-size invariance, everything on
fs = 44100
prog = beat(fs, 3.0, 0.35, seed=21)
sets = {"Input": "9", "Clip": "45", "Clip Shape": "70", "Style": "Punchy", "Stereo Link": "60", "Oversampling": "8x",
        "Dither": "16-bit shaped", "Lookahead": "1.3"}
rs = [Run("b512", fs, sets=sets, blocks="512"), Run("brand", fs, sets=sets, blocks="random 1 4096 7")]
out = run_all(rs, {r.name: prog for r in rs}, jobname="blk")
d = np.abs(out["b512"] - out["brand"]).max()
report("everything on: fixed vs random host block sizes (1..4096)", d < 1e-6, f"max |difference| {d:.3g}")

# ---------------------------------------------------------------- 8. live latency changes in a real-time host
fs = 48000
seq = [("Lookahead", "4"), ("Oversampling", "1x"), ("Lookahead", "0.5"), ("Oversampling", "16x"), ("Oversampling", "2x"),
       ("Lookahead", "2")]
x = fir_lowpass(np.random.default_rng(14).uniform(-0.3, 0.3, (int(fs * 0.5 * (len(seq) + 1)), 1)), fs, 0.4 * fs)
r = Run("pdc", fs, ch=1, sets={"Ceiling": "0"}, ats=[((i + 1) * (fs // 2), k, v) for i, (k, v) in enumerate(seq)],
        nonrt=False, pdc=2, preroll=fs // 4)
y = run_all([r], {"pdc": x}, jobname="pdc")["pdc"][:, 0]
ev = [(int(a), int(b)) for a, b in re.findall(r"pdc (\d+) (-?\d+)", r.log)]
Lh = np.zeros(len(y), int)
for pos, lat in ev:
    Lh[pos:] = lat
idx = np.arange(len(y)) - Lh
xd = np.where(idx >= 0, x[np.clip(idx, 0, None), 0], 0.0)
W = 16; nw = len(y) // W
Y, X = y[:nw * W].reshape(nw, W), xd[:nw * W].reshape(nw, W)
g = (Y * X).sum(1) / np.maximum((X * X).sum(1), 1e-12)
res = np.abs(Y - g[:, None] * X).max(1)
bad = (res > 0.02) | (g < -0.01) | (g > 1.01)
audible = (np.abs(Y).max(1) > 1e-3).mean()
lats = sorted(set(l for _, l in ev))
report("live latency changes (oversampling, lookahead): output always aligned with the host's compensation",
       bad.sum() == 0 and audible > 0.6 and len(lats) >= 5,
       f"{len(ev)} host latency updates {lats}, misaligned 16-sample windows {int(bad.sum())}, audible {100 * audible:.0f} %")

# ---------------------------------------------------------------- 9. CPU through the host (single core, 48 kHz stereo)
fs = 48000
prog = beat(fs, 20.0, 0.35, seed=8)
rs = [Run("cpu4", fs, sets={"Input": "8", "Oversampling": "4x"}), Run("cpu16", fs, sets={"Input": "8", "Clip": "50", "Oversampling": "16x", "Dither": "16-bit shaped"})]
out = run_all(rs, {r.name: prog for r in rs}, jobname="cpu")
for r, lim in zip(rs, [5.0, 12.0]):
    pct = 100 * r.cpu / 20.0 if r.cpu else float("nan")
    report(f"CPU, {'4x' if r.name == 'cpu4' else '16x (default) + clip 50 % + shaped dither'}: % of one core in real time",
           pct < lim, f"{pct:.2f} % (limit {lim} %)")

# ---------------------------------------------------------------- 10. 2.0: the codec check only listens
fs = 48000
prog = beat(fs, 6.0, 0.35, seed=31)
sets = {"Input": "10", "Clip": "40", "Style": "Loud"}
rs = [Run("ccon", fs, sets=dict(sets, **{"Codec Check": "On"})), Run("ccoff", fs, sets=dict(sets, **{"Codec Check": "Off"}))]
out = run_all(rs, {r.name: prog for r in rs}, jobname="codec")
d = np.abs(out["ccon"] - out["ccoff"]).max()
report("codec check on vs off: bit-identical audio (it only measures, on its own thread)", d == 0.0, f"max |difference| {d:.3g}")

# ---------------------------------------------------------------- 11. 4.0: ESS HOLD - 3.0.0 sessions unchanged
def voice_like(fs, secs, seed=4):
    """stereo test voice: 0.22 s vowels (harmonics of 120-160 Hz through two formants) and 0.1 s esses (noise around 7 kHz),
    1.6 s phrases, 0.8 s pauses (written here, independent of the plugin's own test signals)"""
    rng = np.random.default_rng(seed); n = int(secs * fs); t = np.arange(n) / fs
    ph = t % 2.4; syl = ph % 0.32; on = (ph > 0.2) & (ph < 1.8)
    f0 = 140 + 20 * np.sin(2 * np.pi * 0.6 * t); phase = 2 * np.pi * np.cumsum(f0) / fs
    v = np.zeros(n)
    for h in range(1, 40):
        f = h * f0; w = np.exp(-((f - 650) / 250) ** 2) + 0.6 * np.exp(-((f - 1150) / 300) ** 2) + 0.2 * np.exp(-((f - 2600) / 500) ** 2)
        v += w * np.sin(h * phase) / np.sqrt(h)
    b, a = sg.butter(4, [5500 / (fs / 2), 9000 / (fs / 2)], "band")
    e = sg.lfilter(b, a, rng.standard_normal(n))
    env_v = np.clip(np.minimum(syl, 0.22 - syl) / 0.015, 0, 1) * (syl < 0.22)
    env_e = np.sin(np.pi * np.clip((syl - 0.22) / 0.1, 0, 1)) * (syl >= 0.22)
    x = (0.12 * env_v * v + 0.35 * env_e * e) * on + 1e-4 * rng.standard_normal(n)
    return np.repeat(x[:, None], 2, 1)

V3 = os.environ.get("AXELL_V3_VST3", os.path.expanduser("~/axell-master-release/AxellMaster-3.0.0-linux-x86_64/Axell Master.vst3"))
if os.path.isdir(V3):
    fs = 48000
    vx = voice_like(fs, 6.0)
    cases = {"vocal bus +12 dB, VOICE HOLD on": {"Input": "12", "Ceiling": "-6", "Voice Hold": "On"},
             "loud, clipper 45 %, VOICE HOLD off": {"Input": "16", "Clip": "45", "Style": "Loud", "Voice Hold": "Off"}}
    q3 = [Run(f"m3_{i}", fs, sets=c) for i, c in enumerate(cases.values())]
    q4 = [Run(f"m4_{i}", fs, sets=dict(c, **{"Ess Hold": "Off"})) for i, c in enumerate(cases.values())]
    q5 = [Run(f"m5_{i}", fs, sets=dict(c, **{"Ess Hold": "On"})) for i, c in enumerate(cases.values())]
    o3 = run_all(q3, {r.name: vx for r in q3}, jobname="m3", plugin=V3)
    o4 = run_all(q4 + q5, {r.name: vx for r in q4 + q5}, jobname="m4")
    for i, k in enumerate(cases):
        d = np.abs(o3[f"m3_{i}"] - o4[f"m4_{i}"]).max()
        report(f"3.0.0 session compatibility: '{k}', ESS HOLD off renders like Axell Master 3.0.0",
               d == 0.0 and q3[i].latency == q4[i].latency, f"max |difference| {d:.3g}, latency {q3[i].latency} / {q4[i].latency}")
        on, off = o4[f"m5_{i}"], o4[f"m4_{i}"]
        lv = 10 * np.log10(np.mean(on ** 2) / np.mean(off ** 2))
        report(f"ESS HOLD on: '{k}': same latency, level within 0.5 dB, peaks under the ceiling",
               q5[i].latency == q4[i].latency and abs(lv) < 0.5 and db(true_peak(on)) <= float(cases[k].get("Ceiling", "-1")) + 0.1,
               f"latency {q5[i].latency} / {q4[i].latency}, level {lv:+.2f} dB, true peak {db(true_peak(on)):.2f} dBTP (ceiling {cases[k].get('Ceiling', '-1')})")
else:
    report("3.0.0 compatibility", False, f"3.0.0 bundle not found at {V3}")

fails = [r for r in results if not r[1]]
print(f"\nAUDIT: {len(results) - len(fails)}/{len(results)} pass ({__import__('time').time() - AUDIT_START:.0f} s)")
sys.exit(len(fails))
