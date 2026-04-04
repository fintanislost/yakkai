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
- **Thumbnail wallpaper picker** — visual grid with previews and search

## Install

```bash
# Install the wallpaper package
kpackagetool6 -t Plasma/Wallpaper -i wallpapers/io.team7.yakkai

# Build the native scene backend
cmake -S . -B build
cmake --build build

# Update after changes
kpackagetool6 -t Plasma/Wallpaper -u wallpapers/io.team7.yakkai
```

After installation, open the Plasma wallpaper picker and choose **Yakkai**.

## Build

```bash
cmake -S . -B build
cmake --build build
```

The native backend builds `libyakkai_scene_backend.so` and stages it into `wallpapers/io.team7.yakkai/contents/imports/io/team7/scene/`.

### Standalone scene harness

```bash
./build/native/scene_harness/yakkai_scene_harness \
  --backend paper \
  --source /path/to/scene.pkg \
  --assets /path/to/wallpaper_engine/assets \
  --fill crop
```

Add `--capture /tmp/output.png --capture-delay-ms 10000` for automated testing.

## Repo layout

```
.
├── CMakeLists.txt
├── native/
│   ├── scene_backend/       # Native Vulkan scene renderer (C++)
│   │   ├── src/             # YakkaiSceneViewer QML integration
│   │   └── vendor/          # Vendored upstream scene sources + QuickJS + glslang
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
│   └── validate-scene.sh    # Automated render validator
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

## Native scene renderer

The native backend supports:
- **Textures**: TEXB v1-v4 with LZ4 decompression, BC1/BC2/BC3 formats, video textures via FFmpeg
- **Puppets**: MDL bone animation with additive layer blending, UTF-8 names
- **Shaders**: GLSL 150 preprocessing, `inverse()` polyfill stripping, varying type mismatch fix, `#require LightingV1` injection, ENABLEMASK/MASK combo mapping
- **Effects**: Composelayer support, selective effect stripping for alpha compositing, colorkey preservation, flare/lens detection via colorBlendMode
- **Scripts**: QuickJS-based SceneScript evaluation with WE API stubs (thisLayer, WEMath, engine.registerAudioBuffers, engine.timeOfDay, etc.)
- **Properties**: Conditional user property resolution, animation curve evaluation (alpha/color/origin), time-of-day mapping for day-night cycles, container visibility inheritance
- **Particles**: Conditional visibility, parent container hiding

### Known limitations
- Per-layer effect chains break alpha compositing (workaround: effects stripped in puppet scenes)
- Video textures use static first frame for small overlays (continuous decode for main wallpaper videos)
- Full material/lighting fidelity incomplete
- SceneScript support is partial (stubs, not full runtime)

## Remove

```bash
kpackagetool6 -t Plasma/Wallpaper -r io.team7.yakkai
```

## License

MIT
