"""Validate the four authoritative root ICOs without regenerating them."""
from pathlib import Path
from PIL import Image


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    resource = (root / "client/apps/tray/tray.rc").read_text(encoding="utf-8")
    for name in ("default", "connected", "connecting", "disconnected"):
        with Image.open(root / "assets" / f"{name}.ico") as icon:
            assert icon.ico.sizes() == {(16, 16), (32, 32), (128, 128)}, name
            for size in sorted(icon.ico.sizes()):
                frame = icon.ico.getimage(size).convert("RGBA")
                assert frame.size == size
                assert frame.getchannel("A").getextrema()[1] > 0, (name, size)
        symbol = "APP" if name == "default" else name.upper()
        assert any(line.split() == [f"IDI_PHPVPN_{symbol}", "ICON", f'"{name}.ico"'] for line in resource.splitlines()), name
        print(f"PASS: {name}.ico, 16/32/128 frames and resource mapping")


if __name__ == "__main__":
    main()