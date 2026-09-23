// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md, docs/workbook.md): sheet tabs projected
// from Tk.Workbook.sheets by stable sheet ID. Add, rename and move call the
// Workbook (each one undoable step; formulas follow IDs, so references keep
// their targets). Removal is only requested: the host confirms, then calls
// Workbook.removeSheet. The current sheet is owned by the host.
Item {
    id: control

    property Tk.Workbook workbook: null
    property string currentSheetId: ""
    property string tooltip: qsTr("Sheets")
    property string renamingSheetId: ""
    property string errorText: ""
    readonly property var sheets: workbook ? workbook.sheets : []

    signal sheetActivated(string sheetId)
    signal removeRequested(string sheetId, string name)

    implicitWidth: row.implicitWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.PageTabList
    Accessible.name: tooltip

    function indexOfSheet(sheetId) {
        for (let i = 0; i < sheets.length; ++i) if (sheets[i].id === sheetId) return i
        return -1
    }
    function activate(sheetId) {
        if (indexOfSheet(sheetId) < 0) return false
        sheetActivated(sheetId)
        return true
    }
    function addSheet() {
        if (!workbook) return ""
        const id = workbook.addSheet("", indexOfSheet(currentSheetId) + 1)
        if (id.length === 0) { errorText = workbook.lastError; return "" }
        errorText = ""
        activate(id)
        return id
    }
    function beginRename(sheetId) {
        if (indexOfSheet(sheetId) < 0) return false
        renamingSheetId = sheetId
        return true
    }
    function commitRename(name) {
        if (!workbook || renamingSheetId.length === 0) return false
        const id = renamingSheetId
        if (!workbook.renameSheet(id, name)) { errorText = workbook.lastError; return false }
        errorText = ""
        renamingSheetId = ""
        return true
    }
    function cancelRename() {
        renamingSheetId = ""
        errorText = ""
    }
    function moveBy(sheetId, delta) {
        const from = indexOfSheet(sheetId)
        const to = from + delta
        if (!workbook || from < 0 || to < 0 || to >= sheets.length) return false
        if (!workbook.moveSheet(sheetId, to)) { errorText = workbook.lastError; return false }
        errorText = ""
        return true
    }
    function requestRemove(sheetId) {
        const index = indexOfSheet(sheetId)
        if (index < 0 || sheets.length <= 1) return false
        removeRequested(sheetId, sheets[index].name)
        return true
    }

    Tk.Flex {
        id: column
        width: control.width
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            id: row
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Repeater {
                model: control.sheets
                delegate: Item {
                    id: slot
                    required property var modelData
                    required property int index
                    readonly property bool current: modelData.id === control.currentSheetId
                    readonly property bool renaming: modelData.id === control.renamingSheetId
                    objectName: "workbookTabSlot_" + modelData.id
                    implicitWidth: renaming ? editor.implicitWidth : tab.implicitWidth
                    implicitHeight: Tk.Theme.size.tab
                    Tk.Flex.shrink: 0
                    T.TabButton {
                        id: tab
                        objectName: "workbookTab_" + slot.modelData.id
                        visible: !slot.renaming
                        anchors.fill: parent
                        implicitWidth: Math.max(Tk.Theme.size.labelWidth,
                                                label.implicitWidth + leftPadding + rightPadding)
                        leftPadding: Tk.Theme.space.md
                        rightPadding: Tk.Theme.space.md
                        checkable: false
                        checked: slot.current
                        focusPolicy: Qt.TabFocus
                        text: slot.modelData.name
                        Accessible.role: Accessible.PageTab
                        Accessible.name: slot.modelData.name
                        Accessible.description: qsTr("Sheet %1 of %2. F2 renames; Ctrl+Shift+Left or Right moves it.")
                                                    .arg(slot.index + 1).arg(control.sheets.length)
                        Accessible.selected: slot.current
                        onClicked: control.activate(slot.modelData.id)
                        onDoubleClicked: control.beginRename(slot.modelData.id)
                        Keys.onPressed: function(event) {
                            const shifted = (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)
                            if (event.key === Qt.Key_F2) control.beginRename(slot.modelData.id)
                            else if (shifted && event.key === Qt.Key_Left) control.moveBy(slot.modelData.id, -1)
                            else if (shifted && event.key === Qt.Key_Right) control.moveBy(slot.modelData.id, 1)
                            else if (event.key === Qt.Key_Delete) control.requestRemove(slot.modelData.id)
                            else return
                            event.accepted = true
                        }
                        contentItem: Tk.Label {
                            id: label
                            text: tab.text
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: slot.current ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                        }
                        background: Rectangle {
                            color: slot.current ? Tk.Theme.color.panelAlt
                                                : tab.hovered ? Tk.Theme.color.hover : "transparent"
                            border.width: tab.activeFocus ? Tk.Theme.size.focusRing : 0
                            border.color: Tk.Theme.color.focus
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: Tk.Theme.size.focusRing
                                color: Tk.Theme.color.accent
                                visible: slot.current
                            }
                        }
                        Tk.ToolTip { text: qsTr("%1 — double-click to rename").arg(slot.modelData.name); visible: tab.hovered }
                    }
                    Tk.TextField {
                        id: editor
                        objectName: "workbookTabEditor"
                        visible: slot.renaming
                        anchors.fill: parent
                        implicitWidth: Tk.Theme.size.fieldWidth
                        small: true
                        tooltip: qsTr("New name for sheet %1").arg(slot.modelData.name)
                        error: control.errorText.length > 0
                        onVisibleChanged: if (visible) {
                            text = slot.modelData.name
                            forceActiveFocus()
                            selectAll()
                        }
                        onAccepted: control.commitRename(text)
                        Keys.onEscapePressed: function(event) {
                            event.accepted = true
                            control.cancelRename()
                        }
                        onActiveFocusChanged: if (!activeFocus && slot.renaming) control.cancelRename()
                    }
                }
            }
            Tk.IconButton {
                objectName: "workbookTabAdd"
                iconName: "plus"
                small: true
                enabled: !!control.workbook
                tooltip: qsTr("Add a sheet after the current one")
                onClicked: control.addSheet()
            }
        }
        Tk.Caption {
            objectName: "workbookTabError"
            visible: control.errorText.length > 0
            text: control.errorText
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
    }
}
