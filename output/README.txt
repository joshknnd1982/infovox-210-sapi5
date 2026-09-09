Infovox 210 SAPI5 1.6.0
=======================

What this is
------------
The Infovox 210 text-to-speech converter was released by Infovox AB (Solna,
Sweden) in 1996 for the Apple Macintosh, as a classic Mac OS Speech Manager
synthesizer component.  It only ever ran on 68k and PowerPC Macs.

This package runs that original 1996 PowerPC code on Windows 11 under a
PowerPC CPU emulator, and publishes it to Windows as ordinary SAPI5 voices.
Nothing has been re-implemented or re-synthesised: the speech you hear is
produced by the original engine, instruction for instruction.

Voices
------
55 voices: 11 languages, each with five variants.

    American English   British English   Danish     Finnish    French
    German             Icelandic         Italian    Norwegian  Spanish
    Swedish

    1 male   2 female   3 deep male   4 child   5 ghost

Setup can install any subset of the languages.  There is also an
"Infovox 210 Custom Voice" whose language, variant and every parameter come
from the settings dialog.

The demo restrictions
---------------------
The Macintosh release was a demo.  It did not expire and showed no dialog --
instead it spoke its nag.  The first utterance after the engine started was
preceded by a spoken announcement ("This is a demonstration of speech
synthesis, nineteen ninety-six", in the language of the current voice), and
after that every fourth word carried an injected "nineteen ninety-six" or
"one hundred".

That machinery has been removed from the engine's own code.  Nothing is
timed, counted or announced, and every voice speaks only the text it is
given.  The engine imports no date or clock function at all, so it never had
an expiry to remove.

Consonant clarity
-----------------
The 1996 engine's noise source produces nothing, so it has no fricatives at
all.  Measured on its own output, the start of "see", "fee", "thief" and "he"
is the same voiced pulse train as the start of "heed": wherever an s, f, th or
h belongs, the engine emits voicing instead.  Its aspiration control confirms
it -- raising it scales the voice down and adds no noise, and at full scale the
output is silence.  A formant synthesizer with no noise source has no s.

So the frication is synthesised here, and it is placed from the engine's own
phoneme stream rather than guessed from the audio: the engine reports every
phone it speaks, and each language pack carries its own symbol table, so each
consonant gets noise of the right kind, in the right place, at a level that
follows the voice.  The stops get a release burst too, one per place of
articulation and fired where the release happens -- without it a d or a b is
only a silent ramp into the vowel after it.  A shelf also lifts what the engine
does produce above 2.6 kHz.  The Consonant clarity setting controls how much
of this is applied:

    0    the untouched 1996 output
    40   the default a fresh install uses
    100  the strongest setting

Set it to 0 if you would rather have the engine exactly as it shipped.

Clicks when you arrow or tab quickly
------------------------------------
The engine's output sits on a large, speech-dependent DC offset -- measured
per buffer it walks up to about 1200 counts as an utterance starts.  A screen
reader abandons an utterance on every keypress, and cutting from that offset
straight to digital silence is heard as a click.

Two things now prevent it, both always on and not adjustable:

  * the offset is filtered out (25 Hz, well below the lowest pitch the engine
    can produce, so the voice itself is unchanged -- measured at 0.15 dB);
  * a cancelled utterance ends with a 5 ms ramp down to zero instead of
    stopping dead.

Parameters
----------
Every parameter runs on one 0 to 100 scale, where 0 is the engine's minimum
and 100 is the engine's maximum:

    Speaking rate       0..100  ->  0 to 999 words per minute
    Pitch               0..100  ->  the engine's full pitch-base range
    Pitch modulation    0..100  ->  how much the intonation moves
    Breathiness         0..100  ->  the engine's private aspiration control
    Volume              0..100  ->  applied in software
    Consonant clarity   0..100  ->  how much frication to synthesise (see above)

Your screen reader's own 0 to 100 sliders map onto SAPI's -10..+10, which
this engine spreads across that whole range: at 0 you get the engine's
slowest or lowest setting, at 100 its fastest or highest.

SAPI has no slider for pitch modulation, breathiness or consonant clarity, so
those are set per voice in the settings dialog.  It is in the Start menu under
Infovox 210 Settings, and setup can also put a shortcut on the desktop.

Parts
-----
    InfovoxSAPI.dll          the SAPI5 engine (32-bit)
    x64\InfovoxSAPI.dll      the SAPI5 engine (64-bit)
    infovox_host.exe         64-bit worker; owns the emulator
    unicorn.dll              the PowerPC CPU emulator
    engine\*.ivp             the 1996 engine and its voice data
    InfovoxConfig.exe        settings
    InfovoxDiagnostics.exe   writes a report if a voice will not speak

Both SAPI engines talk to the one 64-bit worker, so 32-bit and 64-bit
applications share a single copy of the emulator.  The worker starts on
demand and exits when nothing is speaking.

If a voice will not speak
-------------------------
Run Infovox 210 Diagnostics from the Start menu.  It walks every installed
voice through SAPI and writes a report to your TEMP folder.
