// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Numeric entry for one clip (start, length, source offset) or one range
// (start, end) of an EditTimeline.
//
// AGENT-CONTRACT: fields show and parse text in the owner's display mode
// through TimelineModel (formatTicks/parseTicks for positions,
// formatDuration/parseDuration for lengths), so no value passes through a
// QML Number. A field the user did not change keeps its exact value rather
// than the display-quantized text (a timecode shows whole frames only). In
// clip mode Apply submits one SetClipTiming edit; in range
// mode it emits rangeEdited for the owner's TimeSelection to commit. An
// unparsable field or a rejection keeps the popover open with the reason and
// changes nothing. Escape or Cancel closes it without an edit.
Tk.Popover {
    id: editor

    property Tk.TimelineModel model: null
    property string displayMode: "timecode"
    property bool dropFrame: false
    property bool rippleMode: false
    property string clipId: ""
    property string mode: "clip" // clip or range
    property string errorText: ""

    signal edited(var result)
    signal rangeEdited(string startTicks, string endTicks)

    QtObject {
        id: exact
        property var values: ({})  // field objectName -> exact decimal ticks
        property var texts: ({})   // field objectName -> text shown at open
    }
    function show(field, text, ticks) {
        field.text = text
        exact.values[field.objectName] = ticks
        exact.texts[field.objectName] = text
    }
    // Unchanged fields return their exact ticks; edited ones are parsed.
    function valueOf(field, parse) {
        if (field.text === exact.texts[field.objectName]) return { ok: true, value: exact.values[field.objectName] }
        return parse(field.text)
    }

    function openRange(start, end) {
        if (!editor.model || start.length === 0 || end.length === 0) return false
        editor.mode = "range"
        editor.clipId = ""
        exact.values = ({})
        exact.texts = ({})
        editor.show(startField, editor.model.formatTicks(start, editor.displayMode, editor.dropFrame), start)
        editor.show(durationField, editor.model.formatTicks(end, editor.displayMode, editor.dropFrame), end)
        editor.errorText = ""
        editor.open()
        startField.forceActiveFocus()
        startField.selectAll()
        return true
    }

    function openClip(id) {
        if (!editor.model || !editor.model.hasClip(id)) return false
        const data = editor.model.clipData(id)
        editor.mode = "clip"
        editor.clipId = id
        exact.values = ({})
        exact.texts = ({})
        editor.show(startField, editor.model.formatTicks(data.start, editor.displayMode, editor.dropFrame), data.start)
        editor.show(durationField, editor.model.formatDuration(data.duration, editor.displayMode), data.duration)
        editor.show(offsetField, editor.model.formatDuration(data.sourceOffset, editor.displayMode), data.sourceOffset)
        editor.errorText = ""
        editor.open()
        startField.forceActiveFocus()
        startField.selectAll()
        return true
    }
    function apply() {
        if (!editor.model) return false
        if (editor.mode === "range") {
            const position = function(text) { return editor.model.parseTicks(text, editor.displayMode, editor.dropFrame) }
            const from = editor.valueOf(startField, position)
            const to = editor.valueOf(durationField, position)
            if (!from.ok || !to.ok) {
                editor.errorText = from.ok ? to.error : from.error
                return false
            }
            editor.rangeEdited(from.value, to.value)
            editor.close()
            return true
        }
        if (editor.clipId.length === 0) return false
        const position = function(text) { return editor.model.parseTicks(text, editor.displayMode, editor.dropFrame) }
        const length = function(text) { return editor.model.parseDuration(text, editor.displayMode) }
        const start = editor.valueOf(startField, position)
        const duration = editor.valueOf(durationField, length)
        const offset = editor.valueOf(offsetField, length)
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

    objectName: "timelineTimingEditor"
    placement: "bottom"

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Accessible.role: Accessible.Dialog
        Accessible.name: editor.mode === "range" ? qsTr("Range timing") : qsTr("Clip timing")

        Tk.Overline {
            text: editor.mode === "range" ? qsTr("Range timing") : qsTr("Clip timing")
        }
        Tk.Grid {
            columns: "auto " + Tk.Theme.size.fieldWidth + "px"
            columnGap: Tk.Theme.space.sm
            rowGap: Tk.Theme.space.xs
            alignItems: Tk.Grid.Center

            Tk.Caption { text: qsTr("Start") }
            Tk.TextField {
                id: startField
                objectName: "timingStartField"
                mono: true
                small: true
                tooltip: editor.mode === "range" ? qsTr("Range start") : qsTr("Clip start")
                error: editor.errorText.length > 0
                onAccepted: editor.apply()
            }
            Tk.Caption { text: editor.mode === "range" ? qsTr("End") : qsTr("Length") }
            Tk.TextField {
                id: durationField
                objectName: "timingLengthField"
                mono: true
                small: true
                tooltip: editor.mode === "range" ? qsTr("Range end") : qsTr("Clip length")
                onAccepted: editor.apply()
            }
            Tk.Caption {
                visible: editor.mode === "clip"
                text: qsTr("Source offset")
            }
            Tk.TextField {
                id: offsetField
                objectName: "timingOffsetField"
                visible: editor.mode === "clip"
                mono: true
                small: true
                tooltip: qsTr("Source offset")
                onAccepted: editor.apply()
            }
        }
        Tk.Caption {
            objectName: "timingError"
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
                objectName: "timingCancel"
                text: qsTr("Cancel")
                small: true
                onClicked: editor.close()
            }
            Tk.Button {
                objectName: "timingApply"
                text: qsTr("Apply")
                variant: "accent"
                small: true
                onClicked: editor.apply()
            }
        }
    }
}
