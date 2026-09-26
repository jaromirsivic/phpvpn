#!/usr/bin/env python3
"""AT-SPI scenarios of the Edit Config, Communication contracts and Password SHA
windows of the GTK tray (development harness, WP10 part b).

Started by run-config-checks.sh inside its private D-Bus session with the mock
service (--config-dir) and the tray running. Environment:
  PHPVPN_CHECK_CONFIG   absolute path of the mock's config.json
  PHPVPN_CHECK_CONTROL  FIFO of the mock's stdin control lines
  PHPVPN_CHECK_MOCKLOG  the mock's request log
  PHPVPN_CHECK_TRAYPID  pid of the tray

    config_checks.py editor|contracts|deadline|exit

Prints PASS/FAIL lines; exit code 0 when every check of the group passed.
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import time

import pyatspi

APPLICATION = "phpvpn-tray"
CONFIG = os.environ.get("PHPVPN_CHECK_CONFIG", "")
CONTROL = os.environ.get("PHPVPN_CHECK_CONTROL", "")
MOCKLOG = os.environ.get("PHPVPN_CHECK_MOCKLOG", "")
TRAYPID = int(os.environ.get("PHPVPN_CHECK_TRAYPID", "0") or 0)

EDITOR = "PHPVPN configuration"
CONTRACTS = "Communication contracts"
RESET_QUESTION = ("Restore the default configuration in this editor? "
                  "The file will change only after you choose Save.")
REJECTED = "The service rejected the configuration before publication. Your text has been preserved."
UNKNOWN = ("The file may already have changed. The save result is unknown. Close this editor and open Edit "
           "Config to inspect the current configuration. The save will not be retried automatically.")
RECONNECT = ("Your changes have been saved. It is strictly required that you disconnect and connect again to "
             "apply them. Do you want to disconnect and reconnect now?")
CONTRACTS_UNKNOWN = ("The save result is unknown. The file may already have changed. Check the current "
                     "configuration before saving again.")
KEY_ERROR = "Key must contain exactly 64 lowercase hexadecimal characters."
EMPTY_SHA = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"


class CheckFailed(Exception):
    pass


def fail(text: str) -> None:
    raise CheckFailed(text)


def passed(text: str) -> None:
    print(f"PASS: {text}", flush=True)


# --- AT-SPI access -------------------------------------------------------------
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
    # This process lives for minutes without a listener main loop; drop the AT-SPI
    # cache so new dialogs, names and states are read from the application.
    try:
        app.clear_cache()
    except Exception:  # pylint: disable=broad-except
        pass
    result = []
    for index in range(app.childCount):
        child = app.getChildAtIndex(index)
        if child is not None:
            result.append(child)
    return result


def walk(node):
    yield node
    try:
        children = [node.getChildAtIndex(index) for index in range(node.childCount)]
    except Exception:  # pylint: disable=broad-except
        return  # destroyed while walking
    for child in children:
        if child is not None:
            yield from walk(child)


def describe(item) -> tuple[str, str]:
    try:
        return item.name or "", item.getRoleName()
    except Exception:  # pylint: disable=broad-except
        return "", "defunct"


def matches(item, title: str, role: str | None) -> bool:
    """Title/role test that tolerates a window destroyed meanwhile."""
    try:
        return item.name == title and (role is None or item.getRoleName() == role)
    except Exception:  # pylint: disable=broad-except
        return False


def text_of(node) -> str:
    try:
        text = node.queryText()
        return text.getText(0, text.characterCount)
    except NotImplementedError:
        return node.name or ""


def states(node) -> set[str]:
    state = node.getState()
    names = set()
    for name, value in (("showing", pyatspi.STATE_SHOWING), ("sensitive", pyatspi.STATE_SENSITIVE),
                        ("editable", pyatspi.STATE_EDITABLE), ("checked", pyatspi.STATE_CHECKED),
                        ("focused", pyatspi.STATE_FOCUSED), ("active", pyatspi.STATE_ACTIVE)):
        if state.contains(value):
            names.add(name)
    return names


def window(title: str, role: str | None = None, seconds: float = 10.0):
    """The showing top-level of this title (role "frame" or "alert")."""
    end = time.monotonic() + seconds
    while True:
        for item in windows():
            if matches(item, title, role):
                return item
        if time.monotonic() >= end:
            fail(f"window '{title}' ({role or 'any'}) not found; windows: {[describe(w) for w in windows()]}")
        time.sleep(0.1)


def absent(title: str, role: str | None = None, seconds: float = 10.0) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if not any(matches(item, title, role) for item in windows()):
            return
        time.sleep(0.1)
    fail(f"window '{title}' still open after {seconds} s")


def find(node, name: str, roles=None, seconds: float = 5.0):
    end = time.monotonic() + seconds
    while True:
        for child in walk(node):
            if child.name == name and (roles is None or child.getRoleName() in roles):
                return child
        if time.monotonic() >= end:
            fail(f"'{name}' not found in '{node.name}'")
        time.sleep(0.1)


def button(node, name: str):
    for child in walk(node):
        if child.getRoleName() in ("push button", "button", "toggle button") and \
                (child.name == name or child.name.replace("_", "") == name):
            return child
    fail(f"button '{name}' not found in '{node.name}'")


def press(node, name: str) -> None:
    target = button(node, name)
    if "sensitive" not in states(target):
        fail(f"button '{name}' in '{node.name}' is insensitive")
    action = target.queryAction()
    for index in range(action.nActions):
        if action.getName(index) in ("click", "press", "activate"):
            action.doAction(index)
            return
    fail(f"button '{name}' has no click action")


def toggle(node, name: str) -> None:
    target = find(node, name, ("check box", "toggle button", "switch"))
    action = target.queryAction()
    for index in range(action.nActions):
        if action.getName(index) in ("click", "toggle", "press"):
            action.doAction(index)
            return
    fail(f"'{name}' has no toggle action")


def set_text(node, name: str, text: str) -> None:
    target = find(node, name, ("text", "entry", "password text"))
    target.queryEditableText().setTextContents(text)


def insert_text(node, name: str, position: int, text: str) -> None:
    target = find(node, name, ("text", "entry"))
    target.queryEditableText().insertText(position, text, len(text.encode("utf-8")))


def field_text(node, name: str) -> str:
    return text_of(find(node, name, ("text", "entry")))


def labels(node) -> list[str]:
    return [text_of(child) for child in walk(node)
            if child.getRoleName() == "label" and "showing" in states(child)]


def wait_label(node, text: str, seconds: float = 10.0) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if text in labels(node):
            return
        time.sleep(0.1)
    fail(f"'{node.name}' did not show label {text!r}; labels: {labels(node)!r}")


def wait_until(predicate, what: str, seconds: float = 10.0) -> None:
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(0.1)
    fail(what)


def alert(title: str, text: str, answer: str, seconds: float = 10.0) -> None:
    """Waits for the alert with this title and text and presses answer."""
    dialog = window(title, "alert", seconds)
    wait_label(dialog, text, 3)
    press(dialog, answer)
    absent_alert(dialog)


def absent_alert(dialog) -> None:
    end = time.monotonic() + 5
    while time.monotonic() < end:
        try:
            if dialog.name is None or dialog not in windows():
                return
        except Exception:  # pylint: disable=broad-except
            return
        time.sleep(0.1)


def no_alert(seconds: float = 1.5) -> None:
    time.sleep(seconds)
    shown = [name for name, role in map(describe, windows()) if role == "alert"]
    if shown:
        fail(f"unexpected dialog(s): {shown}")


def has_focus(frame, node) -> bool:
    """Keyboard focus of node. The headless compositor may not activate a window
    that was presented without an activation token; then only the focus widget
    of the window can be checked, which AT-SPI reports as focusable."""
    if "active" in states(frame):
        return "focused" in states(node)
    print(f"NOTE: '{frame.name}' is not the active window; focus of '{node.name}' not verifiable", flush=True)
    return node.getState().contains(pyatspi.STATE_FOCUSABLE)


def exit_from_menu() -> None:
    """Exit of the control window asks its question inside the button's AT-SPI
    action (a nested dialog loop in TrayApp::advanceExit), so the click runs in a
    separate process that may wait for that call; this process stays responsive."""
    subprocess.Popen([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "tray_atspi.py"),
                      "click", "PHPVPN", "Exit"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def control_window():
    return window("PHPVPN", "frame")


def menu(action: str) -> None:
    press(control_window(), action)


# --- mock and file helpers -------------------------------------------------------
def mock(line: str) -> None:
    with open(CONTROL, "w", encoding="utf-8") as fifo:
        fifo.write(line + "\n")
    time.sleep(0.3)


def requests(command: str) -> int:
    with open(MOCKLOG, encoding="utf-8") as log:
        return sum(1 for line in log if line.strip() == f"request {command}")


def config_text() -> str | None:
    try:
        with open(CONFIG, encoding="utf-8") as file:
            return file.read()
    except FileNotFoundError:
        return None


def config_json() -> dict:
    text = config_text()
    if text is None:
        fail("config.json is missing")
    return json.loads(text)


def default_document() -> dict:
    # The shared template of core config.h (kDefaultConfigDocument).
    return {
        "masterSetup": {
            "serverSecret": "CHANGE_THIS_SERVER_SECRET",
            "serverUrl": "http://127.0.0.1/phpvpn.php",
            "httpHeaders": "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; "
                           "Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0",
            "trustAnyTlsCertificate": True,
            "useDefaultServerSecretInWizard": True,
            "vpnCidr": "10.99.99.0/24",
            "virtualIp": "10.99.99.2",
            "autoDetectIpAddressInWizard": True,
            "accessKey": "0202020202020202020202020202020202020202020202020202020202020202",
            "useDefaultAccessKeyInWizard": True,
            "autoConnectOnStartup": False,
        },
        "communicationContracts": {},
    }


def editor():
    return window(EDITOR, "frame")


def editor_view_text() -> str:
    return field_text(editor(), "Configuration JSON")


def open_editor() -> None:
    menu("Edit Config")
    window(EDITOR, "frame")


def wait_status(text: str, seconds: float = 20.0) -> None:
    wait_label(control_window(), text, seconds)


# --- scenarios -------------------------------------------------------------------
def editor_checks() -> None:
    # Missing configuration: the tray asks, No keeps everything unchanged.
    if config_text() is not None:
        fail("the store should start without config.json")
    documents = requests("get-config-document")
    menu("Edit Config")
    dialog = window("Create configuration", "alert")
    wait_label(dialog, f"No configuration exists at:\n{CONFIG}\nCreate the default configuration now and open Edit Config? "
                       "The file will be written immediately.\nYou can also choose No and use Configuration Wizard, or "
                       "copy a configuration supplied by your administrator to this path.", 3)
    press(dialog, "No")
    no_alert(1)
    if config_text() is not None or requests("create-default-config"):
        fail("No created a configuration")
    if requests("get-config-document") != documents + 1:
        fail("Edit Config did not use exactly one get-config-document")
    passed("missing configuration: create question, No leaves the disk untouched")

    menu("Edit Config")
    alert("Create configuration", f"No configuration exists at:\n{CONFIG}\nCreate the default configuration now and "
          "open Edit Config? The file will be written immediately.\nYou can also choose No and use Configuration "
          "Wizard, or copy a configuration supplied by your administrator to this path.", "Yes")
    frame = editor()
    if requests("create-default-config") != 1:
        fail("create-default-config not sent exactly once")
    if config_json() != default_document():
        fail("the created file is not the default template")
    wait_label(frame, "Edit configuration", 3)
    wait_label(frame, CONFIG, 3)
    for name in ("Reset", "Cancel", "Save"):
        button(frame, name)
    view = find(frame, "Configuration JSON", ("text",))
    if not has_focus(frame, view) or "editable" not in states(view):
        fail(f"editor not focused/editable: {states(view)}")
    if json.loads(editor_view_text()) != default_document():
        fail("the editor does not show the created document")
    if '\n  "masterSetup": {\n    "serverSecret"' not in editor_view_text():
        fail("the document is not pretty printed")
    passed("Yes creates the default file and opens Edit Config with heading, path, Reset/Cancel/Save")

    documents = requests("get-config-document")
    menu("Edit Config")
    time.sleep(1)
    if requests("get-config-document") != documents or sum(1 for w in windows() if matches(w, EDITOR, "frame")) != 1:
        fail("a repeated Edit Config read the configuration again or opened a second editor")
    passed("a repeated Edit Config only activates the open editor")

    # Cancel on a clean editor closes directly.
    press(editor(), "Cancel")
    absent(EDITOR, "frame")
    no_alert(0.5)
    passed("Cancel closes a clean editor without a question")

    # Rejected before publication: the text is preserved.
    open_editor()
    before = config_text()
    set_text(editor(), "Configuration JSON", '{"masterSetup": {')
    press(editor(), "Save")
    alert("Edit Config", REJECTED, "OK")
    if editor_view_text() != '{"masterSetup": {' or config_text() != before:
        fail("rejected save changed the text or the file")
    view = find(editor(), "Configuration JSON", ("text",))
    if "editable" not in states(view) or "sensitive" not in states(button(editor(), "Save")):
        fail("editor not editable after a rejection")
    passed("invalid JSON: service rejection, text and file preserved, editor editable")

    # The header grammar error names only path, line and reason.
    document = default_document()
    document["masterSetup"]["httpHeaders"] = "Bad Header Without Colon"
    set_text(editor(), "Configuration JSON", json.dumps(document, indent=2))
    press(editor(), "Save")
    alert("Edit Config", REJECTED + "\n\nmasterSetup.httpHeaders line 1: invalid field name", "OK")
    if config_text() != before:
        fail("header rejection changed the file")
    passed("HTTP header validation error shows path/line/reason only")

    # Busy is a proven rejection too.
    mock("setconfig busy")
    press(editor(), "Save")
    alert("Edit Config", REJECTED, "OK")
    mock("setconfig normal")
    passed("busy reply is classified as rejected")

    # Dirty Cancel asks; No keeps the exact draft.
    press(editor(), "Cancel")
    alert("Edit Config", "Discard unsaved changes?", "No")
    if json.loads(editor_view_text()) != document:
        fail("No did not keep the draft")
    passed("dirty Cancel asks Discard unsaved changes?, No keeps the draft")

    # Reset: question, get-default-config, draft replaced and dirty, disk unchanged.
    press(editor(), "Reset")
    alert("Edit Config", RESET_QUESTION, "No")
    if json.loads(editor_view_text()) != document:
        fail("Reset No changed the draft")
    defaults = requests("get-default-config")
    press(editor(), "Reset")
    alert("Edit Config", RESET_QUESTION, "Yes")
    wait_until(lambda: "CHANGE_THIS_SERVER_SECRET" in editor_view_text() and
               json.loads(editor_view_text()) == default_document(), "Reset did not load the default", 8)
    if requests("get-default-config") != defaults + 1 or config_text() != before:
        fail("Reset did not use get-default-config or changed the file")
    press(editor(), "Cancel")
    alert("Edit Config", "Discard unsaved changes?", "Yes")
    absent(EDITOR, "frame")
    if config_text() != before:
        fail("Discard changed the file")
    passed("Reset (No/Yes) loads get-default-config into the draft only; Discard closes")

    # Confirmed save while disconnected: no question, the editor closes.
    open_editor()
    document = default_document()
    document["masterSetup"]["serverUrl"] = "http://127.0.0.1/saved.php"
    set_text(editor(), "Configuration JSON", json.dumps(document, indent=2))
    sets = requests("set-config")
    press(editor(), "Save")
    absent(EDITOR, "frame", 10)
    if requests("set-config") != sets + 1 or config_json() != document:
        fail("confirmed save did not publish exactly the draft")
    no_alert(1)
    passed("confirmed save while disconnected publishes and closes without a question")

    # Unknown: foreign reply after the write; read-only draft, close asks.
    mock("setconfig foreign")
    open_editor()
    document["masterSetup"]["serverUrl"] = "http://127.0.0.1/unknown.php"
    set_text(editor(), "Configuration JSON", json.dumps(document, indent=2))
    press(editor(), "Save")
    dialog = window("Edit Config", "alert")
    wait_label(dialog, UNKNOWN, 3)
    press(dialog, "OK")
    time.sleep(0.5)
    frame = editor()
    view = find(frame, "Configuration JSON", ("text",))
    if "editable" in states(view) or "sensitive" in states(button(frame, "Save")) or \
            "sensitive" in states(button(frame, "Reset")) or "sensitive" not in states(button(frame, "Cancel")):
        fail(f"unknown state not read-only: view={states(view)}")
    if config_json() != document:
        fail("the foreign reply case should have published the file")
    press(frame, "Cancel")
    alert("Edit Config", "The file may already have changed. Close without checking the save result?", "No")
    editor()
    press(editor(), "Cancel")
    alert("Edit Config", "The file may already have changed. Close without checking the save result?", "Yes")
    absent(EDITOR, "frame")
    mock("setconfig normal")
    passed("unknown result: warning, read-only draft without Save/Reset, close confirmation")

    # Invalid (repairable text) configuration opens as raw text in Edit Config.
    mock("config invalid")
    open_editor()
    if editor_view_text() != '{\n  "masterSetup": {\n    "serverUrl": \n}\n':
        fail(f"invalid JSON not shown verbatim: {editor_view_text()!r}")
    press(editor(), "Cancel")
    absent(EDITOR, "frame")
    menu("Communication contracts")
    alert("Communication contracts", f"The configuration is invalid.\n{CONFIG}\nOpen Edit Config to repair the configuration.", "OK")
    passed("invalid JSON opens verbatim in Edit Config; Contracts refuses it with the repair hint")

    # Repair through the editor, then save while connected -> reconnect question.
    open_editor()
    document = default_document()
    set_text(editor(), "Configuration JSON", json.dumps(document, indent=2))
    press(editor(), "Save")
    absent(EDITOR, "frame")
    mock("connect")
    wait_status("PHPVPN: connected", 20)
    open_editor()
    document["masterSetup"]["autoConnectOnStartup"] = True
    set_text(editor(), "Configuration JSON", json.dumps(document, indent=2))
    disconnects = requests("disconnect")
    press(editor(), "Save")
    alert("Configuration saved", RECONNECT, "No")
    absent(EDITOR, "frame")
    time.sleep(1)
    if requests("disconnect") != disconnects or config_json() != document:
        fail("No must keep the connection and the saved file")
    passed("save while connected: shared reconnect question, No keeps the VPN connected")

    open_editor()
    document["masterSetup"]["autoConnectOnStartup"] = False
    set_text(editor(), "Configuration JSON", json.dumps(document, indent=2))
    connects = requests("connect")
    press(editor(), "Save")
    alert("Configuration saved", RECONNECT, "Yes")
    absent(EDITOR, "frame")
    window("PHPVPN disconnection", "frame", 10)
    wait_until(lambda: requests("disconnect") == disconnects + 1, "Yes did not disconnect", 10)
    wait_until(lambda: requests("connect") == connects + 1, "Yes did not reconnect", 20)
    wait_status("PHPVPN: connected", 30)
    passed("reconnect question Yes: Disconnect progress, then a new Connect")
    absent("PHPVPN connection", "frame", 10)


def deadline_checks() -> None:
    # One original 30 s deadline: a stalled set-config becomes unknown, the editor
    # stays locked meanwhile (Save, Reset, Cancel and X unavailable).
    if config_text() is None:
        fail("the deadline check needs an existing configuration")
    mock("setconfig delay 32000")
    menu("Edit Config")
    document = default_document()
    document["masterSetup"]["serverUrl"] = "http://127.0.0.1/deadline.php"
    set_text(editor(), "Configuration JSON", json.dumps(document, indent=2))
    started = time.monotonic()
    press(editor(), "Save")
    time.sleep(1)
    frame = editor()
    for name in ("Save", "Reset", "Cancel"):
        if "sensitive" in states(button(frame, name)):
            fail(f"{name} available while saving")
    if "sensitive" in states(find(frame, "Configuration JSON", ("text",))):
        fail("text editable while saving")
    dialog = window("Edit Config", "alert", 40)
    elapsed = time.monotonic() - started
    wait_label(dialog, UNKNOWN, 3)
    if not 28 <= elapsed <= 33:
        fail(f"unknown after {elapsed:.1f} s, expected the 30 s deadline")
    press(dialog, "OK")
    mock("setconfig normal")
    press(editor(), "Cancel")
    alert("Edit Config", "The file may already have changed. Close without checking the save result?", "Yes")
    absent(EDITOR, "frame")
    passed(f"stalled set-config: locked editor, unknown after {elapsed:.1f} s (30 s deadline)")


def row_name(peer: int, column: str) -> str:
    return f"10.99.99.2_10.99.99.{peer} {column}"


def contracts_checks() -> None:
    if config_text() is None:
        fail("contracts checks need the configuration of the editor checks")
    mock("setconfig normal")
    mock("disconnect")
    wait_status("PHPVPN: disconnected", 20)
    documents = requests("get-config-document")
    menu("Communication contracts")
    frame = window(CONTRACTS, "frame")
    if requests("get-config-document") != documents + 1:
        fail("Contracts did not read exactly one get-config-document")
    for text in ("Communication contracts", "Manage secure communication between this computer and other virtual IP "
                 "addresses using end-to-end encryption.", "Your virtual IP", "10.99.99.2", "Contract", "Enabled",
                 "Key", "Actions", "10.99.99.2_10.99.99.3"):
        wait_label(frame, text, 3)
    if "Changes take effect on the next connection." in labels(frame):
        fail("next-connection notice shown while disconnected")
    names = [child.name for child in walk(frame)]
    if row_name(22, "Enabled") not in names or row_name(23, "Enabled") in names:
        fail("the first page does not hold exactly the rows .3 to .22")
    find(frame, "Page 1 of 13, current page")
    for name in ("First page", "Previous page", "Next page", "Last page", "Page 13 of 13"):
        find(frame, name)
    save = button(frame, "Save")
    if "sensitive" in states(save):
        fail("Save enabled without changes")
    if not has_focus(frame, find(frame, row_name(3, "Enabled"))):
        fail("focus does not start at the first Enabled")
    passed("Contracts: heading, own IP, four columns, 20 of 252 rows, pager, clean Save disabled")

    documents = requests("get-config-document")
    menu("Communication contracts")
    time.sleep(1)
    if requests("get-config-document") != documents:
        fail("a repeated Communication contracts read the configuration again")
    passed("a repeated Communication contracts only activates the window")

    # Enabling without a key blocks Save with the validation text.
    toggle(frame, row_name(3, "Enabled"))
    wait_label(frame, f"{KEY_ERROR}\nFirst invalid peer: 10.99.99.3", 3)
    save = button(frame, "Save (1 change)")
    if "sensitive" in states(save):
        fail("Save enabled with an invalid enabled key")
    key = find(frame, row_name(3, "Key"))
    if "Invalid" not in (key.description or ""):
        fail(f"invalid Key has no accessible description: {key.description!r}")
    # Whole over-budget and unsupported insertions are rejected unchanged.
    insert_text(frame, row_name(3, "Key"), 0, "a" * 200)
    time.sleep(0.3)
    if field_text(frame, row_name(3, "Key")) != "":
        fail("an over-budget insertion was partially accepted")
    insert_text(frame, row_name(3, "Key"), 0, "ab\ncd")
    wait_label(frame, "Key contains unsupported text.", 3)
    if field_text(frame, row_name(3, "Key")) != "":
        fail("an unsupported insertion changed the key")
    set_text(frame, row_name(3, "Key"), "a" * 64)
    wait_until(lambda: "sensitive" in states(button(frame, "Save (1 change)")), "valid key did not enable Save", 3)
    if "Invalid" in (find(frame, row_name(3, "Key")).description or ""):
        fail("valid Key still described as invalid")
    passed("enable without key blocks Save; over-budget/unsupported insertions rejected; valid key enables Save")

    # A disabled contract keeps its Key read-only and Change unavailable.
    if "editable" in states(find(frame, row_name(4, "Key"))) or \
            "sensitive" in states(find(frame, row_name(4, "Change"))):
        fail("disabled contract editable")
    passed("disabled contract: Key read-only, Change unavailable")

    # Password SHA: Default hashes the canonical pair, OK applies the hash.
    press(frame, row_name(3, "Change"))
    title = "Password SHA - Contract 10.99.99.2_10.99.99.3"
    child = window(title, "frame")
    for text in ("Generate key from password", "Enter a password to create a SHA-256 communication key.",
                 "Password", "The key updates automatically as you type.", "SHA-256 key"):
        wait_label(child, text, 3)
    if field_text(child, "SHA-256 key") != EMPTY_SHA:
        fail("empty password does not show the SHA-256 of the empty string")
    if not has_focus(child, find(child, "Password", ("text", "entry"))):
        fail("focus not in Password")
    if "sensitive" not in states(window(CONTRACTS, "frame")):
        fail("Password SHA disabled its parent")
    press(child, "Default")
    pair = "10.99.99.2_10.99.99.3"
    wait_until(lambda: field_text(child, "Password") == pair, "Default did not set the pair", 3)
    expected = hashlib.sha256(pair.encode()).hexdigest()
    if field_text(child, "SHA-256 key") != expected:
        fail("Default hash differs")
    set_text(child, "Password", "pässwörd 😀")
    wait_until(lambda: field_text(child, "SHA-256 key") == hashlib.sha256("pässwörd 😀".encode()).hexdigest(),
               "hash does not follow typing (UTF-8)", 3)
    press(child, "Random")
    wait_until(lambda: len(field_text(child, "Password")) == 64, "Random did not set 64 characters", 3)
    random_password = field_text(child, "Password")
    if set(random_password) - set("ghjkmnpqrstuvwxyz"):
        fail("Random password outside the alphabet")
    if field_text(child, "SHA-256 key") != hashlib.sha256(random_password.encode()).hexdigest():
        fail("Random hash differs")
    press(child, "Default")
    press(child, "OK")
    absent(title, "frame")
    wait_until(lambda: field_text(frame, row_name(3, "Key")) == expected, "OK did not apply the hash", 3)
    passed("Password SHA: texts, empty hash, Default pair hash, UTF-8 typing, Random 64 chars, OK applies")

    # Pages keep drafts by remote IP.
    press(frame, "Page 2 of 13")
    wait_label(frame, "10.99.99.2_10.99.99.23", 5)
    find(frame, "Page 2 of 13, current page")
    toggle(frame, row_name(23, "Enabled"))
    set_text(frame, row_name(23, "Key"), "b" * 64)
    wait_until(lambda: "sensitive" in states(button(frame, "Save (2 changes)")), "second change not counted", 3)
    press(frame, "First page")
    wait_label(frame, "10.99.99.2_10.99.99.3", 5)
    if field_text(frame, row_name(3, "Key")) != expected or "checked" not in states(find(frame, row_name(3, "Enabled"))):
        fail("page change lost the draft")
    passed("pagination keeps drafts by remote IP; Save counts 2 changes")

    # Dirty Cancel asks; No keeps.
    press(frame, "Cancel")
    alert(CONTRACTS, "Discard unsaved changes?", "No")
    window(CONTRACTS, "frame")
    passed("dirty Cancel asks, No keeps the draft")

    # Rejected before publication keeps the draft editable.
    mock("setconfig busy")
    press(frame, "Save (2 changes)")
    wait_until(lambda: "sensitive" in states(button(frame, "Save (2 changes)")), "rejected save did not return to editing", 8)
    mock("setconfig normal")
    passed("busy rejection returns to editing with the draft")

    # Confirmed save while disconnected closes without a question.
    before = requests("set-config")
    master = config_json()["masterSetup"]
    press(frame, "Save (2 changes)")
    absent(CONTRACTS, "frame", 10)
    contracts = config_json().get("communicationContracts", {})
    if requests("set-config") != before + 1 or contracts.get("10.99.99.3", {}).get("payloadKey") != expected or \
            contracts.get("10.99.99.23", {}).get("payloadKey") != "b" * 64:
        fail(f"saved contracts differ: {contracts}")
    if config_json()["masterSetup"] != master:
        fail("contracts save changed unrelated members")
    no_alert(1)
    passed("confirmed save publishes the two contracts, keeps other members, closes")

    # Unknown -> Check current configuration -> matched -> closes.
    mock("setconfig foreign")
    menu("Communication contracts")
    frame = window(CONTRACTS, "frame")
    toggle(frame, row_name(3, "Enabled"))
    press(frame, "Save (1 change)")
    wait_label(frame, CONTRACTS_UNKNOWN, 8)
    check = button(frame, "Check current configuration")
    if "showing" not in states(check):
        fail("Check current configuration not shown")
    if "showing" in states(button(frame, "Save (1 change)")):
        fail("Save still shown in unknown")
    if "sensitive" in states(find(frame, row_name(3, "Enabled"))):
        fail("rows editable in unknown")
    press(frame, "Check current configuration")
    alert(CONTRACTS, "The current configuration matches your changes.", "OK")
    absent(CONTRACTS, "frame")
    passed("unknown result: Check current configuration finds the published draft and closes")

    # Unknown -> differing file -> Save draft again after confirmation.
    menu("Communication contracts")
    frame = window(CONTRACTS, "frame")
    toggle(frame, row_name(5, "Enabled"))
    set_text(frame, row_name(5, "Key"), "c" * 64)
    press(frame, "Save (1 change)")
    wait_label(frame, CONTRACTS_UNKNOWN, 8)
    mock("config missing")
    mock("setconfig normal")
    press(frame, "Check current configuration")
    wait_label(frame, "The current configuration differs. Your draft has been kept.", 8)
    press(frame, "Save draft again")
    alert("Save draft again", "Replace the current whole configuration with your draft?", "Yes")
    absent(CONTRACTS, "frame", 10)
    if config_json().get("communicationContracts", {}).get("10.99.99.5", {}).get("payloadKey") != "c" * 64:
        fail("Save draft again did not publish the draft")
    passed("differing check offers Save draft again; confirmed repeat publishes the whole draft")

    # Unknown close asks the unknown question.
    mock("setconfig foreign")
    menu("Communication contracts")
    frame = window(CONTRACTS, "frame")
    toggle(frame, row_name(5, "Enabled"))
    press(frame, "Save (1 change)")
    wait_label(frame, CONTRACTS_UNKNOWN, 8)
    press(frame, "Cancel")
    alert(CONTRACTS, "The file may already have changed. Close without checking the save result?", "Yes")
    absent(CONTRACTS, "frame")
    mock("setconfig normal")
    passed("unknown close asks the unknown question")

    # Connected: next-connection notice and the reconnect question.
    mock("connect")
    wait_status("PHPVPN: connected", 20)
    menu("Communication contracts")
    frame = window(CONTRACTS, "frame")
    wait_label(frame, "Changes take effect on the next connection.", 5)
    toggle(frame, row_name(6, "Enabled"))
    set_text(frame, row_name(6, "Key"), "d" * 64)
    disconnects = requests("disconnect")
    press(frame, "Save (1 change)")
    alert("Configuration saved", RECONNECT, "No")
    absent(CONTRACTS, "frame")
    time.sleep(1)
    if requests("disconnect") != disconnects:
        fail("No disconnected")
    passed("connected: next-connection notice, reconnect question No keeps the VPN")

    # Exit with a dirty Contracts draft asks the shared question; No keeps the tray.
    menu("Communication contracts")
    frame = window(CONTRACTS, "frame")
    toggle(frame, row_name(7, "Enabled"))
    exit_from_menu()
    alert("Exit PHPVPN", "Discard unsaved changes?", "No")
    time.sleep(0.5)
    if TRAYPID:
        os.kill(TRAYPID, 0)
    window(CONTRACTS, "frame")
    exit_from_menu()
    alert("Exit PHPVPN", "Discard unsaved changes?", "Yes")
    if TRAYPID:
        wait_until(lambda: not os.path.exists(f"/proc/{TRAYPID}"), "Exit did not end the tray", 10)
    disconnects_after = requests("disconnect")
    if disconnects_after != disconnects:
        fail("Exit sent a disconnect")
    passed("Exit with a dirty draft: shared question, No keeps, Yes ends the tray without Disconnect")


def main(arguments: list[str]) -> int:
    groups = {"editor": editor_checks, "contracts": contracts_checks, "deadline": deadline_checks}
    if not arguments or any(name not in groups for name in arguments):
        print(__doc__)
        return 2
    try:
        for name in arguments:
            groups[name]()
    except CheckFailed as error:
        print(f"FAIL: {error}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
