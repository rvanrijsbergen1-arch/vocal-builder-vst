#!/usr/bin/env python3
"""Export RMVPE (vocal pitch estimator, yxlllc/RMVPE release 230917 `model.pt`, same network RVC/Applio
use) to ONNX for the VocalEngine.

The engine computes the magnitude STFT exactly like RVC (16 kHz, centred/reflect-padded, n_fft 1024,
Hann 1024, hop 160). The graph contains the HTK mel filterbank (128 bands, 30-8000 Hz), log, the
zero-padding of the frame axis to a multiple of 32 that RVC applies, the E2E network and the crop.
Output is the 360-bin salience; the engine decodes it (local weighted average, threshold 0.03).

Input "spec" [1, T, 513]  ->  output "salience" [1, T, 360]
Usage: python export_rmvpe.py <rvc_repo> <model.pt> <out.onnx>
"""
import importlib.util
import sys

import numpy as np
import torch
import torch.nn.functional as F


def load_rvc(rvc):
    sys.path.insert(0, rvc)
    spec = importlib.util.spec_from_file_location("rmvpe_mod", f"{rvc}/infer/rmvpe.py")
    mod = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(mod)
    except Exception as e:  # optional deps of the module (configs, cuda graphs) are not needed here
        print("note:", e)
    return mod


class Net(torch.nn.Module):
    def __init__(self, mod, ckpt):
        super().__init__()
        self.e2e = mod.E2E(4, 1, (2, 2))
        sd = torch.load(ckpt, map_location="cpu")
        sd = sd.get("model", sd)
        res = self.e2e.load_state_dict(sd, strict=False)
        assert not res.missing_keys, res.missing_keys
        self.e2e.eval()
        mel = mod.MelSpectrogram(False, 128, 16000, 1024, 160, None, 30, 8000)
        self.register_buffer("mel_basis", mel.mel_basis.clone())

    def forward(self, spec):  # [1, T, 513]
        mel = torch.matmul(self.mel_basis, spec.transpose(1, 2))          # [1, 128, T]
        mel = torch.log(torch.clamp(mel, min=1e-5))
        t = mel.shape[-1]
        n_pad = (32 - t % 32) % 32
        mel = F.pad(mel, (0, n_pad))
        hidden = self.e2e(mel)                                             # [1, T', 360]
        return hidden[:, :t]


def stft_mag(y):
    s = torch.stft(torch.tensor(y)[None], 1024, hop_length=160, win_length=1024, window=torch.hann_window(1024),
                   center=True, return_complex=True)
    return torch.sqrt(s.real ** 2 + s.imag ** 2).transpose(1, 2)          # [1, T, 513]


def main(rvc, ckpt, out):
    mod = load_rvc(rvc)
    net = Net(mod, ckpt).eval()
    ref = mod.MelSpectrogram(False, 128, 16000, 1024, 160, None, 30, 8000)
    rng = np.random.default_rng(0)
    t = np.arange(16000 * 3) / 16000
    y = (0.3 * np.sin(2 * np.pi * 220 * t) + 0.01 * rng.standard_normal(len(t))).astype(np.float32)
    spec = stft_mag(y)
    torch.onnx.export(net, (spec,), out, input_names=["spec"], output_names=["salience"],
                      dynamic_axes={"spec": {1: "t"}, "salience": {1: "t"}}, opset_version=17, dynamo=False)
    import onnxruntime as ort
    s = ort.InferenceSession(out)
    for n in (len(y), 16000 * 7 + 4800, 16000 * 13 + 160 * 17):
        yy = np.resize(y, n).astype(np.float32)
        sp = stft_mag(yy)
        with torch.no_grad():  # reference: RVC's own path (mel -> pad mel with 0 -> E2E)
            mel = ref(torch.tensor(yy)[None], center=True)
            tt = mel.shape[-1]
            mel = F.pad(mel, (0, 32 * ((tt - 1) // 32 + 1) - tt))
            want = net.e2e(mel)[:, :tt].numpy()
        got = s.run(None, {"spec": sp.numpy()})[0]
        print(f"len {n}: frames {got.shape[1]} max |diff| vs RVC path {np.abs(got - want).max():.2e}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
