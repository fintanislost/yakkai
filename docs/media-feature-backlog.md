# Media Feature Backlog

This document tracks the current media-widget state after the first read-only
Linux MPRIS slice. It is the durable follow-up list for media work that should
not be inferred from README prose alone.

## Implemented

- Native scene mode can read desktop media metadata through MPRIS over the
  session DBus.
- The runtime maps player availability, playback state, title, artist, album,
  duration, one position snapshot, and local `mpris:artUrl` album art into the
  same `__yakkaiMedia` object used by the harness.
- The integration is read-only. Yakkai does not send play, pause, next, or
  previous commands to players.
- Position-only MPRIS polling does not rewrite `SceneGuard.scenePropertiesJson`
  every tick. This avoids restarting the native scene renderer for normal track
  progress.
- Position-only MPRIS changes also flow through runtime `mediaStateJson` and
  update supported SceneScript media timeline solid progress layers inside an
  already-running parsed scene.
- The standalone scene harness accepts `--media-state-timeline-json` for
  synthetic runtime media-position keyframes and records the normalized timeline
  in paper-backend debug manifests when `--debug-effect-captures` is enabled.
- A Plasma setting, `LinuxMediaIntegrationEnabled`, gates the live desktop media
  source for native scene wallpapers.

## Deferred Work

- Click-driven player controls, including play/pause/next/previous actions.
- Native audio-reactive bars from real Linux audio capture. Existing web audio
  coverage still uses harness-only synthetic audio.
- Texture-animation media widgets.
- Broader live SceneScript media event-loop behavior beyond supported timeline
  solid progress scale bindings.
- Full rich text and exact Wallpaper Engine text-layout parity.
- Broader real-player compatibility coverage across common MPRIS providers,
  including players with missing metadata, remote/non-file album art, empty
  playback status, and multiple simultaneous players.

## Regression Gates Before Media Commits

Run these before committing media-runtime changes:

```bash
cmake --build build/native/scene_backend --target yakkai_scene_backend yakkai_scene_backendplugin yakkai_mpris_media_payload_tests yakkai_scene_policy_tests -j2
build/native/scene_backend/yakkai_mpris_media_payload_tests
build/native/scene_backend/yakkai_scene_policy_tests
QT_QPA_PLATFORM=offscreen /usr/lib/qt6/bin/qmltestrunner -input tools/tst_config_persistence.qml
scripts/check-package.sh --skip-kpackage
```

For render-facing changes, also clear the shader cache before smoke validation:

```bash
rm -rf ~/.cache/wescene-renderer/*/spvs01/
```

Then run at least:

```bash
./smoke-tests/run.sh --suite quick
```

When the change can affect media widgets, run targeted synthetic-media harness
checks as well. These should use repo-local `tmp/` outputs and the owned
`native/scene_harness/tests/fixtures/media/instalock.mp3` fixture.

## Current Regression Targets

- `3228578419` Sleeping Arona: primary media-widget parity target. Check
  playback, album art, paused/no-playback clock state, and day/sunset/night
  scene-property variants when relevant.
- `3326873240` Elaina: non-puppet SceneScript and media panel gap coverage.
- `3301291394` Alya Clock and Date: generated text and user property gating.
- Media-widget matrix candidates from
  `tools/media_widget_candidate_inventory.py`, especially metadata widgets and
  property-only media candidates that become visible after authored toggles.
- `893418273` Audio Visualizer: web synthetic-audio coverage only. This does
  not validate real Linux audio capture.

## Promotion Rule

Do not promote a media candidate into an active smoke gate from automated
metrics alone. A candidate needs reviewed artifacts showing correct camera
angle, visible elements, album/card/text placement, color balance, expected
motion, and no Yakkai status overlay before it becomes an active baseline.
