#!/usr/bin/env python3
"""Package a trained RVC-architecture voice as a Vocal Builder voice (synth.onnx + voice.json [+ index.f32]).

The synthesiser code comes from RVC (MIT, github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI);
pass the path of a checkout with --rvc. Do NOT package voices fine-tuned from the RVC Hugging Face
pretrained weights for sale: those files carry a research-only clause. Use a base model trained with
training/train_base.md instead.

Usage (real voice):
  python export_rvc_voice.py --rvc <rvc_repo> --ckpt G_final.pth --config 40k.json --version v2 \
      --features total_fea.npy --id nova --name "Nova" --description "Bright female pop voice" \
      --languages en nl --tags "Powerful Female" Pop --licence "Recorded and licensed by <singer>, 2026" --out <models>/voices/nova

Usage (pipeline test only; produces noise, flagged test_only so the plugin never lists it):
  python export_rvc_voice.py --rvc <rvc_repo> --untrained-test --config 40k.json --version v1 --id test-untrained --out <dir>
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import torch


def patch_attention_for_export():
    """VITS relative attention casts sequence lengths to Python ints, which freezes them during ONNX
    tracing. These equivalents keep the lengths symbolic (valid for sequences longer than the
    attention window, which the engine guarantees: chunks are always >= 100 frames)."""
    import torch.nn.functional as F
    from infer.module import attentions

    def get_rel(self, rel, length):
        pad = length - (self.window_size + 1)
        return F.pad(rel, [0, 0, pad, pad, 0, 0])

    def rel_to_abs(self, x):
        b, h, length, _ = x.size()
        x = F.pad(x, [0, 1, 0, 0, 0, 0, 0, 0])
        x_flat = x.view([b, h, length * 2 * length])
        x_flat = F.pad(x_flat, [0, length - 1, 0, 0, 0, 0])
        return x_flat.view([b, h, length + 1, 2 * length - 1])[:, :, :length, length - 1:]

    def abs_to_rel(self, x):
        b, h, length, _ = x.size()
        x = F.pad(x, [0, length - 1, 0, 0, 0, 0, 0, 0])
        x_flat = x.view([b, h, length * length + length * (length - 1)])
        x_flat = F.pad(x_flat, [length, 0, 0, 0, 0, 0])
        return x_flat.view([b, h, length, 2 * length])[:, :, :, 1:]

    attentions.MultiHeadAttention._get_relative_embeddings = get_rel
    attentions.MultiHeadAttention._relative_position_to_absolute_position = rel_to_abs
    attentions.MultiHeadAttention._absolute_position_to_relative_position = abs_to_rel


def build_from_list(rvc, conf, version):
    """RVC/Applio inference .pth files carry their hyper-parameters as a list under 'config'."""
    sys.path.insert(0, rvc)
    patch_attention_for_export()
    from infer.module.models import SynthesizerTrnMs256NSFsid, SynthesizerTrnMs768NSFsid
    cls = SynthesizerTrnMs768NSFsid if version == "v2" else SynthesizerTrnMs256NSFsid
    conf = list(conf)
    sr = conf[-1]
    if isinstance(sr, str):
        sr = int(sr.replace("k", "000"))
    return cls(*conf[:-1], sr, is_half=False), sr


def build(rvc, cfg, version, sr):
    sys.path.insert(0, rvc)
    patch_attention_for_export()
    from infer.module.models import SynthesizerTrnMs256NSFsid, SynthesizerTrnMs768NSFsid
    cls = SynthesizerTrnMs768NSFsid if version == "v2" else SynthesizerTrnMs256NSFsid
    m = cfg["model"]
    d = cfg["data"]
    net = cls(d["filter_length"] // 2 + 1, cfg["train"]["segment_size"] // d["hop_length"], m["inter_channels"], m["hidden_channels"],
              m["filter_channels"], m["n_heads"], m["n_layers"], m["kernel_size"], m["p_dropout"], m["resblock"],
              m["resblock_kernel_sizes"], m["resblock_dilation_sizes"], m["upsample_rates"], m["upsample_initial_channel"],
              m["upsample_kernel_sizes"], m["spk_embed_dim"], m["gin_channels"], sr, is_half=False)
    return net


class Export(torch.nn.Module):
    """Deterministic-noise inference wrapper: the engine passes `rnd` so results are reproducible."""

    def __init__(self, net):
        super().__init__()
        self.n = net

    def forward(self, phone, phone_lengths, pitch, pitchf, ds, rnd):
        g = self.n.emb_g(ds).unsqueeze(-1)
        m_p, logs_p, x_mask = self.n.enc_p(phone, pitch, phone_lengths)
        z_p = (m_p + torch.exp(logs_p) * rnd * 0.66666) * x_mask
        z = self.n.flow(z_p, x_mask, g=g, reverse=True)
        return self.n.dec(z * x_mask, pitchf, g=g)


def kmeans_bank(feats, k=4000, iters=10):
    if len(feats) <= k:
        return feats
    rng = np.random.default_rng(0)
    c = feats[rng.choice(len(feats), k, replace=False)].copy()
    for _ in range(iters):
        d = (feats ** 2).sum(1)[:, None] - 2 * feats @ c.T + (c ** 2).sum(1)[None]
        a = d.argmin(1)
        for j in range(k):
            sel = feats[a == j]
            if len(sel):
                c[j] = sel.mean(0)
    return c


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--rvc", required=True)
    p.add_argument("--config", help="training config json (not needed when the .pth contains its config)")
    p.add_argument("--version", choices=["v1", "v2"], default="v2")
    p.add_argument("--ckpt")
    p.add_argument("--untrained-test", action="store_true")
    p.add_argument("--features", help="RVC total_fea.npy (optional, enables Voice Character)")
    p.add_argument("--index", help="RVC/Applio .index file (faiss); used instead of --features")
    p.add_argument("--id", required=True)
    p.add_argument("--name", default="")
    p.add_argument("--description", default="")
    p.add_argument("--languages", nargs="*", default=[])
    p.add_argument("--tags", nargs="*", default=[])
    p.add_argument("--licence", default="")
    p.add_argument("--encoder", default=None)
    p.add_argument("--out", required=True)
    a = p.parse_args()

    ck = None if a.untrained_test else torch.load(a.ckpt, map_location="cpu", weights_only=False)
    if ck is not None and isinstance(ck.get("config"), (list, tuple)):
        a.version = ck.get("version", a.version) or a.version
        if ck.get("f0", 1) != 1:
            sys.exit("This model was trained without pitch (f0). Vocal Builder needs a pitch-guided (f0) model.")
        net, sr = build_from_list(a.rvc, ck["config"], a.version)
        hop = 480 if sr == 48000 else 400 if sr == 40000 else 320
        if ck["config"][12] is not None:  # upsample_rates -> hop length
            hop = int(np.prod(ck["config"][12]))
    else:
        cfg = json.loads(Path(a.config).read_text())
        sr = cfg["data"]["sampling_rate"]
        hop = cfg["data"]["hop_length"]
        net = build(a.rvc, cfg, a.version, sr)
    dim = 768 if a.version == "v2" else 256
    if a.untrained_test:
        torch.manual_seed(0)
    else:
        sd = ck.get("weight", ck.get("model", ck))
        res = net.load_state_dict({k: v.float() for k, v in sd.items() if not k.startswith("enc_q")}, strict=False)
        missing = [k for k in res.missing_keys if not k.startswith("enc_q")]
        if missing or res.unexpected_keys:
            sys.exit(f"Checkpoint does not match the RVC architecture. Missing: {missing[:8]} Unexpected: {res.unexpected_keys[:8]}")
    net.eval()
    net.remove_weight_norm()
    del net.enc_q
    exp = Export(net).eval()

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    T = 200
    args = (torch.randn(1, T, dim), torch.tensor([T]), torch.randint(1, 255, (1, T)), torch.rand(1, T) * 300 + 100,
            torch.tensor([0]), torch.randn(1, 192, T))
    torch.onnx.export(exp, args, str(out / "synth.onnx"),
                      input_names=["phone", "phone_lengths", "pitch", "pitchf", "ds", "rnd"], output_names=["audio"],
                      dynamic_axes={"phone": {1: "t"}, "pitch": {1: "t"}, "pitchf": {1: "t"}, "rnd": {2: "t"}, "audio": {2: "n"}},
                      opset_version=17, dynamo=False)
    f = None
    if a.index:
        import faiss
        ix = faiss.read_index(a.index)
        f = ix.reconstruct_n(0, ix.ntotal).astype(np.float32)
    elif a.features:
        f = np.load(a.features).astype(np.float32)
    if f is not None:
        bank = kmeans_bank(f)
        with open(out / "index.f32", "wb") as fh:
            np.array([len(bank), bank.shape[1]], np.int32).tofile(fh)
            bank.astype(np.float32).tofile(fh)
    meta = {"id": a.id, "name": a.name or a.id, "description": a.description, "languages": a.languages, "tags": a.tags,
            "licence": a.licence, "encoder": a.encoder or ("contentvec-768" if a.version == "v2" else "hubert-soft-256"),
            "sample_rate": sr, "hop": hop, "feature_dim": dim, "test_only": bool(a.untrained_test),
            "architecture": f"rvc-{a.version}-nsf"}
    (out / "voice.json").write_text(json.dumps(meta, indent=2))
    import onnxruntime as ort
    s = ort.InferenceSession(str(out / "synth.onnx"))
    y = s.run(None, {"phone": args[0].numpy(), "phone_lengths": args[1].numpy().astype(np.int64), "pitch": args[2].numpy().astype(np.int64),
                     "pitchf": args[3].numpy(), "ds": args[4].numpy().astype(np.int64), "rnd": args[5].numpy()})[0]
    print("synth.onnx ok:", y.shape, f"= {T} frames x hop {hop}" if y.shape[-1] == T * hop else "UNEXPECTED LENGTH")
    T2 = 1237
    y2 = s.run(None, {"phone": np.random.randn(1, T2, dim).astype(np.float32), "phone_lengths": np.array([T2], dtype=np.int64),
                      "pitch": np.random.randint(1, 255, (1, T2)).astype(np.int64), "pitchf": (np.random.rand(1, T2) * 300 + 100).astype(np.float32),
                      "ds": np.array([0], dtype=np.int64), "rnd": np.random.randn(1, 192, T2).astype(np.float32)})[0]
    print("dynamic length:", y2.shape, "ok" if y2.shape[-1] == T2 * hop else "UNEXPECTED LENGTH")


if __name__ == "__main__":
    main()
