#!/usr/bin/env python3
"""Check singer recordings against the recording brief before any training time is spent.

Reports per file: format, duration, peak, clipping, noise floor between phrases, an echo/reverb
indicator and DC offset; then totals per language prefix (NL_, EN_, RANGE_, VAL_).
Usage: python check_recordings.py <folder>
"""
import sys
from pathlib import Path

import numpy as np
import soundfile as sf


def analyse(path: Path) -> dict:
    info = sf.info(path)
    x, sr = sf.read(path, dtype="float32", always_2d=True)
    mono = x.mean(1)
    peak = float(np.abs(mono).max()) if len(mono) else 0.0
    clipped = int((np.abs(x) >= 0.999).sum())
    win = int(0.05 * sr)
    n = len(mono) // win
    frames = mono[: n * win].reshape(n, win) if n else np.zeros((1, 1))
    db = 10 * np.log10((frames ** 2).mean(1) + 1e-12)
    floor = float(np.percentile(db, 5)) if n else -120.0
    # Reverb/echo indicator: how slowly energy decays after phrase ends (dB drop over the next 300 ms).
    decays = []
    for i in range(1, n - 6):
        if db[i - 1] > db.max() - 20 and db[i] < db[i - 1] - 6:
            decays.append(db[i - 1] - db[i + 6])
    tail = float(np.median(decays)) if decays else float("nan")
    issues = []
    if info.samplerate != 48000:
        issues.append(f"sample rate {info.samplerate} Hz (need 48000)")
    if info.subtype not in ("PCM_24", "FLOAT", "PCM_32"):
        issues.append(f"{info.subtype} (need 24-bit)")
    if info.channels != 1:
        issues.append("not mono")
    if clipped:
        issues.append(f"{clipped} clipped samples")
    if peak < 0.1:
        issues.append(f"very quiet (peak {20*np.log10(peak+1e-9):.1f} dBFS)")
    if floor > -60:
        issues.append(f"noise floor {floor:.0f} dBFS (need < -60)")
    if decays and tail < 25:
        issues.append(f"slow decay after phrases ({tail:.0f} dB in 300 ms): possible room reverb or effects")
    if abs(float(mono.mean())) > 0.01:
        issues.append("DC offset")
    return {"file": path.name, "sec": len(mono) / sr, "peak_db": 20 * np.log10(peak + 1e-9), "floor_db": floor, "issues": issues}


def main(folder):
    files = sorted(Path(folder).rglob("*.wav"))
    if not files:
        sys.exit("No WAV files found.")
    totals = {}
    bad = 0
    for f in files:
        r = analyse(f)
        key = f.name.split("_")[0].upper() if "_" in f.name else "OTHER"
        if key == "VAL":
            key = "VAL (validation)"
        totals[key] = totals.get(key, 0.0) + r["sec"]
        status = "OK " if not r["issues"] else "FIX"
        bad += bool(r["issues"])
        print(f"{status} {r['file']:<28} {r['sec']:6.1f}s  peak {r['peak_db']:6.1f} dBFS  floor {r['floor_db']:6.1f} dBFS  {'; '.join(r['issues'])}")
    print("\nTotals:")
    for k, v in sorted(totals.items()):
        print(f"  {k:<18} {v/60:6.1f} min")
    sing = totals.get("NL", 0) + totals.get("EN", 0)
    print(f"\nTraining singing (NL+EN): {sing/60:.1f} min  ->  {'enough' if sing >= 1800 else 'below the 30-minute minimum'}")
    print(f"Files needing attention: {bad} of {len(files)}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
