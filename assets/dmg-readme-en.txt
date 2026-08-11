bpplay 0.9.1 — Read this before you start
============================================

This disk image contains two things: the "bpplay" command-line program,
and the "bpplay drop" app — drop music files or an album folder onto
the latter's icon, and playback starts without any Terminal commands.

Step 1 — Copy both to a permanent location
--------------------------------------------
Drag both "bpplay" AND "bpplay drop" (both are in this disk image) to,
say, your Desktop, or a folder of your choice. Keep them together —
"bpplay drop" uses its own bundled copy of bpplay internally, but it's
convenient to also have the raw "bpplay" available for manual use from
Terminal.

Step 2 — First launch (Gatekeeper warning)
---------------------------------------------
Neither "bpplay" nor "bpplay drop" is notarised by Apple, so macOS will
warn on first launch that they are from an "unidentified developer".
Both are safe — they simply haven't gone through Apple's notarisation
process. You can work around this in two ways (once per file/app):

  A) From Terminal (for "bpplay", simpler):
     xattr -d com.apple.quarantine ~/Desktop/bpplay

  B) From Finder (works for both; the only way for "bpplay drop"):
     Right-click the file/app → "Open" → click "Open" again in the
     dialog that appears.

Step 3 — Playback by dragging (no Terminal needed)
-------------------------------------------------------
Drop a music file, several files, or an album folder onto the "bpplay
drop" icon. A Terminal window opens, bit-perfect playback starts, and
the transport keys are available there too:
  n = next track, b = previous, space = pause/resume, q = quit
It plays on the system's default audio output (changeable in macOS
System Settings → Sound, if you want to pick a specific DAC).

Step 4 (advanced) — Basic usage directly from Terminal
-------------------------------------------------------------
If you want to select a specific DAC/output device with `-d` (instead
of the system default), or call it from a script:

  cd ~/Desktop
  ./bpplay -l                      # lists the available output devices
  ./bpplay -d 1 track.flac         # play on device index 1

Full documentation, formats, troubleshooting: see the project README:
https://github.com/ferenckoscso/bpplay

Licence: GPL-3.0-or-later (see the included LICENSE file).
