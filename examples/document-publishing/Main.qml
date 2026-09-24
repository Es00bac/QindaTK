// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A tiny publishing host. The QML panels send stable-ID requests; this host
// applies the accepted changes to page, style, frame and table snapshots.
Rectangle {
    id: scene
    width: 1280
    height: 900
    color: Tk.Theme.color.bg

    QtObject {
        id: document
        property int revision: 0
        property string lastRequest: qsTr("Ready · sample document has one selected story")
        property string pageId: "page-1"
        property string selectedFrameId: "frame-1"
        property string selectedRowId: "row-1"
        property string selectedColumnId: "column-1"
        property var selectedCellIds: ["cell-1-1"]
        property var pageSetup: ({
            widthMm: 210, heightMm: 297, unit: "mm", orientation: "portrait", scope: "document",
            marginsMm: { top: 18, right: 18, bottom: 20, left: 22 }, gutterMm: 8, columns: 2, bleedMm: 3
        })
        property var styleRows: [
            { id: "body", name: "Body", kind: "paragraph", parentId: "", summary: "Main reading style", preview: "The quick brown fox crosses a quiet page.", compatible: true, builtIn: true },
            { id: "heading-1", name: "Heading 1", kind: "paragraph", parentId: "body", summary: "Section heading", preview: "A new section begins here.", compatible: true, builtIn: false },
            { id: "emphasis", name: "Emphasis", kind: "character", parentId: "", summary: "Italic emphasis", preview: "words that carry emphasis", compatible: true, builtIn: false },
            { id: "table-body", name: "Table body", kind: "table", parentId: "body", summary: "Compact data cells", preview: "Label · Value", compatible: false, compatibilityReason: qsTr("Select a table cell to apply a table style."), builtIn: true }
        ]
        property string selectedStyleId: "body"
        property var targetIds: ["paragraph-1"]
        property var directOverrides: [{ key: "fontSize", label: "Font size", value: "12 pt" }, { key: "ink", label: "Text colour", value: "Accent" }]
        property string appliedStyleId: "body"
        property var frames: [
            { id: "frame-1", label: "Frame 1 · page 1", pageId: "page-1", xMm: 22, yMm: 18, widthMm: 78, heightMm: 105, storyId: "story-1", nextFrameId: "frame-2", columns: 1, gutterMm: 6, insetMm: 4, oversetChars: 68, flowStatus: "overset" },
            { id: "frame-2", label: "Frame 2 · page 1", pageId: "page-1", xMm: 110, yMm: 18, widthMm: 78, heightMm: 105, storyId: "story-1", nextFrameId: "", columns: 1, gutterMm: 6, insetMm: 4, oversetChars: 0, flowStatus: "receiving story" }
        ]
        property var table: ({
            id: "table-1", styleId: "grid", maxRows: 40, maxColumns: 12, canMerge: true,
            styles: [{ id: "grid", label: "Simple grid" }, { id: "banded", label: "Banded rows" }],
            rows: [{ id: "row-1", label: "Row 1" }, { id: "row-2", label: "Row 2" }],
            columns: [{ id: "column-1", label: "Item", width: 42 }, { id: "column-2", label: "Quantity", width: 28 }, { id: "column-3", label: "Notes", width: 64 }],
            cells: [
                { id: "cell-1-1", rowId: "row-1", columnId: "column-1", rowIndex: 0, columnIndex: 0, text: "Paper", rowSpan: 1, columnSpan: 1 },
                { id: "cell-1-2", rowId: "row-1", columnId: "column-2", rowIndex: 0, columnIndex: 1, text: "240 gsm", rowSpan: 1, columnSpan: 1 },
                { id: "cell-1-3", rowId: "row-1", columnId: "column-3", rowIndex: 0, columnIndex: 2, text: "Uncoated", rowSpan: 1, columnSpan: 1 },
                { id: "cell-2-1", rowId: "row-2", columnId: "column-1", rowIndex: 1, columnIndex: 0, text: "Trim", rowSpan: 1, columnSpan: 1 },
                { id: "cell-2-2", rowId: "row-2", columnId: "column-2", rowIndex: 1, columnIndex: 1, text: "A4", rowSpan: 1, columnSpan: 1 },
                { id: "cell-2-3", rowId: "row-2", columnId: "column-3", rowIndex: 1, columnIndex: 2, text: "3 mm bleed", rowSpan: 1, columnSpan: 1 }
            ]
        })

        function changed(message) {
            ++revision
            lastRequest = qsTr("%1 · revision %2").arg(message).arg(revision)
        }
        function styleById(id) {
            for (let i = 0; i < styleRows.length; ++i) if (styleRows[i].id === id) return styleRows[i]
            return null
        }
        function createStyle(name, kind, parentId) {
            const id = "style-" + (styleRows.length + 1)
            styleRows = styleRows.concat([{ id: id, name: name, kind: kind, parentId: parentId, summary: qsTr("Custom %1 style").arg(kind), preview: name, compatible: true, builtIn: false }])
            selectedStyleId = id
            changed(qsTr("Created style %1").arg(name))
        }
        function updateStyle(id, patch) {
            styleRows = styleRows.map(function(style) { return style.id === id ? Object.assign({}, style, patch) : style })
            changed(qsTr("Updated style %1").arg(id))
        }
        function applyStyle(id, targets) {
            if (!styleById(id) || !targets.length) return
            appliedStyleId = id
            directOverrides = []
            changed(qsTr("Applied %1 to %2 target(s)").arg(id).arg(targets.length))
        }
        function resetOverrides(targets, keys) {
            if (!targets.length || !keys.length) return
            directOverrides = []
            changed(qsTr("Reset %1 direct override(s)").arg(keys.length))
        }
        function applyPageSetup(settings) {
            pageSetup = settings
            changed(qsTr("Applied %1 page setup to %2").arg(settings.scope).arg(pageId))
        }
        function selectFrame(id) { selectedFrameId = id }
        function updateFrameGeometry(id, geometry) {
            frames = frames.map(function(frame) { return frame.id === id ? Object.assign({}, frame, geometry) : frame })
            changed(qsTr("Resized %1").arg(id))
        }
        function updateFrameLayout(id, layout) {
            frames = frames.map(function(frame) { return frame.id === id ? Object.assign({}, frame, layout) : frame })
            changed(qsTr("Updated columns and inset for %1").arg(id))
        }
        function connectFrames(sourceId, targetId) {
            frames = frames.map(function(frame) { return frame.id === sourceId ? Object.assign({}, frame, { nextFrameId: targetId, flowStatus: "threaded" }) : frame })
            changed(qsTr("Connected %1 → %2").arg(sourceId).arg(targetId))
        }
        function disconnectFrame(sourceId) {
            frames = frames.map(function(frame) { return frame.id === sourceId ? Object.assign({}, frame, { nextFrameId: "", flowStatus: "overset" }) : frame })
            changed(qsTr("Disconnected %1").arg(sourceId))
        }
        function createFrame(targetPageId, geometry) {
            const id = "frame-" + (frames.length + 1)
            frames = frames.concat([Object.assign({ id: id, label: qsTr("Frame %1 · page 1").arg(frames.length + 1), pageId: targetPageId, storyId: "story-1", nextFrameId: "", columns: 1, gutterMm: 6, insetMm: 4, oversetChars: 0, flowStatus: "new frame" }, geometry)])
            selectedFrameId = id
            changed(qsTr("Created %1").arg(id))
        }
        function cellById(id) {
            for (let i = 0; i < table.cells.length; ++i) if (table.cells[i].id === id) return table.cells[i]
            return null
        }
        function selectCell(id, extend) {
            const next = extend ? selectedCellIds.slice() : []
            const index = next.indexOf(id)
            if (index >= 0) next.splice(index, 1)
            else next.push(id)
            selectedCellIds = next
            const cell = cellById(id)
            if (cell) { selectedRowId = cell.rowId; selectedColumnId = cell.columnId }
        }
        function insertRow(index) {
            index = Math.max(0, Math.min(index, table.rows.length))
            const next = Object.assign({}, table)
            const rowId = "row-" + (table.rows.length + 1)
            next.rows = table.rows.slice()
            next.rows.splice(index, 0, { id: rowId, label: qsTr("Row %1").arg(table.rows.length + 1) })
            next.cells = table.cells.map(function(cell) { return Object.assign({}, cell, { rowIndex: cell.rowIndex >= index ? cell.rowIndex + 1 : cell.rowIndex }) })
            table.columns.forEach(function(column, columnIndex) {
                next.cells.push({ id: "cell-" + rowId + "-" + column.id, rowId: rowId, columnId: column.id, rowIndex: index, columnIndex: columnIndex, text: "", rowSpan: 1, columnSpan: 1 })
            })
            table = next
            selectedRowId = rowId
            changed(qsTr("Inserted %1").arg(rowId))
        }
        function deleteRow(rowId) {
            if (table.rows.length <= 1) return
            const index = table.rows.findIndex(function(row) { return row.id === rowId })
            const next = Object.assign({}, table)
            next.rows = table.rows.filter(function(row) { return row.id !== rowId })
            next.cells = table.cells.filter(function(cell) { return cell.rowId !== rowId }).map(function(cell) { return Object.assign({}, cell, { rowIndex: cell.rowIndex > index ? cell.rowIndex - 1 : cell.rowIndex }) })
            table = next
            selectedRowId = next.rows[Math.max(0, index - 1)].id
            changed(qsTr("Deleted %1").arg(rowId))
        }
        function insertColumn(index) {
            index = Math.max(0, Math.min(index, table.columns.length))
            const next = Object.assign({}, table)
            const columnId = "column-" + (table.columns.length + 1)
            next.columns = table.columns.slice()
            next.columns.splice(index, 0, { id: columnId, label: qsTr("Column %1").arg(table.columns.length + 1), width: 36 })
            next.cells = table.cells.map(function(cell) { return Object.assign({}, cell, { columnIndex: cell.columnIndex >= index ? cell.columnIndex + 1 : cell.columnIndex }) })
            table.rows.forEach(function(row, rowIndex) { next.cells.push({ id: "cell-" + row.id + "-" + columnId, rowId: row.id, columnId: columnId, rowIndex: rowIndex, columnIndex: index, text: "", rowSpan: 1, columnSpan: 1 }) })
            table = next
            selectedColumnId = columnId
            changed(qsTr("Inserted %1").arg(columnId))
        }
        function deleteColumn(columnId) {
            if (table.columns.length <= 1) return
            const index = table.columns.findIndex(function(column) { return column.id === columnId })
            const next = Object.assign({}, table)
            next.columns = table.columns.filter(function(column) { return column.id !== columnId })
            next.cells = table.cells.filter(function(cell) { return cell.columnId !== columnId }).map(function(cell) { return Object.assign({}, cell, { columnIndex: cell.columnIndex > index ? cell.columnIndex - 1 : cell.columnIndex }) })
            table = next
            selectedColumnId = next.columns[Math.max(0, index - 1)].id
            changed(qsTr("Deleted %1").arg(columnId))
        }
        function mergeCells(cellIds) {
            if (cellIds.length < 2) return
            const chosen = cellIds.map(cellById).filter(function(cell) { return cell !== null })
            if (chosen.length < 2) return
            const first = chosen[0]
            const count = chosen.filter(function(cell) { return cell.rowId === first.rowId }).length
            if (count !== chosen.length) return
            const removed = chosen.slice(1).map(function(cell) { return cell.id })
            const next = Object.assign({}, table)
            next.cells = table.cells.filter(function(cell) { return removed.indexOf(cell.id) < 0 }).map(function(cell) {
                return cell.id === first.id ? Object.assign({}, cell, { columnSpan: chosen.length }) : cell
            })
            table = next
            selectedCellIds = [first.id]
            changed(qsTr("Merged %1 adjacent cells").arg(chosen.length))
        }
        function splitCell(cellId) {
            const source = cellById(cellId)
            if (!source || (source.columnSpan || 1) < 2) return
            const next = Object.assign({}, table)
            const span = source.columnSpan
            next.cells = table.cells.map(function(cell) { return cell.id === cellId ? Object.assign({}, cell, { columnSpan: 1 }) : Object.assign({}, cell, { columnIndex: cell.columnIndex > source.columnIndex ? cell.columnIndex + span - 1 : cell.columnIndex }) })
            for (let offset = 1; offset < span; ++offset) next.cells.push({ id: cellId + "-split-" + offset, rowId: source.rowId, columnId: table.columns[source.columnIndex + offset].id, rowIndex: source.rowIndex, columnIndex: source.columnIndex + offset, text: "", rowSpan: 1, columnSpan: 1 })
            table = next
            selectedCellIds = [cellId]
            changed(qsTr("Split merged cell"))
        }
        function updateCellText(id, text) {
            table = Object.assign({}, table, { cells: table.cells.map(function(cell) { return cell.id === id ? Object.assign({}, cell, { text: text }) : cell }) })
            changed(qsTr("Edited %1").arg(id))
        }
        function resizeColumn(id, width) {
            table = Object.assign({}, table, { columns: table.columns.map(function(column) { return column.id === id ? Object.assign({}, column, { width: width }) : column }) })
            changed(qsTr("Resized %1").arg(id))
        }
        function setTableStyle(id) {
            table = Object.assign({}, table, { styleId: id })
            changed(qsTr("Applied table style %1").arg(id))
        }
    }

    QtObject {
        id: frameGeometry
        readonly property var defaults: ({ xMm: 22, yMm: 150, widthMm: 78, heightMm: 90 })
    }

    Tk.PageSetupDialog {
        id: pageSetupDialog
        pageSetup: document.pageSetup
        onPageSetupRequested: function(settings) { document.applyPageSetup(settings) }
        onPageSetupCancelled: document.lastRequest = qsTr("Page setup cancelled · document unchanged")
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.md
            Tk.Overline { title: qsTr("PUBLISHING WORKBENCH"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Badge { text: qsTr("%1 × %2 mm").arg(document.pageSetup.widthMm).arg(document.pageSetup.heightMm); variant: "accent" }
            Tk.Badge { text: qsTr("Revision %1").arg(document.revision); variant: "default" }
            Tk.Button {
                objectName: "openPageSetup"
                text: qsTr("Page setup…")
                iconName: "file-cog"
                tooltip: qsTr("Change page geometry and section scope")
                onClicked: pageSetupDialog.open()
            }
        }

        Tk.Flex {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.lg

            Tk.Box {
                objectName: "publishingDocumentHost"
                color: Tk.Theme.color.panel
                borderColor: Tk.Theme.color.divider
                padding: Tk.Theme.space.md
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0

                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    Tk.SectionHeader { title: qsTr("Press release"); count: qsTr("Page 1") }
                    Item {
                        id: pageStage
                        objectName: "publishingPageStage"
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        Rectangle {
                            id: page
                            anchors.centerIn: parent
                            height: parent.height - Tk.Theme.space.xl * 2
                            width: Math.min(parent.width - Tk.Theme.space.xl * 2, height * document.pageSetup.widthMm / document.pageSetup.heightMm)
                            color: Tk.Theme.color.surface
                            border.width: Tk.Theme.size.border
                            border.color: Tk.Theme.color.divider
                            Rectangle {
                                objectName: "publishingTextFrame"
                                x: parent.width * document.frames[0].xMm / document.pageSetup.widthMm
                                y: parent.height * document.frames[0].yMm / document.pageSetup.heightMm
                                width: parent.width * document.frames[0].widthMm / document.pageSetup.widthMm
                                height: parent.height * document.frames[0].heightMm / document.pageSetup.heightMm
                                color: Tk.Theme.color.inputBg
                                border.width: Tk.Theme.size.border
                                border.color: document.selectedFrameId === "frame-1" ? Tk.Theme.color.accent : Tk.Theme.color.borderStrong
                                Tk.Label {
                                    anchors.fill: parent
                                    anchors.margins: Tk.Theme.space.sm
                                    text: "New edition reaches readers across the city"
                                    color: Tk.Theme.color.text
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: Tk.Theme.font.body
                                }
                            }
                            Rectangle {
                                objectName: "publishingLinkedFrame"
                                x: parent.width * document.frames[1].xMm / document.pageSetup.widthMm
                                y: parent.height * document.frames[1].yMm / document.pageSetup.heightMm
                                width: parent.width * document.frames[1].widthMm / document.pageSetup.widthMm
                                height: parent.height * document.frames[1].heightMm / document.pageSetup.heightMm
                                color: Tk.Theme.color.panelAlt
                                border.width: Tk.Theme.size.border
                                border.color: document.selectedFrameId === "frame-2" ? Tk.Theme.color.accent : Tk.Theme.color.borderStrong
                                Tk.Caption {
                                    anchors { left: parent.left; right: parent.right; top: parent.top }
                                    anchors.margins: Tk.Theme.space.sm
                                    text: qsTr("Linked story continues")
                                    wrapMode: Text.WordWrap
                                }
                            }
                            Tk.Caption {
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                anchors.margins: Tk.Theme.space.md
                                text: qsTr("Applied style: %1 · %2 frames · %3 table rows").arg(document.styleById(document.appliedStyleId).name).arg(document.frames.length).arg(document.table.rows.length)
                                mono: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                    Tk.Notice { objectName: "publishingHostStatus"; text: document.lastRequest; variant: "info" }
                }
            }

            Tk.Scroll {
                objectName: "publishingInspectorScroll"
                width: 450
                Tk.Flex.basis: 450
                Tk.Flex.shrink: 0
                Tk.Flex.minWidth: 380
                overflowY: Tk.Scroll.Auto
                accessibleName: qsTr("Publishing inspector")

                Tk.Flex {
                    width: parent.width
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    padding: Tk.Theme.space.xs

                    Tk.StyleManager {
                        objectName: "publishingStyleManager"
                        width: parent.width
                        styles: document.styleRows
                        targetIds: document.targetIds
                        selectedStyleId: document.selectedStyleId
                        directOverrides: document.directOverrides
                        onStyleSelectionRequested: function(id) { document.selectedStyleId = id }
                        onCreateStyleRequested: function(name, kind, parentId) { document.createStyle(name, kind, parentId) }
                        onUpdateStyleRequested: function(id, patch) { document.updateStyle(id, patch) }
                        onApplyStyleRequested: function(id, targets) { document.applyStyle(id, targets) }
                        onResetOverridesRequested: function(targets, keys) { document.resetOverrides(targets, keys) }
                    }
                    Tk.Divider {}
                    Tk.TextFrameEditor {
                        objectName: "publishingTextFrameEditor"
                        width: parent.width
                        frames: document.frames
                        selectedFrameId: document.selectedFrameId
                        pageId: document.pageId
                        newFrameGeometryMm: frameGeometry.defaults
                        onFrameSelectionRequested: function(id) { document.selectFrame(id) }
                        onGeometryChangeRequested: function(id, geometry) { document.updateFrameGeometry(id, geometry) }
                        onLayoutChangeRequested: function(id, layout) { document.updateFrameLayout(id, layout) }
                        onConnectRequested: function(sourceId, targetId) { document.connectFrames(sourceId, targetId) }
                        onDisconnectRequested: function(id) { document.disconnectFrame(id) }
                        onCreateFrameRequested: function(pageId, geometry) { document.createFrame(pageId, geometry) }
                    }
                    Tk.Divider {}
                    Tk.TableEditor {
                        objectName: "publishingTableEditor"
                        width: parent.width
                        table: document.table
                        selectedCellIds: document.selectedCellIds
                        selectedRowId: document.selectedRowId
                        selectedColumnId: document.selectedColumnId
                        onCellSelectionRequested: function(id, extend) { document.selectCell(id, extend) }
                        onCellTextChangeRequested: function(id, text) { document.updateCellText(id, text) }
                        onRowInsertRequested: function(index) { document.insertRow(index) }
                        onRowDeleteRequested: function(id) { document.deleteRow(id) }
                        onColumnInsertRequested: function(index) { document.insertColumn(index) }
                        onColumnDeleteRequested: function(id) { document.deleteColumn(id) }
                        onCellsMergeRequested: function(ids) { document.mergeCells(ids) }
                        onCellSplitRequested: function(id) { document.splitCell(id) }
                        onColumnWidthChangeRequested: function(id, width) { document.resizeColumn(id, width) }
                        onTableStyleChangeRequested: function(id) { document.setTableStyle(id) }
                    }
                }
            }
        }
        Tk.Caption {
            text: qsTr("Style, page, frame and table operations are applied by this host and advance its revision.")
            wrapMode: Text.WordWrap
        }
    }
}
