"""The reference measurement (PLAN 13.4): what trance measures, and what Parhelion's renders measure, the same way.

For every recording of Tools/ref_sets.txt (fetched by Tools/fetch_refs.py) and for any file given on the command line:

  tempo      from the autocorrelation of the kick band's energy (85 .. 150 BPM), refined on the kicks' grid
  loudness   integrated LUFS, LRA and true peak from ffmpeg's ebur128 (BS.1770-4); the loudest 20 s (the Leveler's
             measure) and the short-term loudness every 100 ms
  form       per bar the low band (40-120 Hz) and the loudness; a breakdown is a run of at least 8 bars whose low band
             lies 12 dB or more under the body's median (kick and bass out, Dok. 6); the share of the track in
             breakdowns, the longest one, where it lies; the gap -- the loudest 3 s of the 64 bars after the longest
             breakdown against its own loudest 3 s (Dok. 6, rule 3: 4 to 8 LU); the drop after it -- the largest rise
             of two bars over the two before them within the 24 bars after the breakdown, the whole band and the low
             band (drop_rise, drop_rise_low)
  balance    in the drop (the loudest 32 bars): power in 20-60, 60-150, 150-400, 400-2k, 2-5k, 5-16k Hz as shares of the
             whole; the power centroid
  width      side over mid above 200 Hz and under 120 Hz, the correlation, in the drop
  kick       the fundamental of the kick's tail (the peak of 35 .. 100 Hz, 80 .. 200 ms after the kicks), in the drop
  pump       the ducking heard (Dok. 7): the envelope of 300-2000 Hz (pads and leads) folded onto the beat -- its median
             level 60 .. 120 ms after the kick against 60 .. 20 ms before the next, in dB (0: no pump), in the drop; the
             bands under 300 Hz hold the kick's own body there and cannot show a duck
  bass       where the low band (60-200 Hz, the kick taken out by the grid) has its onsets: the share on the off-beat
             eighth, on the other sixteenths, on the beat -- off-beat 0.6+ is the "dun-dun-dun", sixteenths the rolling bass
  key        a Krumhansl-Kessler profile on the chroma of 100 .. 2000 Hz; tonic and mode; the kick's pitch class against it

Only statistics leave this tool: Tools/ref_stats.json holds a row per recording and the medians per profile.

    python Tools/analyze_ref.py                      # every recording of ref_sets.txt
    python Tools/analyze_ref.py --only uplifting
    python Tools/analyze_ref.py work/renders/track.wav        # a render, printed the same way

@note The decoding, the loudness, the tempo and the grid are copied from Totality `Tools/analyze_ref.py` at 4d3c0d2
      (29.09.2026); the measures are Parhelion's.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
from scipy import ndimage, signal

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from fetch_refs import default_dir, local_file, read_sets  # noqa: E402

SR = 22050
SR_FULL = 44100
KEYS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
KK_MAJOR = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
KK_MINOR = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])


def decode(path, sr, mono=True, start=0.0, dur=None):
    cmd = ["ffmpeg", "-v", "quiet"]
    if start > 0:
        cmd += ["-ss", str(start)]
    if dur is not None:
        cmd += ["-t", str(dur)]
    cmd += ["-i", str(path), "-ac", "1" if mono else "2", "-ar", str(sr), "-f", "f32le", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype=np.float32).astype(np.float64)
    if mono:
        return x
    n = len(x) // 2
    return x[:2 * n].reshape(-1, 2).T.copy()


def bandpass(x, lo, hi, sr):
    sos = signal.butter(4, [lo, hi], btype="bandpass", fs=sr, output="sos")
    return signal.sosfiltfilt(sos, x)


def loudness(path):
    full = subprocess.run(["ffmpeg", "-v", "info", "-i", str(path), "-af", "ebur128=peak=true", "-f", "null", "-"],
                          capture_output=True, text=True, errors="replace").stderr
    out = full[-4000:]

    def grab(label):
        m = re.findall(label + r":\s*(-?\d+\.\d+)", out)
        return float(m[-1]) if m else float("nan")
    st = np.array([float(v) for v in re.findall(r"\bS:\s*(-?\d+\.\d+)", full)], dtype=np.float64)
    loud20 = float("nan")
    if len(st) > 200:
        e = np.power(10.0, st / 10.0)
        w = np.convolve(e, np.ones(200) / 200.0, mode="valid")
        loud20 = round(10.0 * math.log10(max(float(w.max()), 1e-30)), 2)
    return grab("I"), grab("LRA"), grab("Peak"), loud20, st


def tempo(low, lo=85.0, hi=150.0):
    """Tempo from the autocorrelation of the kick band's 5 ms energy envelope at 1, 2, 4 and 8 beats."""
    hop = int(0.005 * SR)
    n = len(low) // hop
    env = (low[:n * hop] ** 2).reshape(n, hop).mean(axis=1)
    env = env - env.mean()

    def score(bpm):
        v = 0.0
        for m in (1, 2, 4, 8):
            L = 60.0 / bpm * SR / hop * m
            i0 = int(L)
            fr = L - i0
            if i0 + 2 >= len(env):
                continue
            c = np.dot(env[:-i0 - 1], env[i0:len(env) - 1]) * (1 - fr) + np.dot(env[:-i0 - 1], env[i0 + 1:]) * fr
            v += c / (len(env) - i0 - 1)
        return v
    coarse = np.arange(lo, hi + 1e-9, 0.1)
    best = coarse[int(np.argmax([score(b) for b in coarse]))]
    # Trance lies over 120: a tempo under 100 whose double fits almost as well is the double (half-time kick detection).
    if best < 100.0 and 2 * best <= hi + 10 and score(2 * best) > 0.8 * score(best):
        best = 2 * best
    fine = np.arange(best - 0.12, best + 0.12, 0.01)
    return float(fine[int(np.argmax([score(b) for b in fine]))])


