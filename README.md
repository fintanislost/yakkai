# Yakkai

A KDE Plasma 6 wallpaper plugin with native Wallpaper Engine scene rendering support.

## Features

- **Gradient backgrounds** — configurable two-color gradients with animation, vignette, and time-of-day palette blending
- **Video wallpapers** — local video playback via QtMultimedia
- **Wallpaper Engine support** — scan Steam libraries for WE workshops:
  - **Video** projects via QtMultimedia
  - **Web** projects via QtWebEngine with WE API compatibility shim
  - **Scene** projects via a native Vulkan C++ renderer with puppet animation, particle systems, QuickJS script evaluation, and day-night cycle support
- **Scene property editor** — in-settings controls for WE scene toggles (rain, snow, effects, etc.)
- **Playlist mode** — sequential, random, or time-scheduled wallpaper cycling for scenes, with shared playlist libraries and per-monitor active selection
- **All Wallpapers picker** — unified browser across scene, video, and web types, restoring the selected type and project from persisted Plasma config
- **Playlist (All)** — playlists that mix scene, video, and web wallpapers, also shared across monitors
- **Thumbnail wallpaper picker** — visual grid with previews, search, and a large scrollbar for long libraries

## Install From GitHub Release

For the easiest path, download a release asset from GitHub:

- `yakkai-plasma6-archlinux-x86_64-<version>.tar.gz` — prebuilt Plasma package with the native scene module already staged
- `yakkai-source-<version>.tar.gz` — source fallback for systems whose native libraries do not match the prebuilt package

Prebuilt release package:

```bash
tar -xf yakkai-plasma6-archlinux-x86_64-<version>.tar.gz
cd yakkai-plasma6-archlinux-x86_64-<version>
./install.sh
```

If the prebuilt package does not load because Qt, KDE Frameworks, FFmpeg, or other native library versions differ on your system, use the source package instead:

```bash
tar -xf yakkai-source-<version>.tar.gz
cd yakkai-source-<version>
./scripts/install-local.sh
```

After installation, open the Plasma wallpaper picker and choose **Yakkai**.

## Install From Source

From a source checkout:

```bash
./scripts/install-local.sh
```

The installer configures CMake in `${XDG_CACHE_HOME:-$HOME/.cache}/yakkai/build`, builds the native scene backend, stages the generated QML import module into `wallpapers/io.team7.yakkai/contents/imports/io/team7/scene/`, validates the package, and installs or updates the wallpaper for the current user.

Use `--clean` if CMake should start from a fresh build directory:

```bash
./scripts/install-local.sh --clean
```

Use `--no-install` to build, stage, and validate the package without touching the installed Plasma package:

```bash
./scripts/install-local.sh --no-install
```

## Developer Build

Use the dev flag when working on the renderer or smoke-test harness:

```bash
./scripts/install-local.sh --dev
```

`--dev` uses the repo-local `./build` directory and also builds the standalone scene harness. This keeps existing tooling such as `tools/validate-scene.sh`, `smoke-tests/run.sh`, and the harness path stable.

```bash
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source /path/to/scene.pkg \
  --assets /path/to/wallpaper_engine/assets \
  --fill crop
```

Add `--capture /tmp/output.png --capture-delay-ms 10000` for automated testing.

For effect-chain debugging, the paper backend can write intermediate render-target captures and a manifest:

```bash
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source /path/to/scene.pkg \
  --assets /path/to/wallpaper_engine/assets \
  --hide-info-overlay \
  --capture /tmp/yakkai-effect-debug/final.png \
  --capture-delay-ms 8000 \
  --debug-effect-captures /tmp/yakkai-effect-debug
```

`--debug-effect-captures` is harness-only and off by default. It writes `manifest.json` plus `effect-input`, `effect-output`, and `final-publish` TGA captures for preserved effect-chain layers. Rendered effect entries include diagnostic classification fields such as `candidateFamilies`, `candidateMixFamilies`, `candidateRisk`, `candidateChainShape`, `candidateBlockedReason`, and `candidateChecks` when the policy can classify the layer. `candidateChecks` also flags high-risk stripped families with `hasBlurFamily`, `hasLutFamily`, and `hasColorGradingFamily`. These fields are diagnostic metadata only and do not imply that an effect chain is safe to render. The manifest also includes a top-level `strippedCandidates` array for effect chains that policy removed before render graph construction; these entries are metadata only and do not represent failed dumps. Simple isolated water candidates (`waterflow`, `waterripple`, and isolated `waterwaves`) can be preserved, while mixed chains, blur/LUT/color-grading paths, and protected paths remain in stripped diagnostics. These captures are run artifacts for investigation; PNG smoke baselines remain the committed source of truth.

