"""Rasterizes Packaging/pluengine.svg into the committed icon files the build embeds.

    Packaging/pluengine.png  256 px, compiled into PluPlatform as the SDL window icon (Linux).
    Packaging/pluengine.ico  16-256 px, linked into the executables through Packaging/pluengine.rc
                             (Windows: Explorer, taskbar and the WinAPI window class).

The build never runs this: the outputs are committed, so neither the native nor the cross build
needs an SVG rasterizer. Re-run it after editing the SVG.

Requires `rsvg-convert` (librsvg) on PATH and Pillow.

    Python/venv-linux/bin/python PythonTools/GenerateAppIcons.py
"""

import io
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SVG = ROOT / "Packaging" / "pluengine.svg"
PNG = ROOT / "Packaging" / "pluengine.png"
ICO = ROOT / "Packaging" / "pluengine.ico"

PNG_SIZE = 256
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


def Rasterize(size: int) -> Image.Image:
    # Every size is rendered from the vector source, not downscaled from 256 px, so the small
    # frames keep crisp edges.
    data = subprocess.run(
        ["rsvg-convert", "--width", str(size), "--height", str(size), str(SVG)],
        check=True, capture_output=True,
    ).stdout
    return Image.open(io.BytesIO(data)).convert("RGBA")


def Main() -> int:
    if shutil.which("rsvg-convert") is None:
        print("rsvg-convert not found (install librsvg)", file=sys.stderr)
        return 1

    Rasterize(PNG_SIZE).save(PNG, optimize=True)
    print(f"Wrote {PNG.relative_to(ROOT)}")

    frames = [Rasterize(size) for size in ICO_SIZES]
    largest = frames[-1]
    largest.save(ICO, format="ICO", sizes=[(s, s) for s in ICO_SIZES], append_images=frames[:-1])
    print(f"Wrote {ICO.relative_to(ROOT)} ({', '.join(map(str, ICO_SIZES))} px)")
    return 0


if __name__ == "__main__":
    sys.exit(Main())
