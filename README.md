# Paper Gradient

`Paper Gradient` is a KDE Plasma 6 wallpaper plugin focused on a small set of high-value background modes. The current version supports configurable gradients, local video playback, and first-pass Wallpaper Engine video and web modes while staying easy to test in a Plasma VM.

## Current scope

- Plasma 6 wallpaper package under `wallpapers/io.papercompany.gradient`
- Top-level content selector for gradient or local video
- Two configurable colors
- Preset picker for common palette and motion combinations
- Adjustable gradient angle
- Optional slow animation
- Adjustable drift amount
- Optional vignette for edge darkening
- Optional time-of-day mode with separate day and night palettes
- Explicit mode selector so manual and time-of-day editing stay separate
- Local video file playback through `QtMultimedia`
- Wallpaper Engine video-project scanning from a Steam library
- Wallpaper Engine web-project scanning from a Steam library
- Wallpaper Engine scene-project scanning and safe placeholder selection from a Steam library
- Video sizing controls for crop, fit, and stretch
- Audio mute control for media modes
- Persistent settings through Plasma wallpaper configuration

## Repo layout

```text
.
|-- AGENTS.md
|-- fixes.md
|-- LICENSE
|-- README.md
|-- references/
|   `-- wallpaper-engine-kde/
|       |-- README.md
|       |-- qt-qml-module/
|       |   `-- qmldir
|       |-- source/
|       |   `-- src/
|       |       |-- MouseGrabber.cpp
|       |       |-- MouseGrabber.hpp
|       |       |-- TTYSwitchMonitor.cpp
|       |       |-- TTYSwitchMonitor.hpp
|       |       |-- backend_mpv/
|       |       |   |-- MpvBackend.cpp
|       |       |   `-- MpvBackend.hpp
|       |       `-- backend_scene/
|       |           `-- qml_helper/
|       |               |-- SceneBackend.cpp
|       |               `-- SceneBackend.hpp
|       `-- wallpaper-package/
|           |-- metadata.json
|           `-- contents/
|               |-- config/
|               |   `-- main.xml
|               `-- ui/
|                   |-- Common.qml
|                   |-- PowerSource.qml
|                   |-- WindowModel.qml
|                   |-- main.qml
|                   |-- qmldir
|                   `-- backend/
|                       |-- InfoShow.qml
|                       |-- Mpv.qml
|                       |-- QtMultimedia.qml
|                       `-- Scene.qml
`-- wallpapers/
    `-- io.papercompany.gradient/
        |-- metadata.json
        `-- contents/
            |-- config/
            |   `-- main.xml
            |-- tools/
            |   `-- we_video_scan.py
            `-- ui/
                |-- GradientBackground.qml
                |-- SceneGuard.qml
                |-- ScenePlaceholder.qml
                |-- SceneRuntime.qml
                |-- VideoBackground.qml
                |-- WebBackground.qml
                |-- config.qml
                `-- main.qml
```

## Install

Install the wallpaper package for the current user:

```bash
kpackagetool6 -t Plasma/Wallpaper -i wallpapers/io.papercompany.gradient
```

After installation, open the Plasma wallpaper picker and choose `Paper Gradient` from the wallpaper type list.

## Update during development

```bash
kpackagetool6 -t Plasma/Wallpaper -u wallpapers/io.papercompany.gradient
```

## Remove

```bash
kpackagetool6 -t Plasma/Wallpaper -r io.papercompany.gradient
```

## Quick validation

This repo does not include a full Plasma runtime harness. The fastest checks are:

```bash
qmllint wallpapers/io.papercompany.gradient/contents/ui/main.qml
qmllint wallpapers/io.papercompany.gradient/contents/ui/GradientBackground.qml
qmllint wallpapers/io.papercompany.gradient/contents/ui/VideoBackground.qml
qmllint wallpapers/io.papercompany.gradient/contents/ui/WebBackground.qml
qmllint wallpapers/io.papercompany.gradient/contents/ui/config.qml
```

For a package-structure smoke test without writing into your user Plasma directory, install into a fresh temporary package root:

```bash
tmp_root="$(mktemp -d)"
kpackagetool6 -t Plasma/Wallpaper -i "$(pwd)/wallpapers/io.papercompany.gradient" -p "$tmp_root"
```

## Working rules

- Keep `README.md` updated whenever the plugin behavior, config surface, commands, file layout, or workflow changes.
- Do not create git commits unless explicitly requested.

## Reference materials

`references/wallpaper-engine-kde` contains a curated local copy of the installed `Wallpaper Engine for Kde` plugin files that matter for video wallpaper research.

- `wallpaper-package/`: the Plasma wallpaper package QML and config files
- `qt-qml-module/`: the installed compiled QML module manifest
- `source/`: debug-source copies for the compiled module types used by the plugin

