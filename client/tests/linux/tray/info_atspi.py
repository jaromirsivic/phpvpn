#!/usr/bin/env python3
"""AT-SPI probe for the Server information and Statistics windows (WP10c).

Extends tray_atspi.py (same directory) with table, tab, check box and field
commands. Run inside the private session of run-info-checks.sh.

    info_atspi.py dump                          all windows incl. check boxes, tabs, cells
    info_atspi.py title <prefix> [s]            wait for a window whose title starts with prefix; print it
    info_atspi.py table <window>                print the header and the rows of the table
    info_atspi.py rows <window> <n> [s]         wait until the table has n rows
    info_atspi.py tab <window> <tab>            select a notebook page by its tab name
    info_atspi.py text <window> <name>          print the full text of the named text widget
    info_atspi.py fields <window>               print "name = text" of every text field
    info_atspi.py field <window> <name> <text> [s]  wait until the named field shows text
    info_atspi.py toggle <window> <name>        click a check box
    info_atspi.py checked <window> <name>       exit 0 when the check box is checked
    info_atspi.py sensitive <window> <button> yes|no [s]  wait for the button sensitivity
    info_atspi.py anytext <text> [s]            wait until any window shows the text
    info_atspi.py dismiss <text>                press OK in the dialog that shows text
"""

from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pyatspi  # noqa: E402
import tray_atspi as base  # noqa: E402

ROLES = ("label", "push button", "button", "text", "entry", "check box", "page tab", "table cell",
         "table column header", "column header", "separator")


def showing(node) -> bool:
    return node.getState().contains(pyatspi.STATE_SHOWING)


def find(window, roles, name):
    for node, _ in base.walk(window):
        if node.getRoleName() in roles and node.name == name:
            return node
    return None


def window_or_fail(title):
    window = base.find_window(title)
    if window is None:
        print(f"window '{title}' not found")
    return window


def do_action(node, names=("click", "press", "toggle", "activate")) -> bool:
    action = node.queryAction()
    for index in range(action.nActions):
        if action.getName(index) in names:
            action.doAction(index)
            return True
    return False


def dump() -> int:
    found = base.windows()
    if not found:
        print("no phpvpn-tray windows")
        return 1
    for window in found:
        print(f"window '{window.name}' showing={showing(window)}")
        for node, depth in base.walk(window):
            role = node.getRoleName()
            if role in ROLES:
                states = node.getState()
                flags = ("" if showing(node) else " [hidden]") + \
                        ("" if states.contains(pyatspi.STATE_SENSITIVE) else " [insensitive]") + \
                        (" [checked]" if states.contains(pyatspi.STATE_CHECKED) else "") + \
                        (" [selected]" if states.contains(pyatspi.STATE_SELECTED) else "")
                name = f" name={node.name!r}" if role in ("text", "entry") and node.name else ""
                print(f"  {'  ' * min(depth, 6)}{role}: {base.text_of(node)!r}{name}{flags}")
    return 0


def title(prefix: str, seconds: float) -> int:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        for window in base.windows():
            if window.name and window.name.startswith(prefix):
                print(window.name)
                return 0
        time.sleep(0.1)
    print(f"no window title starts with '{prefix}' within {seconds} s")
    return 1


def table_node(window):
    for node, _ in base.walk(window):
        if node.getRoleName() in ("table", "tree table"):
            return node
    return None


def table_rows(window):
    table = table_node(window)
    if table is None:
        return None, []
    iface = table.queryTable()
    header = []
    for column in range(iface.nColumns):
        cell = iface.getColumnHeader(column)
        header.append(cell.name if cell is not None else "")
    rows = []
    for row in range(iface.nRows):
        cells = []
        for column in range(iface.nColumns):
            cell = iface.getAccessibleAt(row, column)
            cells.append(base.text_of(cell) if cell is not None else "")
        rows.append(cells)
    return header, rows


def table(title_: str) -> int:
    window = window_or_fail(title_)
    if window is None:
        return 1
    header, rows = table_rows(window)
    if header is None:
        print("no table")
        return 1
    print("header: " + " | ".join(header))
    print(f"rows: {len(rows)}")
    for cells in rows:
        print("row: " + " | ".join(cells))
    return 0


def rows(title_: str, count: int, seconds: float) -> int:
    end = time.monotonic() + seconds
    last = -1
    while time.monotonic() < end:
        window = base.find_window(title_)
        if window is not None:
            _, found = table_rows(window)
            last = len(found)
            if last == count:
                print(f"table has {count} rows")
                return 0
        time.sleep(0.2)
    print(f"table has {last} rows, expected {count}")
    return 1


