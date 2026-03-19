# Vendored Scene Sources

This directory contains a local vendored copy of the current system's Wallpaper Engine KDE scene backend sources.

Origin used for this copy:

```text
/usr/src/debug/plasma6-wallpapers-wallpaper-engine-git/plasma6-wallpapers-wallpaper-engine-git/src/backend_scene
```

Why this copy exists:
- the repo-owned native backend scaffold needed the missing external-texture import path
- the earlier `references/` tree only included a small subset of the scene backend
- the host debug package includes the missing files such as:
  - `qml_helper/glExtra.cpp`
  - `src/SceneWallpaper.cpp`
  - `src/SceneWallpaperSurface.hpp`
  - `src/Fs/include/Fs/VFS.h`
  - bundled third-party dependencies under `third_party/`
- some bundled third-party directories in the debug package were incomplete locally, so this tree was completed with:
  - a local Chromium `Eigen` checkout
  - a local Chromium `glslang` checkout
  - official single-header `nlohmann/json` replacements

Current policy:
- this vendored tree is now compiled by [native/scene_backend/CMakeLists.txt](/home/peter/repos/papercompany/native/scene_backend/CMakeLists.txt) when `PAPER_ENABLE_VENDORED_SCENE_BACKEND=ON`
- the repo-owned `PaperSceneViewer` wrapper is the QML-facing module built on top of it
- the vendored `glslang` subtree is now built as a coherent local compiler dependency instead of mixing with the system `libglslang.so`
- the current failure point is back in external-texture import, not in shader compilation or Plasma integration

Important entrypoints:
- `qml_helper/SceneBackend.cpp`
- `qml_helper/glExtra.cpp`
- `src/SceneWallpaper.cpp`
- `src/Vulkan/`
- `src/VulkanRender/`
- `src/Swapchain/`

Immediate goal:
- keep the standalone harness as the first integration surface before moving code back into the Plasma wallpaper package
- continue debugging `glExtra` and the external-texture import path now that the repo-owned backend reaches the same stage as the installed system backend
