#!/usr/bin/env python3
"""Convert Deezer's Spleeter 2-stem checkpoint (MIT) to ONNX for the VocalEngine.

Input : the official release https://github.com/deezer/spleeter/releases/download/v1.4.0/2stems.tar.gz
Output: spleeter_2stems.onnx   input "mix_mag" [1, 512, 1536, 2] (|STFT| of stereo mix, NHWC as in Spleeter)
                               outputs "vocals", "accompaniment" (estimated magnitudes, same shape)
The U-Net is re-implemented in PyTorch with TensorFlow's 'same' padding semantics, then checked
numerically against a pure-TensorFlow reference built from the same checkpoint.
Usage: python convert_spleeter.py <checkpoint_dir> <out.onnx>
"""
import sys
import numpy as np
import tensorflow as tf
import torch
import torch.nn as nn
import torch.nn.functional as F

T, FBINS = 512, 1536


def load(ckpt_dir):
    r = tf.train.load_checkpoint(f"{ckpt_dir}/model")
    return {n: r.get_tensor(n) for n in r.get_variable_to_shape_map() if "Adam" not in n and "global_step" not in n and "beta1_power" not in n and "beta2_power" not in n}


def nm(base, i):
    return base if i == 0 else f"{base}_{i}"


class UNet(nn.Module):
    def __init__(self, w, conv0, tconv0, bn0):
        super().__init__()
        self.enc, self.enc_bn, self.dec, self.dec_bn = nn.ModuleList(), nn.ModuleList(), nn.ModuleList(), nn.ModuleList()
        bn = lambda i: self._bn(w, nm("batch_normalization", bn0 + i))
        for i in range(6):
            k = w[nm("conv2d", conv0 + i) + "/kernel"]
            c = nn.Conv2d(k.shape[2], k.shape[3], 5, stride=2)
            c.weight.data = torch.tensor(k.transpose(3, 2, 0, 1)); c.bias.data = torch.tensor(w[nm("conv2d", conv0 + i) + "/bias"])
            self.enc.append(c); self.enc_bn.append(bn(i))
        for i in range(6):
            k = w[nm("conv2d_transpose", tconv0 + i) + "/kernel"]  # H W out in
            c = nn.ConvTranspose2d(k.shape[3], k.shape[2], 5, stride=2)
            c.weight.data = torch.tensor(k.transpose(3, 2, 0, 1)); c.bias.data = torch.tensor(w[nm("conv2d_transpose", tconv0 + i) + "/bias"])
            self.dec.append(c); self.dec_bn.append(bn(6 + i))
        k = w[nm("conv2d", conv0 + 6) + "/kernel"]
        self.final = nn.Conv2d(k.shape[2], k.shape[3], 4, dilation=2)
        self.final.weight.data = torch.tensor(k.transpose(3, 2, 0, 1)); self.final.bias.data = torch.tensor(w[nm("conv2d", conv0 + 6) + "/bias"])

    @staticmethod
    def _bn(w, name):
        b = nn.BatchNorm2d(len(w[name + "/gamma"]), eps=1e-3)
        b.weight.data = torch.tensor(w[name + "/gamma"]); b.bias.data = torch.tensor(w[name + "/beta"])
        b.running_mean.data = torch.tensor(w[name + "/moving_mean"]); b.running_var.data = torch.tensor(w[name + "/moving_variance"])
        return b

    def forward(self, x):  # x: NCHW
        skips, h = [], x
        for i in range(6):
            c = self.enc[i](F.pad(h, (1, 2, 1, 2)))  # TF 'same', stride 2, k=5
            skips.append(c)
            h = F.leaky_relu(self.enc_bn[i](c), 0.2)
        h = skips[5]  # decoder starts from conv6 (pre-BN), as in Spleeter
        for i in range(6):
            u = self.dec[i](h)
            H, W = u.shape[2] - 3, u.shape[3] - 3
            u = u[:, :, 1:1 + H, 1:1 + W]  # crop to TF 'same' output size (2x input)
            u = self.dec_bn[i](F.relu(u))
            if i < 5:
                h = torch.cat([skips[4 - i], u], dim=1)
            else:
                h = u
        m = torch.sigmoid(self.final(F.pad(h, (3, 3, 3, 3))))
        return m * x


class Spleeter2(nn.Module):
    def __init__(self, w):
        super().__init__()
        self.vocals = UNet(w, 0, 0, 0)
        self.accompaniment = UNet(w, 7, 6, 12)

    def forward(self, mix_mag):  # [B, T, F, 2]
        x = mix_mag.permute(0, 3, 1, 2)
        v = self.vocals(x).permute(0, 2, 3, 1)
        a = self.accompaniment(x).permute(0, 2, 3, 1)
        return v, a


def tf_reference(w, x, conv0, tconv0, bn0):
    """Independent pure-TF forward pass (Keras-style 'same' padding)."""
    def bn(h, i):
        n = nm("batch_normalization", bn0 + i)
        return tf.nn.batch_normalization(h, w[n + "/moving_mean"], w[n + "/moving_variance"], w[n + "/beta"], w[n + "/gamma"], 1e-3)
    skips, h = [], x
    for i in range(6):
        n = nm("conv2d", conv0 + i)
        c = tf.nn.conv2d(h, w[n + "/kernel"], 2, "SAME") + w[n + "/bias"]
        skips.append(c)
        h = tf.nn.leaky_relu(bn(c, i), 0.2)
    h = skips[5]
    for i in range(6):
        n = nm("conv2d_transpose", tconv0 + i)
        k = w[n + "/kernel"]
        shp = tf.shape(h)
        out = tf.stack([shp[0], shp[1] * 2, shp[2] * 2, k.shape[2]])
        u = tf.nn.conv2d_transpose(h, k, out, 2, "SAME") + w[n + "/bias"]
        u = bn(tf.nn.relu(u), 6 + i)
        h = tf.concat([skips[4 - i], u], -1) if i < 5 else u
    n = nm("conv2d", conv0 + 6)
    m = tf.sigmoid(tf.nn.conv2d(h, w[n + "/kernel"], 1, "SAME", dilations=2) + w[n + "/bias"])
    return (m * x).numpy()


def main(ckpt, out):
    w = load(ckpt)
    model = Spleeter2(w).eval()
    rng = np.random.default_rng(0)
    x = np.abs(rng.normal(size=(1, T, FBINS, 2))).astype(np.float32) * 2.0
    with torch.no_grad():
        v, a = model(torch.tensor(x))
    rv = tf_reference(w, x, 0, 0, 0)
    ra = tf_reference(w, x, 7, 6, 12)
    err = max(np.abs(v.numpy() - rv).max(), np.abs(a.numpy() - ra).max())
    print(f"max abs difference vs TensorFlow reference: {err:.3e}")
    assert err < 1e-3, "conversion mismatch"
    torch.onnx.export(model, (torch.tensor(x),), out, input_names=["mix_mag"], output_names=["vocals", "accompaniment"],
                      opset_version=17, dynamo=False)
    import onnxruntime as ort
    s = ort.InferenceSession(out)
    ov, oa = s.run(None, {"mix_mag": x})
    print(f"ONNX vs TensorFlow: {max(np.abs(ov - rv).max(), np.abs(oa - ra).max()):.3e}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