This reference is intentionally partial. It focuses on backend loading, pause policy, and the custom QML module rather than the full Workshop browser UI.

`fixes.md` records the concrete issues and debugging fixes that got the current video and Wallpaper Engine paths working in practice.

## Configuration keys

- `StartColor`: gradient start color
- `EndColor`: gradient end color
- `Angle`: base gradient angle in degrees
- `Animate`: enables slow angle drift
- `AnimationDuration`: full animation cycle length in seconds
- `DriftDegrees`: maximum angular drift applied during animation
- `VignetteStrength`: edge darkening amount from `0` to `60`
- `UseTimeOfDay`: enables local-time blending between day and night palettes
- `DayStartHour`: hour when blending into the day palette begins
- `NightStartHour`: hour when blending into the night palette begins
- `TransitionMinutes`: blend duration at each schedule boundary
- `DayStartColor`: first color used for the day palette
- `DayEndColor`: second color used for the day palette
- `NightStartColor`: first color used for the night palette
- `NightEndColor`: second color used for the night palette
- `ContentMode`: selects `Gradient`, `Video`, `Wallpaper Engine Video`, or `Wallpaper Engine Web`
- `VideoSource`: local video file path
- `VideoFillMode`: `Crop`, `Fit`, or `Stretch`
- `VideoMuted`: mutes wallpaper audio
- `WEVideoLibraryPath`: shared Steam library root used for Wallpaper Engine scanning
- `WEVideoProjectPath`: selected Wallpaper Engine `project.json` path
- `WEVideoProjectTitle`: selected Wallpaper Engine project title
- `WEVideoSource`: resolved local video file for the selected Wallpaper Engine project
- `WEWebProjectPath`: selected Wallpaper Engine web `project.json` path
- `WEWebProjectTitle`: selected Wallpaper Engine web project title
- `WEWebSource`: resolved local entry HTML file for the selected Wallpaper Engine web project
- `WEWebPropertiesJson`: serialized default Wallpaper Engine user properties for the selected web project
- `WESceneProjectPath`: selected Wallpaper Engine scene `project.json` path
- `WESceneProjectTitle`: selected Wallpaper Engine scene project title
- `WESceneSource`: resolved local scene source for the selected Wallpaper Engine scene project
- `WESceneSourceKind`: the resolved scene source filename, such as `scene.pkg` or `scene.json`
- `WESceneExperimentalEnabled`: enables the guarded experimental native scene renderer attempt
- `WESceneMouseInput`: enables direct scene mouse and hover input for the experimental scene runtime

## Content modes

The settings panel now starts with a top-level `Content` selector:

- `Gradient`: keeps the original Paper Gradient renderer
- `Video`: plays a local video file through `QtMultimedia`
- `Wallpaper Engine Video`: scans a Steam library for Wallpaper Engine `video` projects and reuses the same `QtMultimedia` playback path
- `Wallpaper Engine Web`: scans a Steam library for Wallpaper Engine `web` projects and loads their local HTML entrypoints through `QtWebEngine`
- `Wallpaper Engine Scene`: scans a Steam library for Wallpaper Engine `scene` projects, defaults to a safe placeholder, and can optionally attempt a guarded native renderer path

## Time-of-day mode

When `Content` is set to `Gradient`, the settings panel exposes a `Mode` selector:

- `Manual gradient`: edit a single two-color gradient for the whole day
- `Time of day`: edit separate day and night palettes plus the transition schedule

In `Time of day`, the wallpaper ignores the manual start and end colors and instead blends between the configured day and night palettes according to the local system time. The first transition starts at `DayStartHour`, the second starts at `NightStartHour`, and each transition lasts `TransitionMinutes`.

## Media modes

`Video` mode is intentionally narrow in the first implementation:

- local file only
- `QtMultimedia` backend only
- infinite loop playback
- file selected through the wallpaper settings picker
- mute toggle
- crop, fit, and stretch sizing

Actual playback support depends on the multimedia codecs installed in the current Plasma system or VM. If a file installs correctly but does not render, test a different container or codec before assuming the wallpaper package is broken.

`Wallpaper Engine Video` mode is also intentionally narrow:

- Steam library picker in the settings page
- scans only Wallpaper Engine projects whose `project.json` type is `video`
- resolves the actual media file during settings-time and saves it into the wallpaper config
- reuses the same `QtMultimedia` runtime backend as local video mode
- does not support Wallpaper Engine `scene` projects yet

`Wallpaper Engine Web` mode is the first-pass web path:

