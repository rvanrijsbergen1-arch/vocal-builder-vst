#!/usr/bin/env python3
"""Download the MDX-Net separation models (UVR community releases on GitHub) and write the sidecar
settings the VocalEngine reads. Checks the files against the hashes UVR uses to identify them.

Personal-use build: these community models have no clear commercial licence (see LICENCE_INVENTORY.md).
Usage: python fetch_mdx_models.py <out_dir>
"""
import hashlib
import json
import sys
import urllib.request
from pathlib import Path

BASE = "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/"
MODELS = {
    # name: (UVR model hash, settings) — settings from UVR's model_data_new.json
    "UVR-MDX-NET-Voc_FT": ("77d07b2667ddf05b9e3175941b4454a0", dict(n_fft=7680, dim_f=3072, dim_t=8, compensate=1.021, primary="Vocals")),
    "UVR_MDXNET_KARA_2": ("1d64a6d2c30f709b8c9b4ce1366d96ee", dict(n_fft=5120, dim_f=2048, dim_t=8, compensate=1.065, primary="Instrumental")),
    "Reverb_HQ_By_FoxJoy": ("cd5b2989ad863f116c855db1dfe24e39", dict(n_fft=6144, dim_f=3072, dim_t=9, compensate=1.035, primary="Reverb")),
}


def uvr_hash(p: Path) -> str:
    with open(p, "rb") as f:
        try:
            f.seek(-10000 * 1024, 2)
        except OSError:
            f.seek(0)
        return hashlib.md5(f.read()).hexdigest()


def main(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for name, (h, settings) in MODELS.items():
        dst = out / f"{name}.onnx"
        if not dst.exists():
            print("downloading", name, flush=True)
            urllib.request.urlretrieve(BASE + dst.name, dst)
        got = uvr_hash(dst)
        if got != h:
            sys.exit(f"{dst.name}: unexpected file (hash {got}, expected {h})")
        (out / f"{name}.json").write_text(json.dumps(settings))
        print("ok", name, dst.stat().st_size // (1 << 20), "MB")


if __name__ == "__main__":
    main(sys.argv[1])
