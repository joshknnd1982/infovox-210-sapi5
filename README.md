# Infovox 210 SAPI5

**The 1996 Macintosh Infovox 210 speech synthesizer, running on Windows 11 as
ordinary SAPI5 voices.**

Nothing here re-implements the synthesizer. The original PowerPC code from 1996
executes instruction for instruction inside a CPU emulator, and a thin Windows
layer feeds it text and collects the audio it produces. 55 voices across 11
languages, with the demo restrictions removed.

---

## The software this is built on

**Infovox 210** was released in 1996 by **Infovox AB** of Solna, Sweden, for the
Apple Macintosh. It is version 2.0.2b0, dated 22 March 1996, and it shipped as a
classic Mac OS **Speech Manager synthesizer component** — an extension you
dropped into the System Folder, after which any Macintosh application that used
Apple's Speech Manager could speak with it.

It only ever ran on Macs. The component carries two builds of itself, one for
68k processors and one for PowerPC, and nothing else: no Windows version, no
Linux version, no source. Infovox AB was later absorbed into Telia Promotor and
the product line eventually became the Acapela Group's. The 210 has not been
sold, supported or supportable for a quarter of a century, and no rights holder
offers it. It is abandonware in the plainest sense.

### Where it comes from

Infovox is the commercial descendant of the speech synthesis work done at **KTH
Royal Institute of Technology in Stockholm** from the 1960s onward — the OVE
line of formant synthesizers built by Gunnar Fant, Rolf Carlson and Björn
Granström. The 68k build inside the component is still literally named
`Mac_OVE_comp`. The engine is a **rule-based formant synthesizer**: it has no
recorded speech in it at all. Every sound is generated from scratch by a model
of the vocal tract, driven by letter-to-sound rules and a pronunciation
dictionary per language.

That is why the whole thing fits in a few hundred kilobytes, why it can speak
eleven languages in the space a modern voice uses for a fraction of one, and
why it sounds the way it does.

The `bin/` directory of this repository includes the published research papers
behind it, as PDFs: Carlson & Granström on the GLOVE synthesizer, the Liljencrants-Fant
glottal model, and the KTH prosody and text-to-speech work of the late 1980s and
early 1990s.

### The voices

11 languages, each with five variants — 55 in total.

| | | |
|---|---|---|
| American English | British English | Danish |
| Finnish | French | German |
| Icelandic | Italian | Norwegian |
| Spanish | Swedish | |

Each language has a **male**, **female**, **deep male**, **child** and **ghost**
voice. The variants are not merely pitch shifts: they differ in pitch base,
intonation depth and formant scaling. The "ghost" voice is the odd one — it
speaks in a near-monotone.

Voice names are the ones Infovox gave them, in their own languages: *Svenska:
mansröst*, *Français: voix d'homme*, *Deutsch: Kinderstimme*, and so on.

---

## What this repository is

A Windows 11 port by emulation, plus everything it was built from.

The engine is a Component Manager component in PEF (Preferred Executable
Format) — the container Apple used for PowerPC code on classic Mac OS. To run
it, this project:

1. **loads the PEF** — parses the container, expands its pattern-initialised
   data, and applies the relocations;
