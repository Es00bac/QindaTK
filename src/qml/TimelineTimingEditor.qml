// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Numeric start, length and source offset entry for one timeline clip.
//
// AGENT-CONTRACT: fields show and parse text in the owner's display mode
// through TimelineModel (formatTicks/parseTicks for the start,
// formatDuration/parseDuration for lengths), so no value passes through a
// QML Number. Apply submits one SetClipTiming edit; an unparsable field or a
// policy rejection keeps the popover open with the reason and changes
// nothing. Escape or Cancel closes it without an edit.
Tk.Popover {
    id: editor

    property Tk.TimelineModel model: null
    property string displayMode: "timecode"
    property bool dropFrame: false
    property bool rippleMode: false
    property string clipId: ""
    property string errorText: ""

    signal edited(var result)

    function openClip(id) {
        if (!editor.model || !editor.model.hasClip(id)) return false
        const data = editor.model.clipData(id)
        editor.clipId = id
        startField.text = editor.model.formatTicks(data.start, editor.displayMode, editor.dropFrame)
        durationField.text = editor.model.formatDuration(data.duration, editor.displayMode)
        offsetField.text = editor.model.formatDuration(data.sourceOffset, editor.displayMode)
        editor.errorText = ""
        editor.open()
        startField.forceActiveFocus()
        startField.selectAll()
        return true
    }
    function apply() {
        if (!editor.model || editor.clipId.length === 0) return false
        const start = editor.model.parseTicks(startField.text, editor.displayMode, editor.dropFrame)
        const duration = editor.model.parseDuration(durationField.text, editor.displayMode)
        const offset = editor.model.parseDuration(offsetField.text, editor.displayMode)
        const invalid = [start, duration, offset].filter(function(result) { return !result.ok })
        if (invalid.length > 0) {
            editor.errorText = invalid[0].error
            return false
        }
        const result = editor.model.applyEdit({ kind: "setClipTiming", clipIds: [editor.clipId],
                                                start: start.value, duration: duration.value,
                                                sourceOffset: offset.value, ripple: editor.rippleMode })
        editor.edited(result)
        if (!result.ok) {
            editor.errorText = result.error
            return false
        }
        editor.close()
        return true
    }

    objectName: "clipTimingEditor"
    placement: "bottom"

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Clip timing")

        Tk.Overline {
            text: qsTr("Clip timing")
        }
        Tk.Grid {
            columns: "auto " + Tk.Theme.size.fieldWidth + "px"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.xs
            alignItems: Tk.Grid.Center

            Tk.Caption { text: qsTr("Start") }
            Tk.TextField {
                id: startField
                objectName: "clipTimingStart"
                mono: true
                small: true
                tooltip: qsTr("Clip start")
                error: editor.errorText.length > 0
                onAccepted: editor.apply()
            }
            Tk.Caption { text: qsTr("Length") }
            Tk.TextField {
                id: durationField
                objectName: "clipTimingDuration"
                mono: true
                small: true
                tooltip: qsTr("Clip length")
                onAccepted: editor.apply()
            }
            Tk.Caption { text: qsTr("Source offset") }
            Tk.TextField {
                id: offsetField
                objectName: "clipTimingOffset"
                mono: true
                small: true
                tooltip: qsTr("Source offset")
                onAccepted: editor.apply()
            }
        }
        Tk.Caption {
            objectName: "clipTimingError"
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
                objectName: "clipTimingCancel"
                text: qsTr("Cancel")
                small: true
                onClicked: editor.close()
            }
            Tk.Button {
                objectName: "clipTimingApply"
                text: qsTr("Apply")
                variant: "accent"
                small: true
                onClicked: editor.apply()
            }
        }
    }
}
