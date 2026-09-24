// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import "publishing.js" as Fixture

// Publishing workbench (WP-051). One Tk.RichTextDocument owns every edit:
// StyleManager, TextFrameEditor, TableEditor and PageSetupDialog only send
// requests, and each accepted request is one document undo step. The page
// view shows the story flowing through linked text frames. The caret picks
// the style targets, the frame and the table cell shown in the panels, and
// choosing a frame or a cell in a panel moves the caret there, so the text,
// the page and the inspector never hold separate copies.
// Run: qtk-preview examples/document-publishing/Main.qml --size 1280x900
Rectangle {
    id: scene
    width: 1280
    height: 900
    color: Tk.Theme.color.bg

    property string status: qsTr("Loading")
    property string selectedStyleId: ""
    property string selectedFrameId: ""
    property var selectedCellIds: []
    property string tableError: ""
    readonly property var emptyTable: ({ id: "", rows: [], columns: [], cells: [], canMerge: false })
    readonly property var table: document.tables.length ? document.tables[0] : emptyTable
    readonly property var selectedStyle: styleById(selectedStyleId)
    // Frames as TextFrameEditor rows: the story's last frame reports the overset text.
    readonly property var frames: {
        let last = ""
        let index = -1
        for (const frame of document.frames)
            if (frame.storyIndex > index) { index = frame.storyIndex; last = frame.id }
        const overset = view.area.oversetCharacters
        return document.frames.map(function(frame) {
            const status = frame.storyIndex < 0 ? qsTr("not threaded")
                : frame.id === last && overset > 0 ? qsTr("overset")
                : qsTr("frame %1 of the story").arg(frame.storyIndex + 1)
            return Object.assign({}, frame, { storyId: frame.storyIndex >= 0 ? qsTr("press release") : qsTr("none"),
                                              oversetChars: frame.id === last ? overset : 0, flowStatus: status })
        })
    }

    function styleById(id) {
        for (const style of document.styles) if (style.id === id) return style
        return null
    }
    function cellById(id) {
        for (const cell of table.cells) if (cell.id === id) return cell
        return null
    }
    function cellOfParagraph(paragraphId) {
        for (const cell of table.cells) if (cell.paragraphIds.indexOf(paragraphId) >= 0) return cell
        return null
    }
    // Reports a request's outcome; a refusal shows the document's reason.
    function report(accepted, message) {
        status = accepted ? message : qsTr("Refused: %1").arg(document.lastError)
        return accepted
    }
    // Style requests reach the text through the view's commands, so the
    // selection and focus stay; table styles go to the table.
    function applyStyle(styleId) {
        const style = styleById(styleId)
        if (!style) return false
        if (style.kind === "table")
            return report(document.setTableStyle(table.id, styleId), qsTr("Applied %1 to the table").arg(style.name))
        const command = style.kind === "character" ? "text.style" : "paragraph.style"
        return report(view.area.invokeCommand(command, { value: styleId }), qsTr("Applied %1").arg(style.name))
    }
    // Redefines the selected style from the paragraph at the caret.
    function matchCaret() {
        const style = selectedStyle
        if (!style || style.kind === "table") return false
        const look = document.stylePatchFromParagraph(view.area.caretParagraphId)
        const patch = style.kind === "character" ? { character: look.character } : look
        return report(document.updateStyle(style.id, patch), qsTr("%1 now matches the caret paragraph").arg(style.name))
    }
    function selectCell(cellId, extend) {
        const cell = cellById(cellId)
        if (!cell) return
        if (extend) {
            const next = selectedCellIds.slice()
            const index = next.indexOf(cellId)
            if (index >= 0) next.splice(index, 1)
            else next.push(cellId)
            selectedCellIds = next
            return
        }
        selectedCellIds = [cellId]
        view.area.goToParagraph(cell.paragraphIds[0], 0)
    }
    function tableRequest(accepted, message) {
        tableError = accepted ? "" : document.lastError
        return report(accepted, message)
    }

    Tk.RichTextDocument {
        id: document
        Component.onCompleted: scene.report(loadJson(Fixture.documentJson()), qsTr("Loaded the press release"))
    }
    Connections {
        target: view.area
        // The caret in a frame or a table cell selects it in the inspector.
        function onCaretChanged() {
            if (view.area.caretFrameId.length) scene.selectedFrameId = view.area.caretFrameId
            const cell = scene.cellOfParagraph(view.area.caretParagraphId)
            if (cell && scene.selectedCellIds.indexOf(cell.id) < 0) scene.selectedCellIds = [cell.id]
        }
    }

    Tk.PageSetupDialog {
        id: pageSetupDialog
        pageSetup: document.pageSetupMm
        onPageSetupRequested: function(settings) {
            scene.report(document.applyPageSetupMm(settings), qsTr("Page setup applied"))
        }
        onPageSetupCancelled: scene.status = qsTr("Page setup cancelled · document unchanged")
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("PUBLISHING WORKBENCH"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Badge {
                objectName: "publishingPages"
                text: qsTr("%1 pages").arg(view.pageCount)
                variant: "default"
            }
            Tk.Badge {
                objectName: "publishingOverset"
                text: view.area.oversetCharacters > 0 ? qsTr("%1 characters overset").arg(view.area.oversetCharacters)
                                                      : qsTr("Story fits")
                variant: view.area.oversetCharacters > 0 ? "warning" : "accent"
            }
            Tk.IconButton {
                objectName: "publishingUndo"
                iconName: "undo-2"
                tooltip: document.canUndo ? qsTr("Undo %1").arg(document.undoText) : qsTr("Undo")
                enabled: document.canUndo
                onClicked: document.undo()
            }
            Tk.IconButton {
                objectName: "publishingRedo"
                iconName: "redo-2"
                tooltip: document.canRedo ? qsTr("Redo %1").arg(document.redoText) : qsTr("Redo")
                enabled: document.canRedo
                onClicked: document.redo()
            }
            Tk.Button {
                objectName: "openPageSetup"
                text: qsTr("Page setup…")
                iconName: "ruler"
                tooltip: qsTr("Change page size, margins and columns")
                onClicked: pageSetupDialog.open()
            }
        }

        Tk.Flex {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.lg

            Tk.RichDocumentView {
                id: view
                objectName: "publishingDocument"
                document: document
                zoom: 0.72
                tooltip: qsTr("Press release pages")
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
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
                    width: parent.width - Tk.Theme.size.scrollbar - Tk.Theme.space.xs
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    padding: Tk.Theme.space.xs

                    Tk.StyleManager {
                        id: styleManager
                        objectName: "publishingStyleManager"
                        width: parent.width
                        styles: document.styles
                        styleKinds: document.styleKinds
                        targetIds: styleManager.selectedStyle && styleManager.selectedStyle.kind === "table"
                                   ? (scene.table.id ? [scene.table.id] : []) : view.area.selectedParagraphIds
                        selectedStyleId: scene.selectedStyleId
                        directOverrides: {
                            document.revision // re-read after every edit
                            return document.directOverrides(view.area.selectedParagraphIds)
                        }
                        onStyleSelectionRequested: function(id) { scene.selectedStyleId = id }
                        onCreateStyleRequested: function(name, kind, parentId) {
                            const id = document.createStyle(name, kind, parentId)
                            if (scene.report(id.length > 0, qsTr("Created style %1").arg(name))) scene.selectedStyleId = id
                        }
                        onUpdateStyleRequested: function(id, patch) {
                            scene.report(document.updateStyle(id, { name: patch.name, parentId: patch.parentId }),
                                         qsTr("Updated style %1").arg(patch.name))
                        }
                        onApplyStyleRequested: function(id, targets) { scene.applyStyle(id) }
                        onResetOverridesRequested: function(targets, keys) {
                            scene.report(document.resetDirectFormatting(targets, keys), qsTr("Direct formatting reset"))
                        }
                    }
                    Tk.Button {
                        objectName: "publishingMatchCaret"
                        text: qsTr("Match caret paragraph")
                        iconName: "pipette"
                        tooltip: qsTr("Redefine the selected style from the paragraph at the caret")
                        enabled: scene.selectedStyle !== null && scene.selectedStyle.kind !== "table"
                        onClicked: scene.matchCaret()
                    }
                    Tk.Divider {}
                    Tk.TextFrameEditor {
                        objectName: "publishingTextFrameEditor"
                        width: parent.width
                        frames: scene.frames
                        selectedFrameId: scene.selectedFrameId
                        pageId: "page-" + (view.currentPage + 1)
                        newFrameGeometryMm: ({ xMm: 20, yMm: 150, widthMm: 80, heightMm: 60 })
                        onFrameSelectionRequested: function(id) {
                            scene.selectedFrameId = id
                            view.area.goToFrame(id)
                        }
                        onGeometryChangeRequested: function(id, geometry) {
                            scene.report(document.setTextFrameGeometry(id, geometry), qsTr("Frame moved or resized"))
                        }
                        onLayoutChangeRequested: function(id, layout) {
                            scene.report(document.setTextFrameLayout(id, layout), qsTr("Frame columns changed"))
                        }
                        onConnectRequested: function(sourceId, targetId) {
                            scene.report(document.connectTextFrames(sourceId, targetId), qsTr("Frames connected"))
                        }
                        onDisconnectRequested: function(id) {
                            scene.report(document.disconnectTextFrame(id), qsTr("Frame disconnected"))
                        }
                        onCreateFrameRequested: function(pageId, geometry) {
                            const id = document.createTextFrame(Number(pageId.slice(5)) - 1, geometry)
                            if (scene.report(id.length > 0, qsTr("Frame created"))) scene.selectedFrameId = id
                        }
                    }
                    Tk.Divider {}
                    Tk.TableEditor {
                        objectName: "publishingTableEditor"
                        width: parent.width
                        table: Object.assign({ validationMessage: scene.tableError }, scene.table)
                        // Cells merged away or deleted leave the selection.
                        selectedCellIds: scene.selectedCellIds.filter(function(id) { return scene.cellById(id) !== null })
                        selectedRowId: {
                            const cell = scene.cellById(scene.selectedCellIds[scene.selectedCellIds.length - 1] || "")
                            return cell ? cell.rowId : ""
                        }
                        selectedColumnId: {
                            const cell = scene.cellById(scene.selectedCellIds[scene.selectedCellIds.length - 1] || "")
                            return cell ? cell.columnId : ""
                        }
                        onCellSelectionRequested: function(id, extend) { scene.selectCell(id, extend) }
                        onCellTextChangeRequested: function(id, text) {
                            scene.tableRequest(document.setTableCellText(scene.table.id, id, text), qsTr("Cell text changed"))
                        }
                        onRowInsertRequested: function(index) {
                            scene.tableRequest(document.insertTableRow(scene.table.id, index), qsTr("Row inserted"))
                        }
                        onRowDeleteRequested: function(id) {
                            scene.tableRequest(document.removeTableRow(scene.table.id, id), qsTr("Row deleted"))
                        }
                        onColumnInsertRequested: function(index) {
                            scene.tableRequest(document.insertTableColumn(scene.table.id, index), qsTr("Column inserted"))
                        }
                        onColumnDeleteRequested: function(id) {
                            scene.tableRequest(document.removeTableColumn(scene.table.id, id), qsTr("Column deleted"))
                        }
                        onCellsMergeRequested: function(ids) {
                            if (scene.tableRequest(document.mergeTableCells(scene.table.id, ids), qsTr("Cells merged")))
                                scene.selectedCellIds = [ids[0]]
                        }
                        onCellSplitRequested: function(id) {
                            scene.tableRequest(document.splitTableCell(scene.table.id, id), qsTr("Cell split"))
                        }
                        onColumnWidthChangeRequested: function(id, width) {
                            scene.tableRequest(document.setTableColumnWidth(scene.table.id, id, width), qsTr("Column width changed"))
                        }
                        onTableStyleChangeRequested: function(id) {
                            scene.tableRequest(document.setTableStyle(scene.table.id, id), qsTr("Table style changed"))
                        }
                    }
                }
            }
        }
        Tk.Notice {
            objectName: "publishingHostStatus"
            text: scene.status
            variant: scene.status.startsWith(qsTr("Refused")) ? "warning" : "info"
        }
    }
}
