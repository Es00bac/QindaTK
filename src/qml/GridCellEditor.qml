// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Editor lifetime and commit semantics belong to EditableGrid. This pane
// chooses a keyboard-operable picker only for cells with a host value list.
Item {
    id: pane
    required property var grid
    objectName: "gridEditorPane"
    readonly property int row: grid.model && grid.dataEpoch >= 0 ? grid.model.rowIndex(grid.editingRowId) : -1
    readonly property int column: grid.model && grid.dataEpoch >= 0 ? grid.model.columnIndex(grid.editingColumnId) : -1
    readonly property size frozen: grid.model && grid.dataEpoch >= 0 ? grid.model.frozenExtent() : Qt.size(0, 0)
    readonly property bool frozenRow: row >= 0 && row < grid.model.frozenRows
    readonly property bool frozenColumn: column >= 0 && column < grid.model.frozenColumns
    readonly property var choices: grid.valueList && grid.cellEditing ? grid.valueList(grid.editingRowId, grid.editingColumnId) : null
    readonly property bool listed: choices !== null && choices !== undefined
    readonly property bool editorHasFocus: input.activeFocus || picker.activeFocus
    property alias text: input.text
    property alias cursorPosition: input.cursorPosition
    function clear() { input.clear() }
    function insert(at, value) { input.insert(at, value) }
    function selectAll() { if (!listed) input.selectAll() }
    function focusEditor() { if (listed) picker.forceActiveFocus(); else input.forceActiveFocus() }
    function accepts(value) { return !listed || choices.limitToList !== true || (choices.values || []).indexOf(value) >= 0 }
    x: frozenColumn ? 0 : frozen.width
    y: frozenRow ? 0 : frozen.height
    width: frozenColumn ? frozen.width : Math.max(0, parent.width - x)
    height: frozenRow ? frozen.height : Math.max(0, parent.height - y)
    z: 5
    clip: true
    Tk.TextField {
        id: input
        objectName: "gridCellEditor"
        visible: pane.grid.cellEditing && !pane.listed
        x: pane.grid.editorRect.x - pane.x
        y: pane.grid.editorRect.y - pane.y
        width: pane.grid.editorRect.width
        height: pane.grid.editorRect.height
        text: pane.grid.editText
        tooltip: qsTr("Edit grid cell")
        Keys.onReturnPressed: event => { event.accepted = true; pane.grid.commitCellEdit(text) }
        Keys.onEnterPressed: event => { event.accepted = true; pane.grid.commitCellEdit(text) }
        Keys.onEscapePressed: event => { event.accepted = true; pane.grid.cancelCellEdit() }
        onActiveFocusChanged: if (!activeFocus && visible && pane.grid.model && pane.grid.model.editing) pane.grid.commitCellEdit(text)
    }
    Tk.ComboBox {
        id: picker
        property bool choosingPopup: false
        objectName: "gridValueListEditor"
        visible: pane.grid.cellEditing && pane.listed
        x: input.x; y: input.y; width: input.width; height: input.height
        model: pane.choices ? pane.choices.values || [] : []
        editable: pane.listed && pane.choices.limitToList !== true
        currentIndex: model.indexOf(pane.grid.editText)
        editText: pane.grid.editText
        tooltip: qsTr("Choose a grid cell value")
        // Closed-combo arrow keys emit activated too. Keep those choices
        // in the draft until Enter, so its release cannot reopen the grid.
        onActivated: index => {
            pane.grid.editText = String(model[index])
            if (choosingPopup) pane.grid.commitCellEdit(model[index])
        }
        Connections {
            target: picker.popup
            function onOpened() { picker.choosingPopup = true }
            function onClosed() { Qt.callLater(function() { picker.choosingPopup = false }) }
        }
        onAccepted: if (editable) pane.grid.commitCellEdit(editText)
        Keys.onEscapePressed: event => { event.accepted = true; pane.grid.cancelCellEdit() }
        Keys.onReturnPressed: event => { event.accepted = true; pane.grid.commitCellEdit(editable ? editText : currentText) }
        Keys.onEnterPressed: event => { event.accepted = true; pane.grid.commitCellEdit(editable ? editText : currentText) }
    }
}
