// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "CanvasBrushControls"
    when: windowShown
    visible: true
    width: 720
    height: 640
    SignalSpy { id: spy }

    function watch(target, signalName) {
        spy.target = null
        spy.signalName = signalName
        spy.clear()
        spy.target = target
    }

    Component {
        id: maskComponent
        Tk.MaskChannelPanel {
            width: 500
            documentId: "doc-1"
            channels: [{ id: "layer-1", name: "Layer", kind: "layer", enabled: true, visible: true },
                       { id: "mask-1", name: "Mask", kind: "mask", enabled: true, visible: true }]
            activeTargetId: "layer-1"
        }
    }
    Component {
        id: presetComponent
        Tk.BrushPresetView {
            width: 580
            presets: [{ id: "soft", name: "Soft", tags: ["paint"] },
                      { id: "ink", name: "Ink", tags: ["line"] }]
        }
    }
    Component {
        id: dynamicsComponent
        Tk.BrushDynamicsEditor {
            width: 640
            brushId: "brush-1"
            parameters: [{ id: "size", label: "Size", value: 0.5, minimum: 0, maximum: 1, step: 0.1 }]
        }
    }

    function test_mask_target_and_channel_are_requests() {
        const panel = createTemporaryObject(maskComponent, root)
        verify(panel !== null)
        waitForRendering(panel)
        watch(panel, "targetRequested")
        mouseClick(findChild(panel, "maskTarget_mask-1"))
        compare(spy.count, 1)
        compare(spy.signalArguments[0][2], "mask-1")
        compare(panel.activeTargetId, "layer-1") // Host snapshot is unchanged.
        watch(panel, "channelStateRequested")
        mouseClick(findChild(panel, "maskEnabled_mask-1"))
        compare(spy.count, 1)
        compare(spy.signalArguments[0][2], "mask-1")
        compare(spy.signalArguments[0][3], "enabled")
        compare(spy.signalArguments[0][4], false)
        watch(panel, "relationshipChangeRequested")
        verify(panel.requestRelationship("mask-1", "clipping", "layer-1"))
        compare(spy.count, 1)
        compare(spy.signalArguments[0][3], "clipping")
        compare(spy.signalArguments[0][4], "layer-1")
        spy.target = null
    }

    function test_preset_selection_request_has_stable_id() {
        const view = createTemporaryObject(presetComponent, root)
        verify(view !== null)
        waitForRendering(view)
        watch(view, "presetRequested")
        mouseClick(findChild(view, "brushPresetSelect_ink"))
        compare(spy.count, 1)
        compare(spy.signalArguments[0][1], "ink")
        compare(spy.signalArguments[0][2], "select")
        compare(view.selectedPresetId, "")
        spy.target = null
    }

    function test_dynamics_preview_commit_and_cancel_are_bounded() {
        const editor = createTemporaryObject(dynamicsComponent, root)
        verify(editor !== null)
        watch(editor, "dynamicsEditRequested")
        verify(editor.beginEdit())
        verify(editor.setParameterValue("size", 2))
        compare(editor.workingParameters[0].value, 1)
        verify(editor.previewEdit())
        verify(editor.commitEdit())
        verify(editor.beginEdit())
        verify(editor.setParameterValue("size", 0.2))
        verify(editor.cancelEdit())
        compare(editor.workingParameters[0].value, 0.5)
        compare(spy.count, 5)
        compare(spy.signalArguments[1][2], "preview")
        compare(spy.signalArguments[2][2], "commit")
        compare(spy.signalArguments[4][2], "cancel")
        spy.target = null
    }
}
