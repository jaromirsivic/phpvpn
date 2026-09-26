#!/usr/bin/env python3
"""AT-SPI probe for the Configuration Wizard windows (development harness, WP10a).

Extends tray_atspi.py (same session requirements, python3-pyatspi) with the
controls of the Wizard, the HTTP headers dialog and the HTTP error detail window.
Windows are found by title; "alert:<title>" selects a message dialog (role alert
or dialog) with that title, so a dialog titled like its parent can be addressed.

    wizard_atspi.py dump [window]                 labels, entries, buttons, switches, tabs
    wizard_atspi.py click <window> <button>       press a (visible) button
    wizard_atspi.py settext <window> <name> <t>   replace an entry/text view by accessible name
    wizard_atspi.py text <window> <name> <t>      expect the exact text of an entry/text view
    wizard_atspi.py contains <window> <name> <t>  expect a substring of an entry/text view
    wizard_atspi.py toggle <window> <name>        flip a switch by accessible name
    wizard_atspi.py checked <window> <name> 0|1   expect a switch state
    wizard_atspi.py wait <window> <text> [s]      wait until a showing label/entry has the text
    wait-prefix <window> <text> [s]               ... starts with the text
    wizard_atspi.py gone <window> <text> [s]      wait until no showing label has the text
    wizard_atspi.py button <window> <button> visible|hidden|sensitive|insensitive
    wizard_atspi.py tabs <window> <a,b>           exact page tab names
    wizard_atspi.py tab <window> <name>           select a page tab
    wizard_atspi.py focused <window> <name>       expect the focused control (exit 3: window not active)
    wizard_atspi.py key <window> <keysym>         synthesize a key press (Escape, Return)
    wizard_atspi.py samerow <window> <b1> <b2>... showing buttons share one row (same vertical extents)
    wizard_atspi.py fits <window> <w> <h> <b>...  window at most w x h, the buttons fully inside it
    wizard_atspi.py width <window> <name> <min> <max>  a showing element's width lies in [min, max]
    wizard_atspi.py close <window>                title bar X
    wizard_atspi.py absent <window> [s]           wait until the window is gone
    wizard_atspi.py present <window> [s]          wait until the window exists

Exit code 0 on success, 1 when the expectation was not met.
"""

from __future__ import annotations

import sys
import time

import pyatspi

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import tray_atspi as base  # noqa: E402

DIALOG_ROLES = ("alert", "dialog")


def find_window(title: str):
    dialog = title.startswith("alert:")
    wanted = title[len("alert:"):] if dialog else title
    for window in base.windows():
        if window.name != wanted:
            continue
        is_dialog = window.getRoleName() in DIALOG_ROLES
        if is_dialog == dialog:
            return window
    return None


def wait_window(title: str, seconds: float = 5.0):
    end = time.monotonic() + seconds
    while True:
        window = find_window(title)
        if window is not None or time.monotonic() >= end:
            return window
        time.sleep(0.1)


def showing(node) -> bool:
    return node.getState().contains(pyatspi.STATE_SHOWING)


def sensitive(node) -> bool:
    return node.getState().contains(pyatspi.STATE_SENSITIVE)


def full_text(node) -> str:
    try:
        text = node.queryText()
        return text.getText(0, text.characterCount)
    except NotImplementedError:
        return node.name or ""


def controls(window, roles, name):
    return [node for node, _ in base.walk(window) if node.getRoleName() in roles and node.name == name]


def one(title, roles, name, want_showing=True):
    window = wait_window(title)
    if window is None:
        print(f"window '{title}' not found")
        return None
    found = [node for node in controls(window, roles, name) if showing(node) or not want_showing]
    if not found:
        print(f"'{name}' ({'/'.join(roles)}) not found in '{title}'")
        return None
    return found[0]


def do_action(node, names=("click", "press", "toggle", "activate")) -> bool:
    action = node.queryAction()
    for index in range(action.nActions):
        if action.getName(index) in names:
            action.doAction(index)
            return True
    return False


