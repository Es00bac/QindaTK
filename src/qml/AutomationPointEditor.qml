// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Numeric entry for one automation point: time, value and the interpolation
// of its outgoing segment.
//
// AGENT-CONTRACT: time is shown and parsed in the owner's display mode by the
// TimelineModel (formatTicks/parseTicks); value in the parameter's canonical
// unit by the AutomationModel (formatValue/parseValue). An untouched field
// keeps its exact stored value. Apply submits one `setPoint` transaction
// guarded by the revision the editor opened at; an error keeps the popover
// open with the reason and changes nothing.
Tk.Popover {
    id: editor

    property Tk.AutomationModel model: null
    property Tk.TimelineModel timeline: null
    property string parameterId: ""
    property string pointId: ""
    property string displayMode: "timecode"
    property bool dropFrame: false
    property string errorText: ""
    readonly property var interpolations: ["step", "linear", "cubic"]

    signal edited(var result)

    QtObject {
        id: exact
        property var opened: ({})
        property string timeText: ""
        property string valueText: ""
        property string revision: ""
    }

    function openPoint(parameterId, pointId) {
        if (!editor.model || !editor.timeline) return false
        const data = editor.model.pointData(parameterId, pointId)
        if (!data.id) return false
        editor.parameterId = parameterId
        editor.pointId = pointId
        exact.opened = data
        exact.revision = editor.model.revision
        exact.timeText = editor.timeline.formatTicks(data.ticks, editor.displayMode, editor.dropFrame)
        exact.valueText = editor.model.formatValue(parameterId, data.value)
        timeField.text = exact.timeText
        valueField.text = exact.valueText
        interpolation.currentIndex = Math.max(0, editor.interpolations.indexOf(data.interpolation))
        editor.errorText = ""
        editor.open()
        valueField.forceActiveFocus()
        valueField.selectAll()
        return true
    }
    function apply() {
        if (!editor.model || editor.pointId.length === 0) return false
        const input = { interpolation: editor.interpolations[interpolation.currentIndex] }
        if (timeField.text !== exact.timeText) {
            const time = editor.timeline.parseTicks(timeField.text, editor.displayMode, editor.dropFrame)
            if (!time.ok) { editor.errorText = time.error; return false }
            input.ticks = time.ticks !== undefined ? time.ticks : time.value
        }
        if (valueField.text !== exact.valueText) {
            const value = editor.model.parseValue(editor.parameterId, valueField.text)
            if (!value.ok) { editor.errorText = value.error; return false }
            input.value = value.value
        }
        const result = editor.model.applyEdit({ kind: "setPoint", parameterId: editor.parameterId,
                                                pointId: editor.pointId, expectedRevision: exact.revision }, input)
        editor.edited(result)
        if (!result.ok) {
            editor.errorText = result.error
            return false
        }
        editor.close()
        return true
    }

    objectName: "automationPointEditor"
    placement: "bottom"
    onModelChanged: editor.close()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Automation point")

        Tk.Overline { text: qsTr("Automation point") }
        Tk.Grid {
            columns: "auto " + Tk.Theme.size.fieldWidth + "px"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.xs
            alignItems: Tk.Grid.Center

            Tk.Caption { text: qsTr("Time") }
            Tk.TextField {
                id: timeField
                objectName: "automationTimeField"
                mono: true
                small: true
                tooltip: qsTr("Point time")
                error: editor.errorText.length > 0
                onAccepted: editor.apply()
            }
            Tk.Caption { text: qsTr("Value") }
            Tk.TextField {
                id: valueField
                objectName: "automationValueField"
                mono: true
                small: true
                tooltip: qsTr("Value in the parameter's unit")
                onAccepted: editor.apply()
            }
            Tk.Caption { text: qsTr("Curve") }
            Tk.Segmented {
                id: interpolation
                objectName: "automationInterpolation"
                small: true
                model: [{ text: qsTr("Step"), value: "step", tooltip: qsTr("Hold until the next point") },
                        { text: qsTr("Linear"), value: "linear", tooltip: qsTr("Straight ramp to the next point") },
                        { text: qsTr("Curve"), value: "cubic", tooltip: qsTr("Cubic curve shaped by its handles") }]
                tooltip: qsTr("Interpolation to the next point")
            }
        }
        Tk.Caption {
            objectName: "automationEditorError"
            visible: editor.errorText.length > 0
            text: editor.errorText
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
            Tk.Flex.maxWidth: Tk.Theme.size.dialogWidth
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            justify: Tk.Flex.End
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "automationEditorCancel"
                text: qsTr("Cancel")
                small: true
                onClicked: editor.close()
            }
            Tk.Button {
                objectName: "automationEditorApply"
                text: qsTr("Apply")
                variant: "accent"
                small: true
                onClicked: editor.apply()
            }
        }
    }
}
