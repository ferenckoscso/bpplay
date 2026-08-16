bpplay 0.9.6 — Read this before you start
============================================

This disk image contains three things:
  - "bpplay" — the command-line program
  - "bpplay drop" — a drag-and-drop app
  - "bpplay play.workflow" — a Finder right-click ("Quick Action")

If you're upgrading from a previous bpplay version
------------------------------------------------------
There's a fourth file on this disk image: "Remove old version.command".
If you already had bpplay installed, run this FIRST (double-click) — it
looks for old copies of "bpplay"/"bpplay drop"/"bpplay play.workflow" in
the usual places and, after confirming with you, moves them to the
Trash. Then continue with Step 1 for the new version. If this is your
first install, you can skip this. (This file isn't notarised either —
it needs the same right-click → "Open" trick on first run as Step 2
below; a plain double-click may just warn you.)

Step 1 — Copy everything to a permanent location
--------------------------------------------------
"bpplay" and "bpplay drop" can go anywhere, e.g. your Desktop — drag
both there. "bpplay play.workflow" is different: for it to appear in
Finder's right-click menu, it must be copied into a specific system
folder, ~/Library/Services/. That folder is hidden by default:
  - Finder → hold Option and click the "Go" menu → "Library" appears →
    open it → open "Services" → drop "bpplay play.workflow" in there.
  - Or, in Finder, press Cmd+Shift+G and type: ~/Library/Services

Step 2 — First launch (Gatekeeper warning)
---------------------------------------------
None of the three are notarised by Apple, so macOS will warn on first
launch/use that they are from an "unidentified developer". All three
are safe — they simply haven't gone through Apple's notarisation
process. Work around this once per item:

  A) From Terminal (for "bpplay", simplest):
     xattr -d com.apple.quarantine ~/Desktop/bpplay

  B) From Finder (works for all three; the only way for "bpplay drop"
     and "bpplay play.workflow"):
     Right-click the file/app → "Open" → click "Open" again in the
     dialog that appears. For the Quick Action, do this once by
     right-clicking "bpplay play.workflow" itself inside
     ~/Library/Services/, BEFORE using it from the context menu.

Step 3 — Playback by dragging (no Terminal needed)
-------------------------------------------------------
Drop a music file, several files, or an album folder onto the "bpplay
drop" icon. A Terminal window opens, bit-perfect playback starts, and
the transport keys are available there too:
  n = next track, b = previous, space = pause/resume, q = quit

Step 4 — Playback by right-clicking (no icon to drag onto)
-----------------------------------------------------------------
Once "bpplay play.workflow" is installed (Step 1), select one or more
music files, or a single album folder, anywhere in Finder, right-click,
and choose "bpplay play" (you may need to open the "Quick Actions"
submenu, depending on your macOS version). Same result as Step 3 — a
Terminal window opens and playback starts.

Both Step 3 and Step 4 play on the system's default audio output
(changeable in macOS System Settings → Sound, if you want to pick a
specific DAC).

Step 5 (advanced) — Basic usage directly from Terminal
-------------------------------------------------------------
If you want to select a specific DAC/output device with `-d` (instead
of the system default), or call it from a script:

  cd ~/Desktop
  ./bpplay -l                      # lists the available output devices
  ./bpplay -d 1 track.flac         # play on device index 1

Full documentation, formats, troubleshooting: see the project README:
https://github.com/ferenckoscso/bpplay

Licence: GPL-3.0-or-later (see the included LICENSE file).
