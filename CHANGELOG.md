# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [0.9] — 2026-06-17

First public release (release candidate) — macOS Core edition.

### Added
- Bit-perfect playback through the CoreAudio HAL, with hog mode and integer mode
- Whole-track-in-RAM model with `mlock`; the realtime IOProc performs nothing but `memcpy`
- WAV (16/24/32-bit integer, 32-bit float), FLAC (16/24-bit), AIFF/AIFC (uncompressed)
- DSD (.dsf) from DSD64 to DSD256 via DoP; `.dsf` files switch to DoP automatically
- FLAC Vorbis metadata reading and MQA **detection** (not decoding)
- Mixed-format playlists split into format segments, gapless within a segment
- Recursive folder playback (`-dir`), sorted by name, album-first ordering
- Transport keys during playback: `n` / `b` / space / `q`; status line with track and queue times
- Bilingual (EN/HU) messages, Hungarian selected with `-hu`
- `--version` flag

### Fixed before the public release
- Heap buffer overflow in the format-matching fast path: a file whose sample data ended
  mid-frame caused a copy past the end of the destination buffer
- The end-of-queue fade was confined to a single CoreAudio block, so in practice it never
  completed and the output still stepped to zero — the very click it was meant to prevent.
  The ramp state now lives in the player and continues across callbacks
- Pausing was an instant mute; it is now ramped as well, and resuming ramps back up
- DoP/DSD streams are excluded from the ramps, since scaling a DoP word would destroy its
  marker byte and the DSD bits along with it
- DSF block padding is now trimmed using the header's sampleCount instead of being played
- Division by zero and unsigned wrap-around on malformed headers (DSF block size and data
  chunk size, WAV/AIFF channel count and sample rate, AIFF SSND offset)
- Out-of-range reads guarded in the DSF de-interleave loop
- Integer and float WAV sources of the same width are no longer grouped into one format segment
- `atexit` was re-registered once per segment; `malloc` return values were unchecked in the
  device-enumeration helpers
- Recursive folder scanning no longer holds its ~1.25 MB scratch buffers across the descent
- `-dir` now appends to the queue instead of replacing it, and the 256-file cap is reported
- All diagnostics are English; `-dop` is documented as accepted-but-inert
- `main()` unconditionally returned 0 regardless of whether the user quit with `q`/Ctrl+C,
  so a shell script chaining multiple `bpplay` invocations could never detect an early
  exit; it now returns 1 on user quit, 0 on normal completion

### Known limitations
- The binary is not notarised (Gatekeeper warns on first launch)
- Higher DoP rates (352.8 / 705.6 kHz) do not pass on every DAC or USB chain
- Memory use can exceed the file size (by design — see README, "Things worth knowing")

[0.9]: https://github.com/ferenckoscso/bpplay/releases/tag/v0.9
