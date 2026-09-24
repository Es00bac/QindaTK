#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Silent AT-SPI check of the WP-055 workbook example (evidence/WP-055.md).

    python3 WP-055-atspi-probe.py --preview build/wp055/tools/preview/qtk-preview \
        --qml examples/workbook/Main.qml [--out result.json]

The script re-runs itself under `dbus-run-session` with a private session bus
that can start only the accessibility bus (no portals or desktop services),
and without the caller's WAYLAND_DISPLAY or DISPLAY. A headless Weston
(pixman) hosts the window, so nothing appears on screen and nothing speaks.
It reads the accessible tree through the real AT-SPI bus, performs the
assistive Press actions on a cell and a sheet tab, and prints JSON. Needs
weston, at-spi2-core, dbus and the GObject-introspection Atspi 2.0 bindings.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

INNER = "WP055_ATSPI_INNER"
BUS_CONFIG = """<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <keep_umask/>
  <listen>unix:tmpdir=/tmp</listen>
  <servicedir>{services}</servicedir>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
"""


def reexec_on_private_bus(argv):
    """Starts the private bus that can activate only org.a11y.Bus."""
    scratch = tempfile.mkdtemp(prefix="wp055-atspi-bus-")
    services = os.path.join(scratch, "services")
    os.mkdir(services)
    shutil.copy("/usr/share/dbus-1/services/org.a11y.Bus.service", services)
    config = os.path.join(scratch, "session.conf")
    with open(config, "w") as out:
        out.write(BUS_CONFIG.format(services=services))
    env = {k: v for k, v in os.environ.items() if k not in ("WAYLAND_DISPLAY", "DISPLAY")}
    env[INNER] = "1"
    try:
        return subprocess.call(["dbus-run-session", "--config-file=" + config, "--",
                                sys.executable, os.path.abspath(__file__)] + argv, env=env)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def wait_for(predicate, timeout, step=0.1):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(step)
    return None


