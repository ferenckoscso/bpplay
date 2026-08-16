<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/logo/bpplay_logo_light_512.png">
    <img src="assets/logo/bpplay_logo_dark_512.png" alt="bpplay logo" width="220">
  </picture>
</p>

# bpplay

**A bit-perfect music player for macOS — the shortest deterministic signal path from file to DAC.**

`v0.9.6` · Core edition · GPL-3.0-or-later

---

## What is this?

bpplay is a minimalist, bit-perfect music player for macOS. It has a single purpose: to deliver
the samples of a music file to the DAC untouched, over the shortest possible deterministic path.
No software volume, no dither, no resampling, no unnecessary conversion in the signal path.
What the recording engineer captured is what reaches the DAC — bit for bit.

Typical macOS players go through the CoreAudio system mixer, which introduces resampling,
floating-point mixing and software volume. bpplay bypasses all of it:

- **Hog mode** — exclusive device access, the system mixer is excluded
- **Integer mode** — both the physical *and* the virtual stream format are integer, where the
  device supports it, so no runtime conversion takes place
- **Whole-track-in-RAM model** — the track is loaded into memory at start and pinned with `mlock`;
  there is no disk I/O during playback, and the realtime IOProc does nothing but `memcpy`
- **A single format-matching step** — once, at load time, lossless, off the realtime thread

## Supported formats

| Format | Details |
|---|---|
| WAV | 16 / 24 / 32-bit integer and 32-bit float |
| FLAC | 16 / 24-bit, with Vorbis metadata and MQA **detection** (not decoding) |
| ALAC (in M4A) | 16 / 24-bit — what iTunes/Music.app produce |
| AIFF / AIFC | 16 / 24 / 32-bit, uncompressed only (`NONE` / `sowt`) |
| DSF (DSD) | DSD64 – DSD256 via DoP (the architecture scales to DSD512) |

## Building

Zero external build dependencies — the Xcode command line tools are all you need:

```sh
cd src && make
```

Or by hand:

```sh
clang -O2 bpplay.c -o bpplay \
  -framework CoreAudio -framework CoreFoundation -framework IOKit
```

For a universal (Apple Silicon + Intel) binary:

```sh
make universal
```

## Usage

```sh
./bpplay -l                      # list available output devices
./bpplay -d 1 music.flac         # play on device index 1
./bpplay -d 1 01.flac 02.flac    # gapless playback across same-format files
./bpplay -d 1 -dir ~/Music/Album # play a whole folder, sorted by name, recursively
./bpplay --version               # version and licence
```

| Option | Meaning |
|---|---|
| `-l` | List available output devices, then exit |
| `-d <index>` | Select the output device (DAC) by index |
| `-dir <folder>` | Play a whole folder recursively, sorted by name |
| `-hu` | Hungarian messages (English is the default) |
| `-dop` | Accepted for backward compatibility; has no effect. `.dsf` files switch to DoP on their own |
| `--version` | Print version and licence information |

During playback: `n` next · `b` previous · `space` pause/resume · `q` quit

## Drag-and-drop, or right-click — without the terminal

The DMG also bundles two zero-configuration launchers, both self-contained (the `bpplay` binary is
bundled inside each) and both playing on the system's default output device:

- **`bpplay drop.app`** — drop a file, several files, or a whole album folder onto its icon, and
  playback starts in a Terminal window (transport keys still work there).
- **`bpplay play.workflow`** — a Finder Quick Action. Copy it once into `~/Library/Services/` (see
  the DMG readme), then select files/a folder anywhere in Finder, right-click, and choose
  "bpplay play".

If you want a specific DAC instead of the system default, either use `-d <index>` from Terminal, or
build your own custom Automator app with a fixed device index — see
[`tools/bpplay-drop.sh`](tools/bpplay-drop.sh) and the manual's drag-and-drop chapter.

## Things worth knowing

- **Memory use can exceed the file size** — by design. The whole track lives in RAM, and the
  format-matching step inflates the data to the output format. A ~3.9 GB DSD256 file occupies
  roughly 7.5 GB in memory. This is neither a conversion nor a loss.
