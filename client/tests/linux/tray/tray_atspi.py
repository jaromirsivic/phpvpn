#!/usr/bin/env python3
"""AT-SPI probe of the running GTK tray (development harness, WP09/WP10).

Run inside the desktop session (or from SSH with the session environment:
XDG_RUNTIME_DIR, WAYLAND_DISPLAY, DBUS_SESSION_BUS_ADDRESS). Needs python3-pyatspi.

    tray_atspi.py dump                      print windows, labels, entries and buttons
    tray_atspi.py click <window> <button>   press a button in the named window
    tray_atspi.py wait <window> <text> [s]  wait until a label/entry shows the text
    tray_atspi.py close <window>            close the window with its title bar X
    tray_atspi.py absent <window> [s]       wait until the window is gone

Exit code 0 on success, 1 when the expectation was not met.
"""

from __future__ import annotations

import sys
import time

import pyatspi

APPLICATION = "phpvpn-tray"


def application():
    desktop = pyatspi.Registry.getDesktop(0)
    for index in range(desktop.childCount):
        app = desktop.getChildAtIndex(index)
        if app is not None and app.name == APPLICATION:
            return app
    return None


def windows():
    app = application()
    if app is None:
        return []
    return [app.getChildAtIndex(i) for i in range(app.childCount) if app.getChildAtIndex(i) is not None]


def walk(node, depth=0):
    yield node, depth
    for index in range(node.childCount):
        child = node.getChildAtIndex(index)
        if child is not None:
            yield from walk(child, depth + 1)


def text_of(node) -> str:
    role = node.getRoleName()
    if role in ("label", "text", "entry"):
        try:
            text = node.queryText()
            return text.getText(0, text.characterCount)
        except NotImplementedError:
            return node.name or ""
    return node.name or ""


def find_window(title: str):
    for window in windows():
        if window.name == title:
            return window
    return None


def dump() -> int:
    found = windows()
    if not found:
        print("no phpvpn-tray windows")
        return 1
    for window in found:
        states = window.getState()
        print(f"window '{window.name}' role={window.getRoleName()} showing={states.contains(pyatspi.STATE_SHOWING)}")
        for node, depth in walk(window):
            role = node.getRoleName()
            if role in ("label", "push button", "button", "text", "entry", "menu item", "separator"):
                showing = node.getState().contains(pyatspi.STATE_SHOWING)
                sensitive = node.getState().contains(pyatspi.STATE_SENSITIVE)
                print(f"  {'  ' * min(depth, 6)}{role}: {text_of(node)!r}"
                      f"{'' if showing else ' [hidden]'}{'' if sensitive else ' [insensitive]'}")
    return 0


def click(title: str, button: str) -> int:
    window = find_window(title)
    if window is None:
        print(f"window '{title}' not found")
        return 1
    for node, _ in walk(window):
        if node.getRoleName() in ("push button", "button") and node.name == button:
            action = node.queryAction()
            for index in range(action.nActions):
                if action.getName(index) in ("click", "press"):
                    action.doAction(index)
                    print(f"clicked '{button}' in '{title}'")
                    return 0
    print(f"button '{button}' not found in '{title}'")
    return 1


def wait(title: str, text: str, seconds: float) -> int:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        window = find_window(title)
        if window is not None:
            for node, _ in walk(window):
                if node.getState().contains(pyatspi.STATE_SHOWING) and text_of(node) == text:
                    print(f"'{title}' shows '{text}'")
                    return 0
        time.sleep(0.1)
    print(f"'{title}' did not show '{text}' within {seconds} s")
    return 1


def absent(title: str, seconds: float) -> int:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if find_window(title) is None:
            print(f"'{title}' is closed")
            return 0
        time.sleep(0.1)
    print(f"'{title}' is still open after {seconds} s")
    return 1


def close(title: str) -> int:
    window = find_window(title)
    if window is None:
        print(f"window '{title}' not found")
        return 1
    # The client-side title bar close button (window X), the same path as a user click.
    for node, _ in walk(window):
        if (node.getRoleName() in ("push button", "button") and node.name == "Close"
                and node.getState().contains(pyatspi.STATE_SHOWING)):
            action = node.queryAction()
            for index in range(action.nActions):
                if action.getName(index) in ("click", "press"):
                    action.doAction(index)
                    print(f"closed '{title}'")
                    return 0
    print(f"no title bar close button in '{title}'")
    return 1


def main(arguments: list[str]) -> int:
    if not arguments:
        print(__doc__)
        return 2
    command, rest = arguments[0], arguments[1:]
    if command == "dump":
        return dump()
    if command == "click" and len(rest) == 2:
        return click(rest[0], rest[1])
    if command == "wait" and len(rest) in (2, 3):
        return wait(rest[0], rest[1], float(rest[2]) if len(rest) == 3 else 10.0)
    if command == "absent" and len(rest) in (1, 2):
        return absent(rest[0], float(rest[1]) if len(rest) == 2 else 10.0)
    if command == "close" and len(rest) == 1:
        return close(rest[0])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
