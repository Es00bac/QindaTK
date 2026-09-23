// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Captures portable shortcut sequences into a local edit set. The map
// validates the complete proposed override set before persistence/activation.
Tk.Box {
    id: control
    objectName: "shortcutEditor"
    property var commandRegistry: null
    property var shortcutMap: null
    property var settingsModel: null
    property string settingsKey: "app:shortcuts.overrides"
    property string searchText: ""
    property string recordingCommandId: ""
    property string errorText: ""
    property string capturePreview: ""
    property var captureStrokes: []
    property var pendingOverrides: ({})
    property var validation: ({ ok: false, conflicts: [], error: qsTr("Shortcut service unavailable") })
    readonly property var filteredCommands: buildCommands()
    readonly property bool dirty: JSON.stringify(pendingOverrides) !== JSON.stringify(shortcutMap ? shortcutMap.overrides() : ({}))

    signal applied()
    signal applyRejected(string reason)
    signal captureCanceled(string commandId)
    signal captureFinished(string commandId)
    signal focusReturnRequested(string commandId)

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Keyboard shortcut editor")

    function buildCommands() {
        if (!commandRegistry) return []
        const generation = commandRegistry.generation
        const all = commandRegistry.paletteCommands()
        const query = searchText.trim().toLocaleLowerCase()
        const rows = []
        for (let i = 0; i < all.length; ++i) {
            const command = all[i]
            if (!command.id) continue
            const fields = [command.label, command.section, command.id, command.keywords].join(" ").toLocaleLowerCase()
            if (query.length > 0 && fields.indexOf(query) < 0) continue
            const override = pendingOverrides[command.id]
            const defaultSequence = command.shortcut || ""
            const sequence = override === undefined ? defaultSequence : override
            rows.push({ id: command.id, label: command.label, section: command.section,
                        defaultSequence: defaultSequence,
                        sequence: sequence,
                        displaySequence: sequence,
                        reason: command.reason || "" })
        }
        return rows
    }
    function beginEdit() {
        errorText = ""
        pendingOverrides = shortcutMap ? Object.assign({}, shortcutMap.overrides()) : ({})
        validatePending()
    }
    function syncPersistedOverrides() {
        if (!settingsModel || !shortcutMap || settingsModel.staging || dirty) return false
        const encoded = settingsModel.valueForKey(settingsKey)
        if (typeof encoded !== "string" || encoded.length === 0) return false
        let parsed
        try { parsed = JSON.parse(encoded) }
        catch (error) {
            errorText = qsTr("Saved shortcuts are invalid JSON")
            return false
        }
        if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)
            || !shortcutMap.validateOverrides(parsed).ok) {
            errorText = qsTr("Saved shortcuts contain invalid or conflicting bindings")
            return false
        }
        if (!shortcutMap.loadOverrides(parsed)) return false
        beginEdit()
        return true
    }
    function validatePending() {
        validation = shortcutMap
            ? shortcutMap.validateOverrides(pendingOverrides)
            : ({ ok: false, conflicts: [], error: qsTr("Shortcut service unavailable") })
        return !!validation.ok
    }
    function setOverride(commandId, sequence) {
        const next = Object.assign({}, pendingOverrides)
        next[commandId] = sequence
        pendingOverrides = next
        errorText = ""
        return validatePending()
    }
    function resetCommand(commandId) {
        const next = Object.assign({}, pendingOverrides)
        delete next[commandId]
        pendingOverrides = next
        errorText = ""
        return validatePending()
    }
    function restoreDefaults() {
        pendingOverrides = ({})
        errorText = ""
        validatePending()
    }
    function finishCapture(commandId, sequence) {
        recordingCommandId = ""
        captureStrokes = []
        capturePreview = ""
        captureFinished(commandId)
        focusReturnRequested(commandId)
        if (sequence.length > 0) setOverride(commandId, sequence)
        restoreCaptureFocus(commandId)
    }
    function cancelCapture(returnFocus) {
        const commandId = recordingCommandId
        recordingCommandId = ""
        captureStrokes = []
        capturePreview = ""
        captureCanceled(commandId)
        if (returnFocus !== false) {
            focusReturnRequested(commandId)
            restoreCaptureFocus(commandId)
        }
    }
    function restoreCaptureFocus(commandId) {
        const index = filteredCommands.findIndex(function(row) { return row.id === commandId })
        Qt.callLater(function() {
            if (!control) return
            const row = index >= 0 ? shortcutList.itemAtIndex(index) : null
            if (row && row.recordButtonRef) row.recordButtonRef.forceActiveFocus(Qt.TabFocusReason)
            else shortcutSearch.forceActiveFocus(Qt.TabFocusReason)
        })
    }
    function captureKeyName(key) {
        if (key >= Qt.Key_A && key <= Qt.Key_Z) return String.fromCharCode(key)
        if (key >= Qt.Key_0 && key <= Qt.Key_9) return String.fromCharCode(key)
        const names = {}
        names[Qt.Key_Space] = "Space"
        names[Qt.Key_Return] = "Return"
        names[Qt.Key_Enter] = "Enter"
        names[Qt.Key_Backspace] = "Backspace"
        names[Qt.Key_Delete] = "Delete"
        names[Qt.Key_Insert] = "Insert"
        names[Qt.Key_Home] = "Home"
        names[Qt.Key_End] = "End"
        names[Qt.Key_PageUp] = "PageUp"
        names[Qt.Key_PageDown] = "PageDown"
        names[Qt.Key_Left] = "Left"
        names[Qt.Key_Right] = "Right"
        names[Qt.Key_Up] = "Up"
        names[Qt.Key_Down] = "Down"
        names[Qt.Key_Comma] = ","
        names[Qt.Key_Plus] = "+"
        names[Qt.Key_Minus] = "-"
        if (key >= Qt.Key_F1 && key <= Qt.Key_F35) return "F" + String(key - Qt.Key_F1 + 1)
        return names[key] || ""
    }
    function portableStroke(event) {
        const key = captureKeyName(event.key)
        if (!key) return ""
        const parts = []
        if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
        if (event.modifiers & Qt.AltModifier) parts.push("Alt")
        if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
        if (event.modifiers & Qt.MetaModifier) parts.push("Meta")
        parts.push(key)
        return parts.join("+")
    }
    function beginCapture(commandId) {
        recordingCommandId = commandId
        captureStrokes = []
        capturePreview = qsTr("Press a shortcut…")
        Qt.callLater(function() { if (captureFocus) captureFocus.forceActiveFocus(Qt.TabFocusReason) })
    }
    function handleCaptureKey(event) {
        const commandId = recordingCommandId
        if (!commandId) return
        if (event.key === Qt.Key_Escape) {
            cancelCapture()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
            cancelCapture(false)
            event.accepted = false
            return
        }
        const stroke = portableStroke(event)
        if (stroke.length === 0) return
        const strokes = captureStrokes.slice()
        strokes.push(stroke)
        captureStrokes = strokes
        capturePreview = captureStrokes.join(", ")
        event.accepted = true
        captureTimer.restart()
    }

    Item {
        id: captureFocus
        objectName: "shortcutCaptureKeyboard"
        width: Tk.Theme.size.border
        height: Tk.Theme.size.border
        visible: control.recordingCommandId.length > 0
        focus: visible
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: function(event) { control.handleCaptureKey(event) }
    }

    Timer {
        id: captureTimer
        interval: control.shortcutMap ? control.shortcutMap.chordTimeoutMs : Tk.Theme.motion.tooltipTimeout
        repeat: false
        running: control.recordingCommandId.length > 0 && control.captureStrokes.length > 0
        onTriggered: control.finishCapture(control.recordingCommandId, control.captureStrokes.join(", "))
    }
    function applyChanges() {
        if (!shortcutMap || !validatePending()) {
            errorText = validation.error || qsTr("Resolve shortcut conflicts before applying")
            applyRejected(errorText)
            return false
        }
        if (settingsModel) {
            const known = settingsModel.settings.some(function(row) { return row.scopedKey === settingsKey })
            if (!known) {
                errorText = qsTr("The shortcut settings key is not registered: %1").arg(settingsKey)
                applyRejected(errorText)
                return false
            }
            if (settingsModel.staging) {
                errorText = qsTr("Apply or cancel the open preferences edit before saving shortcuts")
                applyRejected(errorText)
                return false
            }
            if (!settingsModel.beginStage()) return false
            if (!settingsModel.setValue(settingsKey, JSON.stringify(pendingOverrides))) {
                settingsModel.cancel()
                errorText = settingsModel.error
                applyRejected(errorText)
                return false
            }
            if (!settingsModel.apply()) {
                settingsModel.cancel()
                errorText = settingsModel.error
                applyRejected(errorText)
                return false
            }
        }
        if (!shortcutMap.loadOverrides(pendingOverrides)) {
            errorText = qsTr("The shortcut map rejected the validated override set")
            applyRejected(errorText)
            return false
        }
        beginEdit()
        applied()
        return true
    }

    Component.onCompleted: {
        beginEdit()
        syncPersistedOverrides()
    }
    Connections {
        target: control.shortcutMap
        function onChanged() {
            if (control.recordingCommandId.length === 0 && !control.dirty) control.beginEdit()
        }
    }
    Connections {
        target: control.settingsModel
        function onValuesChanged() { control.syncPersistedOverrides() }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        width: parent ? parent.width : Tk.Theme.size.dialogWidth

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.SearchField {
                id: shortcutSearch
                objectName: "shortcutSearch"
                placeholderText: qsTr("Find a command")
                tooltip: qsTr("Search shortcuts by command name, section, or ID")
                onTextEdited: control.searchText = text
                Tk.Flex.grow: 1
            }
            Tk.Button {
                objectName: "shortcutRestoreDefaults"
                text: qsTr("Restore defaults")
                tooltip: qsTr("Remove all staged shortcut overrides")
                onClicked: control.restoreDefaults()
            }
            Tk.Button {
                objectName: "shortcutApply"
                text: qsTr("Apply")
                variant: "accent"
                enabled: control.dirty && !!control.validation.ok
                tooltip: qsTr("Validate and apply staged shortcut bindings")
                onClicked: control.applyChanges()
            }
        }
        Tk.Label {
            objectName: "shortcutValidation"
            visible: !control.validation.ok && (control.dirty || control.errorText.length > 0)
            text: control.errorText.length > 0 ? control.errorText
                : control.validation.error || qsTr("Resolve shortcut conflicts")
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
        }
        ListView {
            id: shortcutList
            objectName: "shortcutCommands"
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Commands and shortcuts")
            model: control.filteredCommands
            clip: true
            spacing: Tk.Theme.space.xs
            implicitHeight: Math.min(Math.max(contentHeight, Tk.Theme.size.control), Tk.Theme.size.dialogWidth)
            height: implicitHeight
            Tk.Flex.shrink: 0
            delegate: Tk.Box {
                id: commandRow
                required property var modelData
                property var recordButtonRef: null
                objectName: "shortcutRow_" + modelData.id
                width: shortcutList.width
                padding: Tk.Theme.space.sm
                color: Tk.Theme.color.surface
                borderColor: Tk.Theme.color.divider
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.label + ", " + modelData.displaySequence

                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.sm
                    width: parent.width
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        Tk.Label { text: commandRow.modelData.label; Tk.Flex.grow: 1 }
                        Tk.Caption { visible: commandRow.modelData.section.length > 0; text: commandRow.modelData.section }
                        Tk.Flex.shrink: 0
                    }
                    Tk.KeyCap {
                        objectName: "shortcutCurrent_" + commandRow.modelData.id
                        sequence: commandRow.modelData.displaySequence
                        Accessible.name: qsTr("Current shortcut")
                    }
                    Tk.Button {
                        id: recordButton
                        objectName: "shortcutRecord_" + commandRow.modelData.id
                        text: control.recordingCommandId === commandRow.modelData.id ? qsTr("Listening…") : qsTr("Record")
                        tooltip: qsTr("Record a shortcut sequence; Escape cancels and Tab leaves capture")
                        Component.onCompleted: commandRow.recordButtonRef = recordButton
                        onClicked: control.recordingCommandId === commandRow.modelData.id
                            ? control.cancelCapture() : control.beginCapture(commandRow.modelData.id)
                    }
                    Tk.TextField {
                        id: captureField
                        objectName: "shortcutCapture_" + commandRow.modelData.id
                        visible: control.recordingCommandId === commandRow.modelData.id
                        width: Tk.Theme.size.fieldWidth
                        readOnly: true
                        placeholderText: qsTr("Press shortcut…")
                        text: control.capturePreview
                        tooltip: qsTr("Escape cancels capture. Tab exits capture without changing focus order.")
                    }
                    Tk.Button {
                        objectName: "shortcutReset_" + commandRow.modelData.id
                        text: qsTr("Default")
                        variant: "ghost"
                        tooltip: qsTr("Restore the default shortcut for %1").arg(commandRow.modelData.label)
                        onClicked: control.resetCommand(commandRow.modelData.id)
                    }
                }
            }
        }
        ListView {
            id: conflictList
            objectName: "shortcutConflicts"
            visible: control.validation.conflicts && control.validation.conflicts.length > 0
            model: visible ? control.validation.conflicts : []
            implicitHeight: Math.min(contentHeight, Tk.Theme.size.dialogWidth)
            height: implicitHeight
            clip: true
            delegate: Tk.Notice {
                required property var modelData
                required property int index
                objectName: "shortcutConflict_" + index
                width: conflictList.width
                title: qsTr("Shortcut conflict")
                text: qsTr("%1 and %2 both use %3").arg(modelData.first).arg(modelData.second).arg(modelData.sequence)
                variant: "danger"
                Accessible.role: Accessible.AlertMessage
            }
        }
        Tk.Label {
            objectName: "shortcutError"
            visible: control.errorText.length > 0
            text: control.errorText
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
        }
    }
}
