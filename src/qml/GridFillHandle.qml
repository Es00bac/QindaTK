// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/editable-grid.md#fill-handle): the fill handle of
// Tk.EditableGrid, a child of its cell viewport. It sits on the bottom-right
// corner of the grid model's last range; dragging previews a target that
// extends the range along one axis, and releasing calls
// EditableGridModel.fillRange (a domain source performs a formula-aware fill)
// and selects source plus target. It keeps only the transient drag target.
// Keyboard users fill through host commands (SheetView: Ctrl+D / Ctrl+R).
Item {
    id: handle

    property Item grid: null
    property bool active: true
    // The previewed target {firstRow, firstColumn, lastRow, lastColumn}, or null.
    property var target: null
    readonly property var corner: selectionCorner()
    readonly property bool dragging: drag.active

    objectName: "gridFillHandle"
    visible: active && corner !== null
    x: corner !== null ? corner.x - width / 2 : 0
    y: corner !== null ? corner.y - height / 2 : 0
    width: Tk.Theme.size.grip
    height: Tk.Theme.size.grip
    z: 12
    Accessible.role: Accessible.Grip
    Accessible.name: qsTr("Fill handle")
    Accessible.description: qsTr("Drag to fill the selection down, up, left or right")

    function lastRange() {
        const model = grid ? grid.model : null
        return model && model.ranges.length > 0 ? model.ranges[model.ranges.length - 1] : null
    }

    // Bottom-right corner of the last range in viewport coordinates, or null.
    function selectionCorner() {
        const range = grid && grid.dataEpoch >= 0 && grid.model && !grid.model.readOnly ? lastRange() : null
        if (!range) return null
        const rect = grid.model.cellRect(range.lastRow, range.lastColumn, grid.scrollX, grid.scrollY)
        const x = rect.x + rect.width, y = rect.y + rect.height
        return rect.width > 0 && x > 0 && y > 0 && x <= grid.bodyWidth && y <= grid.bodyHeight
            ? Qt.point(x, y) : null
    }

    // Previews the fill toward a viewport point: the axis the point is
    // furthest outside the range wins, as in spreadsheets.
    function previewTo(x, y) {
        const range = lastRange()
        if (!range) return
        const hit = grid.model.hitTest(Math.max(0, Math.min(grid.bodyWidth - 1, x)),
                                       Math.max(0, Math.min(grid.bodyHeight - 1, y)),
                                       grid.scrollX, grid.scrollY)
        if (hit.row === undefined) return
        const down = hit.row - range.lastRow, up = range.firstRow - hit.row
        const right = hit.column - range.lastColumn, left = range.firstColumn - hit.column
        const vertical = Math.max(down, up), horizontal = Math.max(right, left)
        if (vertical <= 0 && horizontal <= 0) target = null
        else if (vertical >= horizontal)
            target = { firstRow: down > 0 ? range.lastRow + 1 : hit.row,
                       lastRow: down > 0 ? hit.row : range.firstRow - 1,
                       firstColumn: range.firstColumn, lastColumn: range.lastColumn }
        else
            target = { firstRow: range.firstRow, lastRow: range.lastRow,
                       firstColumn: right > 0 ? range.lastColumn + 1 : hit.column,
                       lastColumn: right > 0 ? hit.column : range.firstColumn - 1 }
    }

    // Fills the previewed target; false when there is none or the model refuses.
    function commit() {
        const box = target, range = lastRange()
        target = null
        if (!box || !range) return false
        const model = grid.model
        if (!model.fillRange(range.firstRow, range.firstColumn, range.lastRow - range.firstRow + 1,
                             range.lastColumn - range.firstColumn + 1, box.firstRow, box.firstColumn,
                             box.lastRow - box.firstRow + 1, box.lastColumn - box.firstColumn + 1))
            return false
        model.setRange(model.rowIdAt(Math.min(range.firstRow, box.firstRow)),
                       model.columnIdAt(Math.min(range.firstColumn, box.firstColumn)),
                       model.rowIdAt(Math.max(range.lastRow, box.lastRow)),
                       model.columnIdAt(Math.max(range.lastColumn, box.lastColumn)))
        return true
    }

    function areaOf(box) {
        const first = grid.model.cellRect(box.firstRow, box.firstColumn, grid.scrollX, grid.scrollY)
        const last = grid.model.cellRect(box.lastRow, box.lastColumn, grid.scrollX, grid.scrollY)
        return Qt.rect(first.x, first.y, last.x + last.width - first.x, last.y + last.height - first.y)
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.xs
        color: Tk.Theme.color.accent
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.panel
    }
    // Drawn in the viewport (the handle's parent), outside the handle's box.
    Rectangle {
        readonly property rect area: handle.target !== null && handle.grid && handle.grid.model
                                     ? handle.areaOf(handle.target) : Qt.rect(0, 0, 0, 0)
        objectName: "gridFillPreview"
        parent: handle.parent
        visible: handle.target !== null
        x: area.x
        y: area.y
        width: area.width
        height: area.height
        z: 11
        color: Tk.Theme.color.accentSubtle
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.accent
        Accessible.ignored: true
    }
    HoverHandler {
        id: hover
        cursorShape: Qt.CrossCursor
    }
    DragHandler {
        id: drag
        target: null
        dragThreshold: 0
        onCentroidChanged: {
            if (!active || !handle.parent) return
            const point = handle.mapToItem(handle.parent, centroid.position.x, centroid.position.y)
            handle.previewTo(point.x, point.y)
        }
        // A drag cut short (the handle hid, e.g. an edit began) fills nothing.
        onActiveChanged: if (!active) {
            if (handle.visible) handle.commit()
            else handle.target = null
        }
    }
    Tk.ToolTip {
        text: handle.Accessible.description
        visible: hover.hovered && !drag.active
    }
}
