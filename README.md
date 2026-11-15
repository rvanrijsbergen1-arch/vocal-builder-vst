# Vocal Builder (VST3)

Native VST3 plugin for FL Studio on Windows: drop a WAV or MP3 → extract the vocal → choose the
included AI voice → convert → preview A/B → export a dry WAV aligned to the song.
All processing runs **locally**: no API, no credits, no upload, no internet after activation.

**Current state: see [docs/STATUS.md](docs/STATUS.md).** The Windows build and the trained voice are
not finished yet. Nothing in this repository should be described as ready to install until the CI
has produced and tested a Windows installer.

## How it fits together
```
FL Studio ── Vocal Builder.vst3 (JUCE, C++)
              │  UI, waveform, section selection, A/B preview in processBlock, export, licence
              │  starts a helper process for heavy work (DAW audio thread never blocked)
              ▼
           VocalEngine.exe (C++, ONNX Runtime, CPU)   ← installed and managed by the installer
              ├─ separation : Spleeter 2-stem (MIT)                 models/separation/
              ├─ melody     : FCPE (MIT)                            models/pitch/
              ├─ features   : ContentVec (MIT)                      models/encoders/
              └─ voice      : RVC-architecture voice, our weights   models/voices/<id>/
```
Each plugin instance has its own working folder. Closing the window keeps the job running. The
engine runs below normal priority, leaves one CPU core free, and writes files atomically, so
cancelling never leaves half-written files.

## Layout
| Path | What |
|---|---|
| `plugin/` | VST3 plugin (processor, editor, engine job runner, session) |
| `engine/` | VocalEngine helper (separation, conversion, export) |
| `common/` | Licence verification shared by plugin and engine; `PublicKey.h` |
| `tools/licence_admin.py` | **Owner tool**: issue, activate, transfer, revoke and export licences |
| `tools/models/` | Reproducible model conversion and export scripts (ONNX) |
| `training/` | Voice training guide and recording checker |
| `installer/VocalBuilder.iss` | Windows installer (Inno Setup) |
| `.github/workflows/build-windows.yml` | Windows build, tests, pluginval, installer |
| `docs/` | Status, licence inventory, recording brief, licensing guide |

## Building
Windows (CI does exactly this): see the workflow. Locally: Visual Studio 2022, CMake ≥ 3.22,
JUCE 8.0.10 in `external/JUCE`, ONNX Runtime 1.20.1 (win-x64) in `external/onnxruntime`, then:
```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target VocalBuilder_VST3 VocalEngine
```
Linux (development): the same with `-G Ninja` and the linux-x64 ONNX Runtime package.

Before selling: run `python tools/licence_admin.py init` and replace the development key in
`common/PublicKey.h` (the release workflow refuses to build tagged releases with the development key).
