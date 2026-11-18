#!/usr/bin/env python3
"""Turn dry vocal recordings into a clean RVC/Applio training set for ONE singer.

Why: a voice model learns everything in its training audio. Doubles, harmonies, ad-libs by other
people and reverb all end up in the voice and make conversions unstable and off-key. This script
  1. removes backing vocals / doubles / harmonies with the karaoke model (lead vocal only),
  2. optionally removes reverb,
  3. sums to mono, cuts at silences into 2-10 s phrases, drops near-silent or clipped slices,
  4. reports how many minutes of clean singing are left.

Use one output folder per singer. Never mix singers in one dataset.

Usage:
  python prepare_dataset.py --models <models/separation> --out D:\\Datasets\\SingerA  song1.wav song2.wav ...
  options: --keep-backing (skip step 1)  --dereverb  --min-sec 2 --max-sec 10
"""
import argparse
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mdx_ref import MDX  # noqa: E402

SR = 44100


def load44(p):
    x, sr = sf.read(p, dtype="float32", always_2d=True)
    if x.shape[1] == 1:
        x = np.repeat(x, 2, 1)
    x = x[:, :2]
    if sr != SR:
        g = np.gcd(sr, SR)
        x = resample_poly(x, SR // g, sr // g, axis=0).astype(np.float32)
    return x.T  # (2, N)


def slices(mono, min_sec, max_sec):
    hop = SR // 100
    n = len(mono) // hop
    e = 10 * np.log10((mono[: n * hop].reshape(n, hop) ** 2).mean(1) + 1e-12)
    voiced = e > e.max() - 45
    segs, i = [], 0
    while i < n:  # voiced runs, bridging gaps shorter than 0.3 s
        if not voiced[i]:
            i += 1
            continue
        j = i
        while j < n:
            if voiced[j]:
                j += 1
                continue
            k = j
            while k < n and not voiced[k]:
                k += 1
            if k - j < 30 and k < n:
                j = k
                continue
            break
        segs.append((i, j))
        i = j
    out = []
    for a, b in segs:  # split long phrases at the quietest point
        while b - a > int(max_sec * 100):
            lo, hi = a + int(min_sec * 100), a + int(max_sec * 100)
            c = lo + int(np.argmin(e[lo:hi]))
            out.append((a, c))
            a = c
        if b - a >= min_sec * 100 * 0.5:
            out.append((a, b))
    return [(max(0, a * hop - SR // 10), min(len(mono), b * hop + SR // 7)) for a, b in out]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", required=True, help="folder with UVR_MDXNET_KARA_2.onnx (+ .json) etc.")
    ap.add_argument("--out", required=True)
    ap.add_argument("--keep-backing", action="store_true")
    ap.add_argument("--dereverb", action="store_true")
    ap.add_argument("--min-sec", type=float, default=2.0)
    ap.add_argument("--max-sec", type=float, default=10.0)
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    md = Path(a.models)
    kara = None if a.keep_backing else MDX(str(md / "UVR_MDXNET_KARA_2.onnx"))
    rev = MDX(str(md / "Reverb_HQ_By_FoxJoy.onnx")) if a.dereverb else None
    total, count = 0.0, 0
    for f in a.files:
        x = load44(f)
        if kara is not None:
            x = x - kara.separate(x)  # karaoke model outputs the backing vocals
        if rev is not None:
            x = x - rev.separate(x)   # de-reverb model outputs the reverb
        mono = x.mean(0)
        stem = Path(f).stem.replace(" ", "_")[:40]
        kept = 0.0
        for k, (s, e) in enumerate(slices(mono, a.min_sec, a.max_sec)):
            y = mono[s:e]
            peak = np.abs(y).max()
            if peak < 1e-3 or (np.abs(y) > 0.999).mean() > 0.001:
                continue
            sf.write(out / f"{stem}_{k:03d}.wav", (y / peak * 0.7).astype(np.float32), SR, subtype="PCM_24")
            kept += len(y) / SR
            count += 1
        total += kept
        print(f"{Path(f).name}: {kept / 60:.1f} min of lead vocal", flush=True)
    print(f"\n{count} slices, {total / 60:.1f} min in {out}")
    if total < 600:
        print("Below 10 minutes: the voice will work but stays less stable. Add more dry takes of this singer if you can.")


if __name__ == "__main__":
    main()