def dump(title=None) -> int:
    found = [find_window(title)] if title else base.windows()
    found = [window for window in found if window is not None]
    if not found:
        print("no windows")
        return 1
    for window in found:
        print(f"window '{window.name}' role={window.getRoleName()}")
        for node, depth in base.walk(window):
            role = node.getRoleName()
            if role in ("label", "push button", "button", "text", "entry", "toggle button", "switch",
                        "check box", "page tab", "alert"):
                if not showing(node):
                    continue
                value = full_text(node) if role in ("label", "text", "entry") else node.name
                extra = ""
                if role in ("toggle button", "switch", "check box"):
                    extra = " [on]" if node.getState().contains(pyatspi.STATE_CHECKED) else " [off]"
                if role in ("text", "entry") and node.name:
                    extra += f" name={node.name!r}"
                print(f"  {'  ' * min(depth, 6)}{role}: {value!r}{extra}{'' if sensitive(node) else ' [insensitive]'}")
    return 0


def click(title, name) -> int:
    node = one(title, ("push button", "button"), name)
    if node is None:
        return 1
    if not sensitive(node):
        print(f"'{name}' in '{title}' is insensitive")
        return 1
    if do_action(node):
        print(f"clicked '{name}' in '{title}'")
        return 0
    print(f"'{name}' has no click action")
    return 1


def settext(title, name, value) -> int:
    node = one(title, ("text", "entry"), name)
    if node is None:
        return 1
    node.queryEditableText().setTextContents(value)
    print(f"set '{name}' in '{title}'")
    return 0


def expect_text(title, name, value, contains=False, seconds=5.0) -> int:
    end = time.monotonic() + seconds
    actual = None
    while time.monotonic() < end:
        node = one(title, ("text", "entry"), name)
        if node is not None:
            actual = full_text(node)
            if (value in actual) if contains else (actual == value):
                print(f"'{name}' in '{title}' {'contains' if contains else 'is'} {value!r}")
                return 0
        time.sleep(0.1)
    print(f"'{name}' in '{title}' is {actual!r}, expected {'substring ' if contains else ''}{value!r}")
    return 1


def toggle(title, name) -> int:
    node = one(title, ("toggle button", "switch", "check box"), name)
    if node is None:
        return 1
    if do_action(node):
        print(f"toggled '{name}' in '{title}'")
        return 0
    print(f"'{name}' has no toggle action")
    return 1


def checked(title, name, expected) -> int:
    end = time.monotonic() + 5
    state = None
    while time.monotonic() < end:
        node = one(title, ("toggle button", "switch", "check box"), name)
        if node is not None:
            state = node.getState().contains(pyatspi.STATE_CHECKED)
            if state == (expected == "1"):
                print(f"'{name}' in '{title}' is {'on' if state else 'off'}")
                return 0
        time.sleep(0.1)
    print(f"'{name}' in '{title}' is {state}, expected {expected}")
    return 1


def wait(title, text, seconds, prefix=False) -> int:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        window = find_window(title)
        if window is not None:
            for node, _ in base.walk(window):
                if node.getRoleName() in ("label", "text", "entry") and showing(node):
                    value = full_text(node)
                    if value == text or (prefix and value.startswith(text)):
                        print(f"'{title}' shows {text!r}")
                        return 0
        time.sleep(0.1)
    print(f"'{title}' did not show {text!r} within {seconds} s")
    return 1


def gone(title, text, seconds) -> int:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        window = find_window(title)
        present = window is not None and any(
            node.getRoleName() == "label" and showing(node) and full_text(node) == text for node, _ in base.walk(window))
        if not present:
            print(f"'{title}' no longer shows {text!r}")
            return 0
        time.sleep(0.1)
    print(f"'{title}' still shows {text!r}")
    return 1


def button_state(title, name, expected) -> int:
    end = time.monotonic() + 5
    observed = None
    while time.monotonic() < end:
        window = find_window(title)
        if window is not None:
            nodes = controls(window, ("push button", "button"), name)
            visible = any(showing(node) for node in nodes)
            enabled = any(showing(node) and sensitive(node) for node in nodes)
            observed = {"visible": visible, "hidden": not visible, "sensitive": enabled,
                        "insensitive": visible and not enabled}[expected]
            if observed:
                print(f"'{name}' in '{title}' is {expected}")
                return 0
        time.sleep(0.1)
    print(f"'{name}' in '{title}' is not {expected}")
    return 1