def tab(title_: str, name: str) -> int:
    window = window_or_fail(title_)
    if window is None:
        return 1
    for node, _ in base.walk(window):
        if node.getRoleName() == "page tab list":
            for index in range(node.childCount):
                child = node.getChildAtIndex(index)
                if child is not None and child.name == name:
                    node.querySelection().selectChild(index)
                    print(f"selected tab '{name}'")
                    return 0
    print(f"tab '{name}' not found")
    return 1


def text(title_: str, name: str) -> int:
    window = window_or_fail(title_)
    if window is None:
        return 1
    node = find(window, ("text", "entry"), name)
    if node is None:
        print(f"text '{name}' not found")
        return 1
    iface = node.queryText()
    sys.stdout.write(iface.getText(0, iface.characterCount))
    return 0


def fields(title_: str) -> int:
    window = window_or_fail(title_)
    if window is None:
        return 1
    for node, _ in base.walk(window):
        if node.getRoleName() in ("text", "entry"):
            print(f"{node.name} = {base.text_of(node)}")
    return 0


def field(title_: str, name: str, expected: str, seconds: float) -> int:
    end = time.monotonic() + seconds
    value = None
    while time.monotonic() < end:
        window = base.find_window(title_)
        node = find(window, ("text", "entry"), name) if window is not None else None
        if node is not None:
            value = base.text_of(node)
            if value == expected:
                print(f"'{name}' = '{expected}'")
                return 0
        time.sleep(0.1)
    print(f"'{name}' = {value!r}, expected {expected!r}")
    return 1


def toggle(title_: str, name: str) -> int:
    window = window_or_fail(title_)
    node = find(window, ("check box", "toggle button"), name) if window is not None else None
    if node is None or not do_action(node):
        print(f"check box '{name}' not found")
        return 1
    print(f"toggled '{name}'")
    return 0


def checked(title_: str, name: str) -> int:
    window = window_or_fail(title_)
    node = find(window, ("check box", "toggle button"), name) if window is not None else None
    if node is None:
        print(f"check box '{name}' not found")
        return 1
    state = node.getState().contains(pyatspi.STATE_CHECKED)
    print(f"'{name}' checked={state}")
    return 0 if state else 1


def sensitive(title_: str, name: str, expected: str, seconds: float) -> int:
    want = expected == "yes"
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        window = base.find_window(title_)
        node = find(window, ("push button", "button"), name) if window is not None else None
        if node is not None and node.getState().contains(pyatspi.STATE_SENSITIVE) == want:
            print(f"'{name}' sensitive={want}")
            return 0
        time.sleep(0.05)
    print(f"'{name}' did not become sensitive={want} within {seconds} s")
    return 1


def anytext(expected: str, seconds: float) -> int:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        for window in base.windows():
            for node, _ in base.walk(window):
                if base.text_of(node) == expected and showing(node):
                    print(f"'{window.name}' shows '{expected}'")
                    return 0
        time.sleep(0.1)
    print(f"no window shows '{expected}' within {seconds} s")
    return 1


def dismiss(expected: str) -> int:
    for window in base.windows():
        texts = [base.text_of(node) for node, _ in base.walk(window)]
        if expected in texts:
            button = find(window, ("push button", "button"), "OK") or find(window, ("push button", "button"), "_OK")
            if button is not None and do_action(button):
                print(f"dismissed '{expected}'")
                return 0
    print(f"no dialog with '{expected}'")
    return 1


def main(arguments: list[str]) -> int:
    if not arguments:
        print(__doc__)
        return 2
    command, rest = arguments[0], arguments[1:]
    seconds = lambda index, default: float(rest[index]) if len(rest) > index else default  # noqa: E731
    if command == "dump":
        return dump()
    if command == "title" and len(rest) in (1, 2):
        return title(rest[0], seconds(1, 10.0))
    if command == "table" and len(rest) == 1:
        return table(rest[0])
    if command == "rows" and len(rest) in (2, 3):
        return rows(rest[0], int(rest[1]), seconds(2, 10.0))
    if command == "tab" and len(rest) == 2:
        return tab(rest[0], rest[1])
    if command == "text" and len(rest) == 2:
        return text(rest[0], rest[1])
    if command == "fields" and len(rest) == 1:
        return fields(rest[0])
    if command == "field" and len(rest) in (3, 4):
        return field(rest[0], rest[1], rest[2], seconds(3, 10.0))
    if command == "toggle" and len(rest) == 2:
        return toggle(rest[0], rest[1])
    if command == "checked" and len(rest) == 2:
        return checked(rest[0], rest[1])
    if command == "sensitive" and len(rest) in (3, 4):
        return sensitive(rest[0], rest[1], rest[2], seconds(3, 10.0))
    if command == "anytext" and len(rest) in (1, 2):
        return anytext(rest[0], seconds(1, 10.0))
    if command == "dismiss" and len(rest) == 1:
        return dismiss(rest[0])
    return base.main(arguments)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