def flux(x, lo, hi, n=1024, hop=110):
    f, t, Z = signal.stft(x, fs=SR, nperseg=n, noverlap=n - hop, boundary=None, padded=False)
    sel = (f >= lo) & (f <= hi)
    m = np.log1p(1000.0 * np.abs(Z[sel]))
    d = np.maximum(np.diff(m, axis=1), 0.0).sum(axis=0)
    return t[1:] * SR, d


def grid(times, low_flux, bpm):
    """The offset (samples) of the beat grid: where the kick band's onsets fold strongest onto the beat."""
    beat = 60.0 / bpm * SR
    tol = 0.012 * SR
    best, best_score = 0.0, -1e30
    for off in np.linspace(0, beat, 384, endpoint=False):
        ph = (times - off) % beat
        near = (ph < tol) | (ph > beat - tol)
        score = low_flux[near].sum()
        if score > best_score:
            best_score, best = score, off
    return best


def db(x):
    return 10.0 * math.log10(max(float(x), 1e-30))


def sustained(seg, sr):
    """The sustained part's energy per frame between 200 Hz and 5 kHz, and the frames' times (30.09.2026).

    Harmonic-percussive separation by median filtering (Fitzgerald 2010): along time the sustained partials survive,
    along frequency the broadband hits; the soft mask H^2 / (H^2 + P^2) keeps the pads, the chords, the atmosphere and
    the tails, and leaves the kick, the hats and the claps out.
    """
    f, tt, z = signal.stft(seg, fs=sr, nperseg=2048, noverlap=2048 - 512)
    s = np.abs(z) ** 2
    h = ndimage.median_filter(s, size=(1, 17))
    q = ndimage.median_filter(s, size=(17, 1))
    mask = h * h / (h * h + q * q + 1e-30)
    band = (f >= 200.0) & (f <= 5000.0)
    return (s[band] * mask[band]).sum(axis=0), tt


