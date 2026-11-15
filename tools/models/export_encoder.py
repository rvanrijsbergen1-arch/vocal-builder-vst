#!/usr/bin/env python3
"""Export a content encoder to ONNX for the VocalEngine.

Supported:
  contentvec-768 : ContentVec "legacy" checkpoint (MIT, github.com/auspicious3000/contentvec),
                   12th-layer 768-dim features (RVC v2 style).  Needs fairseq to load.
  hubert-soft-256: bshall/hubert soft units (MIT code). Used here for pipeline testing.

Output contract: input "audio" [1, N] float32 mono 16 kHz -> output "units" [1, frames, D], ~50 frames/s.
Usage: python export_encoder.py hubert-soft-256 <hubert-soft-0d54a1f4.pt> <bshall_hubert_repo> <out.onnx>
       python export_encoder.py contentvec-768 <checkpoint_best_legacy_500.pt> - <out.onnx>
"""
import sys
import numpy as np
import torch
import torch.nn.functional as F

torch.backends.mha.set_fastpath_enabled(False)  # the fused fast path cannot be exported to ONNX


class HubertSoftWrapper(torch.nn.Module):
    def __init__(self, ckpt, repo):
        super().__init__()
        sys.path.insert(0, repo)
        from hubert.model import HubertSoft
        self.m = HubertSoft()
        sd = torch.load(ckpt, map_location="cpu")
        sd = sd.get("hubert", sd)
        sd = {k.replace("module.", ""): v for k, v in sd.items()}
        self.m.load_state_dict(sd)
        self.m.eval()

    def forward(self, audio):
        wav = F.pad(audio[:, None], (40, 40))
        return self.m.units(wav)


class ContentVecWrapper(torch.nn.Module):
    """ContentVec 12th-layer features. Accepts either the original fairseq checkpoint (needs fairseq)
    or a Hugging Face `transformers` HubertModel folder converted from it (the layout RVC's
    training code expects in assets/hubert_base), so training and inference use identical features."""

    def __init__(self, ckpt):
        super().__init__()
        from pathlib import Path
        self.hf = Path(ckpt).is_dir()
        if self.hf:
            from transformers import HubertModel
            self.m = HubertModel.from_pretrained(ckpt).eval()
        else:
            from fairseq import checkpoint_utils  # noqa: needs fairseq installed
            models, _, _ = checkpoint_utils.load_model_ensemble_and_task([ckpt], suffix="")
            self.m = models[0].eval()

    def forward(self, audio):
        if self.hf:
            return self.m(audio).last_hidden_state
        feats, _ = self.m.extract_features(source=audio, padding_mask=torch.zeros_like(audio, dtype=torch.bool), output_layer=12)
        return feats


def main(kind, ckpt, repo, out):
    w = HubertSoftWrapper(ckpt, repo) if kind == "hubert-soft-256" else ContentVecWrapper(ckpt)
    x = torch.randn(1, 16000 * 2) * 0.1
    with torch.no_grad():
        ref = w(x).numpy()
    # The dynamo exporter keeps the attention reshapes symbolic, so any audio length works.
    n = torch.export.Dim("n", min=16000, max=16000 * 60)
    prog = torch.onnx.export(w, (x,), input_names=["audio"], output_names=["units"],
                             dynamic_shapes={"audio": {1: n}}, opset_version=18, dynamo=True)
    prog.save(out)
    import onnxruntime as ort
    got = ort.InferenceSession(out).run(None, {"audio": x.numpy()})[0]
    print(kind, "shape", got.shape, "max |diff| vs torch", float(np.abs(got - ref).max()))
    x2 = torch.randn(1, 16000 * 7 + 4800) * 0.1
    with torch.no_grad():
        ref2 = w(x2).numpy()
    got2 = ort.InferenceSession(out).run(None, {"audio": x2.numpy()})[0]
    print("other length:", got2.shape, "max |diff|", float(np.abs(got2 - ref2).max()))


if __name__ == "__main__":
    main(*sys.argv[1:5])
