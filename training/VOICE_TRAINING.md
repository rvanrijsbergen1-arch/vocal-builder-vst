# Training the included voice (owner-side, done once per voice)

Customers never train anything. You produce a voice package once and the installer ships it.

## Why train a base model ourselves
RVC voices are normally fine-tuned from RVC's pretrained base models. Those base files are
distributed with a "research use only" clause, so a voice derived from them isn't safe to sell.
So we train our own base model **from scratch** on CC BY 4.0 speech, then fine-tune it on your
singer. The network architecture and training code are RVC's (MIT).

## What you need
- A Linux or Windows PC or cloud machine with an NVIDIA GPU (24 GB VRAM recommended) and about 500 GB free disk.
- The singer recordings (see `docs/RECORDING_BRIEF.md`), checked with `check_recordings.py`.
- Datasets (all CC BY 4.0): VCTK, Multilingual LibriSpeech **Dutch** (opus version ≈ 23 GB), and the
  MLS English subset or LibriTTS-R.
- ContentVec legacy checkpoint (MIT) from the official ContentVec repository.

Costs: only GPU time if you rent one. Run the pilot in step 2 first and measure how long it takes,
then multiply by the hourly price. I don't give a duration estimate until it has been measured.

## Quick path for personal voices (Applio)
1. One folder per singer, never mixed: `python training/prepare_dataset.py --models <install>/models/separation --out <dataset folder> <dry vocal wavs of that singer>`.
   It removes doubles/harmonies (lead only), cuts at silences and reports the minutes left. Aim for 10+ minutes.
2. In Applio: Training tab, sampling rate 40000, vocoder **HiFi-GAN** (the plugin only supports HiFi-GAN),
   pitch extraction rmvpe, embedder contentvec, Audio cutting "Skip" (already sliced), CPU cores 4 on Windows
   (higher can exhaust the page file), laptop on the charger.
3. Epochs: about 200-300 for 10 minutes; save every 25 and compare by ear.
4. Export: `python tools/models/export_rvc_voice.py --rvc <rvc repo> --ckpt <model>.pth --index <model>.index --id <id> --name "<name>" --out <folder>`,
   then "Add voice..." in the plugin.

## Steps
1. **Check the recordings**: `python training/check_recordings.py <folder>`. Fix every file marked FIX.
2. **Pilot**: train the base model for 1 hour on 10 hours of data and note the steps per hour.
   This tells you the real cost before committing.
3. **Encoder**: convert the ContentVec checkpoint to the Hugging Face `HubertModel` folder layout RVC
   loads from `assets/hubert_base`. Training and the plugin then use exactly the same features.
4. **Base model from scratch**: RVC v2 architecture, 40 kHz, ContentVec 768-dim features, FCPE f0,
   **no** `pretrainG/pretrainD` (leave them empty). Use 50–100 hours of VCTK + MLS-Dutch + MLS-English.
   Keep the speaker-ID embedding large enough for the number of speakers.
5. **Fine-tune on the singer**: start from your base model, train on the singer's NL and EN folders.
   Keep the validation folder out of training.
6. **Listen and measure**: convert the validation phrases and the 3–5 test vocals by another singer.
   Check how clear the words are in Dutch and English, pitch stability, timing and artifacts. An
   optional extra check is to transcribe the outputs with an open speech recogniser (for example
   Whisper, MIT) and compare word error rates between the source and the converted vocal.
7. **Package**:
   ```
   python tools/models/export_encoder.py contentvec-768 assets/hubert_base - out/encoders/contentvec-768.onnx
   python tools/models/export_rvc_voice.py --rvc <rvc_repo> --ckpt G_final.pth --config 40k_v2.json --version v2 \
       --features total_fea.npy --id nova --name "Nova" --description "Bright female pop voice" \
       --languages en nl --tags "Powerful Female" Pop --licence "Licensed from <singer>, 2026" --out out/voices/nova
   ```
   Zip `out/` (encoders + voices) and give its private download link to the CI as the
   `VOICE_PACKAGE_URL` secret. The next build includes the voice.
8. **Regression check**: `PluginWorkflowTest` runs the whole chain on Windows in CI; add the
   validation phrases to your own listening checklist for every voice update.

## Honest expectations
- Pronunciation follows the source singer. The voice changes timbre, so a weak Dutch source stays weak.
- Without Dutch in the base model or the singer data, Dutch vowels such as ui, eu and ij tend to drift
  towards English. That is why both languages are in the brief.
- Very dense or heavily processed source vocals produce artifacts, because separation errors carry through.
