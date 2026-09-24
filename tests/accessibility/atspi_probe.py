#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-or-later
"""Print the desktop AT-SPI tree entries matching QindaTK fixture nodes."""
import sys
import time

import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi


def walk(node, found, depth=0):
    if node is None or depth > 30:
        return
    try:
        name = node.get_name() or ""
        role = node.get_role_name() or ""
        description = node.get_description() or ""
        value = ""
        try:
            iface = node.get_value_iface()
            if iface:
                value = str(iface.get_current_value())
        except Exception:
            pass
        if any(key in (name + " " + description) for key in
               ("Spreadsheet grid", "Canvas objects", "Timeline clips", "A1: 12",
                "B1: 17", "Blue circle", "Intro", "Verse", "Apply name")):
            actions = []
            action_iface = None
            try:
                action_iface = node.get_action_iface()
                if action_iface:
                    actions = [action_iface.get_action_name(i) for i in range(action_iface.get_n_actions())]
            except Exception:
                pass
            found.append((node, name, role, value, description, actions, action_iface))
        count = node.get_child_count()
        for i in range(count):
            walk(node.get_child_at_index(i), found, depth + 1)
    except Exception:
        return


def main():
    invoke_press = "--invoke-press" in sys.argv[1:]
    invoke_workbench_rename = "--invoke-workbench-rename" in sys.argv[1:]
    Atspi.init()
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        desktop = Atspi.get_desktop(0)
        found = []
        walk(desktop, found)
        if found:
            target = next((entry for entry in found if entry[1] == "Intro"), None)
            workbench_target = next((entry for entry in found
                                     if entry[1] == "Apply name" and entry[2].lower() == "button"), None)
            invoked = None
            if invoke_press and target:
                _, _, _, _, _, actions, action_iface = target
                try:
                    index = next(i for i, action in enumerate(actions) if action.lower() == "press")
                    invoked = bool(action_iface.do_action(index))
                    time.sleep(0.25)
                except Exception as error:
                    invoked = f"error: {error}"
            workbench_invoked = None
            if invoke_workbench_rename and workbench_target:
                _, _, _, _, _, actions, action_iface = workbench_target
                try:
                    index = next(i for i, action in enumerate(actions) if action.lower() == "press")
                    workbench_invoked = bool(action_iface.do_action(index))
                    time.sleep(0.25)
                except Exception as error:
                    workbench_invoked = f"error: {error}"
            for node, name, role, value, description, actions, _ in found:
                if invoke_press and name == "Intro":
                    try:
                        value_iface = node.get_value_iface()
                        value = str(value_iface.get_current_value()) if value_iface else value
                    except Exception:
                        pass
                print(f"{role}\t{name}\tvalue={value}\tdescription={description}\tactions={actions}")
            if invoke_press:
                print(f"press_invoked={invoked}")
            if invoke_workbench_rename:
                print(f"workbench_rename_press_invoked={workbench_invoked}")
                if workbench_invoked is not True:
                    return 1
            return
        time.sleep(0.25)
    found = []
    walk(Atspi.get_desktop(0), found)
    if not found:
        print("No matching fixture nodes found through AT-SPI.")
        return 1 if invoke_workbench_rename else 0


if __name__ == "__main__":
    raise SystemExit(main())
