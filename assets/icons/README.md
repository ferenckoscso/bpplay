# App icons

Three variants, each with transparent corners (Apple-style rounding) and a complete `.icns`:

| File | Look | Best on |
|---|---|---|
| `bpplay_dark.icns` | Near-black background, silver "b", gold glow — the original design | Dock, app bundle, dark surfaces |
| `bpplay_graphite.icns` | Darker graphite background, silver "b", gold glow | Websites/documents on a light theme |
| `bpplay_light.icns` | Inverted: light background, dark graphite "b" | Maximum contrast on white, print |

Use one of these as the icon for a custom Automator app built from
[`tools/bpplay-drop.sh`](../tools/bpplay-drop.sh) — after saving the Automator app, select it in
Finder, choose the icon file here in Preview (`Cmd+A`, `Cmd+C`), then `Cmd+I` on the app and paste
onto the icon in the top-left of the Get Info panel.
