// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md, docs/workbook.md): edits the active cell
// of a Tk.SheetView. The name box shows (and jumps to) an address; the editor
// shows the cell's source input, never its computed value, which appears in
// `formulaResult`. An edit remembers its target cell until Enter commits it
// through the Workbook or Escape restores the prior input. While a formula
// is edited, selecting cells in the sheet inserts their reference at the
// caret (point mode); a rejected commit selects the offending source span.
T.Control {
    id: control

    property Tk.SheetView sheetView: null
    property string tooltip: qsTr("Formula bar")
    property bool editing: false
    property string originalText: ""
    property string targetSheetId: ""
    property int targetRow: -1
    property int targetColumn: -1
    property int referenceStart: -1
    property int referenceEnd: -1
    property string errorText: ""
    property int errorStart: -1
    property int errorEnd: -1
    property bool pointing: false
    readonly property Tk.Workbook workbook: sheetView ? sheetView.workbook : null
    readonly property alias text: editor.text
    readonly property string targetAddress: workbook && targetRow >= 0 ? workbook.address(targetRow, targetColumn) : ""
    readonly property string address: editing ? targetAddress : (sheetView ? sheetView.activeAddress : "")
    readonly property string result: sheetView ? sheetView.activeDisplay : ""
    readonly property bool pointMode: editing && editor.text.startsWith("=")
    readonly property var inputCheck: editing && sheetView ? sheetView.adapter.checkInput(editor.text) : null
    readonly property var referenceSpans: inputCheck && inputCheck.ok && inputCheck.references
                                          ? inputCheck.references : []
    readonly property string functionHint: pointMode && workbook
        ? workbook.functionSignature(currentFunction(editor.text, editor.cursorPosition)) : ""

    signal committed(string address, string input)
    signal cancelled()

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: bar.implicitHeight
    Accessible.role: Accessible.ToolBar
    Accessible.name: tooltip

    function beginEdit() {
        if (!sheetView || !sheetView.hasActiveCell) return false
        if (editing) return true
        targetSheetId = sheetView.currentSheetId
        targetRow = sheetView.activeRow
        targetColumn = sheetView.activeColumn
        originalText = sheetView.activeInput
        editor.text = originalText
        referenceStart = -1
        clearError()
        editing = true
        editor.forceActiveFocus()
        return true
    }
    function finishEditing(restoreSelection) {
        const sheet = targetSheetId, row = targetRow, column = targetColumn
        editing = false
        referenceStart = -1
        targetRow = -1
        targetColumn = -1
        if (!sheetView || !restoreSelection) return
        if (sheetView.currentSheetId !== sheet) sheetView.sheetId = sheet
        sheetView.setCurrent(row, column, false)
        sheetView.focusGrid()
    }
    function commit(restoreSelection) {
        if (!editing || !workbook) return false
        const input = editor.text
        const ok = targetSheetId === sheetView.currentSheetId
            ? sheetView.commitInputAt(targetRow, targetColumn, input)
            : workbook.setCellInput(targetSheetId, targetRow, targetColumn, input)
        if (!ok) {
            // The view is asking about a warning/information rule; its answer
            // arrives through validationResolved.
            if (sheetView.validationPending) return false
            showError(workbook.lastError, workbook.lastErrorDetail)
            editor.forceActiveFocus()
            return false
        }
        const where = targetAddress
        finishEditing(restoreSelection !== false)
        committed(where, input)
        return true
    }
    function cancel() {
        if (!editing) return
        editor.text = originalText
        clearError()
        finishEditing(true)
        cancelled()
    }
    function clearError() {
        errorText = ""
        errorStart = -1
        errorEnd = -1
    }
    function showError(message, detail) {
        errorText = message
        errorStart = detail && detail.start !== undefined ? Number(detail.start) : -1
        errorEnd = detail && detail.end !== undefined ? Number(detail.end) : -1
        if (errorStart >= 0 && errorEnd > errorStart && errorStart < editor.length)
            editor.select(errorStart, Math.min(errorEnd, editor.length))
    }
    // Replaces the reference inserted by the previous selection, otherwise
    // the selected text or the caret position.
    function insertReference(reference) {
        const start = referenceStart >= 0 ? referenceStart : editor.selectionStart
        const end = referenceStart >= 0 ? referenceEnd : editor.selectionEnd
        pointing = true
        editor.remove(start, end)
        editor.insert(start, reference)
        referenceStart = start
        referenceEnd = start + reference.length
        editor.cursorPosition = referenceEnd
        pointing = false
    }
    function pointAt(range) {
        if (!range || !workbook) return
        insertReference(workbook.referenceText(targetSheetId, sheetView.currentSheetId, range.firstRow,
                                               range.firstColumn, range.lastRow, range.lastColumn))
        Qt.callLater(function() { if (control.editing) editor.forceActiveFocus() })
    }
    function currentFunction(input, caret) {
        let depth = 0
        for (let i = Math.min(caret, input.length) - 1; i >= 0; --i) {
            const c = input[i]
            if (c === '"') {
                let j = i - 1
                while (j >= 0 && input[j] !== '"') --j
                i = j
            } else if (c === ")") {
                ++depth
            } else if (c === "(") {
                if (depth === 0) {
                    let j = i - 1
                    while (j >= 0 && /[A-Za-z]/.test(input[j])) --j
                    return input.substring(j + 1, i)
                }
                --depth
            }
        }
        return ""
    }
    function jumpTo(text) {
        const parsed = workbook ? workbook.parseAddress(text) : null
        if (!parsed || !parsed.ok || !sheetView) return false
        sheetView.selectRange(parsed.firstRow, parsed.firstColumn, parsed.lastRow, parsed.lastColumn)
        sheetView.focusGrid()
        return true
    }

    Binding {
        target: editor
        property: "text"
        value: control.sheetView ? control.sheetView.activeInput : ""
        when: !control.editing
        restoreMode: Binding.RestoreNone
    }
    // AGENT-GUARD: only user selection changes (SheetView.selectionEdited)
    // point or commit; SheetView's own API moves stay quiet. Reacting to every
    // selection change let a deferred per-sheet restore commit a half-typed
    // entry (tests/qml/tst_workbook_example.qml).
    Connections {
        target: control.sheetView
        function onSelectionEdited() {
            if (!control.editing || control.pointing) return
            if (control.pointMode) control.pointAt(control.sheetView.selection)
            else control.commit(false)
        }
        function onSheetShown() {
            if (control.pointMode && !control.pointing)
                control.pointAt(control.sheetView.selectionOrActive())
        }
        function onValidationResolved(row, column, action) {
            if (!control.editing || row !== control.targetRow || column !== control.targetColumn) return
            if (action === "cancel") {
                control.cancel()
            } else if (action === "commit") {
                const where = control.targetAddress, input = editor.text
                control.finishEditing(true)
                control.committed(where, input)
            } else {
                editor.forceActiveFocus()
            }
        }
    }

    contentItem: Tk.Flex {
        id: bar
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.TextField {
                id: nameBox
                objectName: "formulaNameBox"
                Tk.Flex.basis: Tk.Theme.size.labelWidth
                Tk.Flex.shrink: 0
                mono: true
                text: control.address
                tooltip: qsTr("Active cell address; type an address such as B3 or A1:C4 and press Enter to select it")
                onAccepted: if (!control.jumpTo(text)) text = Qt.binding(function() { return control.address })
            }
            Tk.Caption {
                objectName: "formulaFunctionLabel"
                text: "fx"
                Accessible.ignored: true
            }
            Tk.TextField {
                id: editor
                objectName: "formulaEditor"
                Tk.Flex.grow: 1
                mono: true
                error: control.errorText.length > 0
                enabled: !!control.sheetView && control.sheetView.hasActiveCell
                placeholderText: qsTr("Value or =formula")
                tooltip: control.editing ? qsTr("Formula for %1").arg(control.targetAddress)
                                         : qsTr("Formula for the active cell")
                Accessible.description: control.errorText.length > 0 ? control.errorText
                                        : control.functionHint.length > 0 ? control.functionHint
                                        : qsTr("Result: %1").arg(control.result)
                onActiveFocusChanged: if (activeFocus && !control.editing) control.beginEdit()
                onTextEdited: {
                    if (!control.editing) control.beginEdit()
                    if (!control.pointing) control.referenceStart = -1
                    if (control.errorText.length > 0) control.clearError()
                }
                onAccepted: control.commit(true)
                Keys.onEscapePressed: function(event) {
                    event.accepted = true
                    control.cancel()
                }
            }
            Tk.IconButton {
                objectName: "formulaCancel"
                visible: control.editing
                iconName: "x"
                tooltip: qsTr("Cancel the edit and keep the previous formula (Escape)")
                onClicked: control.cancel()
            }
            Tk.IconButton {
                objectName: "formulaCommit"
                visible: control.editing
                iconName: "check"
                tooltip: qsTr("Commit the formula to %1 (Enter)").arg(control.targetAddress)
                onClicked: control.commit(true)
            }
            Tk.Mono {
                objectName: "formulaResult"
                text: control.result.length > 0 ? "= " + control.result : ""
                color: control.result.startsWith("#") ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                elide: Text.ElideRight
                Tk.Flex.shrink: 1
                Tk.Flex.maxWidth: Tk.Theme.size.fieldWidth * 2
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Result of %1: %2").arg(control.address).arg(control.result)
            }
        }
        Tk.Caption {
            objectName: "formulaError"
            visible: control.errorText.length > 0
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
            text: control.errorStart >= 0 && control.errorEnd > control.errorStart
                  && control.errorStart < editor.length
                  ? qsTr("%1 — at “%2”").arg(control.errorText)
                        .arg(editor.text.substring(control.errorStart, control.errorEnd))
                  : control.errorText
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        Tk.Caption {
            objectName: "formulaHint"
            visible: control.functionHint.length > 0 && control.errorText.length === 0
            text: control.functionHint
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }
}
