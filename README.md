# Paper Gradient

`Paper Gradient` is a KDE Plasma 6 wallpaper plugin focused on a small set of high-value background modes. The current version supports both configurable gradients and local video playback while staying easy to test in a Plasma VM.

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
- Video sizing controls for crop, fit, and stretch
- Video soundtrack muted by default
- Persistent settings through Plasma wallpaper configuration

## Repo layout

```text
.
|-- AGENTS.md
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
                `-- ui/
                    |-- GradientBackground.qml
                    |-- VideoBackground.qml
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
- `ContentMode`: selects `Gradient` or `Video`
- `VideoSource`: local video file path
- `VideoFillMode`: `Crop`, `Fit`, or `Stretch`
- `VideoMuted`: mutes wallpaper audio

## Content modes

The settings panel now starts with a top-level `Content` selector:

- `Gradient`: keeps the original Paper Gradient renderer
- `Video`: plays a local video file through `QtMultimedia`

## Time-of-day mode

When `Content` is set to `Gradient`, the settings panel exposes a `Mode` selector:

- `Manual gradient`: edit a single two-color gradient for the whole day
- `Time of day`: edit separate day and night palettes plus the transition schedule

In `Time of day`, the wallpaper ignores the manual start and end colors and instead blends between the configured day and night palettes according to the local system time. The first transition starts at `DayStartHour`, the second starts at `NightStartHour`, and each transition lasts `TransitionMinutes`.

## Video mode

`Video` mode is intentionally narrow in the first implementation:

- local file only
- `QtMultimedia` backend only
- infinite loop playback
- file selected through the wallpaper settings picker
- mute toggle
- crop, fit, and stretch sizing

Actual playback support depends on the multimedia codecs installed in the current Plasma system or VM. If a file installs correctly but does not render, test a different container or codec before assuming the wallpaper package is broken.

If the video backend itself cannot initialize in the current Plasma session, Paper Gradient now keeps the wallpaper selected and shows an in-wallpaper error message instead of silently falling back to another content path.

If a selected video still renders as black, the wallpaper keeps a small status overlay visible until the first decoded frame arrives. That overlay now distinguishes loading, missing video tracks, stalled playback, and cases where playback time advances without any frames reaching the wallpaper.

For runtime diagnostics from a live Plasma session, the wallpaper now emits QML logs with a `"[Paper Gradient]"` prefix. In the VM, the simplest way to watch them is:

```bash
journalctl --user -f | rg "Paper Gradient"
```

Those logs include the resolved video source, loader status, `MediaPlayer` state transitions, `hasVideo`, playback position, and frame-delivery events.

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

## Video research notes

Video remains the primary feature target, but Plasma 6 does not ship a built-in video wallpaper plugin in the wallpaper set installed on this machine. The closest working reference is the third-party `Wallpaper Engine for Kde` plugin under `references/wallpaper-engine-kde`.

The main takeaways from that reference are:

- A simple video path can be done in QML with `QtMultimedia` and `MediaPlayer`.
- The reference plugin adds a second, optional `mpv` backend through a compiled QML plugin.
- It also uses extra compiled types for scene rendering, mouse capture, and TTY/suspend handling.
- Most of the complexity is around pause policy, battery behavior, and backend selection, not basic playback.

That points to a pragmatic first implementation for this repo:

- local file video only
- `QtMultimedia` backend only
- muted by default
- fit/crop/scale controls
- pause when a window is focused, maximized, or fullscreen as a later step
