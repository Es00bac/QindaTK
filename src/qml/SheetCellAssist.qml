// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#sheetview): Tk.SheetView's data-validation
// assistance, drawn over the sheet. It shows the active cell's input message,
// a list button that enters one of a list rule's values, and the prompt for
// an entry that breaks a warning/information rule (Yes / No / Cancel, or
// OK / Cancel). The Workbook decides validity (Workbook.validateInput); this
// item keeps only the pending prompt.
Item {
    id: assist

    property var view: null // the Tk.SheetView (var: see ConditionalRuleForm)
    // {row, column, input, check, source: "grid" | "api"} while a prompt is open.
    property var pending: null
    readonly property var validation: view ? view.activeValidation : ({})
    readonly property bool listRule: validation.type === "list"
    readonly property var listItems: view && view.epoch >= 0 && listRule
                                     ? view.adapter.validationItems(view.activeRow, view.activeColumn) : []
    readonly property rect cell: activeCellRect()
    readonly property bool cellShown: cell.width > 0
    readonly property bool hasMessage: String(validation.inputMessage || "").length > 0
                                       || String(validation.inputTitle || "").length > 0

    objectName: "sheetCellAssist"
    Accessible.ignored: true

    // The active cell (its merged area) in this item's coordinates, or an empty rect.
    function activeCellRect() {
        const grid = view ? view.grid : null
        if (!grid || !view.hasActiveCell || grid.dataEpoch < 0) return Qt.rect(0, 0, 0, 0)
        const r = grid.areaRect(grid.activeRowId, grid.activeColumnId)
        if (r.width <= 0 || r.x + r.width <= 0 || r.y + r.height <= 0 || r.x >= grid.bodyWidth || r.y >= grid.bodyHeight)
            return Qt.rect(0, 0, 0, 0)
        const x0 = grid.showHeaders ? grid.rowHeaderWidth : 0, y0 = grid.showHeaders ? grid.headerHeight : 0
        const p = grid.mapToItem(assist, x0 + r.x, y0 + r.y)
        return Qt.rect(p.x, p.y, r.width, r.height)
    }

    // Holds `input` until the user answers; the dialog opens outside the caller's stack.
    function prompt(row, column, input, check, source) {
        pending = { row: row, column: column, input: input, check: check, source: source }
        Qt.callLater(promptDialog.open)
    }

    // "commit": apply despite the rule; "retry": keep editing; "cancel": drop the entry.
    function resolve(action) {
        const entry = pending
        pending = null
        if (!entry || !view) return
        if (action === "commit") {
            const ok = view.commitInputAt(entry.row, entry.column, entry.input, true)
            if (entry.source === "grid" && ok) view.grid.cancelCellEdit()
            view.validationResolved(entry.row, entry.column, ok ? "commit" : "retry")
        } else if (action === "retry") {
            if (entry.source === "grid") view.grid.focusCellEditor()
            view.validationResolved(entry.row, entry.column, "retry")
        } else {
            if (entry.source === "grid") view.grid.cancelCellEdit()
            view.validationResolved(entry.row, entry.column, "cancel")
        }
    }

    function openList() {
        if (!listRule || listItems.length === 0 || !listButton.visible) return false
        listMenu.popup(listButton, 0, listButton.height)
        return true
    }

    function choose(value) {
        if (view && view.hasActiveCell) view.commitInputAt(view.activeRow, view.activeColumn, value)
        if (view) view.focusGrid()
    }

    Rectangle {
        id: message
        objectName: "sheetInputMessage"
        readonly property bool below: assist.cell.y + assist.cell.height + height + Tk.Theme.space.xs <= assist.height
        visible: !!assist.view && assist.hasMessage && assist.cellShown && assist.pending === null
                 && !listMenu.visible
        x: Math.max(0, Math.min(assist.cell.x, assist.width - width))
        y: below ? assist.cell.y + assist.cell.height + Tk.Theme.space.xs
                 : Math.max(0, assist.cell.y - height - Tk.Theme.space.xs)
        width: Math.min(Tk.Theme.size.panelMinWidth, assist.width)
        height: messageColumn.implicitHeight + Tk.Theme.space.sm * 2
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.color.tooltipBg
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.popoverBorder
        Accessible.role: Accessible.ToolTip
        Accessible.name: [assist.validation.inputTitle || "", assist.validation.inputMessage || ""]
                         .filter(function(part) { return part.length > 0 }).join(". ")

        Tk.Flex {
            id: messageColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Tk.Theme.space.sm
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Label {
                objectName: "sheetInputMessageTitle"
                visible: text.length > 0
                text: String(assist.validation.inputTitle || "")
                font.bold: true
                color: Tk.Theme.color.tooltipText
                wrapMode: Text.Wrap
                elide: Text.ElideNone
                Tk.Flex.minWidth: 0
            }
            Tk.Label {
                objectName: "sheetInputMessageText"
                visible: text.length > 0
                text: String(assist.validation.inputMessage || "")
                color: Tk.Theme.color.tooltipText
                wrapMode: Text.Wrap
                elide: Text.ElideNone
                Tk.Flex.minWidth: 0
            }
        }
    }

    Tk.IconButton {
        id: listButton
        objectName: "sheetListButton"
        visible: !!assist.view && assist.listRule && assist.cellShown && !assist.view.grid.cellEditing
                 && !assist.view.readOnly
        small: true
        iconName: "chevron-down"
        tooltip: assist.view ? qsTr("Choose a value for %1 (Alt+Down)").arg(assist.view.activeAddress) : ""
        x: assist.cell.x + assist.cell.width - width
        y: assist.cell.y + (assist.cell.height - height) / 2
        onClicked: assist.openList()
    }

    Tk.Menu {
        id: listMenu
        objectName: "sheetListMenu"
        Instantiator {
            model: assist.listItems
            delegate: Tk.MenuItem {
                required property var modelData
                text: String(modelData)
                onTriggered: assist.choose(String(modelData))
            }
            onObjectAdded: function(index, object) { listMenu.insertItem(index, object) }
            onObjectRemoved: function(index, object) { listMenu.removeItem(object) }
        }
    }

    Tk.MessageDialog {
        id: promptDialog
        objectName: "sheetValidationPrompt"
        readonly property var check: assist.pending ? assist.pending.check : ({})
        readonly property bool warning: check.style === "warning"
        title: String(check.title || "").length > 0 ? check.title : qsTr("Check this entry")
        text: warning ? qsTr("%1\n\nKeep this value?").arg(check.message || "") : String(check.message || "")
        variant: warning ? "warning" : "info"
        primaryText: warning ? qsTr("Yes") : qsTr("OK")
        secondaryText: warning ? qsTr("No") : qsTr("Cancel")
        tertiaryText: warning ? qsTr("Cancel") : ""
        onAccepted: assist.resolve("commit")
        onRejected: assist.resolve(warning ? "retry" : "cancel")
        onTertiary: {
            close()
            assist.resolve("cancel")
        }
    }
}
