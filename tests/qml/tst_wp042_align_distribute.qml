// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "WP042AlignDistributePanel"
    when: windowShown
    visible: true
    width: 360
    height: 420

    Component {
        id: panelScene
        Item {
            width: 320
            height: 400
            property alias panel: panelItem
            property alias operationSpy: requestSpy

            Tk.AlignDistributePanel {
                id: panelItem
                width: 280
                selectedIds: ["canvas:9007199254740993", "ellipse:2"]
                referenceMode: "keyObject"
                keyObjectId: "ellipse:2"
                spacing: 12.5
            }

            SignalSpy {
                id: requestSpy
                target: panelItem
                signalName: "operationRequested"
            }
        }
    }

    Component {
        id: emptyPanelScene
        Tk.AlignDistributePanel {
            width: 280
            selectedIds: []
        }
    }

    function test_alignment_emits_stable_ids_and_reference() {
        const scene = createTemporaryObject(panelScene, this)
        verify(scene !== null)
        waitForRendering(scene)

        const panel = scene.panel
        const picker = findChild(panel, "alignReferenceMode")
        verify(picker !== null)
        compare(picker.model.length, 3)
        compare(picker.currentIndex, 1)
        compare(picker.model[0].value, "selection")
        compare(picker.model[1].value, "keyObject")
        compare(picker.model[2].value, "page")

        const button = findChild(panel, "alignLeftButton")
        verify(button !== null)
        verify(button.enabled)
        button.click()

        const spy = scene.operationSpy
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "align.left")
        compare(spy.signalArguments[0][1], ["canvas:9007199254740993", "ellipse:2"])
        compare(spy.signalArguments[0][2], "keyObject")
        compare(spy.signalArguments[0][3], "ellipse:2")
        fuzzyCompare(spy.signalArguments[0][4], 12.5, 0.01)
    }

    function test_distribution_uses_current_gap_and_empty_selection_is_disabled() {
        const scene = createTemporaryObject(panelScene, this)
        verify(scene !== null)
        const panel = scene.panel
        panel.spacing = 18.5
        compare(findChild(panel, "distributeSpacing").value, 18.5)
        findChild(panel, "distributeHorizontalButton").click()

        const spy = scene.operationSpy
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "distribute.horizontal")
        fuzzyCompare(spy.signalArguments[0][4], 18.5, 0.01)

        const empty = createTemporaryObject(emptyPanelScene, this)
        verify(empty !== null)
        const action = findChild(empty, "distributeVerticalButton")
        verify(action !== null)
        verify(!action.enabled)

        empty.selectedIds = ["canvas:9007199254740993"]
        empty.referenceMode = "page"
        verify(action.enabled)
    }
}
