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

## The consonant bug, and how it was found

Version 1.3.0 shipped with a bug. A tester reported that it was "definitely
buggy and not exactly how it sounded on Mac OS", that *s*, *sh* and *h* did not
sound right, and guessed the fault was in the noise generator code. That was the
right guess about the wrong noise generator, and following it turned up
something larger underneath.

**The gate was firing on the wrong sounds.** Consonant clarity worked by
detecting how noise-like each instant was — the ratio of energy above 2.3 kHz to
the total — and adding shaped noise when that ratio was high. Measuring it phone
by phone over a 39-phone inventory on the American male voice showed that it did
not do what it was meant to. The gate opened further on the diphthongs and the
nasals than on the sibilants it was written for:

| | opens to | | opens to |
|---|---|---|---|
| /s/ *see* | 0.19 | /aɪ/ *hide* | **0.48** |
| /ʃ/ *she* | 0.34 | /eɪ/ *hayed* | **0.42** |
| /f/ *fee* | 0.22 | /æ/ *had* | **0.32** |
| /θ/ *thief* | 0.19 | /m/ *me* | **0.93** peak |
| /z/ /v/ /ʒ/ /ð/ | **0.00** | /n/ *knee* | **0.72** peak |

So the feature was spraying its noise onto vowels and nasals, giving the voiced
fricatives nothing at all, and never delivering more than about half its nominal
amount to anything. That alone accounts for the report.

**No gate of that kind can work on this engine.** Four detectors were then
measured over the same inventory — high-band ratio, high band against 0–700 Hz,
high band against 300–1200 Hz, and aperiodicity. The separation score, the
quietest fricative divided by the loudest non-fricative, was 0.66×, 0.60×, 0.66×
and 0.72×. All below 1.0: the two groups overlap completely, so no threshold on
any of them separates a fricative from a vowel.

**Which raised the real question: why not?** Rendering *see*, *fee*, *thief*,
*he* and *heed* and comparing their first 50 ms gave the answer. All five have
the same spectrum to within 2 dB and the same glottal pulse train at 125 Hz.
*hiss* spoken at 50 words per minute is 399 ms of continuously voiced signal
with no fricative segment in it anywhere. The engine was not producing weak
fricatives; it was producing none, and substituting voicing for them.

The engine's own aspiration control settles it. Aspiration is noise, so turning
it up should add noise and reduce periodicity:

| breathiness | RMS | periodicity |
|---|---|---|
| 0 | 3306 | 0.901 |
| 50 | 1040 | 0.902 |
| 100 | **0 — digital silence** | — |

The voicing is scaled down and nothing replaces it. At full aspiration, where
the output should be all noise, there is silence. That is what a formant
synthesizer sounds like when its noise generator returns zero.

**Confirmed inside the emulator.** Hooking every guest memory write during an
utterance shows the synthesizer's per-sample state: 23 doubles updated exactly
once per output frame, in pairs, as a bank of resonators. Six of them — one
whole branch — hold exactly 0.0 from the first sample to the last, through a
sentence full of /s/. That is the noise branch, receiving nothing. Injecting
entropy into those words does not revive it: they are dead stores, and the live
state is kept in registers inside the synthesis loop, so reviving the engine's
own generator means reverse-engineering that loop. That is still the fix worth
having, and it is not done here.

**What was done instead.** The engine cannot say where its fricatives are
through the audio, but it says so directly: it fires a phoneme callback for
every phone it speaks, and every language pack carries its own symbol table. So
the noise is now scheduled from that stream:

* callbacks used to be timestamped with the frame count at the moment they were
  collected, which put every phone in a buffer at one time and could be two
  buffers — about 93 ms — from the audio it described. The runtime now tracks
  how far into the buffer the engine has written when a callback fires, so
  phonemes and word boundaries land on the right sample;
* each phoneme symbol selects a noise class, from the pack's own table, so this
  works the same way in all eleven languages;
* the noise is a crossfade between two fixed bands rather than a retuned filter,
  because retuning inside an utterance clicks;
* levels follow a slow envelope of the engine's own speech, with a floor so that
  a fricative that opens an utterance still gets one, and a duck so that
  trailing silence stays silent.

Measured over all 55 voices with `tools/verify_clarity.py`, which renders each
voice twice — clarity off and on — and attributes the difference phone by phone:
frication now lands 6–13 dB below the utterance's speech level, vowels and
nasals receive 42 dB below it or less, trailing silence receives nothing at all,
and nothing clips.

---

## Two things the engine cannot do, and what is done about them

