# Repository Instructions

- Update `README.md` in the same change whenever behavior, configuration, commands, file layout, or workflow changes.
- Treat `wallpapers/io.team7.yakkai` as the installable Plasma wallpaper package root.
- Prefer lightweight validation after edits, such as `qmllint` on changed QML files and a throwaway-package install with `kpackagetool6 -t Plasma/Wallpaper -i ... -p "$(mktemp -d)"`.
- Do not create commits unless the user explicitly asks for one.
- Do not merge branches into master unless the user explicitly asks for it.
- When verifying scene renders against smoke tests, do a thorough visual comparison: check camera angle, visible elements (desk, background, character position), color balance, and composition — not just "first frame ok". Ask the user if unsure whether the output matches.
- Before modifying shader preprocessing, effect chain logic, or blend modes, read `ARONA_SITREP.md` for context on the puppet scene rendering constraints. Run `smoke-tests/run.sh` before and after changes.
- When iterating on a specific scene's rendering, follow the process in `SCENE_DEV_PROCESS.md`: baseline → investigate → modify → validate → compare metrics → regression check.
- Use `tools/validate-scene.sh <scene_id>` for automated render validation without visual comparison. It checks structural state (render graph, effects, puppets, shaders) and pixel quality (variance, color, diversity).
- Always clear the shader cache (`rm -rf ~/.cache/wescene-renderer/*/spvs01/`) before validating — stale SPIR-V masks regressions.
- When making changes to the scene validator, document the changes in `SCENE_DEV_PROCESS.md` under the Tools section.
