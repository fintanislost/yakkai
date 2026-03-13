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

## Preset helper

The wallpaper settings panel includes a preset picker that seeds the current controls with a ready-made palette and motion profile. It is a quick-fill helper, not a locked mode, so you can choose a preset and then keep tweaking the individual fields.

## Next useful extensions

- Add time-of-day switching
- Expose accent-color behavior explicitly
- Add a third color stop or midpoint control
- Replace placeholder metadata before publishing
