# Notices

The code written for this project is licensed under the MIT License (see [LICENSE](LICENSE)). The material
below is not covered by that licence and stays under its own terms.

## Infovox 210 engine and voices

The README says of the software this project is built on:

> Infovox 210 is the work of Infovox AB and, before that, of the speech group at
> KTH. This repository does not claim any right to it. It is published as a
> preservation and accessibility project: the software has been unavailable and
> unsupported for decades, runs on hardware almost nobody still has, and would
> otherwise simply be lost. If a rights holder objects, it will be taken down.

That covers:

- `bin/`: the original 1996 resource fork of the Macintosh component, exactly as extracted. The version strings in
  these files read "© 1994 Infovox AB, Sweden." for most of the language packs, "© 1995, Infovox AB, Sweden." for
  the Infovox 210 component itself and "© 1996 Infovox AB, Sweden." for the Spanish and French packs.
- `engine/` and `output/engine/`: the engine packs (`.ivp`) built from `bin/` with the demo patches applied. They
  contain the Infovox 210 engine and its voices, and the installer built from `installer/` installs them.
- `samples/`: speech rendered with those voices.

## Published research papers

`bin/` also holds the published research papers behind the engine, as PDFs: Carlson & Granström on the GLOVE
synthesizer, the Liljencrants-Fant glottal model, and the KTH prosody and text-to-speech work of the late 1980s
and early 1990s. They are other people's published work and are not covered by the MIT License.

## Unicorn CPU emulator

The README says: "Unicorn is licensed under the GPLv2 by its own authors; see
[unicorn-engine.org](https://www.unicorn-engine.org/)."

`third_party/unicorn/` holds Unicorn's headers, its import library and `unicorn.dll`. The same `unicorn.dll` is in
`output/` and is installed by the installer. None of it is covered by the MIT License. The headers carry their
authors' own notices: most are marked as by Nguyen Anh Quynh, with years between 2014 and 2021, and say "This file
is released under LGPL2. See COPYING.LGPL2 in root directory for more details"; `tricore.h` says it was created for
Unicorn Engine by Eric Poole, 2022, "Copyright 2022 Aptiv". Those notices are left exactly as they are. The
licence file they point to (`COPYING.LGPL2`) is not part of this repository; see
[unicorn-engine.org](https://www.unicorn-engine.org/).
