"""Render the tray icons from the Material Symbols `shield_lock` glyph.

The upstream glyph is distributed by Google under the Apache License 2.0, see
`client/assets/icons/LICENSE-material-symbols.txt`. The script rasterises the
single SVG path into the three state colours the tray uses: grey for
disconnected, orange for connecting and green for a verified connection.

Run it with `uv run client/tools/make_icons.py`. The generated `.ico` files are
committed, so a normal build does not need Python at all.
"""

# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow>=10.0", "svgelements>=1.9"]
# ///

from __future__ import annotations

import pathlib
import sys

from PIL import Image, ImageChops, ImageDraw
from svgelements import Path

# Material Symbols Outlined, `shield_lock`, 24px grade 0 weight 400.
SHIELD_LOCK_PATH = (
    "M480-80q-139-35-229.5-159.5T160-516v-244l320-120 320 120v244q0 152-90.5 "
    "276.5T480-80Zm0-84q104-33 172-132t68-220v-189l-240-90-240 90v189q0 121 68 "
    "220t172 132Zm0-316Zm-80 160h160q17 0 28.5-11.5T600-360v-120q0-17-11.5-28.5T560-520v-40q0-33-23.5-56.5T480-640q-33 "
    "0-56.5 23.5T400-560v40q-17 0-28.5 11.5T360-480v120q0 17 11.5 28.5T400-320Zm40-200v-40q0-17 "
    "11.5-28.5T480-600q17 0 28.5 11.5T520-560v40h-80Z"
)

# The viewBox of the Material Symbols set is "0 -960 960 960".
VIEW_BOX = (0.0, -960.0, 960.0, 960.0)

# Sizes a Windows icon should carry so that both the notification area and the
# high resolution shell surfaces stay sharp.
ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 256)

# Supersampling factor of the rasteriser, which is a plain even-odd polygon fill.
SUPERSAMPLE = 8

# Curve flattening tolerance expressed as segments per path segment.
CURVE_STEPS = 24

STATES = {
    "shield_lock_grey.ico": (0x6E, 0x74, 0x7C, 0xFF),
    "shield_lock_orange.ico": (0xE8, 0x7B, 0x1F, 0xFF),
    "shield_lock_green.ico": (0x2E, 0x9E, 0x4F, 0xFF),
}


def subpath_polygons(path: Path, size: int) -> list[list[tuple[float, float]]]:
    """Flattens every closed subpath into a polygon in device coordinates."""
    min_x, min_y, width, height = VIEW_BOX
    scale = size / max(width, height)

    polygons: list[list[tuple[float, float]]] = []
    current: list[tuple[float, float]] = []

    for segment in path.segments():
        name = type(segment).__name__
        if name == "Move":
            if len(current) >= 3:
                polygons.append(current)
            current = []
            point = segment.end
            current.append(((point.x - min_x) * scale, (point.y - min_y) * scale))
            continue
        if name == "Close":
            if len(current) >= 3:
                polygons.append(current)
            current = []
            continue

        # Every remaining segment type exposes `point(t)`, so lines and curves
        # are flattened through the same sampling loop.
        steps = 1 if name == "Line" else CURVE_STEPS
        for step in range(1, steps + 1):
            point = segment.point(step / steps)
            current.append(((point.x - min_x) * scale, (point.y - min_y) * scale))

    if len(current) >= 3:
        polygons.append(current)
    return polygons


def render(path: Path, size: int, colour: tuple[int, int, int, int]) -> Image.Image:
    hi_res = size * SUPERSAMPLE
    coverage = Image.new("1", (hi_res, hi_res), 0)

    for polygon in subpath_polygons(path, hi_res):
        layer = Image.new("1", (hi_res, hi_res), 0)
        ImageDraw.Draw(layer).polygon(polygon, fill=1)
        # Even-odd accumulation, so an inner subpath punches a hole into the
        # shape that encloses it.
        coverage = ImageChops.logical_xor(coverage, layer)

    alpha = coverage.convert("L").resize((size, size), Image.LANCZOS)
    image = Image.new("RGBA", (size, size), colour[:3] + (0,))
    image.putalpha(alpha)
    return image


def main() -> int:
    target = pathlib.Path(__file__).resolve().parents[1] / "assets" / "icons"
    target.mkdir(parents=True, exist_ok=True)

    path = Path(SHIELD_LOCK_PATH)

    for name, colour in STATES.items():
        frames = [render(path, size, colour) for size in ICON_SIZES]
        destination = target / name
        frames[-1].save(destination, format="ICO", sizes=[(s, s) for s in ICON_SIZES])
        print(f"wrote {destination}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
