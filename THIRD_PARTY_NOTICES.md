# Third-party notices

License texts are retained in each snapshot under `vendor/`.

| Snapshot | Upstream | Pin | Vendored (UTC) | SPDX | Local changes |
| --- | --- | --- | --- | --- | --- |
| imgui | <https://github.com/ocornut/imgui> | `v1.92.9b-docking` | 2026-11-11 | MIT | Local patches add opt-in FreeType hinting by density and forward Cocoa event modifier flags through GLFW. |
| imanim | <https://github.com/soufianekhiat/ImAnim> | `1ea19a55a28d54acb7f7f22a617f969aec2a073e` | 2026-11-11 | MIT | scripts/patches/imanim-gc.patch refreshes the age of live tween channels |
| spdlog | <https://github.com/gabime/spdlog> | `v1.17.0` | 2026-11-11 | MIT | None |
| glfw | <https://github.com/glfw/glfw> | `3.4` | 2026-11-11 | Zlib | None |
| dawn | <https://dawn.googlesource.com/dawn> | `ac7ee564275dd9c75f4a9e7425c6ce4f5a4f7ab5` | 2026-11-11 | BSD-3-Clause | CMake applies scripts/patches/dawn-version-file.patch to a build-local source copy. ImGui WebGPU backend unpatched. |
| inter | <https://github.com/rsms/inter/releases/tag/v4.1> | `v4.1` | 2026-11-11 | OFL-1.1 | Static Regular, Medium and SemiBold only |
| lucide | <https://github.com/lucide-icons/lucide> | `0.468.0` | 2026-11-11 | ISC AND MIT | Font and glyph map only, generated C++ codepoints |
| lunasvg | <https://github.com/sammycage/lunasvg> | `470e78e2953dcf0d4b46322d64eed6ca84156fec` | 2026-11-11 | MIT | None, includes MIT PlutoVG |
| freetype | <https://gitlab.freedesktop.org/freetype/freetype> | `VER-2-13-3` | 2026-11-11 | FTL | None, see docs/FTL.TXT |
| doctest | <https://github.com/doctest/doctest> | `v2.4.12` | 2026-11-11 | MIT | None |
| stb_image_write | <https://github.com/nothings/stb> | `4e9b368bfc071e97206ea5c6adba68dd0cbf8897` | 2026-11-11 | MIT OR LicenseRef-Public-Domain | Header only |
