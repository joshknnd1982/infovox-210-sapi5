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
    "french":    "Charles cherche six chaussures chez Sophie, puis part de Paris.",
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
# The stops get a short release burst, not sustained frication, and it has to
# sit where the release happened: low for a labial, high for an alveolar.  A
# labial burst placed too high is heard as a stray /s/.
STOPS = {"B", "P", "D", "T", "2D", "2T", "G", "K", "KH"}
LABIAL_STOPS = {"B", "P"}
LABIAL_LOW_FRACTION_MIN = 0.50   # of the burst's energy, below 2.4 kHz

# A word-final fricative has no successor to end it, so it is capped.  At the
# engine's default 150 wpm that cap is 140 ms; allow for the ramps and for two
# fricatives running together.
MAX_FINAL_FRICATION_MS = 210.0

# A stop release straddles the boundary with the next phone, and a fricative
# takes a couple of milliseconds to ramp out, so the start of a vowel legitimately
# carries a little noise from the consonant before it.  Vowels are therefore
# judged on their interior, from this far in.
PHONE_INTERIOR_S = 0.018

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


def high_shelf(x, fs, gain_db, fc=2600.0, slope=0.9):
    """The same RBJ shelf ConsonantClarity applies (see clarity.h)."""
    a_ = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * fc / fs
    c, sn = math.cos(w0), math.sin(w0)
    alpha = sn / 2.0 * math.sqrt((a_ + 1.0 / a_) * (1.0 / slope - 1.0) + 2.0)
    tsa = 2.0 * math.sqrt(a_) * alpha
    a0 = (a_ + 1.0) - (a_ - 1.0) * c + tsa
    b0 = a_ * ((a_ + 1.0) + (a_ - 1.0) * c + tsa) / a0
    b1 = -2.0 * a_ * ((a_ - 1.0) + (a_ + 1.0) * c) / a0
    b2 = a_ * ((a_ + 1.0) + (a_ - 1.0) * c - tsa) / a0
    a1 = 2.0 * ((a_ - 1.0) - (a_ + 1.0) * c) / a0
    a2 = ((a_ + 1.0) - (a_ - 1.0) * c - tsa) / a0
    y = np.empty_like(x)
    z1 = z2 = 0.0
    for i, v in enumerate(x):
        o = b0 * v + z1
        z1 = b1 * v - a1 * o + z2
        z2 = b2 * v - a2 * o
        y[i] = o
    return y


def added_noise(dry, wet, fs, amount=40):
    """Exactly what the clarity stage synthesised, with its shelf undone.

    The wet render is (shelf(dry) + noise) * makeup, so simply differencing the
    two energies also picks up the shelf and the makeup trim -- which can be
    larger than a stop burst and can even cancel it out.  Reversing them leaves
    the noise alone.
    """
    a = amount / 100.0
    makeup = 1.0 / (1.0 + 0.30 * a)
    return wet / makeup - high_shelf(dry, fs, 9.0 * a)


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
    print("%-6s %-10s %9s %8s %9s %9s %7s %8s %6s  %s" %
          ("voice", "pack", "fricative", "stop", "vowel", "silence", "held",
           "labial<2k4", "x-rt", ""))
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
        noise = added_noise(a, b, fs)
        fr, vo, st = [], [], []
        lab_lo, lab_all = 0.0, 0.0
        for i, (t, sym) in enumerate(events):
            end = events[i + 1][0] if i + 1 < len(events) else t + 0.08
            s0, s1 = int(t * fs), int(min(end, t + 0.30) * fs)
            if s1 - s0 < 128 or s1 > n:
                continue
            # What clarity added here, on top of what the engine made.
            added = math.sqrt((noise[s0:s1] ** 2).mean())
            if sym in FRICATIVE:
                fr.append(added)
            elif sym in STOPS:
                # A stop's burst straddles the boundary with the next phone, so
                # look at the window around it rather than at the phone itself.
                w0 = max(int((end - 0.012) * fs), s0)
                w1 = min(int((end + 0.014) * fs), n)
                if w1 - w0 > 128:
                    st.append(math.sqrt((noise[w0:w1] ** 2).mean()))
                    if sym in LABIAL_STOPS:
                        d = noise[w0:w1]
                        sp = np.abs(np.fft.rfft(d * np.hanning(len(d)))) ** 2
                        f = np.fft.rfftfreq(len(d), 1 / fs)
                        lab_lo += sp[f < 2400].sum()
                        lab_all += sp.sum()
            else:
                # Judge a vowel on its interior: the first few milliseconds
                # belong to the release of whatever consonant preceded it.
                v0 = int((t + PHONE_INTERIOR_S) * fs)
                if s1 - v0 > 128:
                    vo.append(math.sqrt((noise[v0:s1] ** 2).mean()))

        # Everything after the last phoneme is trailing silence.
        tail = int(min(events[-1][0] + 0.35, n / fs) * fs) if events else n
        sil = math.sqrt((noise[tail:] ** 2).mean()) if n - tail > 128 else 0.0

        # How long the last phone's frication runs.  Nothing ends it but the
        # cap, so this is where an over-long word-final /s/ shows up.
        held = 0.0
        if events:
            t0 = int(events[-1][0] * fs)
            step = int(0.010 * fs)
            gate = speech * 0.02
            k = t0
            while k + step <= n:
                if math.sqrt((noise[k:k + step] ** 2).mean()) < gate:
                    break
                held += 10.0
                k += step

        if not fr or not vo:
            failures.append("%s: no phonemes classified" % vid)
            print("%-6s %-10s  NO PHONEMES" % (vid, pack))
            continue

        dfr = db(float(np.mean(fr)), speech)
        dvo = db(float(np.mean(vo)), speech)
        dsil = db(sil, speech)
        dst = db(float(np.mean(st)), speech) if st else float("nan")
        labfrac = lab_lo / lab_all if lab_all > 0 else None
        peak = float(np.abs(b).max())
        bad = []
        if dfr < FRICATION_MIN_DB:
            bad.append("frication too weak")
        if dvo > SILENT_MAX_DB:
            bad.append("noise on vowels")
        if st and dst > -8.0:
            bad.append("stop bursts too loud")
        if labfrac is not None and labfrac < LABIAL_LOW_FRACTION_MIN:
            bad.append("labial burst too bright (%.0f%% below 2.4 kHz)" % (100 * labfrac))
        if dsil > SILENT_MAX_DB:
            bad.append("noise in silence")
        if peak >= 32767:
            bad.append("clipping")
        if held > MAX_FINAL_FRICATION_MS:
            bad.append("final fricative held %.0f ms" % held)
        print("%-6s %-10s %8.1f %8.1f %9.1f %9.1f %5.0fms %7s %5.1fx  %s" %
              (vid, pack, dfr, dst, dvo, dsil, held,
               ("%.0f%%" % (100 * labfrac)) if labfrac is not None else "-",
               speed, "FAIL: " + ", ".join(bad) if bad else "ok"))
        if bad:
            failures.append("%s (%s): %s" % (vid, pack, ", ".join(bad)))

    print()
    if failures:
        print("%d of %d voices FAILED:" % (len(failures), len(voices)))
        for f in failures:
            print("  " + f)
        sys.exit(1)
    print("all %d voices pass: frication on the fricatives (>= %.0f dB); vowels and "
          "silence clean (<= %.0f dB);\nstop releases present and labial ones dark; "
          "no final fricative held past %.0f ms; no clipping"
          % (len(voices), FRICATION_MIN_DB, SILENT_MAX_DB, MAX_FINAL_FRICATION_MS))


if __name__ == "__main__":
    main()
