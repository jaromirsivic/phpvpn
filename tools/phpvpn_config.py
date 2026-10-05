"""Reader of the global development configuration phpvpn.config.json.

One JSON file in the repository root holds every machine specific value of the
build, distribution and test tooling. PHPVPN_CONFIG selects another file. A
relative path resolves against the repository root and a leading ~ against the
home directory. A value may be an object with "windows" and "linux" members, so
one file serves the Windows host and a synchronized Linux build host. An empty
tool path means the executable is looked up in PATH. Distributed artefacts
(installers, server scripts, servers) never read this file.

    python tools/phpvpn_config.py get  <dotted.key>   prints the raw value
    python tools/phpvpn_config.py path <dotted.key>   prints an absolute path
    python tools/phpvpn_config.py tool <name>         prints the tools.<name> executable
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
PLATFORM = "windows" if os.name == "nt" else "linux" if sys.platform.startswith("linux") else sys.platform

_cache: dict[Path, dict] = {}


def config_file() -> Path:
    override = os.environ.get("PHPVPN_CONFIG")
    return Path(override).expanduser().resolve() if override else ROOT / "phpvpn.config.json"


def load() -> dict:
    path = config_file()
    if path not in _cache:
        if not path.is_file():
            raise FileNotFoundError(f"global configuration not found: {path} (set PHPVPN_CONFIG or create it)")
        _cache[path] = json.loads(path.read_text(encoding="utf-8-sig"))
    return _cache[path]


def get(key: str, default: Any = None) -> Any:
    value: Any = load()
    for part in key.split("."):
        if not isinstance(value, dict) or part not in value:
            return default
        value = value[part]
    if isinstance(value, dict) and set(value) <= {"windows", "linux", "macos"}:
        value = value.get(PLATFORM, "")
    return value


def optional(key: str, default: Any = None) -> Any:
    """Like get(), but the default also applies when the file does not exist."""
    try:
        value = get(key, default)
    except FileNotFoundError:
        return default
    return default if value in (None, "") else value


def path(key: str) -> Path | None:
    value = get(key)
    if not value:
        return None
    candidate = Path(os.path.expanduser(str(value)))
    return candidate if candidate.is_absolute() else (ROOT / candidate).resolve()


def tool(name: str, command: str | None = None) -> str | None:
    """The configured tools.<name> executable, otherwise <command> from PATH."""
    configured = path(f"tools.{name}")
    if configured is not None:
        if not configured.is_file():
            raise FileNotFoundError(f"tools.{name} in {config_file()} does not exist: {configured}")
        return str(configured)
    return shutil.which(command or name)


def main(argv: list[str]) -> int:
    if len(argv) != 2 or argv[0] not in ("get", "path", "tool"):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    action, key = argv
    value = {"get": get, "path": path, "tool": tool}[action](key)
    if value is None:
        return 1
    print(value if not isinstance(value, (dict, list)) else json.dumps(value))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
