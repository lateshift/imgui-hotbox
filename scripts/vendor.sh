#!/usr/bin/env bash
# Materialize pinned source snapshots. Run from any directory.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
vendor="$root/vendor"
mkdir -p "$vendor"
stage=$(mktemp -d "$vendor/.staging.XXXXXXXX")
trap 'rm -rf "$stage"' EXIT
manifest="$stage/VENDOR.md"
cat > "$manifest" <<'HEADER'
# Vendored dependencies

Run `bash scripts/vendor.sh` to reproduce these snapshots. Dawn
is a pinned Git submodule. Its CMake build fetches transitive dependencies from
Dawn's `DEPS` file.

| Snapshot | Upstream | Pin | Vendored (UTC) | SPDX | Local changes |
| --- | --- | --- | --- | --- | --- |
HEADER

download() {
    curl --fail --silent --show-error --location --retry 2 --max-time 300 "$@"
}

verify_sha256() {
    local path=$1 expected=$2 actual
    actual=$(shasum -a 256 "$path")
    if [[ "${actual%% *}" != "$expected" ]]; then
        echo "SHA-256 mismatch: $path" >&2
        return 1
    fi
}

github_archive() {
    local repo=$1 ref=$2 dest=$3
    mkdir -p "$dest"
    download "https://codeload.github.com/$repo/tar.gz/$ref" -o "$dest.tar.gz"
    tar -xzf "$dest.tar.gz" --strip-components=1 -C "$dest"
}

prepare_dawn() {
    local pin=$1
    if [[ ! -e "$vendor/dawn/.git" ]]; then
        git -C "$root" submodule update --init -- vendor/dawn
    fi
    if [[ $(git -C "$vendor/dawn" rev-parse HEAD) != "$pin" ]]; then
        echo "vendor/dawn must be at $pin. Run git submodule update --init -- vendor/dawn." >&2
        return 1
    fi

    if [[ $(cat "$vendor/dawn-revision" 2>/dev/null || true) != "$pin" ]]; then
        printf '%s' "$pin" > "$vendor/dawn-revision"
    fi
}

install_snapshot() {
    local name=$1 pin=$2
    if [[ "$name" == dawn ]]; then
        prepare_dawn "$pin"
        return
    fi
    if [[ -d "$vendor/$name" ]]; then
        if [[ $(head -n 1 "$vendor/$name/.hotbox-pin" 2>/dev/null || true) != "$pin" ]]; then
            echo "Move vendor/$name aside before installing pin $pin." >&2
            return 1
        fi
        return
    fi
    echo "Vendoring $name ($pin)"
    case "$name" in
        inter)
            download -H 'Accept: application/octet-stream' \
                'https://api.github.com/repos/rsms/inter/releases/assets/206862670' -o "$stage/inter.zip"
            mkdir -p "$stage/inter"
            unzip -jq "$stage/inter.zip" 'extras/ttf/Inter-Regular.ttf' 'extras/ttf/Inter-Medium.ttf' \
                'extras/ttf/Inter-SemiBold.ttf' 'LICENSE.txt' -d "$stage/inter"
            verify_sha256 "$stage/inter/Inter-Regular.ttf" 40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82
            verify_sha256 "$stage/inter/Inter-Medium.ttf" 97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872
            verify_sha256 "$stage/inter/Inter-SemiBold.ttf" 78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3
            ;;
        lucide)
            download "https://registry.npmjs.org/lucide-static/-/lucide-static-$pin.tgz" -o "$stage/lucide.tgz"
            verify_sha256 "$stage/lucide.tgz" d87c47325f0b2ac3ad2d6f056889da32b8f1216eafe4ed6534bc02786effe8cc
            mkdir -p "$stage/lucide"
            tar -xzf "$stage/lucide.tgz" -C "$stage/lucide" --strip-components=2 \
                package/font/lucide.ttf package/font/info.json
            tar -xOf "$stage/lucide.tgz" package/LICENSE > "$stage/lucide/LICENSE"
            ;;
        stb_image_write)
            mkdir -p "$stage/stb_image_write"
            for file in stb_image_write.h LICENSE; do
                download "https://raw.githubusercontent.com/nothings/stb/$pin/$file" -o "$stage/stb_image_write/$file"
            done
            ;;
        *)
            github_archive "$3" "$pin" "$stage/$name"
            ;;
    esac
    if [[ "$name" == imanim ]]; then
        patch -d "$stage/$name" -p1 < "$root/scripts/patches/imanim-gc.patch"
    elif [[ "$name" == imgui ]]; then
        patch -d "$stage/$name" -p1 < "$root/scripts/patches/imgui-freetype-density.patch"
        patch -d "$stage/$name" -p1 < "$root/scripts/patches/imgui-glfw-cocoa-modifiers.patch"
    fi
    compgen -G "$stage/$name/LICENSE*" > /dev/null || {
        echo "Missing license for $name" >&2
        return 1
    }
    printf '%s\n%s\n' "$pin" "$(date -u +%F)" > "$stage/$name/.hotbox-pin"
    mv "$stage/$name" "$vendor/$name"
}

