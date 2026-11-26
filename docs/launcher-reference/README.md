# Launch window references

The Mac app's launch window (platform/macos/launcher.m, "Midway Marquee", 2026-11-25), rendered for platform/sdl to
match page by page. Drawn with the fallbacks only (`TP_LAUNCHER_NO_ART=1`: no GOG art, no park picture: nothing
game-derived) and the default settings; the clock at 0.9 s (1.4 s for the fireworks). `main.png` is 2x (2000 x 1400,
the window is 1000 x 700 points); the rest 1x.

With the player's art the sign carries GOG's logo, the poster the riders from GOG's key art (crop x 0.484, w 0.25,
h 0.978 of `keyart.jpg`), and the player's park (or GOG's park screenshots) pans behind the dusk tint; see
media/screenshots/screenshot-launcher.png.

| File | What |
|---|---|
| main.png | the main window (2x) |
| main-hover-ticket.png | the pointer over the Display ticket (it lifts and glows) |
| main-hover-tile.png | the pointer over the Game Speed tile (chevrons, lit edge) |
| main-focus-play.png | keyboard focus on Play |
| main-fireworks.png | a firework mid-burst behind the tagline |
| page-*.png | each page: game, display, sound, free-mode, controls, music, effects |
| page-display-focus.png | a row with keyboard focus (Panel Size: the slider) |

The rows, tickets, tiles, controls table, defaults and words are runtime/app_spec.c (shared). Re-render on a Mac with
`tools/launcher_reference.sh docs/launcher-reference` after a `TP_NO_INSTALL=1 tools/build_app.sh`.
Layout (points, top-down, window 1000 x 700): sign 44,30 584x166; poster 664,30 292x412; tickets y 310 h 132, five
across x 44..628 (gap 12); tiles y 484 h 82, four across x 44..956 (gap 12); Saved Parks 44,592 176x56; Updates
232,592 236x56; Quit 556,592 162x56; Play 734,588 222x64; page board 44,30 912x536. Fonts: Futura Condensed ExtraBold
(display), Futura Bold (kerned caps), American Typewriter Bold (ticket print), Optima (body).