- **Higher DoP rates** (352.8 and 705.6 kHz) do not pass on every DAC or USB chain. If the DAC
  cannot be set to the rate, bpplay reports it explicitly.
- **Mixed-format playlists** are split into format segments. Playback is gapless within a
  segment; at a segment boundary the DAC re-locks to the new rate, which means a short but
  clean (click-free) pause.
- **Stopping and pausing are ramped**, not cut. The ramp scales a copy of the last frame that
  was written, never a music sample, so the audible content stays bit-exact. For DoP/DSD the
  ramp is disabled on purpose, because scaling a DoP word would destroy its marker byte.
- The queue is limited to 256 files per invocation.
- bpplay is **not notarised**. Gatekeeper will warn on first launch. Either right-click →
  "Open" in Finder, or run `xattr -d com.apple.quarantine bpplay` once.

## Requirements

macOS 10.15 or later, at least 8 GB RAM (more for large DSD albums — see above).

## Validated state

bpplay is validated up to DSD256, through Gustard XMOS and Amanero Combo384/768 interfaces,
with TT and Gustard DACs. The chain was tested in three connection topologies — the DAC plugged
directly into a MacBook Air port, through a Sonnet Tech Thunderbolt 5 hub, and through a no-name
USB hub — with both expensive and cheap cabling. Bit-perfect behaviour was identical in all three
cases: the bit path is independent of the connection method.

## Documentation

- [`docs/about-bpplay.md`](docs/about-bpplay.md) — philosophy and architecture (EN / HU)
- [`docs/bpplay-manual-en.pdf`](docs/bpplay-manual-en.pdf) — full user manual (English)
- [`docs/bpplay-manual-hu.pdf`](docs/bpplay-manual-hu.pdf) — full user manual (Hungarian)
- `bpplay drop.app` / `bpplay play.workflow` (in the DMG) — ready-to-use launchers, zero
  configuration, each bundles its own `bpplay` binary. See "Drag-and-drop, or right-click" above.
- [`tools/bpplay-drop.sh`](tools/bpplay-drop.sh) — the same drag-and-drop logic as a standalone
  script, for building your **own** custom Automator app with a fixed output device. Set `BPPLAY`
  and `DEVICE` at the top before first use, and run `chmod +x tools/bpplay-drop.sh` after cloning.
- [`tools/build-dmg.sh`](tools/build-dmg.sh) / [`tools/build-drop-app.sh`](tools/build-drop-app.sh)
  / [`tools/build-quickaction.sh`](tools/build-quickaction.sh) — reproducible build scripts for the
  release DMG and the two bundled launchers.

---

## Magyar

A bpplay egy minimalista, bithelyes zenelejátszó macOS-re. Egyetlen célja van: a zenei fájl
mintáit a lehető legrövidebb, determinisztikus úton, érintetlenül eljuttatni a DAC-hoz.
Nincs szoftveres hangerő, nincs dither, nincs újramintavétel vagy fölösleges konverzió a jelútban.

A szokásos macOS-lejátszók a CoreAudio rendszermixerén keresztül dolgoznak, ami újramintavételt,
lebegőpontos keverést és szoftveres hangerőt visz a láncba. A bpplay ezt megkerüli: hog mode
(kizárólagos eszköz-hozzáférés), integer mode, teljes-RAM modell `mlock`-kal, és egyetlen,
betöltéskori formátum-illesztés — a valós idejű szálon már csak `memcpy` fut.

Fordítás: `cd src && make` (nulla külső függőség, az Xcode parancssori eszközei kellenek).
Magyar nyelvű üzenetek: `-hu` kapcsoló.

Részletes magyar kézikönyv: [`docs/bpplay-manual-hu.pdf`](docs/bpplay-manual-hu.pdf).
Filozófia és architektúra (magyarul is): [`docs/about-bpplay.md`](docs/about-bpplay.md)

---

## Licence / Licenc

Copyright (C) 2026 Koscsó Ferenc

GPL-3.0-or-later — full text in [`LICENSE`](LICENSE).
Third-party components: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

This program comes with ABSOLUTELY NO WARRANTY. This is free software, and you are welcome to
redistribute it under the terms of the GNU General Public License version 3 or later.
