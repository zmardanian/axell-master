"""Checks every 2.0 factory preset on real material with the shipping engine (tools/MasterRender):
- masters: the artist's three mixes, GAIN found for the preset's loudness target (as LEARN does); integrated loudness,
  true peak, PLR, decoded codec peaks (the in-plugin check);
- vocal-bus presets: the artist's dry vocal and the processed vocal, GAIN set so the loudest peaks are reduced by the
  amount the preset's description promises;
- beat / drum-bus presets: the beat-only intros of two bounces (the first 21 s, before the vocal enters), same rule.
Also for all: the high band (10-20 kHz) added re a plain gain change (what clipping adds: brightness / grit), and the
crest factor (peak to loudness) before and after. usage: python3 presets_study.py [first last]"""
import os, sys, json, subprocess, tempfile, numpy as np
from concurrent.futures import ThreadPoolExecutor
from scipy import signal as sg
HERE = os.path.dirname(os.path.abspath(__file__))
rng_args = [int(a) for a in sys.argv[1:3]]
sys.argv = ["x"]; exec(open(os.path.join(HERE, "real_masters.py")).read().split("if __name__")[0])
TOOL = os.path.join(os.path.dirname(HERE), "build", "MasterRender")
TARGETS = {0: None, 1: -14.0, 2: -16.0, 3: -9.0, 4: -23.0, 5: -24.0}
# (index, name, kind, target index, ceiling, promised peak reduction dB for buses) - mirrors Source/Presets.h
PRESETS = [(8, "Boom Bap Streaming Master (-14)", "master", 1, -1.0, None), (9, "Boom Bap Loud, Codec-Ready (-9)", "master", 3, -3.0, None),
           (10, "Booth Rap Vocal Bus", "vocal", 0, -3.0, 4.0), (11, "Boom Bap Drum Bus Knock", "beat", 0, -2.0, 5.0),
           (12, "Beat Tape / Instrumental Master (-14)", "beatmaster", 1, -1.0, None),
           (13, "Hip-Hop / R&B Streaming (-14)", "master", 1, -1.0, None), (14, "Pop / Modern Master", "master", 1, -1.0, None),
           (15, "EDM / Electronic Loud (-9)", "master", 3, -1.0, None), (16, "Rock / Band Master", "master", 1, -1.0, None),
           (17, "Lo-Fi / Chill Master (-14)", "master", 1, -1.0, None), (18, "Podcast / Spoken Word (-16)", "speech", 2, -1.5, None),
           (19, "Mix Bus Safety (peaks only)", "mixbus", 0, -1.0, 1.0),
           (20, "Vocal Peak Catcher", "vocal", 0, -6.0, 3.0), (21, "Vocal Upfront (Dense)", "vocal", 0, -4.0, 5.0),
           (22, "Ad-libs / Background Vocals", "vocal", 0, -6.0, 6.0), (23, "Sung Vocal (Smooth)", "vocal", 0, -4.0, 3.0),
           (24, "Drum Bus Punch", "beat", 0, -2.0, 6.0), (25, "808 / Sub Control", "beat", 0, -2.0, 3.0),
           (26, "Trap Beat Hard Clip", "beat", 0, -1.0, 8.0), (27, "Sample-Based Beat Glue", "beat", 0, -1.5, 4.0)]
if rng_args:
    PRESETS = [p for p in PRESETS if rng_args[0] <= p[0] <= rng_args[-1]]
os.makedirs(os.path.expanduser("~/axell-master-data"), exist_ok=True)
tmp = tempfile.mkdtemp(prefix="axm-presets-", dir=os.path.expanduser("~/axell-master-data"))

def stereo(x):
    return x if x.ndim == 2 and x.shape[1] == 2 else np.stack([x.reshape(-1)] * 2, 1)

MAT = {}
MAT["master"] = {k: read(p)[1] for k, p in MIXES.items()}
_, dv = read(os.path.expanduser("~/Desktop/dry vocal.wav")); _, pv = read(os.path.expanduser("~/Desktop/axell penta eq vocal.wav"))
MAT["vocal"] = {"dry vocal": stereo(dv), "processed vocal": stereo(pv)}
MAT["speech"] = {"dry vocal": stereo(dv)}
MAT["beat"] = {"beat (dry bounce)": read(MIXES["dry"])[1][:21 * FS], "beat (penta bounce)": read(MIXES["penta"])[1][:21 * FS]}
MAT["beatmaster"] = MAT["beat"]
MAT["mixbus"] = MAT["master"]