- shares the same Steam library picker and scan helper as Wallpaper Engine video mode
- scans only Wallpaper Engine projects whose `project.json` type is `web`
- loads the resolved local HTML entry file through `QtWebEngine`
- injects a direct JavaScript compatibility shim for `window.wpeQml`, `window.wallpaperPropertyListener`, and `window.wallpaperRegisterAudioListener(...)` instead of depending on a live `QWebChannel` bootstrap
- does not yet emulate the full Wallpaper Engine audio, media-integration, or per-project settings stack
- explicitly enables `QtWebEngine` WebGL and accelerated 2D canvas support, though some VM GPU stacks may still report WebGL as unavailable

The settings-side scan uses the bundled `contents/tools/we_video_scan.py` helper, so the Plasma system running the settings UI needs `python3` available. The web mode requires `QtWebEngine` in the Plasma session.

`Wallpaper Engine Scene` mode is currently a guarded research path:

- shares the same Steam library picker and scan helper as the other Wallpaper Engine modes
- scans only Wallpaper Engine projects whose `project.json` type is `scene`
- resolves the real on-disk scene source conservatively, preferring an existing `scene.pkg` or `scene.json` instead of trusting metadata alone
- stores the selected project and source path in the wallpaper config
- shows placeholder diagnostics for source kind, source existence, Wallpaper Engine assets-path existence, and reference `SceneViewer` module presence
- keeps the placeholder as the default path because crash resistance is the current priority
- exposes an explicit `Enable experimental native scene renderer` toggle in the settings
- reuses the existing sizing and mute controls when the experimental scene renderer is enabled
- keeps direct scene mouse and hover input off by default, with an explicit opt-in toggle for testing parallax-sensitive scenes
- routes that experimental attempt through a guard component that falls back to the placeholder if the scene runtime fails to load cleanly
- restarts the guarded experimental scene runtime when sizing or mouse-input settings change, so backend behavior can be tested from a clean launch
- logs scene guard, source, fill-mode, mouse-input, first-frame state, geometry, DPR, and Qt Quick graphics API details through the existing `[Paper Gradient]` journal prefix

This mode exists to validate selection, source resolution, and future runtime assumptions while making the native scene path an explicit, guarded opt-in.

Current limitation from host-side validation:
- the QML wrapper now launches scenes cleanly, restarts on sizing and mouse-input changes, and matches the native swapchain size, but some scenes still render offset because the underlying `SceneViewer` backend continues to hit `GL_INVALID_ENUM` in its external-texture import path

If the video backend itself cannot initialize in the current Plasma session, Paper Gradient now keeps the wallpaper selected and shows an in-wallpaper error message instead of silently falling back to another content path.

If the web backend cannot initialize in the current Plasma session, Paper Gradient keeps the wallpaper selected and shows an in-wallpaper error message instead of silently falling back to another content path.

If a selected video still renders as black, the wallpaper keeps a small status overlay visible until the first decoded frame arrives. That overlay now distinguishes loading, missing video tracks, stalled playback, and cases where playback time advances without any frames reaching the wallpaper.

For Wallpaper Engine web mode, the on-wallpaper status overlay is now limited to actual page-load problems. If the page is already rendering, Wallpaper Engine bridge or property-payload timing issues are logged instead of being shown on screen.

For runtime diagnostics from a live Plasma session, the wallpaper now emits QML logs with a `"[Paper Gradient]"` prefix. In the VM, the simplest way to watch them is:

```bash
journalctl --user -f | rg "Paper Gradient"
```

Those logs include the resolved video source, loader status, `MediaPlayer` state transitions, `hasVideo`, playback position, and frame-delivery events.

For Wallpaper Engine web debugging, the same `"[Paper Gradient]"` log prefix now also includes `QtWebEngine` console messages emitted by the loaded page.

## Preset helper

The wallpaper settings panel includes a preset picker that seeds the current controls with a ready-made palette and motion profile. It is a quick-fill helper, not a locked mode, so you can choose a preset and then keep tweaking the individual fields.

## Next useful extensions

- Expose accent-color behavior explicitly
- Add a third color stop or midpoint control
- Add presets that seed both day and night palettes
- Add a gradient overlay mode on top of video
- Add pause policy based on focused, maximized, or fullscreen windows
- Add playback rate and optional audio controls
- Replace placeholder metadata before publishing

## Wallpaper Engine research notes

Wallpaper Engine integration is split by source type:

- `video`: feasible with a small scan helper plus the existing `QtMultimedia` backend
- `web`: feasible with `QtWebEngine`, `QtWebChannel`, and a lightweight compatibility bridge
- `scene`: scan-and-select support exists, plus a guarded experimental renderer path that depends on compiled `SceneViewer` types and a larger native runtime

The closest working reference remains the third-party `Wallpaper Engine for Kde` plugin under `references/wallpaper-engine-kde`.