def measure(path, bpm_hint=None):
    x = decode(path, SR)
    dur = len(x) / SR
    low = bandpass(x, 40.0, 200.0, SR)
    bpm = tempo(low) if bpm_hint is None else bpm_hint
    t_low, f_low = flux(x, 40.0, 200.0)
    off = grid(t_low, f_low, bpm)
    beat = 60.0 / bpm * SR
    bar = 4 * beat
    nbars = int((len(x) - off) // bar)
    r = {"bpm": round(bpm, 2), "minutes": round(dur / 60.0, 2), "bars": nbars}

    integ, lra, peak, loud20, st = loudness(path)
    r.update({"lufs": integ, "lra": lra, "true_peak": peak, "loud20": loud20})

    # Form: the low band and the loudness per bar.
    sub = bandpass(x, 40.0, 120.0, SR)
    low_bar = np.array([np.mean(sub[int(off + b * bar):int(off + (b + 1) * bar)] ** 2) for b in range(nbars)])
    all_bar = np.array([np.mean(x[int(off + b * bar):int(off + (b + 1) * bar)] ** 2) for b in range(nbars)])
    low_db = np.array([db(v) for v in low_bar])
    body = np.median(low_db[low_db > np.percentile(low_db, 30)])
    # A bar is quiet when the low band, smoothed over three bars, lies 8 dB under the body (a breakdown keeps a stray bar
    # of bass or a sub swell now and then; measured on Time To Rest and Communication, 29.09.2026); runs closer than
    # three bars are one.
    sm = np.array([np.median(low_db[max(0, b - 1):b + 2]) for b in range(nbars)])
    quiet = sm < body - 8.0
    runs, b = [], 0
    while b < nbars:
        if quiet[b]:
            e = b
            while e < nbars and (quiet[e] or (e + 1 < nbars and quiet[e + 1]) or (e + 2 < nbars and quiet[e + 2])):
                e += 1
            runs.append((b, e - b))
            b = e
        else:
            b += 1
    r["intro_bars"] = runs[0][1] if runs and runs[0][0] == 0 else 0
    r["outro_bars"] = runs[-1][1] if runs and runs[-1][0] + runs[-1][1] >= nbars else 0
    # Intro and outro without kick are not breakdowns: only runs with the body's low band before and after them.
    breaks = [(s, n) for s, n in runs if n >= 8 and s > 0 and s + n < nbars]
    r["breakdown_share"] = round(sum(n for _, n in breaks) / max(nbars, 1), 3)
    r["breakdowns"] = len(breaks)
    if breaks:
        s, n = max(breaks, key=lambda q: q[1])
        r["breakdown_bars"] = n
        r["breakdown_at"] = round(s / max(nbars, 1), 3)
        # The gap (Dok. 6, rule 3): the loudest 3 s of the 64 bars after it against its own loudest 3 s.
        sec_per_bar = bar / SR
        # ffmpeg's short-term value at t is the loudness of the 3 s before t: the breakdown's own begins 3 s in (without
        # this the drop before it counted as the breakdown's loudest part, and the gap came out 1 to 3 LU too small).
        off_s = off / SR
        a0, a1 = int((off_s + s * sec_per_bar + 3.0) * 10), int((off_s + (s + n) * sec_per_bar) * 10)
        d1 = int((off_s + min(nbars, s + n + 64) * sec_per_bar) * 10)
        if len(st) > d1 > a1 > a0:
            r["gap_lu"] = round(float(np.max(st[a1:d1]) - np.max(st[a0:a1])), 2)
        # The drop after it (29.09.2026, for Tools/eval_report.py): within the 24 bars after the breakdown's end (the build
        # comes first where the kick returns before the drop), the largest rise of two bars over the two before them --
        # the whole band and the low band. The same search on the references and on Parhelion's tracks, so neither is
        # measured at a place the other is not.
        rises, rises_low = [], []
        for b in range(s + n, min(nbars - 2, s + n + 24)):
            if b < 2:
                continue
            rises.append(db(np.mean(all_bar[b:b + 2])) - db(np.mean(all_bar[b - 2:b])))
            rises_low.append(db(np.mean(low_bar[b:b + 2])) - db(np.mean(low_bar[b - 2:b])))
        if rises:
            r["drop_rise"] = round(float(max(rises)), 2)
            r["drop_rise_low"] = round(float(max(rises_low)), 2)
    # The drop: the loudest 32 bars.
    if nbars >= 32:
        w = np.convolve(all_bar, np.ones(32), mode="valid")
        d0 = int(np.argmax(w))
    else:
        d0 = 0
    drop_start = (off + d0 * bar) / SR
    drop_len = min(32, nbars) * bar / SR
    r["drop_at"] = round(d0 / max(nbars, 1), 3)

    # The intro's atmosphere (30.09.2026, the user: "Fangen typische Trance-Songs nicht eher mit einer Atmosphaere oder
    # einem Pad an?"): the sustained energy (pads, chords, atmosphere, tails; sustained()) between 200 Hz and 5 kHz in
    # bars 1 .. 16 and 17 .. 32, against the drop's, in dB -- and the share of the first 32 bars' bars whose sustained
    # energy lies no more than 30 dB under the drop's (where anything sustained is heard at all).
    if nbars >= 64:
        e_drop, _ = sustained(x[int(off + d0 * bar):int(off + (d0 + 32) * bar)], SR)
        e_in, t_in = sustained(x[int(off):int(off + 32 * bar)], SR)
        ref_e = float(np.mean(e_drop)) + 1e-30
        bar_s = bar / SR
        per_bar = np.array([np.mean(e_in[(t_in >= b * bar_s) & (t_in < (b + 1) * bar_s)]) for b in range(32)])
        r["intro_sus1"] = round(db(np.mean(per_bar[:16]) / ref_e), 1)
        r["intro_sus2"] = round(db(np.mean(per_bar[16:]) / ref_e), 1)
        r["intro_sus_bars"] = round(float(np.mean(per_bar / ref_e > 10 ** (-30 / 10))), 3)

    # Balance and width in the drop.
    st2 = decode(path, SR_FULL, mono=False, start=drop_start, dur=drop_len)
    m = 0.5 * (st2[0] + st2[1])
    s2 = 0.5 * (st2[0] - st2[1])
    f, P = signal.welch(m, fs=SR_FULL, nperseg=8192)
    tot = P[(f >= 20) & (f <= 16000)].sum()
    for name, lo, hi in (("b20_60", 20, 60), ("b60_150", 60, 150), ("b150_400", 150, 400), ("b400_2k", 400, 2000),
                         ("b2k_5k", 2000, 5000), ("b5k_16k", 5000, 16000)):
        r[name] = round(float(P[(f >= lo) & (f < hi)].sum() / tot), 4)
    band = (f >= 20) & (f <= 16000)
    r["centroid"] = round(float((f[band] * P[band]).sum() / P[band].sum()), 0)
    mh, sh = bandpass(m, 200, 16000, SR_FULL), bandpass(s2, 200, 16000, SR_FULL)
    ml, sl = bandpass(m, 30, 120, SR_FULL), bandpass(s2, 30, 120, SR_FULL)
    r["side_hi_db"] = round(db(np.mean(sh ** 2) / max(np.mean(mh ** 2), 1e-30)), 1)
    r["side_lo_db"] = round(db(np.mean(sl ** 2) / max(np.mean(ml ** 2), 1e-30)), 1)
    r["correlation"] = round(float(np.corrcoef(st2[0], st2[1])[0, 1]), 3)

    # The drop at 22.05 kHz for the kick, the pump and the bass.
    i0, i1 = int(off + d0 * bar), int(off + (d0 + min(32, nbars)) * bar)
    xd = x[i0:i1]
    kicks = np.arange(0, len(xd) - beat, beat)
    # Kick: the peak of 35 .. 100 Hz in 80 .. 200 ms after each kick, averaged spectra.
    n = 4096
    acc = np.zeros(n // 2 + 1)
    for k in kicks.astype(int):
        seg = xd[k + int(0.08 * SR):k + int(0.08 * SR) + n]
        if len(seg) == n:
            acc += np.abs(np.fft.rfft(seg * np.hanning(n))) ** 2
    ff = np.fft.rfftfreq(n, 1.0 / SR)
    sel = (ff >= 35) & (ff <= 100)
    kf = float(ff[sel][int(np.argmax(acc[sel]))]) if acc[sel].any() else float("nan")
    r["kick_hz"] = round(kf, 1)

    # Pump: the envelope of a band folded onto the beat.
    hop = int(0.005 * SR)
    for name, lo, hi in (("pump_mid", 300, 2000),):
        y = bandpass(xd, lo, hi, SR)
        e = np.sqrt(np.convolve(y ** 2, np.ones(hop) / hop, mode="same"))
        after, before = [], []
        for k in kicks.astype(int):
            a = e[k + int(0.06 * SR):k + int(0.12 * SR)]
            bfr = e[k + int(beat) - int(0.06 * SR):k + int(beat) - int(0.02 * SR)]
            if len(a) and len(bfr):
                after.append(np.mean(a))
                before.append(np.mean(bfr))
        if after:
            r[name] = round(20.0 * math.log10(max(np.median(after), 1e-12) / max(np.median(before), 1e-12)), 2)

    # Bass onsets: where the 60-200 Hz band starts notes, in sixteenths of the beat.
    tl, fl = flux(xd, 60.0, 200.0)
    step = beat / 4.0
    ph = np.floor(((tl + 0.5 * step) % beat) / step).astype(int) % 4
    tot_f = fl.sum() + 1e-12
    # The kick's own onset sits on step 0; the other steps are the bass's.
    r["bass_offbeat"] = round(float(fl[ph == 2].sum() / (fl[ph != 0].sum() + 1e-12)), 3)
    r["bass_16ths"] = round(float(fl[(ph == 1) | (ph == 3)].sum() / (fl[ph != 0].sum() + 1e-12)), 3)
    r["low_on_beat"] = round(float(fl[ph == 0].sum() / tot_f), 3)

    # Key: chroma of 100 .. 2000 Hz over the whole track.
    f3, t3, Z = signal.stft(x, fs=SR, nperseg=8192, noverlap=4096)
    mag = np.abs(Z).mean(axis=1)
    chroma = np.zeros(12)
    for fi, a in zip(f3, mag):
        if 100 <= fi <= 2000:
            pc = int(round(12 * math.log2(fi / 440.0))) % 12
            chroma[(pc + 9) % 12] += a * a
    best, best_s = None, -9
    for k in range(12):
        for mode, prof in (("minor", KK_MINOR), ("major", KK_MAJOR)):
            s = float(np.corrcoef(np.roll(prof, k), chroma)[0, 1])
            if s > best_s:
                best, best_s = (k, mode), s
    r["key"] = KEYS[best[0]] + ("m" if best[1] == "minor" else "")
    if kf == kf:
        kpc = int(round(12 * math.log2(kf / 440.0) + 69)) % 12
        r["kick_vs_key"] = (kpc - best[0]) % 12   # 0 tonic, 7 fifth
    return r


COLS = [("bpm", 6), ("minutes", 5), ("lufs", 6), ("loud20", 6), ("intro_bars", 4), ("intro_sus1", 6), ("intro_sus2", 6),
        ("outro_bars", 4), ("breakdown_share", 6), ("breakdown_bars", 4),
        ("gap_lu", 5), ("kick_hz", 5), ("pump_mid", 6), ("bass_offbeat", 5), ("b20_60", 6),
        ("b60_150", 6), ("b2k_5k", 6), ("b5k_16k", 6), ("side_hi_db", 6), ("key", 4)]


def row_text(name, r):
    out = f"{name[:34]:34s}"
    for k, w in COLS:
        v = r.get(k, "")
        out += f" {v:>{w}}" if not isinstance(v, float) else f" {v:>{w}.{2 if abs(v) < 10 else 1}f}"
    return out


def header():
    return f"{'':34s}" + "".join(f" {k[:w]:>{w}}" for k, w in COLS)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*")
    ap.add_argument("--only", default="")
    ap.add_argument("--dir", type=Path, default=default_dir())
    args = ap.parse_args()
    print(header())
    if args.files:
        for f in args.files:
            print(row_text(Path(f).name, measure(f)))
        return 0
    rows = []
    for ref in read_sets(HERE / "ref_sets.txt"):
        if args.only and ref["profile"] != args.only:
            continue
        p = local_file(args.dir, ref["id"])
        if p is None:
            print(f"missing {ref['name']}")
            continue
        try:
            r = measure(p, ref["bpm"])
        except Exception as e:   # a file that cannot be decoded: noted, not fatal
            print(f"failed {ref['name']}: {e}")
            continue
        r.update({"profile": ref["profile"], "name": ref["name"]})
        rows.append(r)
        print(row_text(ref["name"], r), flush=True)
    stats = {"rows": rows, "profiles": {}}
    for prof in sorted({r["profile"] for r in rows}):
        sel = [r for r in rows if r["profile"] == prof]
        med = {}
        for k in sorted({key for r in rows for key in r}):
            vals = [r[k] for r in sel if isinstance(r.get(k), (int, float)) and r[k] == r[k]]
            if vals:
                med[k] = round(float(np.median(vals)), 4)
        stats["profiles"][prof] = med
        print(row_text(f"== median {prof} ({len(sel)})", med))
    (HERE / "ref_stats.json").write_text(json.dumps(stats, indent=1), encoding="utf-8", newline="\n")
    print("statistics: Tools/ref_stats.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
