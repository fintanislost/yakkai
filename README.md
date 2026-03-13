# Paper Gradient

`Paper Gradient` is a minimal KDE Plasma 6 wallpaper plugin scaffold. The current version renders a configurable two-color gradient with an optional slow drift animation so the package stays simple, QML-only, and easy to extend.

## Current scope

- Plasma 6 wallpaper package under `wallpapers/io.papercompany.gradient`
- Two configurable colors
- Preset picker for common palette and motion combinations
- Adjustable gradient angle
- Optional slow animation
- Adjustable drift amount
- Optional vignette for edge darkening
- Optional time-of-day mode with separate day and night palettes
- Explicit mode selector so manual and time-of-day editing stay separate
- Persistent settings through Plasma wallpaper configuration

## Repo layout

```text
.
|-- AGENTS.md
|-- LICENSE
|-- README.md
`-- wallpapers/
    `-- io.papercompany.gradient/
        |-- metadata.json
        `-- contents/
            |-- config/
            |   `-- main.xml
            `-- ui/
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

## Time-of-day mode

The settings panel now has an explicit `Mode` selector:

- `Manual gradient`: edit a single two-color gradient for the whole day
- `Time of day`: edit separate day and night palettes plus the transition schedule

In `Time of day`, the wallpaper ignores the manual start and end colors and instead blends between the configured day and night palettes according to the local system time. The first transition starts at `DayStartHour`, the second starts at `NightStartHour`, and each transition lasts `TransitionMinutes`.

## Preset helper

The wallpaper settings panel includes a preset picker that seeds the current controls with a ready-made palette and motion profile. It is a quick-fill helper, not a locked mode, so you can choose a preset and then keep tweaking the individual fields.

## Next useful extensions

- Expose accent-color behavior explicitly
- Add a third color stop or midpoint control
- Add presets that seed both day and night palettes
- Replace placeholder metadata before publishing
