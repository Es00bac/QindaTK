#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Silent AT-SPI client check for one qualification configuration.

Run by qualify_checks.check_atspi inside a private dbus-run-session (the bus
can start only org.a11y.Bus) with the configuration's private display: the
pattern of evidence/WP-055-atspi-probe.py, but the harness owns the display.
It starts qtk-preview on fixtures/atspi_probe.qml, finds the application in
the AT-SPI registry, reads the button and check box, performs their Press
actions through the accessibility bus and waits for the application to
change their accessible names. Nothing speaks and nothing reaches a screen.

Exit status: 0 passed, 1 failed, 3 the AT-SPI client libraries are missing.
"""
import argparse
import json
import subprocess
import sys
import time


def wait_for(predicate, timeout, step=0.2):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(step)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preview", required=True)
    parser.add_argument("--qml", required=True)
    parser.add_argument("--out")
    args = parser.parse_args()
    try:
        import gi
        gi.require_version("Atspi", "2.0")
        from gi.repository import Atspi
    except (ImportError, ValueError) as error:
        print("AT-SPI client libraries unavailable: %s" % error)
        return 3

    def safe(call, fallback):
        try:
            return call()
        except Exception:
            return fallback

    def walk(node, depth=0):
        yield node
        if depth < 14:
            for i in range(safe(node.get_child_count, 0)):
                child = safe(lambda: node.get_child_at_index(i), None)
                if child is not None:
                    yield from walk(child, depth + 1)

    def find(app, role, prefix):
        for node in walk(app):
            if safe(node.get_role_name, "") == role and (safe(node.get_name, "") or "").startswith(prefix):
                return node
        return None

    def press(node):
        action = node.get_action_iface()
        names = [action.get_action_name(i) for i in range(action.get_n_actions())]
        for i, name in enumerate(names):
            if name.lower() in ("press", "toggle", "click"):
                return action.do_action(i), names
        raise RuntimeError("no press action; actions: %s" % names)

    report = {"ok": False}
    app_proc = subprocess.Popen([args.preview, args.qml, "--size", "360x160"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        Atspi.init()

        def find_app():
            desktop = Atspi.get_desktop(0)
            for i in range(safe(desktop.get_child_count, 0)):
                app = safe(lambda: desktop.get_child_at_index(i), None)
                if app is not None and "qtk-preview" in (safe(app.get_name, "") or ""):
                    return app
            return None

        app = wait_for(find_app, 40)
        if app is None:
            raise RuntimeError("the application never registered with the AT-SPI registry")
        report["application"] = safe(app.get_name, "")
        button = wait_for(lambda: find(app, "push button", "Qualify press") or find(app, "button", "Qualify press"), 30)
        check = wait_for(lambda: find(app, "check box", "Qualify toggle"), 10)
        if button is None or check is None:
            roles = sorted({safe(node.get_role_name, "?") for node in walk(app)})
            raise RuntimeError("fixture controls not exposed; roles seen: %s" % roles)
        report["button"] = {"name": button.get_name(), "role": button.get_role_name(),
                            "description": safe(button.get_description, "")}
        report["pressResult"], report["buttonActions"] = press(button)
        pressed = wait_for(lambda: find(app, button.get_role_name(), "Pressed 1"), 10)
        report["buttonAfterPress"] = safe(pressed.get_name, None) if pressed else None
        report["checkBefore"] = check.get_name()
        report["toggleResult"], report["checkActions"] = press(check)
        toggled = wait_for(lambda: find(app, "check box", "Qualify toggle on"), 10)
        report["checkAfterPress"] = safe(toggled.get_name, None) if toggled else None
        report["ok"] = bool(pressed and toggled)
    except Exception as error:  # report, never hang
        report["error"] = repr(error)
    finally:
        app_proc.terminate()
        try:
            _, err = app_proc.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            app_proc.kill()
            _, err = app_proc.communicate()
        report["applicationStderrTail"] = (err or "").strip().splitlines()[-5:]
    text = json.dumps(report, indent=2, ensure_ascii=False)
    if args.out:
        with open(args.out, "w") as out:
            out.write(text + "\n")
    print(text)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
