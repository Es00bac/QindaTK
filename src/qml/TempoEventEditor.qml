// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Numeric entry for one tempo or meter event of a TempoLane.
//
// AGENT-CONTRACT: the position field shows and parses bar.beat.tick through
// TempoMapModel, the tempo field BPM text (stored as exact microseconds per
// quarter), the meter fields integers. An untouched field keeps its exact
// value. Apply submits one `edit` transaction guarded by the revision the
// editor opened at; a parse error or rejection keeps the popover open with
// the reason and changes nothing. Escape or Cancel closes it.
Tk.Popover {
    id: editor

    property Tk.TempoMapModel tempoModel: null
    property string eventId: ""
    property string kind: "tempo"
    property string errorText: ""

    signal edited(var result)

    QtObject {
        id: exact
        property var opened: ({})
        property string revision: ""
    }

    function openEvent(id) {
        if (!editor.tempoModel) return false
        const data = editor.tempoModel.eventData(id)
        if (!data.id) return false
        editor.eventId = id
        editor.kind = data.kind
        exact.opened = data
        exact.revision = editor.tempoModel.revision
        positionField.text = data.position
        bpmField.text = data.bpmText
        numeratorField.text = String(data.numerator)
        denominatorField.text = String(data.denominator)
        editor.errorText = ""
        editor.open()
        const first = data.initial ? (data.kind === "tempo" ? bpmField : numeratorField) : positionField
        first.forceActiveFocus()
        first.selectAll()
        return true
    }
    function apply() {
        if (!editor.tempoModel || editor.eventId.length === 0) return false
        const input = {}
        if (positionField.text !== exact.opened.position) {
            const position = editor.tempoModel.parsePosition(positionField.text)
            if (!position.ok) { editor.errorText = position.error; return false }
            input.tick = position.tick
        }
        if (editor.kind === "tempo" && bpmField.text !== exact.opened.bpmText) {
            const tempo = editor.tempoModel.parseBpm(bpmField.text)
            if (!tempo.ok) { editor.errorText = tempo.error; return false }
            input.microsecondsPerQuarter = tempo.microsecondsPerQuarter
        }
        if (editor.kind === "meter") {
            const numerator = parseInt(numeratorField.text, 10)
            const denominator = parseInt(denominatorField.text, 10)
            if (!(numerator > 0) || !(denominator > 0)) { editor.errorText = qsTr("Enter the meter as whole numbers"); return false }
            input.numerator = numerator
            input.denominator = denominator
        }
        const result = editor.tempoModel.applyEdit({ kind: "edit", eventId: editor.eventId,
                                                     expectedRevision: exact.revision }, input)
        editor.edited(result)
        if (!result.ok) {
            editor.errorText = result.error
            return false
        }
        editor.close()
        return true
    }

    objectName: "tempoEventEditor"
    placement: "bottom"
    onTempoModelChanged: editor.close()

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Accessible.role: Accessible.Dialog
        Accessible.name: editor.kind === "tempo" ? qsTr("Tempo change") : qsTr("Meter change")

        Tk.Overline { text: editor.kind === "tempo" ? qsTr("Tempo change") : qsTr("Meter change") }
        Tk.Grid {
            columns: "auto " + Tk.Theme.size.fieldWidth + "px"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.xs
            alignItems: Tk.Grid.Center

            Tk.Caption { text: qsTr("Position") }
            Tk.TextField {
                id: positionField
                objectName: "tempoPositionField"
                mono: true
                small: true
                enabled: !exact.opened.initial
                tooltip: exact.opened.initial ? qsTr("The first event stays at bar 1") : qsTr("Bar.beat.tick")
                error: editor.errorText.length > 0
                onAccepted: editor.apply()
            }
            Tk.Caption { visible: editor.kind === "tempo"; text: qsTr("Tempo (BPM)") }
            Tk.TextField {
                id: bpmField
                objectName: "tempoBpmField"
                visible: editor.kind === "tempo"
                mono: true
                small: true
                tooltip: qsTr("Beats per minute, stored as exact microseconds per quarter note")
                onAccepted: editor.apply()
            }
            Tk.Caption { visible: editor.kind === "meter"; text: qsTr("Beats per bar") }
            Tk.TextField {
                id: numeratorField
                objectName: "tempoNumeratorField"
                visible: editor.kind === "meter"
                mono: true
                small: true
                tooltip: qsTr("Meter numerator")
                onAccepted: editor.apply()
            }
            Tk.Caption { visible: editor.kind === "meter"; text: qsTr("Beat value") }
            Tk.TextField {
                id: denominatorField
                objectName: "tempoDenominatorField"
                visible: editor.kind === "meter"
                mono: true
                small: true
                tooltip: qsTr("Meter denominator: 1, 2, 4, 8, 16, 32 or 64")
                onAccepted: editor.apply()
            }
        }
        Tk.Caption {
            objectName: "tempoEditorError"
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
                objectName: "tempoEditorCancel"
                text: qsTr("Cancel")
                small: true
                onClicked: editor.close()
            }
            Tk.Button {
                objectName: "tempoEditorApply"
                text: qsTr("Apply")
                variant: "accent"
                small: true
                onClicked: editor.apply()
            }
        }
    }
}
