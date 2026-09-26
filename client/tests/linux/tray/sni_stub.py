#!/usr/bin/env python3
"""Minimal org.kde.StatusNotifierWatcher for testing the tray's SNI path without the
GNOME AppIndicator extension (development harness, WP09).

    sni_stub.py serve     own org.kde.StatusNotifierWatcher on the session bus and
                          print every registered item; runs until killed
    sni_stub.py click <label>  activate a menu item through com.canonical.dbusmenu
    sni_stub.py inspect   print IconName, Title, Status and the dbusmenu labels of the
                          registered items (exit 1 when none is registered)

Prefer a private bus (dbus-run-session) so the desktop session is not affected.
Needs python3-gobject.
"""

from __future__ import annotations

import sys

from gi.repository import Gio, GLib

WATCHER = "org.kde.StatusNotifierWatcher"
PATH = "/StatusNotifierWatcher"
INTROSPECTION = f"""
<node>
  <interface name="{WATCHER}">
    <method name="RegisterStatusNotifierItem"><arg name="service" type="s" direction="in"/></method>
    <method name="RegisterStatusNotifierHost"><arg name="service" type="s" direction="in"/></method>
    <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
    <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
    <property name="ProtocolVersion" type="i" access="read"/>
    <signal name="StatusNotifierItemRegistered"><arg type="s"/></signal>
    <signal name="StatusNotifierItemUnregistered"><arg type="s"/></signal>
    <signal name="StatusNotifierHostRegistered"/>
  </interface>
</node>
"""


def serve() -> int:
    items: list[str] = []
    node = Gio.DBusNodeInfo.new_for_xml(INTROSPECTION)
    loop = GLib.MainLoop()

    def method(connection, sender, path, interface, name, parameters, invocation):
        if name == "RegisterStatusNotifierItem":
            service = parameters.unpack()[0]
            # Items may pass an object path; the bus name is then the sender.
            item = f"{sender}{service}" if service.startswith("/") else service
            if item not in items:
                items.append(item)
                print(f"registered {item}", flush=True)
                connection.emit_signal(None, PATH, WATCHER, "StatusNotifierItemRegistered", GLib.Variant("(s)", (item,)))
        invocation.return_value(None)

    def get_property(connection, sender, path, interface, name):
        if name == "RegisteredStatusNotifierItems":
            return GLib.Variant("as", items)
        if name == "IsStatusNotifierHostRegistered":
            return GLib.Variant("b", True)
        return GLib.Variant("i", 0)

    def acquired(connection, name):
        connection.register_object(PATH, node.interfaces[0], method, get_property, None)

    def owned(connection, name):
        print(f"owning {name}", flush=True)

    def lost(connection, name):
        print(f"could not own {name}", flush=True)
        loop.quit()

    Gio.bus_own_name(Gio.BusType.SESSION, WATCHER, Gio.BusNameOwnerFlags.NONE, acquired, owned, lost)
    loop.run()
    return 1


def labels(layout, depth=0, out=None):
    out = [] if out is None else out
    _, properties, children = layout
    kind = properties.get("type", "standard")
    label = properties.get("label", "")
    enabled = properties.get("enabled", True)
    visible = properties.get("visible", True)
    if depth > 0 and visible:
        out.append("-" if kind == "separator" else label + ("" if enabled else " [insensitive]"))
    for child in children:
        labels(child, depth + 1, out)
    return out


def inspect() -> int:
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    reply = bus.call_sync(WATCHER, PATH, "org.freedesktop.DBus.Properties", "Get",
                          GLib.Variant("(ss)", (WATCHER, "RegisteredStatusNotifierItems")),
                          GLib.VariantType("(v)"), Gio.DBusCallFlags.NONE, 2000, None)
    items = reply.unpack()[0]
    if not items:
        print("no registered StatusNotifierItem")
        return 1
    for item in items:
        slash = item.index("/")
        name, path = item[:slash], item[slash:]
        properties = bus.call_sync(name, path, "org.freedesktop.DBus.Properties", "GetAll",
                                   GLib.Variant("(s)", ("org.kde.StatusNotifierItem",)),
                                   GLib.VariantType("(a{sv})"), Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
        print(f"item {item}")
        for key in ("Id", "Category", "Status", "IconName", "Title", "IconThemePath", "Menu"):
            print(f"  {key}: {properties.get(key)!r}")
        menu = properties.get("Menu")
        if menu:
            layout = bus.call_sync(name, menu, "com.canonical.dbusmenu", "GetLayout",
                                   GLib.Variant("(iias)", (0, -1, [])),
                                   GLib.VariantType("(u(ia{sv}av))"), Gio.DBusCallFlags.NONE, 2000, None).unpack()[1]
            print("  menu: " + " | ".join(labels(layout)))
    return 0


def find_item(layout, label):
    identifier, properties, children = layout
    if properties.get("label") == label:
        return identifier
    for child in children:
        found = find_item(child, label)
        if found is not None:
            return found
    return None


def click(label: str) -> int:
    """Activate the menu item with this label, as a StatusNotifierHost does."""
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    reply = bus.call_sync(WATCHER, PATH, "org.freedesktop.DBus.Properties", "Get",
                          GLib.Variant("(ss)", (WATCHER, "RegisteredStatusNotifierItems")),
                          GLib.VariantType("(v)"), Gio.DBusCallFlags.NONE, 2000, None)
    for item in reply.unpack()[0]:
        slash = item.index("/")
        name, path = item[:slash], item[slash:]
        menu = bus.call_sync(name, path, "org.freedesktop.DBus.Properties", "Get",
                             GLib.Variant("(ss)", ("org.kde.StatusNotifierItem", "Menu")),
                             GLib.VariantType("(v)"), Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
        layout = bus.call_sync(name, menu, "com.canonical.dbusmenu", "GetLayout", GLib.Variant("(iias)", (0, -1, [])),
                               GLib.VariantType("(u(ia{sv}av))"), Gio.DBusCallFlags.NONE, 2000, None).unpack()[1]
        identifier = find_item(layout, label)
        if identifier is None:
            continue
        bus.call_sync(name, menu, "com.canonical.dbusmenu", "Event",
                      GLib.Variant("(isvu)", (identifier, "clicked", GLib.Variant("i", 0), 0)),
                      None, Gio.DBusCallFlags.NONE, 2000, None)
        print(f"clicked menu item '{label}'")
        return 0
    print(f"menu item '{label}' not found")
    return 1


def main(arguments: list[str]) -> int:
    if arguments == ["serve"]:
        return serve()
    if arguments == ["inspect"]:
        return inspect()
    if len(arguments) == 2 and arguments[0] == "click":
        return click(arguments[1])
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
