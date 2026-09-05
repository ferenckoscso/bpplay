#!/usr/bin/env python3
"""Set the Run Shell Script action's COMMAND_STRING in a .workflow's
document.wflow, via plistlib (no shell-quoting fragility, unlike
PlistBuddy -c "Set ...").

Usage: set-quickaction-command.py <document.wflow> <command-string-file>
"""
import plistlib
import sys

wflow_path, command_file = sys.argv[1], sys.argv[2]

with open(command_file, "r") as f:
    command_string = f.read()

with open(wflow_path, "rb") as f:
    data = plistlib.load(f)

data["actions"][0]["action"]["ActionParameters"]["COMMAND_STRING"] = command_string

with open(wflow_path, "wb") as f:
    plistlib.dump(data, f)

print(f"COMMAND_STRING set ({len(command_string)} chars) in {wflow_path}")
