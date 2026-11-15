# Status — version 0.1.0 (15 November 2026, updated 15:30)

## What exists
| Item | State |
|---|---|
| Source code (plugin, engine, licensing, model tools, installer script, CI) | Written |
| Real AI processing | **Separation: real** (Spleeter, verified). **Conversion: the full pipeline is real, but there is no trained voice yet** (see blockers) |
| Compiled **Linux** VST3 | Built and passes pluginval at strictness 10, with GUI under a virtual display |
| Compiled **Windows** VST3 | **Built** by GitHub Actions (windows-2022); passes pluginval strictness 10 on Windows |
| Windows installer | **Built**: `VocalBuilder-0.1.0-win64-setup.exe` (CI artifact). Not yet installed on a real PC by me |
| Tested in FL Studio | **Not yet verified** |

## Implemented and tested (Linux x64, in this build environment)
- VocalEngine helper: local separation with Spleeter 2-stem in ONNX Runtime, no network.
  The C++ pipeline matches a TensorFlow reference of Spleeter to 133 dB SNR. Vocal + instrumental
  reconstruct the mix exactly.
- Voice conversion pipeline: content encoder → FCPE melody → RVC synthesiser, chunked at quiet
  points. Output is sample-exactly the same length as the input. The C++ melody front end matches
  torchfcpe (100 % voicing agreement, < 0.01 cent). It was run end to end with an **untrained test
  network**, so plumbing and timing are verified, not sound quality.
- Plugin workflow test (the real processor code, headless): import, section extraction, conversion,
  preview through `processBlock` with A/B and loudness matching at a different host sample rate,
  export aligned to the song start (48 kHz / 24-bit, silence before the section) and section-only
  export (16-bit, start time in the file name), cancelling, corrupt-model error, original file
  unchanged, separate folders per instance, closing during processing. **All pass.**
- Licensing: Ed25519 keys and machine-bound activation codes. Python owner tool ↔ C++ verification
  cross-tested (15/15). Full activation flow exercised. Engine refuses to process without activation.
- Unicode file paths (é, ü, en dash).

## Verified on Windows (GitHub Actions, windows-2022, CPU)
- VST3 + engine compile with MSVC; pluginval strictness 10 (incl. editor tests): pass.
- Real separation, the conversion pipeline (untrained test voice), preview, exports, cancel,
  corrupt-model error and teardown during processing: the full PluginWorkflowTest passes (0 failures).
- ContentVec-768 encoder exported to ONNX on the runner (matches PyTorch within 3e-4) and shipped.
- Installer compiled with Inno Setup.

## Implemented, awaiting verification
- Loading and scanning in FL Studio, preview on an FL mixer insert, drag-and-drop from the plugin
  into the playlist.
- Installation on a clean Windows PC and processing with the network disabled.
- Help popovers on touch screens (they use clicks, never hover; not tried on a touch device).

## Blockers
1. **No trained voice exists yet.** Personal-use build: the voice will be trained from the owner's own
   dry vocals on his NVIDIA RTX PC (Applio / RVC v2, ContentVec, 40k or 48k), then converted with
   `tools/models/export_rvc_voice.py` and added in the plugin with "Add voice...". Original note: None of the pretrained voices or base models
   checked can be redistributed commercially (see LICENCE_INVENTORY.md). Next step: recordings per
   RECORDING_BRIEF.md plus a signed singer agreement, then GPU training per training/VOICE_TRAINING.md.
   Until then the plugin says "No voice installed" and the Convert button stays disabled.
2. ~~Windows binaries~~ solved: built by CI in the private repo rvanrijsbergen1-arch/vocal-builder-vst.

## Measured performance (Linux, 2-core Intel Xeon 2.1 GHz VM, CPU only)
| Task | Audio | Time | Peak memory |
|---|---|---|---|
| Separation | 10.9 s @ 44.1 kHz (1 thread) | 1.1 s | 535 MB |
| Separation | 4:00 @ 44.1 kHz | 13.2 s | 1.0 GB |
| Separation | 4:00 @ 48 kHz (includes resampling) | 23.9 s | 1.3 GB |
| Conversion (RVC v1-size network, HuBERT-soft encoder) | 4:00 | 203 s | 1.6 GB |
Windows numbers have not been measured yet. The real voice (ContentVec-768, RVC v2) has a similar
model size; it will be measured once trained.

## Supported baseline (proposed, to be confirmed on Windows)
Windows 10/11 64-bit · x64 CPU with AVX2, 4+ cores · 8 GB RAM (16 GB recommended) · ~1.5 GB disk ·
FL Studio 21 or newer (any VST3 host should work). GPU acceleration is not used in 0.1.0. DirectML
(any DirectX 12 GPU) is the candidate and will only be added after it is measured to help.

## Planned
- Option B online activation service (optional).
- GPU acceleration (DirectML) after measurement.
- De-reverb and lead/backing-vocal split, only with commercially licensed models.
- New Vocal Performance mode (lyrics and notes → singing), separately, with a licensed singing-synthesis engine.
- macOS / AU builds.
- Code signing certificate for the installer (cost, your decision), so Windows SmartScreen doesn't warn.
