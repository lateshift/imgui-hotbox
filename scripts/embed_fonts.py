"""Run ImGui's compressor for each static font and assemble one include."""

from pathlib import Path
import subprocess
import sys

compressor, output, *fonts = sys.argv[1:]
output = Path(output)
output.parent.mkdir(parents=True, exist_ok=True)
parts = ["// Generated from the pinned Inter and Lucide font files.\n"]
for font in fonts:
    symbol = Path(font).stem.replace("-", "_")
    parts.append(subprocess.check_output([compressor, "-u8", font, symbol], text=True))
output.write_text("\n".join(parts), encoding="utf-8")
