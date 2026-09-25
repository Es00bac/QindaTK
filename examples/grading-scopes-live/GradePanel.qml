// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The grade controls of examples/grading-scopes-live. The wheels and the
// exposure field edit the program grade through ColorGradeEditSession's
// wheel API (previewWheel/finishWheel/setExposure), so a drag and a typed
// value are the same one undo step; VideoFinishing hands every preview to the
// player, which re-renders the presented frame.
Tk.Panel {
    id: panel
    objectName: "gradingGradePanel"
    title: qsTr("Program grade")
    iconName: "palette"

    required property Tk.ColorGradeEditSession session
    property string statusText: qsTr("Drag a wheel; each drag is one undo step")

    function exposureText(ev) {
        return (ev >= 0 ? "+" : "") + Number(ev).toFixed(2) + " EV"
    }

    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.md
        Tk.ColorWheels {
            id: wheels
            objectName: "gradingWheels"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            height: parent.height
            gradeId: panel.session.clipId
            wheels: panel.session.wheels
            selectedWheelId: "gain"
            tooltip: qsTr("Lift, gamma and gain: opponent chroma offsets in linear sRGB")
            statusText: panel.session.errorMessage || panel.statusText
            onWheelSelectionRequested: function(gradeId, wheelId) { wheels.selectedWheelId = wheelId }
            onWheelPreviewRequested: function(gesture, gradeId, wheel, x, y) {
                if (panel.session.previewWheel(gesture, wheel, x, y))
                    panel.statusText = qsTr("Previewing %1 on the monitor").arg(wheel)
            }
            onWheelEditFinished: function(gesture, gradeId, wheel, x, y) {
                if (panel.session.finishWheel(gesture, wheel, x, y))
                    panel.statusText = qsTr("%1 committed · one undo step").arg(wheel)
            }
            onWheelEditCanceled: function(gesture) {
                if (panel.session.editing) panel.session.cancelGesture(gesture)
                panel.statusText = qsTr("Drag canceled · committed grade restored")
            }
            onResetRequested: function(gradeId, wheel) {
                if (panel.session.resetWheel(wheel))
                    panel.statusText = qsTr("%1 reset · one undo step").arg(wheel)
            }
        }
        Tk.Flex {
            objectName: "gradingGradeReadout"
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Flex.shrink: 0
            implicitWidth: Tk.Theme.size.panelMinWidth
            Tk.Overline { title: qsTr("Exposure") }
            Tk.NumberField {
                objectName: "gradingExposure"
                width: parent.width
                value: panel.session.exposure
                from: -8; to: 8; decimals: 2; stepSize: 0.05; suffix: " EV"
                tooltip: qsTr("Exposure in stops; one undo step per edit")
                onValueModified: function(value) {
                    if (panel.session.setExposure(value))
                        panel.statusText = qsTr("Exposure %1 · one undo step").arg(panel.exposureText(value))
                }
            }
            Tk.Overline { title: qsTr("Wheels") }
            Repeater {
                model: panel.session.wheels
                delegate: Tk.KeyValue {
                    required property var modelData
                    width: parent.width
                    key: modelData.label
                    value: Number(modelData.x).toFixed(2) + ", " + Number(modelData.y).toFixed(2)
                }
            }
            Tk.Overline { title: qsTr("History") }
            Tk.KeyValue {
                width: parent.width
                key: qsTr("Undo")
                value: panel.session.canUndo ? qsTr("available") : qsTr("none")
            }
            Tk.KeyValue {
                width: parent.width
                key: qsTr("Gesture")
                value: panel.session.editing ? qsTr("open") : qsTr("idle")
                accent: panel.session.editing
            }
        }
    }
}
