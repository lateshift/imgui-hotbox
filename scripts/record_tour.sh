#!/usr/bin/env bash
# Render the harness "tour" scenario and encode the README animation.
#
#   scripts/record_tour.sh [build-dir] [output-stem]
#
# Writes <output-stem>.gif (inline in the README) and <output-stem>.mp4
# (full resolution, for a GitHub video attachment). The default stem is
# docs/media/hotbox-tour. Requires a built hotbox_harness and ffmpeg with
# libx264. Frames are rendered offscreen, so no screen recording is needed.
#
# TOUR_SCALE sets the framebuffer scale (default 2) and TOUR_GIF_WIDTH the
# GIF width in pixels (default 1280). The MP4 keeps the full 960 × 600 pt
# viewport at TOUR_SCALE.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$root/build/macos-debug}"
stem="${2:-$root/docs/media/hotbox-tour}"
harness="$build/hotbox_harness"
scale="${TOUR_SCALE:-2}"
gif_width="${TOUR_GIF_WIDTH:-1280}"
fps=30

command -v ffmpeg >/dev/null || { echo "ffmpeg is required" >&2; exit 1; }
[[ -x "$harness" ]] || { echo "Build $harness first" >&2; exit 1; }

frames="$(mktemp -d)"
trap 'rm -rf "$frames"' EXIT

"$harness" --scenario tour --icons public --theme dark --scale "$scale" --sequence "$frames"
mkdir -p "$(dirname "$stem")"

# GIF: a shared 128-colour palette and rectangle diffs keep it small.
filters="scale=${gif_width}:-1:flags=lanczos"
ffmpeg -loglevel error -y -framerate "$fps" -i "$frames/%04d.png" \
  -vf "$filters,palettegen=max_colors=128:stats_mode=diff" "$frames/palette.png"
ffmpeg -loglevel error -y -framerate "$fps" -i "$frames/%04d.png" -i "$frames/palette.png" \
  -lavfi "$filters [video]; [video][1:v] paletteuse=dither=none:diff_mode=rectangle" \
  -loop 0 "$stem.gif"

# MP4: H.264 in yuv420p with the index up front, which browsers and GitHub play.
ffmpeg -loglevel error -y -framerate "$fps" -i "$frames/%04d.png" \
  -c:v libx264 -preset slow -crf 18 -tune animation -pix_fmt yuv420p \
  -movflags +faststart "$stem.mp4"

echo "Wrote $stem.gif ($(du -h "$stem.gif" | cut -f1)) and $stem.mp4 ($(du -h "$stem.mp4" | cut -f1))"
