# Repository Instructions

- Update `README.md` in the same change whenever behavior, configuration, commands, file layout, or workflow changes.
- Treat `wallpapers/io.papercompany.gradient` as the installable Plasma wallpaper package root.
- Prefer lightweight validation after edits, such as `qmllint` on changed QML files and a throwaway-package install with `kpackagetool6 -t Plasma/Wallpaper -i ... -p "$(mktemp -d)"`.
- Do not create commits unless the user explicitly asks for one.
- Do not merge branches into master unless the user explicitly asks for it.
- When verifying scene renders against smoke tests, do a thorough visual comparison: check camera angle, visible elements (desk, background, character position), color balance, and composition — not just "first frame ok". Ask the user if unsure whether the output matches.
