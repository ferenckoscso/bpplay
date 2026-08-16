bpplay 0.9.7 — Read this before you start
============================================

This disk image contains three things:
  - "bpplay" — the command-line program (signed and notarised)
  - "bpplay drop" — a drag-and-drop app (signed and notarised)
  - "bpplay play.workflow" — a Finder right-click ("Quick Action";
    signed, but can't be notarised — see Step 2)

If you're upgrading from a previous bpplay version
------------------------------------------------------
There's a fourth file on this disk image: "Remove old version.command".
If you already had bpplay installed, run this FIRST (double-click) — it
looks for old copies of "bpplay"/"bpplay drop"/"bpplay play.workflow" in
the usual places and, after confirming with you, moves them to the
Trash. Then continue with Step 1 for the new version. If this is your
first install, you can skip this. (This file is a plain script, not an
app — macOS can't notarise that kind of file, so it may still prompt
for the right-click → "Open" trick described in Step 2.)

Step 1 — Copy everything to a permanent location
--------------------------------------------------
"bpplay" and "bpplay drop" can go anywhere, e.g. your Desktop — drag
both there. "bpplay play.workflow" is different: for it to appear in
Finder's right-click menu, it must be copied into a specific system
folder, ~/Library/Services/. That folder is hidden by default:
  - Finder → hold Option and click the "Go" menu → "Library" appears →
    open it → open "Services" → drop "bpplay play.workflow" in there.
  - Or, in Finder, press Cmd+Shift+G and type: ~/Library/Services
  - If the "Services" folder doesn't exist at all (normal on a brand
    new user account that has never had a Quick Action/Service before):
    open Terminal and run `mkdir -p ~/Library/Services` — then you can
    drop "bpplay play.workflow" in there.
  - If you copied it in but "bpplay play" doesn't show up in the
    right-click → Quick Actions menu (or in the "Customize..." list):
    the background process that serves the Services menu doesn't
    always notice a new item right away. Force it from Terminal:
    `/System/Library/CoreServices/pbs -flush`
    — then open the right-click menu again, it should be there.

Step 2 — First launch (only needed for the Quick Action and the
   "Remove old version.command" above)
--------------------------------------------------------------------
"bpplay" and "bpplay drop" are signed AND notarised — no warning at
all, they just launch.

"bpplay play.workflow" is signed, but Apple's notarisation tooling
specifically doesn't support Quick Action (.workflow) files — so this
one (and the plain script "Remove old version.command", which can't be
notarised either) will still warn on first use that it's from an
"unidentified developer". Both are safe. Work around it once:

  A) From Terminal (RECOMMENDED — won't break again if Apple reshuffles
     the warning dialog):
     xattr -dr com.apple.quarantine "$HOME/Library/Services/bpplay play.workflow"
     (If you put it somewhere else, adjust the path.)

  B) From System Settings (if you'd rather not use Terminal):
     Try to open the item (double-click, or pick it from the
     right-click menu for the Quick Action) — this shows a "cannot be
     opened" error, dismiss it ("Done"). Then: System Settings →
     Privacy & Security → scroll down → an "Open Anyway" button
     appears next to that item. Try opening it again afterwards — it
     may ask for one more confirmation.
     (The older "right-click → Open → Open" two-step trick no longer
     works on recent macOS — Sequoia and later: the warning dialog
     has no "Open Anyway" button in it anymore. If that's what you're
     seeing, use A) or the System Settings route above instead.)

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