For investigation only, combine `--debug-effect-captures` with `--debug-effect-probe-layers 168,22` to render specific stripped puppet mixed-chain layers and dump their usual effect captures. This probe is rejected without `--debug-effect-captures`, is available only through the harness, and does not change normal Plasma wallpaper behavior. The manifest records the configured `probeLayerIds` and per-layer `debugProbe` metadata so probe artifacts are visibly distinct from default policy output.

Summarize a capture manifest before comparing images:

```bash
tools/effect-capture-summary.py /tmp/yakkai-effect-debug/manifest.json
```

`tools/effect-capture-summary.py` reports stripped-candidate risk, chain-shape, family, and mix-family counts so mixed water chains can be triaged before enabling any new effect behavior. It also prints `stripped-high-risk-*` sections for blur, LUT, and color-grading candidates so Arona/background parity work can start from exact layer and shader evidence.

`tools/validate-scene.sh` also writes a debug effect manifest during validation. For the renderer-risk fixtures, it fails if `3476236738` has no allowed simple-water candidates, if any simple-water candidate remains stripped there, or if `3228578419` gains any allowed simple-water candidates.

### Manual Build

The normal build target for installable packages is `yakkai_stage_wallyakkai_scene_import`:

```bash
cmake -S . -B build
cmake --build build --target yakkai_stage_wallyakkai_scene_import --parallel
./scripts/check-package.sh
```

Then install or update the staged package:

```bash
# First install
kpackagetool6 -t Plasma/Wallpaper -i wallpapers/io.team7.yakkai

# Subsequent updates
kpackagetool6 -t Plasma/Wallpaper -u wallpapers/io.team7.yakkai
```

## Dependencies

Yakkai requires KDE Plasma 6, Qt 6.6 or newer with Core/Gui/Qml/Quick, CMake 3.24 or newer, a C++20 compiler, Vulkan development files, and liblz4. Qt Multimedia and Qt WebEngine runtime packages are needed for video and web wallpaper modes. FFmpeg development packages enable Wallpaper Engine video textures in native scene rendering when available.

On distro packages, look for the Qt Quick/QML development packages, Qt Multimedia, Qt WebEngine, `extra-cmake-modules`, `vulkan-headers`/`vulkan-loader`, `lz4`, and FFmpeg libraries (`libavformat`, `libavcodec`, `libswscale`, `libavutil`). The Python scanner used by the wallpaper package needs Python 3.

## Package Validation

```bash
./scripts/check-package.sh
```

The package check verifies required Plasma files, verifies the generated native QML import files are staged, runs `qmllint` when available, and performs a throwaway `kpackagetool6` install. Use `--skip-kpackage` when only the file and QML checks are needed.

### Render Regression Checks

Use the local smoke-test harness before renderer-risk changes and before releases:

```bash
./smoke-tests/run.sh --coverage
./smoke-tests/run.sh --suite quick
./smoke-tests/run.sh --suite deep
./smoke-tests/run.sh --suite release --strict --require-assets
```

The quick suite is for normal development loops. The deep suite is required for changes affecting pixels, scene loading, animation timing, video textures, shader preprocessing, effect policy, blend/composition, model/material/light behavior, SceneScript, QML render plumbing, harness capture behavior, or validator behavior.
The release suite contains the currently baselined required scenes. Add optional deep candidates only after reviewing the local asset and promoting baselines.

Use the coverage command before renderer phase work to confirm the limitation has an active baseline or candidate fixture. It performs no rendering and does not promote candidates into active smoke suites.

The harness compares deterministic PNG captures against versioned baselines and writes review artifacts to `/tmp/yakkai-smoke`. Smoke-test captures hide the local harness info overlay; generated review videos are for human inspection only, and PNG frames remain the source of truth.

Native renderer policy decisions for effect preservation, video texture playback, static model fallback, and SceneScript stubs live in focused C++ modules under `native/scene_backend/vendor/upstream_debug/src/Policy/`. Run `yakkai_scene_policy_tests` alongside smoke tests when changing these boundaries:

```bash
cmake --build build --target yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_scene_policy_tests
```

## Release Packaging

Release assets are built from tags by `.github/workflows/release.yml`. To create a release, push a tag such as `v0.1.0`; the workflow builds an Arch Linux x86_64 prebuilt package, builds a source fallback tarball, writes `SHA256SUMS`, and creates or updates the matching GitHub release.

The same packaging can be reproduced locally:

```bash
scripts/package-release.sh --output-dir dist --version v0.1.0 --target archlinux-x86_64
```

### Settings persistence test

