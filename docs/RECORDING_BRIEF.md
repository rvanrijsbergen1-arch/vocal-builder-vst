# Recording brief: the included Vocal Builder voice

This brief is for the RVC-style voice model the plugin uses (content features + melody → singer's
timbre). The model learns **timbre and pronunciation habits** from these recordings. It does not
learn melodies or lyrics, so cover the sounds and the range rather than particular songs.

## 1. Permission first
Before recording, sign a written agreement with the singer that covers:
- training an AI voice model from the recordings;
- selling that model inside a commercial plugin, worldwide, and for how long;
- the name or description shown in the plugin (a real name, or an anonymous label such as "Nova");
- payment (fixed fee and/or royalty) and credit;
- what happens if they later want to stop (copies already sold cannot be recalled);
- that the singer is not responsible for what customers make with it, and any limits you agree on.

Have a lawyer review it. Keep the signed agreement with the recordings.

## 2. How much
| Part | Duration | Notes |
|---|---|---|
| Dutch singing | 30–45 min | Full phrases at normal performance level |
| English singing | 30–45 min | Same |
| Range and technique set | 10–15 min | See section 4 |
| Spoken reading, Dutch and English | 5–10 min | Helps consonants; optional |
| **Validation set (never used for training)** | 5 min | 10 Dutch and 10 English phrases |

At least 30 minutes of clean singing is the minimum for a usable model. 60–90 minutes gives
noticeably more stable results across styles. More data helps only if it is just as clean.

## 3. Dry and clean
- Mono, **48 kHz, 24-bit WAV**. Peaks around −6 dBFS, never clipped.
- No reverb, delay, compression, EQ, de-essing, pitch correction, doubling or harmonies. Raw takes only.
- A quiet, treated room. The noise floor between phrases should be below −60 dBFS. No fans or air conditioning.
- One microphone, same position and distance (15–20 cm, pop filter) for every session. Note the mic model.
- Closed-back headphones, with the backing track low, so it doesn't bleed into the mic.
- Leave about 0.5 s of silence before and after each phrase. Keep breaths natural; don't edit them out.
- File names: `NL_001.wav`, `EN_001.wav`, `RANGE_001.wav`, `VAL_NL_01.wav` …, plus a text file with the lyrics of each.

## 4. Range, pitch and expression coverage
- Chromatic scales on each vowel from the lowest comfortable note to the highest. Do the open vowels
  a, e, i, o, u, and the Dutch ones too: ui, eu, ij/ei, ou/au, oe, uu.
- Slow glides (sirens) up and down, and sustained notes of 3–4 s at low, mid and high pitch.
- Dynamics: soft and breathy, normal, and full belt, on the same lines.
- Styles you will use: euphoric hardstyle toplines (long high sustained chorus notes), rawstyle
  intensity (grit only if the singer can do it safely), Dutch party/meezing style (bright, rhythmic,
  clear consonants).
- Fast lyrics as well as slow ones, so consonant clusters are covered (Dutch: "schreeuw", "sprong",
  "lichtjes"; English: "strengths", "twelfths").
- Dutch g/ch, r variants (as the singer naturally speaks), and the English th, w and v sounds.

## 5. What to sing
Use material the singer has the right to perform for this purpose: their own songs, public-domain
melodies, or phonetically rich phrases written for the session. Popular copyrighted songs aren't
needed. Melodies and lyrics don't matter to the model; coverage of sounds and range does.

## 6. Validation material
- The 20 validation phrases (section 2), recorded in the same session and kept out of training.
- 3–5 test vocals sung by **someone else** (for example you), in Dutch and English, in the target
  genres. These are what we convert to judge clarity of words, pitch stability, timing and artifacts.

## 7. Delivery
A folder with all WAVs, the lyric text files, a session note (mic, room, dates) and a copy of the
signed agreement. Send it as a zip by file transfer. These are the raw ingredients of your product,
so keep a backup.
