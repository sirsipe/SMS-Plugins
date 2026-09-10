#!/usr/bin/python3

import os
import sys

import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib


PORTAL_BUS_NAME = "org.freedesktop.portal.Desktop"
PORTAL_OBJECT = "/org/freedesktop/portal/desktop"
FILE_CHOOSER_INTERFACE = "org.freedesktop.portal.FileChooser"
REQUEST_INTERFACE = "org.freedesktop.portal.Request"


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] not in {"open", "save"}:
        print("usage: portal-dialog-health <open|save> <title>", file=sys.stderr)
        return 2

    DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    portal = bus.get_object(PORTAL_BUS_NAME, PORTAL_OBJECT)
    chooser = dbus.Interface(portal, FILE_CHOOSER_INTERFACE)
    method = chooser.OpenFile if sys.argv[1] == "open" else chooser.SaveFile
    token = f"sms_health_{os.getpid()}"
    options = dbus.Dictionary(
        {"handle_token": dbus.String(token, variant_level=1)}, signature="sv"
    )
    request_path = method("", sys.argv[2], options)
    print(f"request={request_path}", flush=True)

    result = {"response": None}
    loop = GLib.MainLoop()

    def on_response(response, _results):
        result["response"] = int(response)
        loop.quit()

    request = bus.get_object(PORTAL_BUS_NAME, request_path)
    request.connect_to_signal(
        "Response", on_response, dbus_interface=REQUEST_INTERFACE
    )

    def on_timeout():
        loop.quit()
        return False

    GLib.timeout_add_seconds(15, on_timeout)
    loop.run()
    if result["response"] != 1:
        print(
            f"portal request did not report user cancellation: {result['response']}",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except dbus.DBusException as error:
        print(f"portal D-Bus error: {error}", file=sys.stderr)
        raise SystemExit(1)
