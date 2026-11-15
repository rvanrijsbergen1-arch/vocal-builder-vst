# Component and model licence inventory

Checked November 2026. "OK" means the licence allows the intended use: a closed-source commercial
plugin, sold per customer, with the files redistributed in the installer. This is an engineering
review, not legal advice. Have a lawyer confirm before selling.

## Shipped in the product

| Component | Role | Code licence | Weights licence | Redistribution | Runs offline | Notes |
|---|---|---|---|---|---|---|
| JUCE 8 | Plugin framework | JUCE 8 EULA, Starter tier, or AGPLv3 | – | OK under Starter | yes | Starter tier is free up to USD 20,000 revenue/funding in the previous 12 months. Above that you need Indie (USD 40/month or USD 800 perpetual) or Pro. No splash screen required. |
| Steinberg VST 3 SDK 3.8 (inside JUCE) | VST3 format | MIT (since Oct 2025) | – | OK | yes | Keep the MIT notice. The "VST" logo is optional and has its own trademark rules. |
| ONNX Runtime 1.20.1 (CPU) | Neural network inference | MIT | – | OK | yes | Ships as `onnxruntime.dll`. |
| Monocypher 4 | Ed25519 licence signatures | BSD-2-Clause or CC0 | – | OK | yes | |
| Spleeter 2-stem model (Deezer) | Vocal / instrumental separation | MIT | MIT. Deezer's JOSS paper states that the code **and pre-trained models** are MIT. | OK | yes | Converted to ONNX by `tools/models/convert_spleeter.py` and checked against TensorFlow (max error 5.7e-6). Training data was Deezer's internal catalogue and was not released. |
| FCPE (torchfcpe 0.0.4, bundled `fcpe_c_v001`) | Melody (f0) tracking | MIT | MIT. The weights ship inside the MIT-licensed PyPI package. | OK | yes | Its training data is not documented. The MIT grant from the authors covers the weights. |
| RVC synthesiser architecture (VITS + NSF-HiFiGAN) | Voice conversion network | MIT (GitHub repo) | **Our own weights.** The official pretrained base and HuBERT files are not used (see below). | OK | yes | The GitHub repo's companion file adds that users are responsible for the voices they produce. |
| ContentVec "legacy" (auspicious3000) | Content features for the real voice | MIT | Same repository, MIT | OK | yes | Download from the official link (ibm.box.com) when training. Exported with `tools/models/export_encoder.py contentvec-768`. |
| Microsoft VC++ redistributable | C++ runtime for onnxruntime.dll | Microsoft redistributable licence | – | OK (explicitly redistributable) | yes | |
| Inno Setup 6 | Installer builder (not shipped) | Inno Setup licence (free, commercial use allowed) | – | – | – | |

## Evaluated and rejected

| Candidate | Why it is not used |
|---|---|
| Demucs v4 / htdemucs (Meta) | The code is MIT. For the v3/v4 weights, the author has said in the project's issues that they are not covered by MIT and are for research, citing the MusDB terms. Mirrors now label them MIT, but the question is unresolved. Better quality than Spleeter, but too risky for a paid product. |
| BS-RoFormer / Mel-RoFormer (UVR community) | The weights are CC BY-NC-SA 4.0, so no commercial use. |
| UVR MDX-Net models | Licensing of the trained weights is unclear or inconsistent. |
| RVC pretrained base models, `hubert_base.pt`, `rmvpe.pt` (lj1995 on Hugging Face) | The licence file distributed with them adds "for research use only". A voice fine-tuned from these bases would inherit that problem. |
| RMVPE (yxlllc) | No licence published. It was trained partly on M4Singer (CC BY-NC-SA). |
| Kits.ai / Controlla / other APIs | Cloud services with per-use credits. That breaks the "offline, no credits" requirement. |
| Celebrity or community voice models | No consent or licence from the singer. |

## Voice training data

| Dataset | Licence | Use |
|---|---|---|
| Your singer's recordings | Written agreement (see RECORDING_BRIEF.md) | The included voice |
| VCTK (English speech, 110 speakers) | CC BY 4.0 | Base model (from scratch) |
| Multilingual LibriSpeech, Dutch subset | CC BY 4.0 | Base model: Dutch pronunciation |
| LibriTTS-R or the MLS English subset | CC BY 4.0 | Base model: English |

CC BY 4.0 requires attribution: list these datasets in the plugin's About text and in the licences folder.
