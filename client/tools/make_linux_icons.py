"""Generate the hicolor PNG icons of the Linux tray from the authoritative ICOs.

Sources are `assets/{default,disconnected,connecting,connected}.ico` (frames 16, 32
and 128 px). Each target size uses the exact frame when the ICO has it, otherwise
the next larger frame downscaled with Lanczos. Output:

    client/assets/linux/hicolor/<n>x<n>/apps/{phpvpn,phpvpn-disconnected,
        phpvpn-connecting,phpvpn-connected}.png   for n in 16 22 24 32 48 64 128

The PNGs are committed, so a normal build needs no Python. `--check` regenerates in
memory and compares size, mode and every RGBA pixel with the committed files (PNG
encoder bytes may differ between Pillow versions, pixels may not).

Run with `uv run --with pillow python client/tools/make_linux_icons.py [--check]`.
Development tool only, not part of a distribution.
"""

# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow>=10.0"]
# ///

from __future__ import annotations

import argparse
import pathlib
import sys

from PIL import Image

SIZES = (16, 22, 24, 32, 48, 64, 128)
SOURCES = {
    "default": "phpvpn",
    "disconnected": "phpvpn-disconnected",
    "connecting": "phpvpn-connecting",
    "connected": "phpvpn-connected",
}


def repository_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def render(icon_path: pathlib.Path) -> dict[int, Image.Image]:
    with Image.open(icon_path) as icon:
        frames = {size[0]: icon.ico.getimage(size).convert("RGBA") for size in icon.ico.sizes() if size[0] == size[1]}
    if not frames:
        raise SystemExit(f"{icon_path}: no square frames")
    result: dict[int, Image.Image] = {}
    for size in SIZES:
        if size in frames:
            result[size] = frames[size].copy()
            continue
        larger = [available for available in sorted(frames) if available > size]
        source = frames[larger[0] if larger else max(frames)]
        result[size] = source.resize((size, size), Image.Resampling.LANCZOS)
    return result


def output_path(root: pathlib.Path, size: int, name: str) -> pathlib.Path:
    return root / "client" / "assets" / "linux" / "hicolor" / f"{size}x{size}" / "apps" / f"{name}.png"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="verify the committed PNGs instead of writing them")
    arguments = parser.parse_args()
    root = repository_root()
    failures = 0
    for source, name in SOURCES.items():
        images = render(root / "assets" / f"{source}.ico")
        for size, image in images.items():
            target = output_path(root, size, name)
            if arguments.check:
                if not target.exists():
                    print(f"MISSING: {target.relative_to(root)}")
                    failures += 1
                    continue
                with Image.open(target) as committed:
                    same = committed.size == image.size and committed.convert("RGBA").tobytes() == image.tobytes()
                if not same:
                    print(f"DIFFERENT: {target.relative_to(root)}")
                    failures += 1
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            # No metadata (no timestamps), fixed compression: reproducible output.
            image.save(target, format="PNG", optimize=False, compress_level=9)
    if arguments.check:
        total = len(SOURCES) * len(SIZES)
        if failures:
            print(f"FAIL: {failures} of {total} icons differ from the ICO sources")
            return 1
        print(f"PASS: {total} icons match the ICO sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
