// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    objectName: "workbenchRoot"
    width: 1280
    height: 800
    color: Tk.Theme.color.bg
    Accessible.role: Accessible.Window
    Accessible.name: qsTr("Reference workbench")

    readonly property var manager: host.manager
    readonly property var commands: host.commands
    readonly property var boardSession: host.boardSession
    readonly property var sheetSession: host.sheetSession
    property string statusText: qsTr("Two reference documents are open")
    property string gridFilterText: ""
    property bool gridSortByName: false
    property bool restoringGridSelection: false
    property string gridDocumentId: ""
    property string pendingGridRow: ""
    property string pendingGridDocumentId: ""
    property int gridViewGeneration: 0
    property var rememberedRows: ({})
    property var protectedRows: ({})
    readonly property var activeSession: manager.activeSession
    readonly property bool boardActive: activeSession && activeSession === boardSession
    readonly property var activeGridModel: !activeSession ? null
        : boardActive ? host.boardGrid : host.sheetGrid
    readonly property var activeCollection: !activeSession ? null
        : boardActive ? host.boardAssets : host.sheetAssets
    readonly property var activeSelection: !activeSession ? null
        : boardActive ? boardSelection : sheetSelection
    readonly property string activeWorkspaceName: !activeSession ? qsTr("No document")
        : boardActive ? qsTr("Storyboard") : qsTr("Mix sheet")
    readonly property string inspectedRowId: activeGridModel
        ? activeGridModel.rowIndex(grid.activeRowId) >= 0 ? grid.activeRowId
                                                     : activeGridModel.rowIdAt(0) : ""
    readonly property string selectedName: {
        if (!activeGridModel || !inspectedRowId) return ""
        const revision = activeSession ? activeSession.document.revision : ""
        const value = activeSession ? activeSession.document.valueFor(inspectedRowId, "name") : ""
        return value === undefined || value === null ? "" : String(value)
    }
    readonly property int documentCount: manager.sessions.length
    readonly property string activeDocumentId: activeSession ? activeSession.documentId : ""

    function stageNameDraft() {
        return host.stageNameEdit(inspectedRowId, nameEditor.text)
    }
    function stageNameValue(value) {
        nameEditor.text = value
        return stageNameDraft()
    }
    function triggerRename() {
        return host.triggerRename()
    }
    function invokeRenameFromToolbar() {
        renameButton.button.click()
        return true
    }
    function invokeRenameFromMenu() {
        renameContextMenu.popup()
        renameContextItem.click()
        renameContextMenu.close()
        return true
    }
    function editGridCell(rowId, columnId, value) {
        if (!activeGridModel) return false
        const row = activeGridModel.rowIndex(rowId)
        const column = activeGridModel.columnIndex(columnId)
        if (row < 0 || column < 0) return false
        grid.setCurrent(row, column, false)
        return grid.beginCellEdit() && grid.commitCellEdit(value)
    }
    function pasteGridRows(firstRow, rows) {
        return grid.pasteMatrix(firstRow, 0, rows)
    }
    function saveActive() {
        if (!activeSession) return
        const path = lifecyclePanel.documentPath.trim()
        const started = activeSession.uri.length
            ? activeSession.requestSave() : activeSession.requestSaveAs(path, false)
        statusText = started ? qsTr("Saving native reference document")
                             : activeSession.lastError
    }
    function selectWorkspace(session) {
        if (session) manager.activate(session.documentId)
    }
    function applyGridView() {
        if (!activeGridModel || !activeSession) return
        const docId = activeSession.documentId
        const allRows = host.rowIdsForDocument(docId)
        let wanted = protectedRows[docId] || rememberedRows[docId] || grid.activeRowId
        if (!wanted || allRows.indexOf(wanted) < 0) wanted = allRows.length ? allRows[0] : ""
        const needle = gridFilterText.trim().toLowerCase()
        let visibleRows = allRows.filter(function(rowId) {
            if (!needle) return true
            for (let column = 0; column < activeGridModel.columnCount; ++column) {
                const columnId = activeGridModel.columnIdAt(column)
                if (String(activeSession.document.valueFor(rowId, columnId)).toLowerCase().includes(needle))
                    return true
            }
            return false
        })
        if (gridSortByName) visibleRows.sort(function(left, right) {
            const a = String(activeSession.document.valueFor(left, "name")).toLowerCase()
            const b = String(activeSession.document.valueFor(right, "name")).toLowerCase()
            return a < b ? -1 : a > b ? 1 : left.localeCompare(right)
        })
        restoringGridSelection = true
        gridViewGeneration += 1
        if (wanted) protectedRows[docId] = wanted
        activeGridModel.setRowIds(visibleRows)
        const isVisible = visibleRows.indexOf(wanted) >= 0
        pendingGridRow = isVisible ? wanted : ""
        pendingGridDocumentId = docId
        Qt.callLater(restorePendingGridSelection, gridViewGeneration)
    }
    function restorePendingGridSelection(generation) {
        if (generation !== gridViewGeneration) return
        if (pendingGridDocumentId === activeDocumentId && pendingGridRow) {
            const index = activeGridModel.rowIndex(pendingGridRow)
            if (index >= 0) {
                grid.setCurrent(index, Math.max(0, activeGridModel.columnIndex("name")), false)
                rememberedRows[pendingGridDocumentId] = pendingGridRow
            }
        }
        restoringGridSelection = false
    }
    function rememberGridRow(rowId) {
        if (!restoringGridSelection && gridDocumentId === activeDocumentId && gridDocumentId && rowId
                && (!protectedRows[gridDocumentId] || protectedRows[gridDocumentId] === rowId)) {
            rememberedRows[gridDocumentId] = rowId
        }
    }
    function setGridFilter(value) {
        gridFilterText = value
        applyGridView()
        return true
    }
    function toggleGridSort(unused) {
        gridSortByName = !gridSortByName
        applyGridView()
        return true
    }
    function selectGridRow(rowId) {
        const index = activeGridModel ? activeGridModel.rowIndex(rowId) : -1
        if (index < 0) return false
        grid.setCurrent(index, Math.max(0, activeGridModel.columnIndex("name")), false)
        rememberedRows[activeDocumentId] = rowId
        protectedRows[activeDocumentId] = rowId
        gridViewGeneration += 1
        return grid.activeRowId === rowId
    }

    Tk.SelectionModel { id: boardSelection }
    Tk.SelectionModel { id: sheetSelection }
    Tk.PropertyBinding {
        id: nameBinding
        objectName: "workbenchNameBinding"
        document: root.activeSession ? root.activeSession.document : null
        undoService: manager.undoService
        propertyId: "name"
        targetIds: root.inspectedRowId ? [root.inspectedRowId] : []
    }
    Connections {
        target: manager
        function onCloseBlocked(documentId, reason) {
            root.statusText = qsTr("Close kept %1 open: %2").arg(documentId).arg(reason)
        }
    }
    Connections {
        target: root.activeSession ? root.activeSession.document : null
        function onChanged() { grid.dataEpoch += 1 }
    }
    onSelectedNameChanged: {
        nameEditor.text = selectedName
        stageNameDraft()
    }
    onInspectedRowIdChanged: stageNameDraft()
    Connections {
        target: root.activeSession ? root.activeSession.document : null
        function onPropertyChanged() {
            if (root.gridSortByName || root.gridFilterText)
                Qt.callLater(root.applyGridView)
        }
    }
    Connections {
        target: root.activeSession
        function onSaveFinished(success, message, savedRevision) {
            root.statusText = success ? qsTr("Saved revision %1").arg(savedRevision)
                                      : qsTr("Save failed; recovery is available: %1").arg(message)
        }
    }
    onActiveDocumentIdChanged: {
        restoringGridSelection = true
        gridDocumentId = activeDocumentId
        Qt.callLater(applyGridView)
    }
    Component.onCompleted: {
        gridDocumentId = activeDocumentId
        Qt.callLater(applyGridView)
    }

    Tk.ShortcutMap {
        id: workbenchShortcuts
        objectName: "workbenchShortcutMap"
        registry: root.commands
        window: root.Window.window
        Component.onCompleted: bindCommand("workbench.rename")
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.ToolBar {
            objectName: "workbenchCommandBar"
            width: parent.width
            Tk.Overline { title: qsTr("WORKBENCH"); wide: true; color: Tk.Theme.color.accent }
            Tk.ToolSeparator {}
            Tk.Button {
                objectName: "workbenchBoardAction"
                text: qsTr("Storyboard")
                variant: root.boardActive ? "outline" : "ghost"
                tooltip: qsTr("Activate the storyboard document")
                onClicked: root.selectWorkspace(root.boardSession)
            }
            Tk.Button {
                objectName: "workbenchSheetAction"
                text: qsTr("Mix sheet")
                variant: !root.boardActive ? "outline" : "ghost"
                tooltip: qsTr("Activate the mix sheet document")
                onClicked: root.selectWorkspace(root.sheetSession)
            }
            Tk.ToolSeparator {}
            Tk.Button {
                objectName: "workbenchUndoAction"
                text: qsTr("Undo")
                iconName: "undo-2"
                tooltip: qsTr("Undo one edit in the active reference document")
                enabled: manager.undoService.canUndo
                onClicked: manager.undoService.undo()
            }
            Tk.Button {
                objectName: "workbenchRedoAction"
                text: qsTr("Redo")
                iconName: "redo-2"
                tooltip: qsTr("Redo one edit in the active reference document")
                enabled: manager.undoService.canRedo
                onClicked: manager.undoService.redo()
            }
            Tk.CommandButton {
                id: renameButton
                objectName: "workbenchRenameAction"
                registry: root.commands
                shortcutMap: workbenchShortcuts
                commandId: "workbench.rename"
                tooltip: qsTr("Apply the inspector Name edit")
            }
            Tk.Menu {
                id: renameContextMenu
                objectName: "workbenchRenameContextMenu"
                Tk.MenuItem {
                    id: renameContextItem
                    objectName: "workbenchRenameMenuItem"
                    commandRegistry: root.commands
                    shortcutMap: workbenchShortcuts
                    commandId: "workbench.rename"
                }
            }
            Tk.Spacer {}
            Tk.Button {
                objectName: "workbenchSaveAction"
                text: qsTr("Save reference")
                iconName: "save"
                variant: "accent"
                tooltip: qsTr("Save the active native reference document")
                enabled: !!root.activeSession && !root.activeSession.saving
                onClicked: root.saveActive()
            }
        }

        Tk.DocumentTabs {
            id: tabs
            objectName: "workbenchDocumentTabs"
            width: parent.width
            documentManager: manager
            tooltip: qsTr("Reference document sessions")
            onCloseRequested: function(documentId, viewId) {
                root.statusText = qsTr("Dirty document kept open; use the explicit close action to decide.")
                tabs.completeClose(documentId, viewId, Tk.DocumentManager.Cancel)
            }
        }

        Tk.Flex {
            objectName: "workbenchPanels"
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            paddingLeft: Tk.Theme.space.sm
            paddingRight: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0

            Tk.Box {
                objectName: "workbenchCollectionPanel"
                color: Tk.Theme.color.panel
                borderColor: Tk.Theme.color.divider
                borderWidth: Tk.Theme.size.border
                Tk.Flex.basis: Tk.Theme.size.fieldWidth * 1.45
                Tk.Flex.shrink: 0
                Tk.Flex.minHeight: 0
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    padding: Tk.Theme.space.sm
                    Tk.Overline { title: qsTr("COLLECTION"); wide: true }
                    Tk.Caption { text: root.activeWorkspaceName + qsTr(" assets") }
                    Tk.AssetGrid {
                        id: collection
                        objectName: "workbenchAssetGrid"
                        model: root.activeCollection
                        selectionModel: root.activeSelection
                        tooltip: qsTr("Assets in the active reference workspace")
                        tileWidth: Tk.Theme.size.thumbnail * 1.5
                        tileHeight: Tk.Theme.size.thumbnailHeight + Tk.Theme.size.row
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        Tk.Flex.minHeight: 0
                        onActivated: function(id) {
                            const row = root.activeGridModel.rowIndex(id)
                            if (row >= 0) grid.setCurrent(row, 0, false)
                            root.statusText = qsTr("Selected asset %1").arg(id)
                        }
                    }
                }
            }

            Tk.Box {
                objectName: "workbenchGridPanel"
                color: Tk.Theme.color.panel
                borderColor: Tk.Theme.color.divider
                borderWidth: Tk.Theme.size.border
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.minWidth: 0
                Tk.Flex.minHeight: 0
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    padding: Tk.Theme.space.sm
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        Tk.Overline { title: root.activeWorkspaceName.toUpperCase(); wide: true }
                        Tk.Spacer {}
                        Tk.Badge {
                            text: !root.activeSession ? qsTr("No document")
                                : root.activeSession.dirty ? qsTr("Dirty") : qsTr("Clean")
                            variant: root.activeSession && root.activeSession.dirty ? "warning" : "default"
                        }
                    }
                    Tk.Caption { text: qsTr("Document rows · Edit a cell or use the Inspector") }
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.xs
                        Tk.TextField {
                            objectName: "workbenchGridFilter"
                            placeholderText: qsTr("Filter rows")
                            tooltip: qsTr("Filter the active document grid")
                            Accessible.name: qsTr("Filter rows")
                            text: root.gridFilterText
                            onTextChanged: root.setGridFilter(text)
                            Tk.Flex.grow: 1
                        }
                        Tk.Button {
                            objectName: "workbenchGridSort"
                            text: root.gridSortByName ? qsTr("Name ↑") : qsTr("Sort name")
                            tooltip: qsTr("Sort rows by Name")
                            Accessible.name: tooltip
                            onClicked: root.toggleGridSort()
                        }
                    }
                    Tk.EditableGrid {
                        id: grid
                        objectName: "workbenchEditableGrid"
                        model: root.activeGridModel
                        tooltip: qsTr("Editable table in the active reference workspace")
                        alternatingRows: true
                        onCurrentCellChanged: function(rowId) { root.rememberGridRow(rowId) }
                        onModelChanged: Qt.callLater(root.applyGridView)
                        Keys.onPressed: function(event) {
                            delete root.protectedRows[root.activeDocumentId]
                        }
                        onActiveFocusChanged: {
                            if (activeFocus) delete root.protectedRows[root.activeDocumentId]
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: delete root.protectedRows[root.activeDocumentId]
                        }
                        columnLabel: function(id, index) {
                            const names = [qsTr("Name"), qsTr("Kind"), qsTr("Owner"), qsTr("State")]
                            return index < names.length ? names[index] : id
                        }
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        Tk.Flex.minHeight: 0
                    }
                }
            }

            Tk.Box {
                objectName: "workbenchInspectorPanel"
                color: Tk.Theme.color.panel
                borderColor: Tk.Theme.color.divider
                borderWidth: Tk.Theme.size.border
                Tk.Flex.basis: Tk.Theme.size.fieldWidth * 1.7
                Tk.Flex.shrink: 0
                Tk.Flex.minHeight: 0
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    padding: Tk.Theme.space.sm
                    Tk.Overline { title: qsTr("INSPECTOR"); wide: true }
                    Tk.PropertyGroup {
                        objectName: "workbenchDocumentProperties"
                        title: qsTr("Document")
                        collapsible: false
                        Tk.PropertyRow {
                            label: qsTr("Workspace")
                            Tk.Caption { text: root.activeWorkspaceName }
                        }
                        Tk.PropertyRow {
                            label: qsTr("Revision")
                            Tk.Caption { text: root.activeSession ? root.activeSession.revision : "" }
                        }
                        Tk.PropertyRow {
                            label: qsTr("Session")
                            Tk.Caption {
                                text: root.activeDocumentId
                                elide: Text.ElideMiddle
                                Accessible.name: root.activeDocumentId
                            }
                        }
                    }
                    Tk.PropertyGroup {
                        objectName: "workbenchRowProperties"
                        title: qsTr("Selected row")
                        collapsible: false
                        Tk.PropertyRow {
                            label: qsTr("Row ID")
                            Tk.Caption { text: root.inspectedRowId }
                        }
                        Tk.PropertyRow {
                            label: qsTr("Name")
                            Tk.TextField {
                                id: nameEditor
                                objectName: "workbenchNameEditor"
                                text: root.selectedName
                                tooltip: qsTr("Rename the selected grid row")
                                Accessible.name: qsTr("Name")
                                onTextChanged: root.stageNameDraft()
                                onAccepted: root.triggerRename()
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: renameContextMenu.popup()
                                }
                            }
                        }
                    }
                    WorkbenchLifecyclePanel {
                        id: lifecyclePanel
                        width: parent.width
                        host: root.host
                        activeSession: root.activeSession
                        activeDocumentId: root.activeDocumentId
                        inspectedRowId: root.inspectedRowId
                        onStatusRequested: function(message) { root.statusText = message }
                    }
                }
            }
        }

        Tk.ToolBar {
            objectName: "workbenchStatusBar"
            width: parent.width
            edge: "top"
            Tk.Caption { text: root.statusText + (host.lastJobMessage ? qsTr(" · ") + host.lastJobMessage : ""); elide: Text.ElideRight; Tk.Flex.grow: 1 }
            Tk.Caption { text: qsTr("%1 documents").arg(root.documentCount) }
        }
    }
}
