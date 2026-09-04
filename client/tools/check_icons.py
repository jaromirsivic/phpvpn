"""Check every committed icon frame. Run with uv run client/tools/check_icons.py."""
# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow>=10.0"]
# ///

from pathlib import Path

from PIL import Image

SIZES = (16, 20, 24, 32, 40, 48, 64, 256)
COLOURS = {"grey": (110, 116, 124), "orange": (232, 123, 31), "green": (46, 158, 79)}


def main() -> None:
    root = Path(__file__).resolve().parents[1] / "assets" / "icons"
    masks = {}
    for variant, colour in COLOURS.items():
        with Image.open(root / f"shield_lock_{variant}.ico") as icon:
            assert icon.ico.sizes() == {(s, s) for s in SIZES}, variant
            for size in SIZES:
                frame = icon.ico.getimage((size, size)).convert("RGBA")
                border = max(1, size // 16)
                pixels = [frame.getpixel((x, y)) for y in range(size) for x in range(size)]
                assert all(p[3] == 255 for p in pixels), (variant, size, "alpha")
                for y in range(size):
                    for x in range(size):
                        if min(x, y, size - 1 - x, size - 1 - y) < border:
                            assert frame.getpixel((x, y)) == (0, 0, 0, 255), (variant, size, "border")
                assert (255, 255, 255, 255) in pixels, (variant, size, "background")
                assert colour + (255,) in pixels, (variant, size, "colour")
                mask = tuple(p[:3] not in ((0, 0, 0), (255, 255, 255)) for p in pixels)
                if size in masks:
                    assert masks[size] == mask, (variant, size, "geometry")
                masks[size] = mask
        print(f"Verified shield_lock_{variant}.ico: all {len(SIZES)} frames")


if __name__ == "__main__":
    main()