cat > "$stage/pins" <<'PINS'
imgui|v1.92.9b-docking|ocornut/imgui|https://github.com/ocornut/imgui|MIT|Local patches add opt-in FreeType hinting by density and forward Cocoa event modifier flags through GLFW.
imanim|1ea19a55a28d54acb7f7f22a617f969aec2a073e|soufianekhiat/ImAnim|https://github.com/soufianekhiat/ImAnim|MIT|scripts/patches/imanim-gc.patch refreshes the age of live tween channels
spdlog|v1.17.0|gabime/spdlog|https://github.com/gabime/spdlog|MIT|None
glfw|3.4|glfw/glfw|https://github.com/glfw/glfw|Zlib|None
dawn|ac7ee564275dd9c75f4a9e7425c6ce4f5a4f7ab5|google/dawn|https://dawn.googlesource.com/dawn|BSD-3-Clause|CMake applies scripts/patches/dawn-version-file.patch to a build-local source copy. ImGui WebGPU backend unpatched.
inter|v4.1||https://github.com/rsms/inter/releases/tag/v4.1|OFL-1.1|Static Regular, Medium and SemiBold only
lucide|0.468.0||https://github.com/lucide-icons/lucide|ISC AND MIT|Font and glyph map only, generated C++ codepoints
lunasvg|470e78e2953dcf0d4b46322d64eed6ca84156fec|sammycage/lunasvg|https://github.com/sammycage/lunasvg|MIT|None, includes MIT PlutoVG
freetype|VER-2-13-3|freetype/freetype|https://gitlab.freedesktop.org/freetype/freetype|FTL|None, see docs/FTL.TXT
doctest|v2.4.12|doctest/doctest|https://github.com/doctest/doctest|MIT|None
stb_image_write|4e9b368bfc071e97206ea5c6adba68dd0cbf8897||https://github.com/nothings/stb|MIT OR LicenseRef-Public-Domain|Header only
PINS

pids=()
while IFS='|' read -r name pin repo url license changes; do
    install_snapshot "$name" "$pin" "$repo" &
    pids+=( "$!" )
done < "$stage/pins"
failed=0
for pid in "${pids[@]}"; do
    wait "$pid" || failed=1
done
(( failed == 0 )) || exit 1

python3 - "$vendor/lucide" <<'PY'
import json
from pathlib import Path
import re
import sys

directory = Path(sys.argv[1])
icons = json.loads((directory / "info.json").read_text(encoding="utf-8"))
lines = ["// Generated from the pinned lucide-static glyph map.", "#pragma once",
         "", "namespace hotbox::lucide {"]
for name, icon in sorted(icons.items()):
    identifier = re.sub(r"[^a-zA-Z0-9_]", "_", name)
    codepoint = int(icon["encodedCode"].removeprefix("\\"), 16)
    lines.append(f"inline constexpr unsigned icon_{identifier} = 0x{codepoint:04x}u;")
lines.append("} // namespace hotbox::lucide")
(directory / "codepoints.hpp").write_text("\n".join(lines) + "\n", encoding="utf-8")
PY

while IFS='|' read -r name pin repo url license changes; do
    if [[ "$name" == dawn ]]; then
        date=2026-11-11
    else
        date=$(sed -n '2p' "$vendor/$name/.hotbox-pin")
    fi
    printf '| %s | <%s> | `%s` | %s | %s | %s |\n' \
        "$name" "$url" "$pin" "$date" "$license" "$changes" >> "$manifest"
done < "$stage/pins"

mv "$manifest" "$vendor/VENDOR.md"
{
    printf '# Third-party notices\n\n'
    printf 'License texts are retained in each snapshot under `vendor/`.\n\n'
    sed -n '/^| Snapshot/,$p' "$vendor/VENDOR.md"
} > "$stage/THIRD_PARTY_NOTICES.md"
mv "$stage/THIRD_PARTY_NOTICES.md" "$root/THIRD_PARTY_NOTICES.md"