def tabs(title, names) -> int:
    window = wait_window(title)
    if window is None:
        print(f"window '{title}' not found")
        return 1
    found = [node.name for node, _ in base.walk(window) if node.getRoleName() == "page tab"]
    if found == names.split(","):
        print(f"'{title}' tabs {found}")
        return 0
    print(f"'{title}' tabs {found}, expected {names}")
    return 1


def tab(title, name) -> int:
    window = wait_window(title)
    if window is None:
        return 1
    for node, _ in base.walk(window):
        if node.getRoleName() == "page tab" and node.name == name:
            parent = node.parent
            selection = parent.querySelection()
            selection.selectChild(node.getIndexInParent())
            print(f"selected tab '{name}'")
            return 0
    print(f"tab '{name}' not found")
    return 1


def activate(window) -> None:
    # A newly opened window of a headless compositor may not be active yet; the
    # focus state of its controls is only reported for the active window.
    if window.getState().contains(pyatspi.STATE_ACTIVE):
        return
    try:
        window.queryComponent().grabFocus()
    except NotImplementedError:
        pass
    time.sleep(0.3)


def focused(title, name) -> int:
    end = time.monotonic() + 5
    current = None
    while time.monotonic() < end:
        window = find_window(title)
        if window is not None:
            activate(window)
            for node, _ in base.walk(window):
                if node.getState().contains(pyatspi.STATE_FOCUSED):
                    current = node.name
                    if node.name == name:
                        print(f"'{name}' has the focus in '{title}'")
                        return 0
        time.sleep(0.1)
    window = find_window(title)
    active = window is not None and window.getState().contains(pyatspi.STATE_ACTIVE)
    print(f"focus in '{title}' is {current!r}, expected {name!r} (window active: {active})")
    # Exit code 3: not verifiable, the compositor never activated the window.
    return 1 if active else 3


def key(title, keysym) -> int:
    window = wait_window(title)
    if window is None:
        print(f"window '{title}' not found")
        return 1
    # Make sure the window is active, then synthesize the key through AT-SPI.
    try:
        window.queryComponent().grabFocus()
    except NotImplementedError:
        pass
    time.sleep(0.2)
    code = {"Escape": 0xff1b, "Return": 0xff0d}[keysym]
    pyatspi.Registry.generateKeyboardEvent(code, None, pyatspi.KEY_SYM)
    print(f"key {keysym} to '{title}'")
    return 0


def extents(node, frame=pyatspi.WINDOW_COORDS):
    box = node.queryComponent().getExtents(frame)
    return box.x, box.y, box.width, box.height


def samerow(title, names) -> int:
    rows = []
    for name in names:
        node = one(title, ("push button", "button"), name)
        if node is None:
            return 1
        x, y, width, height = extents(node)
        print(f"'{name}': x={x} y={y} {width}x{height}")
        rows.append((y, height))
    if max(y for y, _ in rows) - min(y for y, _ in rows) > 2:
        print(f"{', '.join(names)} are not in one row of '{title}'")
        return 1
    print(f"{', '.join(names)} share one row in '{title}'")
    return 0


def fits(title, max_width, max_height, names) -> int:
    window = wait_window(title)
    if window is None:
        print(f"window '{title}' not found")
        return 1
    # The frame's own extents include the client-side shadow; the visible window is
    # the union of its showing direct children (content and title bar).
    boxes = []
    for index in range(window.childCount):
        child = window.getChildAtIndex(index)
        if child is not None and showing(child):
            x, y, w, h = extents(child)
            boxes.append((x, y, x + w, y + h))
    if not boxes:
        print(f"'{title}' has no visible content")
        return 1
    left, top = min(b[0] for b in boxes), min(b[1] for b in boxes)
    right, bottom = max(b[2] for b in boxes), max(b[3] for b in boxes)
    print(f"'{title}': {right - left}x{bottom - top} (content and title bar)")
    if right - left > max_width or bottom - top > max_height:
        print(f"'{title}' is larger than {max_width}x{max_height}")
        return 1
    for name in names:
        node = one(title, ("push button", "button"), name)
        if node is None:
            return 1
        x, y, w, h = extents(node)
        print(f"'{name}': x={x} y={y} {w}x{h}")
        if w <= 0 or h <= 0 or x < left or y < top or x + w > right or y + h > bottom:
            print(f"'{name}' is not fully inside '{title}'")
            return 1
    print(f"'{title}' fits {max_width}x{max_height} with {', '.join(names)} inside")
    return 0


