// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "PillSwitcher"
    when: windowShown
    visible: true
    width: 480
    height: 180

    Component {
        id: switcherComponent
        Tk.PillSwitcher {
            model: [
                { "text": "Flow", "value": "flow", "iconName": "workflow",
                  "tooltip": "Flow workspace", "accent": Tk.Theme.color.accent,
                  "objectName": "flowWorkspacePill" },
                { "text": "Video", "value": "editor", "iconName": "film",
                  "tooltip": "Video workspace", "accent": Tk.Theme.color.info },
                { "text": "Paper", "value": "paper", "iconName": "file-text",
                  "tooltip": "Paper workspace", "accent": Tk.Theme.color.warning }
            ]
            showLabels: false
        }
    }

    SignalSpy {
        id: activationSpy
        signalName: "activated"
    }

    function test_model_geometry_and_values() {
        const control = createTemporaryObject(switcherComponent, root)
        verify(control !== null)
        compare(control.count, 3)
        compare(control.currentValue, "flow")
        compare(control.implicitHeight, Tk.Theme.size.control + Tk.Theme.space.xs * 2)
        waitForRendering(control)
        verify(control.itemAt(0) !== null)
        verify(findChild(control, "flowWorkspacePill") !== null)
        verify(findChild(control, "pillSwitcherItem_2") !== null)
    }

    function test_user_activation_is_distinct_from_programmatic_change() {
        const control = createTemporaryObject(switcherComponent, root, { "y": 60 })
        activationSpy.target = control
        activationSpy.clear()
        control.currentIndex = 1
        compare(activationSpy.count, 0)
        const third = findChild(control, "pillSwitcherItem_2")
        waitForRendering(control)
        mouseClick(third)
        compare(control.currentIndex, 2)
        compare(control.currentValue, "paper")
        compare(activationSpy.count, 1)
    }
}
