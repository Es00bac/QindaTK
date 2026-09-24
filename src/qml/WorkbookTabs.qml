// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md, docs/workbook.md): sheet tabs projected
// from Tk.Workbook.sheets by stable sheet ID. Add, rename, move, tab color,
// hide/show and protect call the Workbook (each one undoable step; formulas
// follow IDs, so references keep their targets). Hidden sheets leave the
// strip and are listed by the "hidden sheets" button. Removal is only
// requested: the host confirms, then calls Workbook.removeSheet. The current
// sheet is owned by the host. A tab's menu opens on right-click, the Menu key
// or Shift+F10.
Item {
    id: control

    property Tk.Workbook workbook: null
    property string currentSheetId: ""
    property string tooltip: qsTr("Sheets")
    property string renamingSheetId: ""
    property string errorText: ""
    // Tab colors offered by the menu; picked values are stored as #rrggbb
    // document colors, so they do not follow later theme changes.
    property var tabPalette: [{ name: qsTr("Danger"), color: Tk.Theme.color.danger },
                              { name: qsTr("Warning"), color: Tk.Theme.color.warning },
                              { name: qsTr("Success"), color: Tk.Theme.color.success },
                              { name: qsTr("Info"), color: Tk.Theme.color.info },
                              { name: qsTr("Accent"), color: Tk.Theme.color.accent }]
    property string menuSheetId: ""
    property Item menuAnchor: null
    readonly property var sheets: workbook ? workbook.sheets : []
    readonly property var visibleSheets: sheets.filter(function(sheet) { return !sheet.hidden })
    readonly property var hiddenSheets: sheets.filter(function(sheet) { return sheet.hidden })
    readonly property var menuSheet: sheets[indexOfSheet(menuSheetId)] || null

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
    function report(ok) {
        errorText = ok || !workbook ? "" : workbook.lastError
        return ok
    }
    function activate(sheetId) {
        if (indexOfSheet(sheetId) < 0) return false
        sheetActivated(sheetId)
        return true
    }
    function addSheet() {
        if (!workbook) return ""
        const id = workbook.addSheet("", indexOfSheet(currentSheetId) + 1)
        if (!report(id.length > 0)) return ""
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
        if (!report(workbook.renameSheet(renamingSheetId, name))) return false
        renamingSheetId = ""
        return true
    }
    function cancelRename() {
        renamingSheetId = ""
        errorText = ""
    }
    // Moves past the neighbouring visible tab (hidden sheets keep their places).
    function moveBy(sheetId, delta) {
        const shown = visibleSheets.map(function(sheet) { return sheet.id })
        const at = shown.indexOf(sheetId)
        if (!workbook || at < 0 || at + delta < 0 || at + delta >= shown.length) return false
        return report(workbook.moveSheet(sheetId, indexOfSheet(shown[at + delta])))
    }
    function requestRemove(sheetId) {
        const index = indexOfSheet(sheetId)
        if (index < 0 || sheets.length <= 1) return false
        removeRequested(sheetId, sheets[index].name)
        return true
    }
    function setTabColor(sheetId, color) {
        return !!workbook && report(workbook.setSheetProperties(sheetId, { tabColor: color ? String(color) : "" }))
    }
    function setHidden(sheetId, hidden) {
        if (!workbook || !report(workbook.setSheetProperties(sheetId, { hidden: hidden }))) return false
        if (hidden && sheetId === currentSheetId && visibleSheets.length > 0) activate(visibleSheets[0].id)
        if (!hidden) activate(sheetId)
        return true
    }
    function setProtected(sheetId, locked) {
        return !!workbook && report(workbook.setSheetProperties(sheetId, { protected: locked }))
    }
    function openMenu(sheetId, anchor) {
        if (indexOfSheet(sheetId) < 0) return false
        menuSheetId = sheetId
        menuAnchor = anchor
        tabMenu.popup(anchor, 0, anchor.height)
        return true
    }
    function openColorPicker(sheetId, anchor) {
        if (indexOfSheet(sheetId) < 0) return false
        menuSheetId = sheetId
        colorPopover.anchorItem = anchor
        colorPopover.open()
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
                model: control.visibleSheets
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
                                                tabContent.implicitWidth + leftPadding + rightPadding)
                        leftPadding: Tk.Theme.space.md
                        rightPadding: Tk.Theme.space.md
                        checkable: false
                        checked: slot.current
                        focusPolicy: Qt.TabFocus
                        text: slot.modelData.name
                        Accessible.role: Accessible.PageTab
                        Accessible.name: slot.modelData.name
                        Accessible.description: (slot.modelData.protected ? qsTr("Protected. ") : "")
                            + qsTr("Sheet %1 of %2. F2 renames; Ctrl+Shift+Left or Right moves it; the Menu key opens its menu.")
                              .arg(slot.index + 1).arg(control.visibleSheets.length)
                        Accessible.selected: slot.current
                        // The PageTab role drops TabButton's own press action:
                        // an assistive press must switch sheets like a click.
                        Accessible.onPressAction: control.activate(slot.modelData.id)
                        onClicked: control.activate(slot.modelData.id)
                        onDoubleClicked: control.beginRename(slot.modelData.id)
                        Keys.onPressed: function(event) {
                            const shifted = (event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)
                            if (event.key === Qt.Key_F2) control.beginRename(slot.modelData.id)
                            else if (shifted && event.key === Qt.Key_Left) control.moveBy(slot.modelData.id, -1)
                            else if (shifted && event.key === Qt.Key_Right) control.moveBy(slot.modelData.id, 1)
                            else if (event.key === Qt.Key_Delete) control.requestRemove(slot.modelData.id)
                            else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)))
                                control.openMenu(slot.modelData.id, tab)
                            else return
                            event.accepted = true
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: control.openMenu(slot.modelData.id, tab)
                        }
                        contentItem: Tk.Flex {
                            id: tabContent
                            direction: Tk.Flex.Row
                            align: Tk.Flex.Center
                            justify: Tk.Flex.Center
                            gap: Tk.Theme.space.xs
                            Tk.Icon {
                                objectName: "workbookTabLock"
                                visible: slot.modelData.protected === true
                                name: "lock"
                                size: Tk.Theme.size.iconSm
                                color: Tk.Theme.color.textMuted
                            }
                            Tk.Label {
                                text: tab.text
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                                color: slot.current ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                                Tk.Flex.minWidth: 0
                            }
                        }
                        background: Rectangle {
                            color: slot.current ? Tk.Theme.color.panelAlt
                                                : tab.hovered ? Tk.Theme.color.hover : "transparent"
                            border.width: tab.activeFocus ? Tk.Theme.size.focusRing : 0
                            border.color: Tk.Theme.color.focus
                            Rectangle {
                                // The document's tab color along the top edge.
                                objectName: "workbookTabColor"
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                height: Tk.Theme.size.focusRing
                                color: slot.modelData.tabColor || "transparent"
                                visible: String(slot.modelData.tabColor || "").length > 0
                            }
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
            Tk.IconButton {
                id: hiddenButton
                objectName: "workbookTabHidden"
                iconName: "eye-off"
                small: true
                visible: control.hiddenSheets.length > 0
                tooltip: qsTr("Hidden sheets (%1)").arg(control.hiddenSheets.length)
                onClicked: hiddenMenu.popup(hiddenButton, 0, hiddenButton.height)
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

    Tk.Menu {
        id: hiddenMenu
        objectName: "workbookHiddenMenu"
        Instantiator {
            model: control.hiddenSheets
            delegate: Tk.MenuItem {
                required property var modelData
                text: qsTr("Show %1").arg(modelData.name)
                onTriggered: control.setHidden(modelData.id, false)
            }
            onObjectAdded: function(index, object) { hiddenMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { hiddenMenu.removeItem(object) }
        }
    }

    Tk.Menu {
        id: tabMenu
        objectName: "workbookTabMenu"
        Tk.MenuItem {
            objectName: "workbookTabMenuRename"
            text: qsTr("Rename")
            onTriggered: control.beginRename(control.menuSheetId)
        }
        Tk.MenuItem {
            text: qsTr("Move left")
            enabled: control.visibleSheets.length > 0 && control.visibleSheets[0].id !== control.menuSheetId
            onTriggered: control.moveBy(control.menuSheetId, -1)
        }
        Tk.MenuItem {
            text: qsTr("Move right")
            enabled: control.visibleSheets.length > 0
                     && control.visibleSheets[control.visibleSheets.length - 1].id !== control.menuSheetId
            onTriggered: control.moveBy(control.menuSheetId, 1)
        }
        Tk.MenuSeparator {}
        Tk.MenuItem {
            objectName: "workbookTabMenuColor"
            text: qsTr("Tab color…")
            onTriggered: control.openColorPicker(control.menuSheetId, control.menuAnchor)
        }
        Tk.MenuItem {
            objectName: "workbookTabMenuHide"
            text: qsTr("Hide sheet")
            enabled: control.visibleSheets.length > 1
            onTriggered: control.setHidden(control.menuSheetId, true)
        }
        Tk.MenuItem {
            objectName: "workbookTabMenuProtect"
            text: control.menuSheet && control.menuSheet.protected ? qsTr("Unprotect sheet") : qsTr("Protect sheet")
            onTriggered: control.setProtected(control.menuSheetId, !(control.menuSheet && control.menuSheet.protected))
        }
        Tk.MenuSeparator {}
        Tk.MenuItem {
            objectName: "workbookTabMenuRemove"
            text: qsTr("Remove sheet…")
            enabled: control.sheets.length > 1
            onTriggered: control.requestRemove(control.menuSheetId)
        }
    }

    Tk.Popover {
        id: colorPopover
        objectName: "workbookTabColorPopover"
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.ColorSwatches {
                objectName: "workbookTabSwatches"
                colors: control.tabPalette
                columns: control.tabPalette.length
                customButton: false
                current: control.menuSheet && control.menuSheet.tabColor ? control.menuSheet.tabColor : "transparent"
                onSelected: function(color) {
                    control.setTabColor(control.menuSheetId, color.toString())
                    colorPopover.close()
                }
            }
            Tk.Button {
                objectName: "workbookTabColorNone"
                text: qsTr("No color")
                variant: "ghost"
                onClicked: {
                    control.setTabColor(control.menuSheetId, "")
                    colorPopover.close()
                }
            }
        }
    }
}