def width_between(title, name, low, high) -> int:
    window = wait_window(title)
    if window is None:
        print(f"window '{title}' not found")
        return 1
    total, _ = window.queryComponent().getSize()
    for node, _ in base.walk(window):
        if node.name == name and showing(node):
            _, _, width, height = extents(node)
            print(f"'{name}': {width}x{height} in '{title}' {total} wide")
            if low <= width <= high:
                return 0
            print(f"'{name}' width {width} is not within {low}..{high}")
            return 1
    print(f"'{name}' not found in '{title}'")
    return 1


def close(title) -> int:
    window = wait_window(title)
    if window is None:
        print(f"window '{title}' not found")
        return 1
    for node, _ in base.walk(window):
        if node.getRoleName() in ("push button", "button") and node.name == "Close" and showing(node):
            if do_action(node):
                print(f"closed '{title}'")
                return 0
    print(f"no title bar close button in '{title}'")
    return 1


def absent(title, seconds) -> int:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if find_window(title) is None:
            print(f"'{title}' is closed")
            return 0
        time.sleep(0.1)
    print(f"'{title}' is still open after {seconds} s")
    return 1


def present(title, seconds) -> int:
    if wait_window(title, seconds) is not None:
        print(f"'{title}' is open")
        return 0
    print(f"'{title}' did not open within {seconds} s")
    return 1


def main(arguments: list[str]) -> int:
    if not arguments:
        print(__doc__)
        return 2
    command, rest = arguments[0], arguments[1:]
    table = {
        ("dump", 0): lambda: dump(),
        ("dump", 1): lambda: dump(rest[0]),
        ("click", 2): lambda: click(*rest),
        ("settext", 3): lambda: settext(*rest),
        ("text", 3): lambda: expect_text(*rest),
        ("contains", 3): lambda: expect_text(*rest, contains=True),
        ("toggle", 2): lambda: toggle(*rest),
        ("checked", 3): lambda: checked(*rest),
        ("wait", 2): lambda: wait(rest[0], rest[1], 10.0),
        ("wait", 3): lambda: wait(rest[0], rest[1], float(rest[2])),
        ("wait-prefix", 3): lambda: wait(rest[0], rest[1], float(rest[2]), prefix=True),
        ("gone", 3): lambda: gone(rest[0], rest[1], float(rest[2])),
        ("button", 3): lambda: button_state(*rest),
        ("tabs", 2): lambda: tabs(*rest),
        ("tab", 2): lambda: tab(*rest),
        ("focused", 2): lambda: focused(*rest),
        ("key", 2): lambda: key(*rest),
        ("close", 1): lambda: close(rest[0]),
        ("absent", 1): lambda: absent(rest[0], 10.0),
        ("absent", 2): lambda: absent(rest[0], float(rest[1])),
        ("present", 1): lambda: present(rest[0], 10.0),
        ("present", 2): lambda: present(rest[0], float(rest[1])),
    }
    if command == "samerow" and len(rest) >= 3:
        return samerow(rest[0], rest[1:])
    if command == "fits" and len(rest) >= 4:
        return fits(rest[0], int(rest[1]), int(rest[2]), rest[3:])
    if command == "width" and len(rest) == 4:
        return width_between(rest[0], rest[1], int(rest[2]), int(rest[3]))
    handler = table.get((command, len(rest)))
    if handler is None:
        print(__doc__)
        return 2
    return handler()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
