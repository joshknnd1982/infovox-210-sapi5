#!/usr/bin/env python3
"""Check that consonant clarity puts frication on consonants and nowhere else.

Every voice speaks a fricative-rich sentence twice, once with clarity off and
once with it at the shipping default.  Subtracting the two gives exactly what
the clarity stage added, and the engine's own phoneme stream says where each
phone is, so the added energy can be attributed phone by phone.

Passing means: the fricatives get frication, the vowels and nasals get none,
silence stays silent, and nothing clips.

    python tools/verify_clarity.py [path-to-infovox_probe.exe]
"""
import math
import os
import re
import subprocess
import sys
import tempfile
import wave

import numpy as np

# ASCII-only so the command line survives the trip to Mac Roman unchanged.
SENTENCES = {
    "american":  "She sells sea shells, this is the sixth thing, Sarah.",
    "british":   "She sells sea shells, this is the sixth thing, Sarah.",
    "danish":    "Sofie hvisker sagte i huset.",
    "finnish":   "Sisko istuu hiljaa suuressa salissa.",
    "french":    "Charles cherche six chaussures chez Sophie.",
    "german":    "Sechs schone Fische schwimmen sicher durch das Wasser.",
    "icelandic": "Sigga hvitir sokkar hja husinu.",
    "italian":   "Sei sciocchezze sono scritte sulla sedia.",
    "norwegian": "Solen skinner sikkert over sjoen i sommer.",
    "spanish":   "Justo Jose escogio seis zapatos sucios.",
    "swedish":   "Sju sjosjuka sjoman skottes av sju skona sjukskoterskor.",
}

# Mirrors fricationFor() in src/ppc/infovox.cpp.
FRICATIVE = {"S", "S1", "ts", "Z", "sh", "SH", "2S", "SJ", "TJ", "ch",
             "zh", "ZH", "jh", "F", "V", "th", "TH", "dh", "DH",
             "hh", "H", "X", "KJ", "GH", "CH"}
BURST = {"P", "T", "K", "2T", "KH"}

# A fricative must be at least this loud against the utterance's own speech
# level, and anything that is not a consonant must be quieter than this.
FRICATION_MIN_DB = -22.0
SILENT_MAX_DB = -26.0


def read_wav(path):
    with wave.open(path, "rb") as w:
        return (np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(float),
                w.getframerate())


def run(probe, voice, text, clarity, wav):
    env = dict(os.environ, INFOVOX_EVENTS="1")
    r = subprocess.run([probe, "speak", voice, wav, "15", "50", "25", "0", "100",
                        str(clarity), text],
                       capture_output=True, text=True, env=env,
                       encoding="utf-8", errors="replace",
                       cwd=os.path.dirname(probe) or ".")
    if r.returncode != 0:
        return None, 0.0, r.stderr.strip()
    events, speed = [], 0.0
    for line in r.stdout.splitlines():
        m = re.match(r"\s+([\d.]+)\s+phoneme (\S+)\s+\((\d+)\)", line)
        if m:
            events.append((float(m.group(1)), m.group(2)))
        m = re.search(r"\(([\d.]+)x realtime\)", line)
        if m:
            speed = float(m.group(1))
    return events, speed, None


def db(x, ref):
    return 20.0 * math.log10(max(x, 1e-9) / max(ref, 1e-9))


def main():
    probe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        "build_x64", "bin", "Release", "infovox_probe.exe")
    probe = os.path.abspath(probe)
    if not os.path.exists(probe):
        sys.exit("no infovox_probe at %s" % probe)

    listing = subprocess.run([probe, "list"], capture_output=True, text=True,
                             encoding="utf-8", errors="replace",
                             cwd=os.path.dirname(probe))
    voices = [(p[0], p[1]) for p in (l.split() for l in listing.stdout.splitlines()[1:])
              if len(p) >= 4 and len(p[0]) == 4 and p[1] in SENTENCES]
    if not voices:
        sys.exit("no voices listed")

    tmp = tempfile.mkdtemp(prefix="clarity_")
    print("Added frication, in dB against each utterance's own speech level.")
    print("%-6s %-10s %9s %9s %9s %9s %7s  %s" %
          ("voice", "pack", "fricative", "vowel", "silence", "peak", "x-rt", ""))
    failures = []
    for vid, pack in voices:
        dry = os.path.join(tmp, vid + "_0.wav")
        wet = os.path.join(tmp, vid + "_40.wav")
        events, _, err = run(probe, vid, SENTENCES[pack], 0, dry)
        if err:
            failures.append("%s: %s" % (vid, err))
            print("%-6s %-10s  RENDER FAILED: %s" % (vid, pack, err))
            continue
        _, speed, err = run(probe, vid, SENTENCES[pack], 40, wet)
        a, fs = read_wav(dry)
        b, _ = read_wav(wet)
        n = min(len(a), len(b))
        a, b = a[:n], b[:n]

        speech = math.sqrt((a[np.abs(a) > 1] ** 2).mean()) if (np.abs(a) > 1).any() else 1.0
        fr, vo = [], []
        for i, (t, sym) in enumerate(events):
            end = events[i + 1][0] if i + 1 < len(events) else t + 0.08
            s0, s1 = int(t * fs), int(min(end, t + 0.30) * fs)
            if s1 - s0 < 128 or s1 > n:
                continue
            # What clarity added here, on top of what the engine made.
            ea = (a[s0:s1] ** 2).mean()
            eb = (b[s0:s1] ** 2).mean()
            added = math.sqrt(max(eb - ea, 0.0))
            (fr if sym in FRICATIVE else vo if sym not in BURST else []).append(added)

        # Everything after the last phoneme is trailing silence.
        tail = int(min(events[-1][0] + 0.35, n / fs) * fs) if events else n
        sil = math.sqrt((b[tail:] ** 2).mean()) if n - tail > 128 else 0.0

        if not fr or not vo:
            failures.append("%s: no phonemes classified" % vid)
            print("%-6s %-10s  NO PHONEMES" % (vid, pack))
            continue

        dfr = db(float(np.mean(fr)), speech)
        dvo = db(float(np.mean(vo)), speech)
        dsil = db(sil, speech)
        peak = float(np.abs(b).max())
        bad = []
        if dfr < FRICATION_MIN_DB:
            bad.append("frication too weak")
        if dvo > SILENT_MAX_DB:
            bad.append("noise on vowels")
        if dsil > SILENT_MAX_DB:
            bad.append("noise in silence")
        if peak >= 32767:
            bad.append("clipping")
        print("%-6s %-10s %8.1f %9.1f %9.1f %9.0f %6.1fx  %s" %
              (vid, pack, dfr, dvo, dsil, peak, speed,
               "FAIL: " + ", ".join(bad) if bad else "ok"))
        if bad:
            failures.append("%s (%s): %s" % (vid, pack, ", ".join(bad)))

    print()
    if failures:
        print("%d of %d voices FAILED:" % (len(failures), len(voices)))
        for f in failures:
            print("  " + f)
        sys.exit(1)
    print("all %d voices pass: frication on the fricatives (>= %.0f dB), vowels and "
          "silence clean (<= %.0f dB), no clipping"
          % (len(voices), FRICATION_MIN_DB, SILENT_MAX_DB))


if __name__ == "__main__":
    main()