2. **emulates a PowerPC CPU** with [Unicorn](https://www.unicorn-engine.org/);
3. **implements the parts of classic Mac OS it asks for.** The component
   imports 56 symbols. All of them are provided here: the Memory Manager
   (handles), the Resource Manager, the Sound Manager's double-buffer playback
   path, the Time, Notification and Component Managers, Mixed Mode, and six
   MathLib functions;
4. **drives it as the Speech Manager would**, through the Component Manager
   calling convention, and collects the PCM it writes into its double buffers.

The result is 22050 Hz, 16-bit, mono audio — the engine's own output format.

### Architecture

```
  application (NVDA, Narrator, Balabolka, ...)
        |  SAPI5
  InfovoxSAPI.dll  (32-bit)      x64\InfovoxSAPI.dll  (64-bit)
        \                                /
         \____ named pipe, overlapped ___/
                        |
              infovox_host.exe  (64-bit)
                        |
              Unicorn PowerPC CPU
                        |
              PPCm 128  --  the 1996 component
```

Both SAPI engines are thin and go out of process to one 64-bit worker. Unicorn
is only available here as a 64-bit build, and keeping a JIT out of every
application that happens to speak is worth the pipe. Each connection gets its
own engine instance, so one application reading a long document cannot stall
another; that is affordable because the emulated Mac heap peaks at about 600 KB
with all eleven languages loaded.

Throughput is roughly **11x realtime**, which is the PowerPC emulation itself —
the Windows side is not the cost. A cancelled utterance stops in about 55 ms.

---

## The demo restriction, and its removal

The Macintosh release was a demo. It is worth being precise about what that
meant, because it was not what you would expect.

There was **no expiry and no dialog**. The engine imports no date or clock
function at all, so it could not have had a time bomb. What it had instead was a
**spoken** nag:

* the first utterance after the engine started was preceded by a spoken
  announcement — *"This is a demonstration of speech synthesis, nineteen
  ninety-six"* — localised into whichever language was in use, held in each
  language's rule file under the dictionary key `DEMO`;
* after that, a counter reloaded to 4, and **every fourth word** carried an
  injected *"nineteen ninety-six"* or *"one hundred"*.

The mechanism is a signed counter at offset `0x112` in the engine's own state.
Negative meant "speak the announcement"; zero meant "reload and nag". Two
four-byte branch instructions in the PowerPC code decide both, and both are
patched in the engine packs this repository ships. Verified silent over 30+
consecutive utterances on all 55 voices.

---

## Two things the engine cannot do, and what is done about them

**It stops at 4 kHz.** The synthesizer runs at roughly 8 kHz internally and
resamples to the 22050 Hz it declares, so its output is hard band-limited at
about 4.1 kHz — measured at −100 dB by 4.5 kHz. Sibilants live at 4–8 kHz, so
*s*, *f*, *h* and *th* arrive faint and are easy to confuse. Nothing in the
engine changes this: every `Gestalt` answer, every Sound Manager version and
every speech parameter produce byte-identical output.

So the missing octave is restored afterwards. A shelf lifts the top of what the
engine does produce, and the band above its ceiling is regenerated as shaped
noise that tracks the level of the band below. The **Consonant clarity** setting
controls how much; `0` gives the untouched 1996 output, and a fresh install uses
`40`.

**Its output sits on a DC offset**, walking up to about 1200 counts as an
utterance starts. Harmless while it plays — but a screen reader abandons an
utterance on every keypress, and cutting from that offset to silence is a click.
The offset is now filtered out at 25 Hz (well below the engine's lowest pitch,
so the voice is unchanged to within 0.15 dB), and a cancelled utterance ends
with a 5 ms ramp instead of stopping dead.

---

## Known limitations

These are worth knowing before you install. None of them is a bug in the port;
they are what this particular engine is.

**Consonants are still weak, and cannot really be fixed.** The 4 kHz ceiling
above is a hard limit of the synthesizer. The Consonant clarity setting makes
*s*, *f*, *h* and *th* far more present, but it is reconstructing a band the
engine never generated — it is putting plausible noise where the real cue
should be, not recovering it. Fine distinctions still get lost: *s* against
*f* against *th* can be hard to tell apart, particularly at speed, and no
setting here will make them as crisp as a modern synthesizer. This is the main
reason a 1996 formant synthesizer sounds the way it does, and running it under
emulation neither causes nor cures it.

**It is an early version, and it mispronounces more than later ones.** The 210's
letter-to-sound rules and pronunciation dictionaries are from 1996 and were
revised substantially afterwards. Compared with the later **Infovox 230**, this
release gets more words wrong — irregular spellings, loan words, names and
abbreviations especially. Some of the language packs here are older still than
others: the version stamps in the voice files range from 1.0 to 3.1, so Danish
(1.2) and Icelandic (1.0) are noticeably rougher than Swedish or British
English (both 3.1).

**It has fewer languages than the 230.** Eleven here. The later releases added
more — Dutch among them — so if you need a language that is not in the table
above, this is not the engine for it.

What the 210 does have over its successors is that it exists, in a form that
could be recovered and made to run again.

## Parameters

Every parameter is one 0–100 scale, where **0 is the engine's minimum and 100
its maximum**, so a screen reader's own sliders reach both extremes.

| Parameter | Range in engine terms |
|---|---|
| Speaking rate | 0 to 999 words per minute |
| Pitch | the engine's full pitch-base range |
| Pitch modulation | how far the intonation moves |
| Breathiness | the engine's private `InVx`/`aspi` aspiration control |
| Volume | applied in software — the engine accepts `soVolume` but never applies it |
| Consonant clarity | restores the band above 4 kHz |

SAPI has no slider for pitch modulation, breathiness or consonant clarity, so
those are set per voice in **Infovox 210 Settings**. There is also an **Infovox
210 Custom Voice** token whose language, variant and every parameter come from
that dialog.

---

## Installing

Download the installer from
[Releases](../../releases) and run it. It needs administrator rights, because
SAPI reads voice tokens from `HKEY_LOCAL_MACHINE` only.

Setup lets you choose which languages to install, and can put a shortcut to the
settings dialog on your desktop.

If a voice will not speak, run **Infovox 210 Diagnostics** from the Start menu:
it walks every installed voice through SAPI and writes a report to your `TEMP`
folder. `InfovoxDiagnostics.exe --pipe-test` checks the emulator and the worker
directly, without needing the voices registered.

## Building

Requires Visual Studio 2022 Build Tools (C++), CMake, and Inno Setup 6 for the
installer.

```
build_all.bat
```

That builds x64 and x86, stages `output\`, and compiles the installer.

---

## What is in here

| Path | |
|---|---|
| `bin/` | the original 1996 resource fork, exactly as extracted, plus the KTH research papers as PDFs |
| `engine/` | the shipped engine packs (`.ivp`), built from `bin/` with the demo patches applied |
| `src/ppc/` | PEF loader, the classic Mac OS runtime, the engine driver, the audio conditioning |
| `src/host/` | the 64-bit worker |
| `src/sapi/` | the SAPI5 engine, for both architectures |
| `tools/` | settings dialog, diagnostics, the command-line probe, sample and verification scripts |
| `installer/` | the Inno Setup script |
| `samples/` | 97 rendered WAVs: all 55 voices, and a sweep of every parameter |
| `output/` | prebuilt binaries, ready to run |
| `third_party/unicorn/` | the Unicorn CPU emulator |

### The original files

`bin/` holds the component's resource fork as extracted from the Macintosh
original, one file per resource:

* `PPCm` 128 — the PowerPC build of the component (a PEF container)
* `68km` 128 — the 68k build, named `Mac_OVE_comp`
* `TTch`, `TTct`, `TTfr`, `SYct`, `SYsa` — the 68k text-to-speech and
  synthesis modules
* `thng` 128 — the Component Manager registration: type `ttsc`, manufacturer
  `InVx`
* `IVvx` — each language's letter-to-sound rules and dictionary
* `ttss` — each language's phoneme table, with symbols and example words
* `ttvd` — one `VoiceDescription` per voice

Everything needed to rebuild the engine packs from the originals is here.

---

## Status and licence

Infovox 210 is the work of Infovox AB and, before that, of the speech group at
KTH. This repository does not claim any right to it. It is published as a
preservation and accessibility project: the software has been unavailable and
unsupported for decades, runs on hardware almost nobody still has, and would
otherwise simply be lost. If a rights holder objects, it will be taken down.

The Windows port — the PEF loader, the Mac OS runtime, the SAPI5 engines, the
worker, the settings dialog and the tooling, everything under `src/`, `tools/`
and `installer/` — is released into the public domain, so anyone can do the same
for another engine.

Unicorn is licensed under the GPLv2 by its own authors; see
[unicorn-engine.org](https://www.unicorn-engine.org/).
