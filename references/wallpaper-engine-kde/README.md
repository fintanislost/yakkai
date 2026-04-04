# Wallpaper Engine KDE Reference

This directory contains a curated local copy of the installed `Wallpaper Engine for Kde` plugin files that are relevant to Yakkai's video-wallpaper research.

Origin on this machine:

- Wallpaper package: `/usr/share/plasma/wallpapers/com.github.catsout.wallpaperEngineKde`
- Compiled QML module: `/usr/lib/qt6/qml/com/github/catsout/wallpaperEngineKde`
- Debug sources: `/usr/src/debug/plasma6-wallpapers-wallpaper-engine-git/plasma6-wallpapers-wallpaper-engine-git`

Copied on: `2026-03-13`

Scope of this reference:

- `wallpaper-package/`
  - Plasma wallpaper package files for backend loading, runtime behavior, and config keys
- `qt-qml-module/`
  - QML module manifest showing the compiled plugin types exposed to QML
- `source/`
  - Source files for the compiled types most relevant to video, scene rendering, and input/pause behavior

Key findings:

- The plugin supports `video`, `web`, and `scene` wallpaper types through a loader in `wallpaper-package/contents/ui/main.qml`.
- Video playback can run through `QtMultimedia` alone via `wallpaper-package/contents/ui/backend/QtMultimedia.qml`.
- The full plugin is not QML-only. It imports a compiled module, `com.github.catsout.wallpaperEngineKde`, whose installed `qmldir` exposes `PluginInfo`, `MouseGrabber`, `SceneViewer`, and `Mpv`.
- Additional runtime behavior such as TTY switching, mouse forwarding, scene playback, and mpv playback depends on compiled code.

What is likely reusable for Yakkai:

- The idea of a small backend loader that swaps between content modes.
- A minimal `QtMultimedia` video backend for local file playback.
- Later, optional pause policies based on focused/maximized/fullscreen windows and battery state.

What is probably out of scope for a first Yakkai video milestone:

- Wallpaper Engine scene playback
- Workshop integration
- Mouse input forwarding into the wallpaper
- Multiple native playback backends