**Its noise source produces nothing, so it has no fricatives.** This is worse
than a bandwidth limit, and it took measuring to see. Rendered on the American
male voice, the first 50 ms of *see*, *fee*, *thief*, *he* and *heed* have the
same spectrum to within 2 dB and the same glottal pulse train at F0: wherever a
voiceless fricative belongs, the engine emits voicing instead. Its own
aspiration control says the same thing — turning it up scales the voice down and
adds no noise at all, and at full scale the output is digital silence, which is
what a synthesizer sounds like when its noise generator returns zero. Inside the
emulated synthesizer, six of the twenty-three per-sample filter states — one
whole resonator branch, the noise branch — hold exactly 0.0 for an entire
utterance. A formant synthesizer with no noise source has no *s*, no *sh*, no
*f*, no *h*.

That also means the audio carries no clue as to where the fricatives are. Four
detectors were measured over a 39-phone inventory — high-band ratio, high band
against 0–700 Hz, high band against 300–1200 Hz, and aperiodicity — and none
separated *s*, *sh*, *f*, *th* and *h* from the vowels; the best margin was
0.72×, worse than chance.

But the engine *says* what it is speaking. It fires a phoneme callback for every
phone, and each language pack carries its own symbol table. So the frication is
scheduled from that stream rather than guessed from the audio: the callback is
timestamped against how far into the buffer the engine had written when it
fired, which places it to the sample, and each phoneme symbol selects a noise
class — *s* sharp and high, *sh* lower and broader, *ch* and *j* sharper still
because an affricate is, *f* and *th* weak and flat, *h* breathy, and the voiced
members of each pair quieter because their voicing is already there. Levels
follow the engine's own speech, so frication tracks the voice and the speaking
rate. A high shelf still lifts what the engine does produce above 2.6 kHz.

The stops get nothing. A synthesised release burst is a bare click of noise with
no formant transition behind it, and after a */p/* it is heard as a stray *s* —
plainly, in the word *reproduce*.

The **Consonant clarity** setting controls how much; `0` gives the untouched
1996 output, and a fresh install uses `40`. `tools/verify_clarity.py` checks the
result on every installed voice by rendering each one twice, with clarity off
and on, and attributing the difference phone by phone against the engine's own
phoneme stream.

### What 1.5.0 fixed

The first version of this was measured for whether the noise landed on the right
phones, and it did. It was not measured for how loud or how long, and four
things were wrong:

* **A word-final fricative ran on.** The last phoneme of an utterance has no
  successor to end it, so it stopped only when a fixed 320 ms cap expired —
  *pass* held its *s* for a third of a second. The cap now comes from the
  speaking rate: 140 ms at the engine's default 150 wpm, scaled from there, so
  it stays right when a screen reader is set to 400 wpm or to 60.
* **A word-initial fricative before a stop was inaudible.** A voiceless
  fricative is often digital silence in this engine, so an utterance opening
  with one — *spot*, *see*, *Sofie* — offered nothing to scale the noise to and
  fell back on a floor. Measured against 485 fricative onsets across all 55
  voices, that floor sat **11.7 dB** below the same consonant mid-utterance,
  which is why the *s* of *spot* sounded dropped. The floor is now set from that
  measurement.
* **The two noise bands were mixed wrongly.** The low band's normalising gain
  was 2.35 where 3.218 was needed, leaving it 2.7 dB down; and because one noise
  source through a 1.9–5.2 kHz and a 4.2–9.5 kHz filter comes out
  anti-correlated (ρ = −0.248), a plain crossfade lost a further 4.2 dB in the
  middle of its travel. Between them these were quietest for exactly the sounds
  that needed them most — *ch*, *sh*, *f*, *v*. The crossfade is now equal-power
  and the gains are the measured ones.
* **The voiced fricatives were too quiet to identify a letter by.** Arrowing
  over *v*, *z*, *g* and *j*, the engine's own pronunciations are right — *vee*,
  *zee* (*zed* in British), *jee*, *jay* — but with */v/* at a tenth of the
  level of */s/* the consonant simply was not there, and the letter was heard as
  a vowel. *ch* got its own class at the same time, so *aych* and *chat* have an
  affricate rather than a soft *sh*.

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

**The fricatives are synthesised, not recovered.** The engine's noise source is
dead, so there is no original frication to restore; what you hear is generated
here, from the engine's phoneme stream, and it is only as good as the class each
phoneme is put into. The classes are broad — one *s*, one *sh*, one *f*/*th* —
so *f* against *th* is still a fine distinction, and a phoneme symbol the
classifier does not recognise is left exactly as the engine rendered it, which
means silent where a fricative should be. Reviving the engine's own noise
generator, rather than working around it, would be the real fix; it is somewhere
in the PowerPC synthesis loop, and its live state is held in registers rather
than memory, so finding it means reverse-engineering that loop.

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
| Consonant clarity | how much frication to synthesise for the fricatives |

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
| `src/ppc/` | PEF loader, the classic Mac OS runtime, the engine driver, the frication synthesis |
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
