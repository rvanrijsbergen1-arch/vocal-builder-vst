#!/usr/bin/env python3
"""Export the FCPE pitch estimator (torchfcpe 0.0.4, MIT, bundled weights fcpe_c_v001) to ONNX.

The ONNX graph takes the magnitude STFT (computed by the engine, exactly as torchfcpe does it:
16 kHz, n_fft 1024, Hann 1024, hop 160, reflect pad 432 both sides, no centering) and contains
the mel filterbank, log compression, network and local-argmax decoder. torchfcpe appends a copy of
the last frame so there are N//160 + 1 frames; the engine does the same.

Output: fcpe.onnx  input "spec" [1, T, 513] -> output "f0" [1, T] in Hz (0 = unvoiced)
Usage : python export_fcpe.py <out.onnx> [test_vocal.wav]
"""
import sys
import numpy as np
import torch
from torchfcpe import spawn_bundled_infer_model

THRESHOLD = 0.006  # torchfcpe default for inference


class Net(torch.nn.Module):
    def __init__(self, m):
        super().__init__()
        self.model = m.model
        self.register_buffer("mel_basis", m.wav2mel.mel_extractor.mel_basis.clone())

    def forward(self, spec):  # [1, T, 513]
        mel = torch.matmul(spec, self.mel_basis.T)
        mel = torch.log(torch.clamp(mel, min=1e-5))
        return self.model.infer(mel, decoder="local_argmax", threshold=THRESHOLD).squeeze(-1)


def stft_mag(x):
    """Reference front end (mirrors the C++ engine)."""
    y = torch.nn.functional.pad(torch.tensor(x)[None, None], (432, 432), mode="reflect")[0]
    s = torch.stft(y, 1024, hop_length=160, win_length=1024, window=torch.hann_window(1024), center=False,
                   onesided=True, return_complex=True)
    m = torch.sqrt(s.real ** 2 + s.imag ** 2 + 1e-9).transpose(1, 2)  # [1, T, 513]
    n_frames = len(x) // 160 + 1  # torchfcpe repeats the last frame to reach N//160 + 1
    while m.shape[1] < n_frames:
        m = torch.cat([m, m[:, -1:]], 1)
    return m[:, :n_frames]


def main(out, test=None):
    m = spawn_bundled_infer_model("cpu")
    net = Net(m).eval()
    if test:
        import soundfile as sf
        from scipy.signal import resample_poly
        x, sr = sf.read(test, dtype="float32", always_2d=True)
        x = x.mean(1)
        g = np.gcd(int(sr), 16000)
        x = resample_poly(x, 16000 // g, int(sr) // g).astype(np.float32)[: 16000 * 8]
    else:
        t = np.arange(16000 * 3) / 16000
        x = (0.3 * np.sin(2 * np.pi * 220 * t)).astype(np.float32)
    with torch.no_grad():
        ref = m(torch.tensor(x)[None, :, None], 16000, decoder_mode="local_argmax", threshold=THRESHOLD).squeeze(-1).numpy()
        spec = stft_mag(x)
        mine = net(spec).numpy()
    print("front-end check vs torchfcpe: max |diff| Hz =", float(np.abs(mine - ref).max()))
    torch.onnx.export(net, (spec,), out, input_names=["spec"], output_names=["f0"],
                      dynamic_axes={"spec": {1: "t"}, "f0": {1: "t"}}, opset_version=17, dynamo=False)
    import onnxruntime as ort
    got = ort.InferenceSession(out).run(None, {"spec": spec.numpy()})[0]
    voiced = ref > 0
    cents = 1200 * np.abs(np.log2((got[voiced] + 1e-6) / (ref[voiced] + 1e-6)))
    print("ONNX vs torchfcpe: voicing agreement", float(((got > 0) == voiced).mean()), " max cents error",
          float(cents.max()) if cents.size else 0.0, " voiced frames", int(voiced.sum()), "/", voiced.size)
    np.save(out + ".testvec.npy", np.stack([x[: 16000 * 2]]))


if __name__ == "__main__":
    main(*sys.argv[1:])
