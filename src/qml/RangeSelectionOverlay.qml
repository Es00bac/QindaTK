// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: endpoints live in EditableGridModel by stable ID; this
// overlay only resolves the current geometry and never stores selection state.
Item {
    id: overlay

    property Tk.EditableGridModel gridModel: null
    property real scrollX: 0
    property real scrollY: 0
    property real rowHeaderWidth: 0
    property real headerHeight: 0
    readonly property var rectangles: resolveRectangles()
    clip: true
    Accessible.ignored: true

    function resolveRectangles() {
        const result = []
        if (!gridModel) return result
        const ranges = gridModel.ranges
        for (let i = 0; i < ranges.length; ++i) {
            const range = ranges[i]
            if (range.firstRow < 0 || range.firstColumn < 0) continue
            const rowBands = split(range.firstRow, range.lastRow, gridModel.frozenRows)
            const columnBands = split(range.firstColumn, range.lastColumn,
                                      gridModel.frozenColumns)
            for (let r = 0; r < rowBands.length; ++r) {
                for (let c = 0; c < columnBands.length; ++c) {
                    const rows = rowBands[r]
                    const columns = columnBands[c]
                    const first = gridModel.cellRect(rows[0], columns[0], scrollX, scrollY)
                    const last = gridModel.cellRect(rows[1], columns[1], scrollX, scrollY)
                    if (first.width <= 0 || last.width <= 0) continue
                    result.push({ x: rowHeaderWidth + first.x, y: headerHeight + first.y,
                                  width: last.x + last.width - first.x,
                                  height: last.y + last.height - first.y })
                }
            }
        }
        return result
    }

    function split(first, last, frozenCount) {
        const result = []
        const frozenLast = Math.min(last, frozenCount - 1)
        if (first <= frozenLast) result.push([first, frozenLast])
        const scrollingFirst = Math.max(first, frozenCount)
        if (scrollingFirst <= last) result.push([scrollingFirst, last])
        return result
    }

    Repeater {
        model: overlay.rectangles
        delegate: Rectangle {
            required property var modelData
            required property int index
            objectName: "gridRangeSelection_" + index
            x: modelData.x
            y: modelData.y
            width: Math.max(0, modelData.width)
            height: Math.max(0, modelData.height)
            color: Tk.Theme.alpha(Tk.Theme.color.accent, Tk.Theme.opacity.hidden)
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.accent
            z: 10
        }
    }
}
