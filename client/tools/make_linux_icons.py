"""Generate the hicolor icons of the Linux tray from the authoritative PHPVPN SVGs.

Sources are `assets/{phpvpn,phpvpn-disconnected,phpvpn-connecting,phpvpn-connected}.svg`,
the vector form of the PHPVPN icon of `assets/{default,disconnected,connecting,
connected}.ico` (field, vertical bar, horizontal bar and centre; colours sampled
from the ICO frames). Output:

    client/assets/linux/hicolor/<n>x<n>/apps/<name>.png   for n in 16 22 24 32 48 64 128
    client/assets/linux/hicolor/scalable/apps/<name>.svg  (the source, LF line ends)

The SVGs use a deliberately small subset: a `viewBox` and axis-aligned `<rect>`
elements with `x`, `y`, `width`, `height` and a `#RRGGBB` `fill`, painted in order.
The rasteriser snaps every edge to the pixel grid of the target size (symmetrically
around the centre, each rectangle at least one pixel), so the small tray sizes stay
crisp and the output is identical on every Pillow version.

The icons are committed, so a normal build needs no Python. `--check` regenerates in
memory and compares size, mode and every RGBA pixel of the PNGs and the exact text of
the scalable SVGs with the committed files.

Run with `uv run --with pillow python client/tools/make_linux_icons.py [--check]`.
Development tool only, not part of a distribution.
"""

# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow>=10.0"]
# ///

from __future__ import annotations

import argparse
import math
import pathlib
import sys
import xml.etree.ElementTree as ElementTree

from PIL import Image, ImageDraw

SIZES = (16, 22, 24, 32, 48, 64, 128)
NAMES = ("phpvpn", "phpvpn-disconnected", "phpvpn-connecting", "phpvpn-connected")
SVG = "{http://www.w3.org/2000/svg}"


def repository_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def source_text(path: pathlib.Path) -> str:
    return path.read_bytes().decode("utf-8").replace("\r\n", "\n")


def parse(path: pathlib.Path) -> tuple[tuple[float, float, float, float], list[tuple[float, float, float, float, tuple[int, int, int]]]]:
    root = ElementTree.fromstring(source_text(path))
    if root.tag != f"{SVG}svg":
        raise SystemExit(f"{path}: not an SVG document")
    view_box = tuple(float(value) for value in root.attrib["viewBox"].split())
    if len(view_box) != 4 or view_box[2] != view_box[3] or view_box[2] <= 0:
        raise SystemExit(f"{path}: the viewBox must be a square")
    rects = []
    for element in root:
        if element.tag == f"{SVG}title":
            continue
        if element.tag != f"{SVG}rect" or set(element.attrib) != {"x", "y", "width", "height", "fill"}:
            raise SystemExit(f"{path}: only <rect x y width height fill> is supported, found {element.tag}")
        fill = element.attrib["fill"]
        if len(fill) != 7 or fill[0] != "#":
            raise SystemExit(f"{path}: fill must be #RRGGBB, found {fill}")
        color = (int(fill[1:3], 16), int(fill[3:5], 16), int(fill[5:7], 16))
        x, y, width, height = (float(element.attrib[key]) for key in ("x", "y", "width", "height"))
        rects.append((x, y, width, height, color))
    if not rects:
        raise SystemExit(f"{path}: no rectangles")
    return view_box, rects


def snap(value: float, size: int) -> int:
    # Round half away from the centre on both sides, so symmetric shapes stay symmetric.
    if value <= size / 2:
        return math.floor(value + 0.5)
    return size - math.floor(size - value + 0.5)


def render(path: pathlib.Path, size: int) -> Image.Image:
    (origin_x, origin_y, extent, _), rects = parse(path)
    scale = size / extent
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    for x, y, width, height, color in rects:
        left, top = snap((x - origin_x) * scale, size), snap((y - origin_y) * scale, size)
        right, bottom = snap((x + width - origin_x) * scale, size), snap((y + height - origin_y) * scale, size)
        right, bottom = max(right, left + 1), max(bottom, top + 1)
        left, top, right, bottom = max(left, 0), max(top, 0), min(right, size), min(bottom, size)
        if right > left and bottom > top:
            draw.rectangle((left, top, right - 1, bottom - 1), fill=color + (255,))
    return image


def icon_directory(root: pathlib.Path) -> pathlib.Path:
    return root / "client" / "assets" / "linux" / "hicolor"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="verify the committed icons instead of writing them")
    arguments = parser.parse_args()
    root = repository_root()
    target_root = icon_directory(root)
    failures = 0
    for name in NAMES:
        source = root / "assets" / f"{name}.svg"
        outputs: list[tuple[pathlib.Path, Image.Image | str]] = [
            (target_root / f"{size}x{size}" / "apps" / f"{name}.png", render(source, size)) for size in SIZES]
        outputs.append((target_root / "scalable" / "apps" / f"{name}.svg", source_text(source)))
        for target, content in outputs:
            if arguments.check:
                if not target.exists():
                    print(f"MISSING: {target.relative_to(root)}")
                    failures += 1
                    continue
                if isinstance(content, str):
                    same = source_text(target) == content
                else:
                    with Image.open(target) as committed:
                        same = committed.size == content.size and committed.convert("RGBA").tobytes() == content.tobytes()
                if not same:
                    print(f"DIFFERENT: {target.relative_to(root)}")
                    failures += 1
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            if isinstance(content, str):
                target.write_bytes(content.encode("utf-8"))
            else:
                # No metadata (no timestamps), fixed compression: reproducible output.
                content.save(target, format="PNG", optimize=False, compress_level=9)
    if arguments.check:
        total = len(NAMES) * (len(SIZES) + 1)
        if failures:
            print(f"FAIL: {failures} of {total} icons differ from the SVG sources")
            return 1
        print(f"PASS: {total} icons match the SVG sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