```bash
QT_QPA_PLATFORM=offscreen /usr/lib/qt6/bin/qmltestrunner \
  -input tools/tst_config_persistence.qml \
  -o -,txt -v2 -maxwarnings 0 -nocrashhandler
```

## Repo layout

```
.
├── CMakeLists.txt
├── native/
│   ├── scene_backend/       # Native Vulkan scene renderer (C++)
│   │   ├── src/             # YakkaiSceneViewer QML integration
│   │   ├── tests/           # Native no-framework renderer policy tests
│   │   └── vendor/          # Vendored upstream scene sources + QuickJS + glslang
│   │       └── upstream_debug/src/Policy/ # Behavior-preserving renderer policy boundaries
│   └── scene_harness/       # Standalone debugging app
├── wallpapers/
│   └── io.team7.yakkai/     # Plasma wallpaper package
│       ├── metadata.json
│       └── contents/
│           ├── config/main.xml
│           ├── imports/     # (generated) native QML module
│           ├── tools/       # Python scan helper
│           └── ui/          # QML UI components
├── tools/                   # Development utilities
│   ├── color-lab/           # Interactive color debugging web tool
│   ├── effect-capture-summary.py # Summarize harness effect-capture manifests
│   ├── tst_config_persistence.qml # QML regression test for settings persistence
│   └── validate-scene.sh    # Automated render validator
├── scripts/                 # Local install and package validation helpers
├── .github/workflows/       # GitHub release packaging workflow
├── smoke-tests/             # Regression test runner
└── references/              # Third-party reference materials
```

## Content modes

| Mode | Description |
|------|-------------|
| Gradient | Two-color gradient with animation and time-of-day support |
| Video | Local video file via QtMultimedia |
| WE Video | Wallpaper Engine video projects from Steam library |
| WE Web | Wallpaper Engine web projects via QtWebEngine |
| WE Scene (diagnostics) | Safe scan/selection with placeholder diagnostics |
| WE Scene (native) | Native Vulkan renderer for WE scene projects |
| Playlist | Sequential, random, or scheduled cycling of scene wallpapers |
| All Wallpapers | Unified picker across scene, video, and web types |
| Playlist (All) | Playlists mixing scene, video, and web wallpapers |

## Native scene renderer

The native backend supports:
- **Qt Quick startup safety**: image-backed placeholder texture before the first external Vulkan frame
- **Plasma backend guard**: the native scene runtime only starts when Plasma reports an OpenGL Qt Quick scenegraph; software-rendered Plasma sessions stay on the diagnostic placeholder instead of invoking the Vulkan/OpenGL interop path
- **Textures**: TEXB v1-v4 with LZ4 decompression, BC1/BC2/BC3/BC7 formats, embedded PNG/JPEG detection, video textures via FFmpeg
- **Puppets**: MDL bone animation with additive layer blending, UTF-8 names
- **Shaders**: GLSL 150 preprocessing, authored combo preservation, `inverse()` polyfill stripping, varying type mismatch fix, `#require LightingV1` injection, ENABLEMASK/MASK combo mapping
- **Effects**: Composelayer support, selective effect stripping for alpha compositing, no-op skip for stripped fullscreen/composelayer effect carriers, colorkey preservation, flare/lens detection via colorBlendMode
- **Scripts**: QuickJS-based SceneScript evaluation with WE API stubs (thisLayer, WEMath, engine.registerAudioBuffers, engine.timeOfDay, etc.)
- **Properties**: Conditional user property resolution, animation curve evaluation (alpha/color/origin), time-of-day mapping for day-night cycles, container visibility inheritance
- **Particles**: Conditional visibility, parent container hiding

### Known limitations
- Regular per-layer offscreen effect chains in puppet scenes can break alpha compositing. Yakkai selectively strips regular/heavy effects in puppet scenes while preserving composelayers, colorkey, flare/lens, and other essential effect paths.
- The current stripped-effect family backlog, including candidate scenes and blocked follow-up slices, is tracked in `docs/renderer-effect-candidate-backlog.md`.
- Small embedded video textures are decoded as static first frames to keep CPU use bounded. Continuous decode is enabled only for large/main videos when FFmpeg is available at build time.
- Static model scenes use an experimental fallback for basis correction, camera framing, and material selection.
- Material/lighting fidelity is partial: generic materials and point lights are supported, but full Wallpaper Engine PBR, shadow, and reflection parity is not.
- SceneScript support is partial: Yakkai evaluates simple layer bindings (origin/color/alpha/visible) with API stubs, not the full Wallpaper Engine runtime.

## Remove

```bash
kpackagetool6 -t Plasma/Wallpaper -r io.team7.yakkai
```

## License

MIT
