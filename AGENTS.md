# AGENTS.md

Guidance for coding agents (Claude Code, Codex and others) working in this
repository. Read this file before changing code. `CLAUDE.md` only imports it.

## What this is

`imgui-hotbox` is a C++20 Dear ImGui widget library that implements a
**hotbox** with **marking menus**, inspired by the hotbox in Autodesk Maya,
plus a GLFW + Dawn (WebGPU) harness to run it. The library owns no window,
GPU device or render loop. [README.md](README.md) explains the interaction
for people who have never used a hotbox, and documents the public API.

The project is MIT-licensed and independent of Autodesk. Never copy
Autodesk code, artwork, icons, screenshots or documentation into the repo,
and keep the trademark notice in the README intact.

The look follows the **IFCStudio UI design system**. Its colours and type
ramp are the generated tables in `src/design_tokens.inc`.

## Build, test, render

```sh
cmake --preset macos-debug          # once; needs network for Dawn deps
cmake --build build/macos-debug
./build/macos-debug/hotbox_tests     # doctest, all tests must pass
ctest --preset macos-debug           # same tests through CTest
./build/macos-debug/hotbox_harness   # interactive window
```

Use `linux-debug` / `windows-debug` on those platforms, and the `asan` /
`ubsan` presets for library-only sanitizer builds. Run builds longer than a
couple of minutes (a first Dawn build) in the background.

**Verify visual changes with an offscreen render**, not a screenshot of the
live window (agents usually lack screen-recording permission):

```sh
./build/macos-debug/hotbox_harness --scenario open-layout --scale 2 \
  --theme dark --out build/check.png
```

Scenarios: `open-layout`, `menu-edit`, `marking-n2`, `open-close`, `submenu`,
`submenu-cameras`, `marking-invoke`, `marking-disabled`, `mark-ahead`,
`theme-shape`, `tour`, `search`, `keyboard`, `recent`. Add `--shape square`, `--theme light`, `--time S`,
`--motion 0.1`, `--icons public` and `--frames N --sequence DIR` as needed.
Measure in device pixels (2× scale means 1 pt = 2 px).

## Layout of the code

| Path | Contents |
|---|---|
| `include/hotbox/` | Public API. `hotbox.hpp` is the umbrella header. |
| `src/geometry.*`, `src/gesture.*`, `src/state.*` | Pure logic: zones, sectors, hysteresis, stroke recogniser, trigger state machine. **No ImGui calls**; unit-tested directly. |
| `src/registry.cpp`, `src/layout.cpp` | Item registry, menus, menu sets, marking slots, the layout text format. |
| `src/search.*` | Fuzzy matching, ranking and command history (recents, frecency). **No ImGui calls.** |
| `src/persistence.*` | The `imgui.ini` settings handler for saved hotbox state. |
| `src/overlay.*` | Lays out rows, dropdowns and marking menus, and does hit testing. Row, pitch and dropdown constants live here. |
| `src/paint.*`, `src/atlas.*`, `src/icons.cpp` | Drawing primitives, shadows, text, icon rasterising into the font atlas. |
| `src/hotbox.cpp` | `Hotbox::frame()`: input, state, dispatch queue, draw order. |
| `src/motion*` | ImAnim integration, motion preferences, reduced motion. |
| `src/theme.cpp`, `src/design_tokens.inc` | Themes. The `.inc` is **generated**; do not edit it by hand. |
| `src/system_appearance_*` | OS light/dark detection per platform. |
| `harness/` | GLFW + Dawn app, demo commands and icons, the README tour, offscreen capture. |
| `tests/` | `core.cpp` (pure logic), `interaction.cpp` (scripted input), `paint.cpp` (draw-list checks). |
| `vendor/` | Pinned third-party snapshots. See `vendor/VENDOR.md`. |

## Rules

- **C++20, warnings as errors in `src/`** (`-Wall -Wextra -Wpedantic`, `/W4`).
  `vendor/` is exempt.
- **Units:** every size in code is logical points (pt), multiplied by the
  scale at draw time. Layout sits on a 4 pt grid; 2 pt steps only inside
  controls. Hairlines are 1 device pixel.
- **Font sizes:** ImGui 1.92's stb/FreeType loaders treat the requested size
  as ascent + descent, not em. Inter is about 1.21 em per ImGui size unit.
  Always convert design em sizes with `hotbox::fonts::imgui_size(font, em)`.
- **Keys while open:** the hook locks Tab, the arrows, Enter, Backspace and
  Esc to itself. Read them with `Impl::Pressed()`, which passes the hook's
  owner ID; `ImGui::IsKeyPressed(key)` returns false for a locked key.
- **Callbacks** are queued and dispatched at the end of `Hotbox::frame()`.
  Registry changes made during a frame become visible on the next frame.
  Keep it that way.
- **Vendor changes** go through `scripts/vendor.sh` and a patch in
  `scripts/patches/`, recorded in `vendor/VENDOR.md` and
  `THIRD_PARTY_NOTICES.md`. Never edit a vendored file by hand without a patch.
- **Library vs harness:** the library must build with `HOTBOX_BUILD_HARNESS=OFF`
  and must not depend on GLFW, Dawn or WebGPU.
- **Public API changes** need Doxygen comments in the header and an update
  to the "Programming interface" section of the README.
- **The README demo** (`docs/media/hotbox-tour.gif` and `.mp4`) is
  rendered from the harness `tour` scenario (`harness/tour.cpp`) with
  `scripts/record_tour.sh`. The tour's pointer targets are hard-coded
  positions in the demo layout: when the look or the demo layout changes,
  check them on a contact sheet and re-record.

## Tests that pin geometry

Some tests in `tests/paint.cpp` and `tests/interaction.cpp` check exact
coordinates (for example the divider y range in "The overlay uses the main
framebuffer density when its viewport scale is unset"). When you change a
layout constant on purpose, recompute those values and leave a comment that
shows the arithmetic, e.g. `240 - 46 - 8 = 186`.

## Known gotchas

- **⌥Space on macOS** is often taken by another app's global hotkey (for
  example the ChatGPT launcher, Raycast or Alfred). The chord then never
  reaches the harness. Use the **Open Hotbox** button, or change
  `Config::trigger`. GNOME binds Alt+Space to the window menu.
- `Config::trigger = ImGuiKey_Space` (plain Space, no modifier) is not
  supported yet and is ignored.
- `grep --include=*.cpp` fails under zsh globbing; quote the pattern.

## Working with other agents

Several agent sessions often share this working tree. Do not stash, reset,
switch branches or revert files you did not change. Commit only your own
files, and only when asked. If a file already has someone else's uncommitted
edits, keep them and mention that in your report.

## Commits

One milestone or one fix per commit, imperative subject
(`M3: motion`, `Fix dropdown label alignment`). Do not commit `build/`.