def probe(args):
    import gi
    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi

    def safe(call, fallback):
        try:
            return call()
        except Exception:
            return fallback

    def role(node):
        return safe(node.get_role_name, "?")

    def name(node):
        return safe(node.get_name, "") or ""

    def description(node):
        return safe(node.get_description, "") or ""

    def states(node):
        return safe(lambda: sorted(Atspi.StateType(s).value_nick for s in node.get_state_set().get_states()), [])

    def actions(node):
        action = safe(node.get_action_iface, None)
        return [action.get_action_name(i) for i in range(action.get_n_actions())] if action else []

    def walk(node, depth=0):
        yield node
        if depth < 12:
            for i in range(safe(node.get_child_count, 0)):
                child = safe(lambda: node.get_child_at_index(i), None)
                if child is not None:
                    yield from walk(child, depth + 1)

    def first(nodes, role_name=None, prefix=None, contains=None):
        for node in nodes:
            if role_name and role(node) != role_name:
                continue
            if prefix is not None and not name(node).startswith(prefix):
                continue
            if contains is not None and contains not in name(node):
                continue
            return node
        return None

    def describe(node):
        if node is None:
            return None
        return {"role": role(node), "name": name(node), "description": description(node),
                "states": states(node), "actions": actions(node)}

    def press(node):
        action = node.get_action_iface()
        for i in range(action.get_n_actions()):
            if action.get_action_name(i) == "Press":
                return action.do_action(i)
        raise RuntimeError("no Press action on %s %r" % (role(node), name(node)))

    # A Unix socket path must fit in 108 bytes: keep the runtime dir short.
    runtime = tempfile.mkdtemp(prefix="wp055-a11y-", dir="/run/user/%d" % os.getuid())
    os.chmod(runtime, 0o700)
    env = dict(os.environ, XDG_RUNTIME_DIR=runtime)
    socket = "wp055-a11y"
    weston = subprocess.Popen(["weston", "--backend=headless", "--renderer=pixman", "--shell=kiosk",
                               "--socket=" + socket, "--width=1440", "--height=900", "--idle-time=0"],
                              env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    result = {"ok": False}
    app_proc = None
    try:
        if not wait_for(lambda: os.path.exists(os.path.join(runtime, socket)), 20):
            raise RuntimeError("weston did not create its socket")
        app_env = dict(env, WAYLAND_DISPLAY=socket, QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
                       QT_LINUX_ACCESSIBILITY_ALWAYS_ON="1", QT_ACCESSIBILITY="1")
        app_proc = subprocess.Popen([args.preview, args.qml, "--size", "1440x820"], env=app_env,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        Atspi.init()

        def find_app():
            desktop = Atspi.get_desktop(0)
            for i in range(desktop.get_child_count()):
                app = desktop.get_child_at_index(i)
                if app is not None and "qtk-preview" in name(app):
                    return app
            return None

        app = wait_for(find_app, 40, 0.5)
        if app is None:
            raise RuntimeError("the application never registered with the AT-SPI registry")
        nodes = wait_for(lambda: (lambda n: n if first(n, "table cell") else None)(list(walk(app))), 40, 0.5)
        if not nodes:
            raise RuntimeError("no table cells exposed")
        counts = {}
        for node in nodes:
            counts[role(node)] = counts.get(role(node), 0) + 1
        pane = first(nodes, "panel", contains=", sheet ")
        cell = first(nodes, "table cell", prefix="Row 3, column 2:")
        budget_tab = first(nodes, "page tab", prefix="Budget")
        tab_list = budget_tab.get_parent() if budget_tab is not None else None
        while tab_list is not None and role(tab_list) != "page tab list":
            tab_list = tab_list.get_parent()
        result.update({
            "application": name(app), "roleCounts": counts,
            "sheetPane": describe(pane), "table": describe(first(nodes, "table")),
            "formulaBar": describe(first(nodes, "tool bar", contains="ormula")),
            "formulaResult": describe(first(nodes, "label", prefix="Result of")),
            "tabList": describe(tab_list), "budgetTab": describe(budget_tab),
            "summaryTab": describe(first(nodes, "page tab", prefix="Summary")),
            "fillHandle": describe(first(nodes, None, prefix="Fill handle")),
            "activeCell": describe(first(nodes, "table cell", prefix="Row 2, column 6:")),
            "cellB3": describe(cell),
        })
        # An assistive Press on B3 makes it current, as a click does.
        before = description(pane)
        press(cell)
        after = wait_for(lambda: (lambda d: d if d and d != before else None)(description(pane)), 10, 0.2)
        nodes = list(walk(app))
        result["pressCellB3"] = {"sheetBefore": before, "sheetAfter": after,
                                 "formulaResult": name(first(nodes, "label", prefix="Result of") or app),
                                 "cellB3": describe(first(nodes, "table cell", prefix="Row 3, column 2:"))}
        # An assistive Press on the Summary tab switches sheets.
        press(first(nodes, "page tab", prefix="Summary"))
        switched = wait_for(lambda: first(list(walk(app)), "panel", contains="sheet Summary"), 10, 0.3)
        result["pressSummaryTab"] = {"sheetPane": describe(switched),
                                     "summaryTab": describe(first(list(walk(app)), "page tab", prefix="Summary"))}
        result["ok"] = bool(pane and cell and after and switched)
    except Exception as error:  # report, never hang
        result["error"] = repr(error)
    finally:
        for process in (app_proc, weston):
            if process is not None:
                process.terminate()
                try:
                    process.wait(10)
                except subprocess.TimeoutExpired:
                    process.kill()
        shutil.rmtree(runtime, ignore_errors=True)
    text = json.dumps(result, indent=2, ensure_ascii=False)
    if args.out:
        with open(args.out, "w") as out:
            out.write(text + "\n")
    print(text)
    return 0 if result["ok"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preview", required=True, help="the qtk-preview binary")
    parser.add_argument("--qml", required=True, help="examples/workbook/Main.qml")
    parser.add_argument("--out", help="also write the JSON result here")
    args = parser.parse_args()
    args.preview, args.qml = os.path.abspath(args.preview), os.path.abspath(args.qml)
    if args.out:
        args.out = os.path.abspath(args.out)
    if os.environ.get(INNER) != "1":
        return reexec_on_private_bus(sys.argv[1:])
    return probe(args)


if __name__ == "__main__":
    sys.exit(main())
