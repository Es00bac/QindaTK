// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: ResourcePlanModel owns allocation, capacity, unit, conflict
// and history state; this view owns stable-id focus and pixel scroll offsets.
T.Control {
    id: table
    objectName: "resourceTable"
    property Tk.ResourcePlanModel model: null
    property string tooltip: qsTr("Resource allocation table")
    property string activeResourceId: ""
    property string activeBucketId: ""
    property real scrollX: 0
    property real scrollY: 0
    property real resourceColumnWidth: Tk.Theme.size.tableColumn * 2.5
    property real bucketColumnWidth: Tk.Theme.size.tableColumn
    property real headerHeight: Tk.Theme.size.header
    // false leaves unallocated (zero) cells blank, as usage sheets do.
    property bool showZeros: true
    // The row header's figure: "capacity" = usage/capacity unit (default),
    // "usage" = usage and unit only, "none" = the name alone.
    property string summaryMode: "capacity"
    property bool editing: false
    property string editText: ""
    property string editingResourceId: ""
    property string editingBucketId: ""
    property string scrollAnchorResourceId: ""
    property string scrollAnchorBucketId: ""
    property real scrollAnchorRowOffset: 0
    property real scrollAnchorBucketOffset: 0
    property int dataEpoch: 0
    readonly property real bodyWidth: Math.max(0, width - resourceColumnWidth)
    readonly property real bodyHeight: Math.max(0, height - headerHeight)
    readonly property int activeRow: model ? model.resourceIndex(activeResourceId) : -1
    readonly property int activeColumn: model ? model.bucketIndex(activeBucketId) : -1
    readonly property var visibleRows: collectRows()
    readonly property var visibleBuckets: collectBuckets()
    readonly property var visibleCellRecords: collectCells()

    signal currentCellChanged(string resourceId, string bucketId)
    signal editCommitted(string resourceId, string bucketId, real allocation)
    signal editRejected(string reason)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    activeFocusOnTab: enabled
    focusPolicy: Qt.StrongFocus
    clip: true
    Accessible.role: Accessible.Table
    Accessible.name: tooltip
    Accessible.focusable: enabled
    Accessible.focused: activeFocus
    Accessible.description: accessibleDescription()

    function accessibleDescription() {
        const epoch = dataEpoch
        if (!model) return qsTr("No resource plan")
        if (activeRow < 0 || activeColumn < 0)
            return qsTr("%1 resources by %2 time buckets. Use arrow keys to navigate.")
                    .arg(model.resourceCount).arg(model.bucketCount)
        return qsTr("%1, %2. %3 %4 allocated; capacity %5 %4%6. Use arrows to navigate, Enter to edit.")
                .arg(model.resourceName(activeResourceId)).arg(model.bucketName(activeBucketId))
                .arg(model.allocation(activeResourceId, activeBucketId))
                .arg(model.unit(activeResourceId)).arg(model.capacity(activeResourceId, activeBucketId))
                .arg(model.overAllocated(activeResourceId, activeBucketId)
                     ? qsTr(". Over allocated") : "")
    }

    function collectRows() {
        const result = []
        if (!model || bodyHeight <= 0) return result
        const first = Math.max(0, Math.floor(scrollY / Tk.Theme.size.row))
        const count = Math.min(model.resourceCount - first,
                               Math.ceil(bodyHeight / Tk.Theme.size.row) + 1)
        for (let row = first; row < first + count; ++row) result.push(row)
        return result
    }

    function collectBuckets() {
        const result = []
        if (!model || bodyWidth <= 0) return result
        const first = Math.max(0, Math.floor(scrollX / bucketColumnWidth))
        const count = Math.min(model.bucketCount - first,
                               Math.ceil(bodyWidth / bucketColumnWidth) + 1)
        for (let column = first; column < first + count; ++column) result.push(column)
        return result
    }

    function collectCells() {
        const epoch = dataEpoch
        const result = []
        const rows = visibleRows, columns = visibleBuckets
        for (let r = 0; r < rows.length; ++r) {
            for (let c = 0; c < columns.length; ++c) {
                if (result.length >= 4096) return result
                const row = rows[r], column = columns[c]
                const resourceId = model.resourceIdAt(row)
                const bucketId = model.bucketIdAt(column)
                result.push({ row: row, column: column, resourceId: resourceId,
                              bucketId: bucketId, x: column * bucketColumnWidth - scrollX,
                              y: row * Tk.Theme.size.row - scrollY,
                              allocation: model.allocation(resourceId, bucketId),
                              capacity: model.capacity(resourceId, bucketId),
                              unit: model.unit(resourceId),
                              conflict: model.overAllocated(resourceId, bucketId) })
            }
        }
        return result
    }

    function resourceSummaryText(id) {
        const epoch = dataEpoch
        if (summaryMode === "none") return ""
        if (summaryMode === "usage") return qsTr("%1 %2").arg(model.resourceUsage(id)).arg(model.unit(id))
        return qsTr("%1/%2 %3")
                .arg(model.resourceUsage(id)).arg(model.resourceCapacity(id)).arg(model.unit(id))
    }

    function resourceSummaryAccessible(id) {
        const epoch = dataEpoch
        return qsTr("%1, %2 %3 allocated of %4 across the plan%5")
                .arg(model.resourceName(id)).arg(model.resourceUsage(id)).arg(model.unit(id))
                .arg(model.resourceCapacity(id))
                .arg(model.resourceConflict(id) ? qsTr(". Over allocation conflict") : "")
    }

    function setCurrent(row, column) {
        if (!model || row < 0 || column < 0 || row >= model.resourceCount
                || column >= model.bucketCount) return
        activeResourceId = model.resourceIdAt(row)
        activeBucketId = model.bucketIdAt(column)
        currentCellChanged(activeResourceId, activeBucketId)
        ensureCurrentVisible()
    }

    function moveCurrent(rowDelta, columnDelta) {
        if (!model || model.resourceCount === 0 || model.bucketCount === 0) return
        const row = Math.max(0, Math.min(model.resourceCount - 1,
                                         (activeRow < 0 ? 0 : activeRow) + rowDelta))
        const column = Math.max(0, Math.min(model.bucketCount - 1,
                                            (activeColumn < 0 ? 0 : activeColumn) + columnDelta))
        setCurrent(row, column)
    }

    function ensureCurrentVisible() {
        if (!model || activeRow < 0 || activeColumn < 0) return
        const left = activeColumn * bucketColumnWidth
        const top = activeRow * Tk.Theme.size.row
        if (left < scrollX) scrollX = left
        else if (left + bucketColumnWidth > scrollX + bodyWidth)
            scrollX = left + bucketColumnWidth - bodyWidth
        if (top < scrollY) scrollY = top
        else if (top + Tk.Theme.size.row > scrollY + bodyHeight)
            scrollY = top + Tk.Theme.size.row - bodyHeight
        clampScroll()
    }

    function clampScroll() {
        if (!model) { scrollX = 0; scrollY = 0; return }
        scrollX = Math.max(0, Math.min(scrollX,
                   Math.max(0, model.bucketCount * bucketColumnWidth - bodyWidth)))
        scrollY = Math.max(0, Math.min(scrollY,
                   Math.max(0, model.resourceCount * Tk.Theme.size.row - bodyHeight)))
    }

    function beginCellEdit() {
        if (!model || activeRow < 0 || activeColumn < 0 || !enabled) return false
        editingResourceId = activeResourceId
        editingBucketId = activeBucketId
        editText = String(model.allocation(editingResourceId, editingBucketId))
        editing = true
        editor.forceActiveFocus()
        editor.selectAll()
        return true
    }

    function commitCellEdit(candidate) {
        const value = Number(String(candidate === undefined ? editText : candidate).trim())
        if (!Number.isFinite(value) || value < 0) {
            editRejected(qsTr("Enter a finite, nonnegative allocation."))
            return false
        }
        const resourceId = editingResourceId, bucketId = editingBucketId
        if (!model.requestAllocation(resourceId, bucketId, value)) {
            editRejected(model.lastError)
            return false
        }
        editing = false
        editingResourceId = ""; editingBucketId = ""
        editCommitted(resourceId, bucketId, value)
        forceActiveFocus()
        return true
    }

    function cancelCellEdit() {
        if (!editing) return
        editing = false
        editingResourceId = ""; editingBucketId = ""
        forceActiveFocus()
    }

    onWidthChanged: clampScroll()
    onHeightChanged: clampScroll()

    Keys.onPressed: function(event) {
        if (!enabled || !model) return
        switch (event.key) {
        case Qt.Key_Up: moveCurrent(-1, 0); break
        case Qt.Key_Down: moveCurrent(1, 0); break
        case Qt.Key_Left: moveCurrent(0, -1); break
        case Qt.Key_Right: moveCurrent(0, 1); break
        case Qt.Key_Home: setCurrent(activeRow < 0 ? 0 : activeRow, 0); break
        case Qt.Key_End: setCurrent(activeRow < 0 ? model.resourceCount - 1 : activeRow,
                                    model.bucketCount - 1); break
        case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_F2: beginCellEdit(); break
        case Qt.Key_Escape: cancelCellEdit(); break
        case Qt.Key_Z:
            if (event.modifiers & Qt.ControlModifier) model.undo()
            else return
            break
        case Qt.Key_Y:
            if (event.modifiers & Qt.ControlModifier) model.redo()
            else return
            break
        default: return
        }
        event.accepted = true
    }

    Connections {
        target: table.model
        ignoreUnknownSignals: true
        function onStructureChanged() {
            const rowAnchor = table.model.resourceIndex(table.scrollAnchorResourceId)
            const columnAnchor = table.model.bucketIndex(table.scrollAnchorBucketId)
            if (rowAnchor >= 0)
                table.scrollY = rowAnchor * Tk.Theme.size.row + table.scrollAnchorRowOffset
            if (columnAnchor >= 0)
                table.scrollX = columnAnchor * table.bucketColumnWidth
                                 + table.scrollAnchorBucketOffset
            table.clampScroll()
            if (table.model.resourceIndex(table.activeResourceId) < 0
                    || table.model.bucketIndex(table.activeBucketId) < 0)
                table.setCurrent(0, 0)
        }
        function onPlanChanged() { table.dataEpoch += 1 }
        function onModelAboutToBeReset() {
            if (!table.model) return
            const row = Math.max(0, Math.floor(table.scrollY / Tk.Theme.size.row))
            const column = Math.max(0, Math.floor(table.scrollX / table.bucketColumnWidth))
            table.scrollAnchorResourceId = table.model.resourceIdAt(row)
            table.scrollAnchorBucketId = table.model.bucketIdAt(column)
            table.scrollAnchorRowOffset = table.scrollY - row * Tk.Theme.size.row
            table.scrollAnchorBucketOffset = table.scrollX - column * table.bucketColumnWidth
        }
    }

    contentItem: Item {
        clip: true
        Rectangle {
            anchors.fill: parent
            color: Tk.Theme.color.panel
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.divider
        }

        Rectangle {
            objectName: "resourceCornerHeader"
            width: table.resourceColumnWidth; height: table.headerHeight
            color: Tk.Theme.color.headerBg
            border.width: Tk.Theme.size.border; border.color: Tk.Theme.color.divider
            Tk.Label {
                anchors.fill: parent; anchors.leftMargin: Tk.Theme.space.sm
                text: qsTr("Resource")
                color: Tk.Theme.color.textMuted; font.pixelSize: Tk.Theme.font.caption
                verticalAlignment: Text.AlignVCenter
            }
        }

        Item {
            objectName: "resourceBucketHeaders"
            x: table.resourceColumnWidth; width: table.bodyWidth; height: table.headerHeight
            clip: true
            Repeater {
                model: table.visibleBuckets
                delegate: Rectangle {
                    required property int modelData
                    objectName: "resourceBucketHeader_" + table.model.bucketIdAt(modelData)
                    x: modelData * table.bucketColumnWidth - table.scrollX
                    width: table.bucketColumnWidth; height: table.headerHeight
                    color: Tk.Theme.color.headerBg
                    border.width: Tk.Theme.size.border; border.color: Tk.Theme.color.divider
                    Tk.Label {
                        anchors.fill: parent; anchors.leftMargin: Tk.Theme.space.xs
                        anchors.rightMargin: Tk.Theme.space.xs
                        text: table.model.bucketName(table.model.bucketIdAt(parent.modelData))
                        color: Tk.Theme.color.textMuted; font.pixelSize: Tk.Theme.font.caption
                        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                    }
                }
            }
        }

        Item {
            objectName: "resourceRowHeaders"
            y: table.headerHeight; width: table.resourceColumnWidth; height: table.bodyHeight
            clip: true
            Repeater {
                model: table.visibleRows
                delegate: Rectangle {
                    required property int modelData
                    readonly property string resourceId: table.model.resourceIdAt(modelData)
                    objectName: "resourceRowHeader_" + resourceId
                    y: modelData * Tk.Theme.size.row - table.scrollY
                    width: table.resourceColumnWidth; height: Tk.Theme.size.row
                    color: modelData % 2 ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
                    border.width: Tk.Theme.size.border; border.color: Tk.Theme.color.divider
                    Accessible.role: Accessible.RowHeader
                    Accessible.name: table.resourceSummaryAccessible(resourceId)
                    Tk.Label {
                        anchors.left: parent.left; anchors.right: summary.left
                        anchors.leftMargin: Tk.Theme.space.sm
                        anchors.rightMargin: Tk.Theme.space.xs
                        anchors.verticalCenter: parent.verticalCenter
                        text: table.model.resourceName(parent.resourceId)
                        color: Tk.Theme.color.text; font.pixelSize: Tk.Theme.font.body
                        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                    }
                    Tk.Label {
                        id: summary
                        objectName: "resourceRowSummary"
                        anchors.right: parent.right
                        anchors.rightMargin: Tk.Theme.space.xs
                        anchors.verticalCenter: parent.verticalCenter
                        text: table.resourceSummaryText(parent.resourceId)
                        color: table.model.resourceConflict(parent.resourceId)
                               ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                        font.pixelSize: Tk.Theme.font.caption
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }
        }

        Item {
            id: viewport
            objectName: "resourceCellViewport"
            x: table.resourceColumnWidth; y: table.headerHeight
            width: table.bodyWidth; height: table.bodyHeight; clip: true
            WheelHandler {
                objectName: "resourceTableWheel"
                blocking: false
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function(event) {
                    if (event.modifiers & Qt.ShiftModifier)
                        table.scrollX = Math.max(0, table.scrollX - event.angleDelta.y / 120
                                                 * table.bucketColumnWidth)
                    else {
                        table.scrollX = Math.max(0, table.scrollX - event.angleDelta.x / 120
                                                 * table.bucketColumnWidth)
                        table.scrollY = Math.max(0, table.scrollY - event.angleDelta.y / 120
                                                 * Tk.Theme.size.row * 3)
                    }
                    table.clampScroll()
                    event.accepted = true
                }
            }
            Repeater {
                model: table.visibleCellRecords
                delegate: Item {
                    id: cell
                    required property var modelData
                    objectName: "resourceCell_" + modelData.resourceId + "_" + modelData.bucketId
                    x: modelData.x; y: modelData.y
                    width: table.bucketColumnWidth; height: Tk.Theme.size.row
                    Accessible.role: Accessible.Cell
                    Accessible.name: qsTr("%1, %2: %3 %4 allocated of %5 %4 capacity%6")
                                      .arg(table.model.resourceName(modelData.resourceId))
                                      .arg(table.model.bucketName(modelData.bucketId))
                                      .arg(modelData.allocation).arg(modelData.unit)
                                      .arg(modelData.capacity)
                                      .arg(modelData.conflict ? qsTr(". Over allocated") : "")
                    Accessible.focusable: false
                    Rectangle {
                        anchors.fill: parent
                        color: cell.modelData.conflict ? Tk.Theme.color.dangerSubtle
                              : (cell.modelData.row % 2 ? Tk.Theme.color.panelAlt
                                                        : Tk.Theme.color.panel)
                        border.width: Tk.Theme.size.border
                        border.color: cell.modelData.conflict ? Tk.Theme.color.danger
                                                               : Tk.Theme.color.divider
                    }
                    Tk.Label {
                        objectName: "resourceCellValue"
                        anchors.fill: parent
                        anchors.leftMargin: Tk.Theme.space.sm
                        anchors.rightMargin: Tk.Theme.space.sm
                        visible: !(table.editing && table.editingResourceId === cell.modelData.resourceId
                                   && table.editingBucketId === cell.modelData.bucketId)
                        text: !table.showZeros && Number(cell.modelData.allocation) === 0
                              ? "" : String(cell.modelData.allocation)
                        color: cell.modelData.conflict ? Tk.Theme.color.danger
                                                       : Tk.Theme.color.text
                        font.pixelSize: Tk.Theme.font.body
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignRight
                    }
                    MouseArea {
                        id: cellMouse
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        onPressed: function(mouse) {
                            table.setCurrent(cell.modelData.row, cell.modelData.column)
                            table.forceActiveFocus()
                        }
                        onDoubleClicked: table.beginCellEdit()
                    }
                    Tk.ToolTip {
                        text: qsTr("%1 %2 of %3 capacity")
                              .arg(cell.modelData.allocation).arg(cell.modelData.unit)
                              .arg(cell.modelData.capacity)
                              + (cell.modelData.conflict ? qsTr(" · Over allocated") : "")
                        visible: cellMouse.containsMouse
                    }
                }
            }
            Rectangle {
                objectName: "resourceActiveCell"
                visible: table.activeRow >= 0 && table.activeColumn >= 0
                x: table.activeColumn * table.bucketColumnWidth - table.scrollX
                y: table.activeRow * Tk.Theme.size.row - table.scrollY
                width: table.bucketColumnWidth; height: Tk.Theme.size.row
                color: Tk.Theme.alpha(Tk.Theme.color.panel, Tk.Theme.opacity.hidden)
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.focus
                z: 4
            }
            Tk.TextField {
                id: editor
                objectName: "resourceAllocationEditor"
                visible: table.editing
                x: table.activeColumn * table.bucketColumnWidth - table.scrollX
                y: table.activeRow * Tk.Theme.size.row - table.scrollY
                width: table.bucketColumnWidth; height: Tk.Theme.size.row
                text: table.editText
                validator: RegularExpressionValidator { regularExpression: /[0-9]+([.][0-9]*)?/ }
                onTextEdited: table.editText = text
                Keys.onReturnPressed: table.commitCellEdit(text)
                Keys.onEnterPressed: table.commitCellEdit(text)
                Keys.onEscapePressed: table.cancelCellEdit()
                onEditingFinished: if (table.editing) table.commitCellEdit(text)
            }
        }
    }

    Tk.ToolTip { text: table.tooltip; visible: table.hovered }
}
