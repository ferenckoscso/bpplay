-- bpplay drop.app — self-contained drag-and-drop launcher droplet.
-- SPDX-License-Identifier: GPL-3.0-or-later
--
-- Built via build-drop-app.sh (osacompile), then bundled into the
-- DMG together with the bpplay binary and bpplay-drop-runtime.sh
-- (Contents/Resources/) — zero configuration needed after install.
--
-- Drop a file, several files, or a single folder onto this app's icon.
-- It forwards the paths to bpplay-drop-runtime.sh, which opens a
-- Terminal window and starts bit-perfect playback there.

on open theFiles
	set posixPaths to {}
	repeat with f in theFiles
		set end of posixPaths to POSIX path of f
	end repeat

	set appPosixPath to POSIX path of (path to me)
	set runtimeScript to appPosixPath & "Contents/Resources/bpplay-drop-runtime.sh"

	set cmd to quoted form of runtimeScript
	repeat with p in posixPaths
		set cmd to cmd & " " & quoted form of p
	end repeat

	do shell script cmd
end open

on run
	display dialog "Húzz rá egy zenefájlt, több fájlt, vagy egy albummappát ennek az ikonnak a lejátszáshoz." with title "bpplay drop" buttons {"OK"} default button "OK"
end run