def render(x, idx, gain, codecs):
    import uuid; i = f"{tmp}/{idx}_{uuid.uuid4().hex}"
    x.astype("<f4").tofile(i + ".in")
    r = subprocess.run([TOOL, i + ".in", i + ".out", str(FS), str(idx), f"{gain:.3f}"] + ([] if codecs else ["--no-codecs"]),
                       capture_output=True, text=True, check=True)
    y = np.fromfile(i + ".out", "<f4").astype(np.float64).reshape(-1, 2)
    os.remove(i + ".in"); os.remove(i + ".out")
    return y, json.loads(r.stdout)

def hf_added(x, y, g):
    """10-20 kHz energy of the output re the input after a plain gain change, minus the broadband change: > 0 = the
    processing added high frequencies (clipping grit / brightness)"""
    f, px = sg.welch(x[:, 0] * g, FS, nperseg=8192); _, py = sg.welch(y[:, 0], FS, nperseg=8192)
    band = (f >= 10000) & (f < 20000)
    return 10 * np.log10(py[band].sum() / px[band].sum()) - 10 * np.log10(py.sum() / px.sum())

def one(p):
    idx, name, kind, tgt, ceil, reduce = p
    rows = []
    for mname, x in MAT[kind].items():
        tp_in = 20 * np.log10(true_peak(x, 8, 2000))
        if TARGETS[tgt] is not None:          # LEARN: GAIN for the target, three passes (limiting is non-linear)
            gain = TARGETS[tgt] - integrated(x, FS)
            for _ in range(3):
                y, r = render(x, idx, gain, False)
                gain += TARGETS[tgt] - integrated(y, FS)
        else:                                 # buses: the loudest peaks reduced by the promised amount
            gain = ceil - tp_in + reduce
        y, r = render(x, idx, gain, kind in ("master", "beatmaster"))
        g = 10 ** (gain / 20)
        lu_in, lu_out = integrated(x, FS), integrated(y, FS)
        tp_out = 20 * np.log10(true_peak(y, 8, 2000))
        rows.append(dict(preset=name, index=idx, material=mname, gain=round(gain, 2), lufs=round(lu_out, 2),
                         tp=round(tp_out, 2), peak_reduction=round(tp_in + gain - tp_out, 2),
                         loudness_gain_vs_peak=round((lu_out - lu_in) - (tp_out - tp_in), 2),
                         crest_in=round(tp_in - lu_in, 2), crest_out=round(tp_out - lu_out, 2),
                         hf_added=round(hf_added(x, y, g), 2),
                         codec=round(max(r["opus_db"], r["vorbis_db"]), 2) if kind in ("master", "beatmaster") else None,
                         codec_clips=(r["opus_clips"] + r["vorbis_clips"]) if kind in ("master", "beatmaster") else None))
    return rows

if __name__ == "__main__":
    with ThreadPoolExecutor(5) as ex:
        res = [r for rows in ex.map(one, PRESETS) for r in rows]
    print(f"{'preset':38s} {'material':20s} {'gain':>6s} {'LUFS':>6s} {'TP':>6s} {'pkred':>6s} {'crest':>11s} {'HF+':>5s} {'codec':>6s}")
    for r in res:
        c = "" if r["codec"] is None else f"{r['codec']:+6.2f}/{r['codec_clips']}"
        print(f"{r['preset'][:38]:38s} {r['material'][:20]:20s} {r['gain']:+6.1f} {r['lufs']:6.1f} {r['tp']:6.2f} {r['peak_reduction']:6.1f} "
              f"{r['crest_in']:5.1f}>{r['crest_out']:4.1f} {r['hf_added']:+5.2f} {c}", flush=True)
    json.dump(res, open(os.path.join(HERE, "presets_study.json"), "w"), indent=1)
    import shutil; shutil.rmtree(tmp)
