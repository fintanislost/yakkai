# Fixes

This document tracks the practical fixes and debugging outcomes that got `Paper Gradient` to a working state for local video, Wallpaper Engine video, and first-pass Wallpaper Engine web testing.

## Wallpaper Engine Web bridge

Problem:
- The earlier web bridge depended on a `QWebChannel` bootstrap inside `QtWebEngine`.
- On the host, the runtime logged `QWebChannel is not defined`.
- Wallpaper Engine web pages were loading, but their property bridge was unreliable or missing.

Fix:
- Reworked [WebBackground.qml](/home/peter/repos/papercompany/wallpapers/io.papercompany.gradient/contents/ui/WebBackground.qml) to use a direct JavaScript compatibility shim instead of a live `QWebChannel` dependency.
- The shim now creates and maintains:
  - `window.wpeQml`
  - `window.wallpaperRegisterAudioListener(...)`
  - cached property delivery into `window.wallpaperPropertyListener`
- Property payloads are now pushed with `runJavaScript(...)` after page load and delivered directly into the page contract used by Wallpaper Engine web projects.

Result:
- The core bridge bootstrap no longer depends on `QWebChannel`.
- Remaining WE web failures are now page-specific or graphics-stack-specific, which is easier to reason about.

## Stale Plasma QML cache

Problem:
- Even after updating the package, `plasmashell` kept logging strings that no longer existed in the installed [WebBackground.qml](/home/peter/.local/share/plasma/wallpapers/io.papercompany.gradient/contents/ui/WebBackground.qml).
- That showed Plasma was still running an older in-memory or cached QML instance.

Fix:
- Clear the Plasma QML cache and restart `plasmashell` after significant runtime changes:

```bash
rm -rf ~/.cache/plasmashell/qmlcache ~/.cache/plasmashell/qtpipelinecache-*
killall plasmashell
plasmashell --replace &
```

Result:
- Plasma reloads the current wallpaper code from disk.
- Updated WE web bridge behavior becomes visible immediately.

## VM WebGL limitation

Problem:
- Inside the VM, WE web wallpapers hit:
  - `WebGL1 blocklisted`
  - `egl: failed to create dri2 screen`
  - `eglInitialize failed: EGL_NOT_INITIALIZED`
- Some pages also displayed an explicit `WebGL is unavailable` warning.

Diagnosis:
- The VM issue was not the wallpaper plugin itself.
- The guest graphics path was the blocker.
- The libvirt XML was structurally correct for virgl, but the host-side render path was not usable for this test stack.

Result:
- The VM is acceptable for non-WebGL checks and general workflow validation.
- It is not the right place to validate WE web wallpapers that require working WebGL on this hardware/software stack.

## Host-side validation path

Decision:
- Pivoted WE web testing to the main machine instead of the VM.

Why:
- The host avoids the VM virgl/SPICE/EGL/WebGL bottleneck.
- Host failures are more likely to reflect real compatibility issues in the plugin or the target wallpaper.

## Known remaining realities

- Some Wallpaper Engine web wallpapers still fail for page-specific reasons unrelated to the bridge.
- Some WE web wallpapers require WebGL and will not work in environments where Chromium/`QtWebEngine` blocklists or cannot initialize that graphics path.
- `Wallpaper Engine Web` is functional, but not yet a full reimplementation of the upstream Wallpaper Engine runtime.

## Useful commands

Update the wallpaper package during development:

```bash
kpackagetool6 -t Plasma/Wallpaper -u /home/peter/repos/papercompany/wallpapers/io.papercompany.gradient
```

Watch runtime logs:

```bash
journalctl --user -f | grep "Paper Gradient"
```

Do a temporary package-structure smoke test:

```bash
tmp_root="$(mktemp -d)"
kpackagetool6 -t Plasma/Wallpaper -i "/home/peter/repos/papercompany/wallpapers/io.papercompany.gradient" -p "$tmp_root"
```
