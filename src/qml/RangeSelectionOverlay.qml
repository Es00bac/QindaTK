// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
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
    property int geometryEpoch: 0
    readonly property var rectangles: resolveRectangles()
    clip: true
    Accessible.ignored: true

    function resolveRectangles() {
        const result = []
        if (geometryEpoch < 0 || !gridModel) return result
        const frozen = gridModel.frozenExtent()
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
                    // The band's pane: frozen axes end at the frozen extent,
                    // scrolling axes start there and run to the overlay's edge.
                    const frozenRow = rows[0] < gridModel.frozenRows
                    const frozenColumn = columns[0] < gridModel.frozenColumns
                    result.push({ x: rowHeaderWidth + first.x, y: headerHeight + first.y,
                                  width: last.x + last.width - first.x,
                                  height: last.y + last.height - first.y,
                                  paneX: rowHeaderWidth + (frozenColumn ? 0 : frozen.width),
                                  paneY: headerHeight + (frozenRow ? 0 : frozen.height),
                                  paneWidth: frozenColumn ? frozen.width : -1,
                                  paneHeight: frozenRow ? frozen.height : -1 })
                }
            }
        }
        return result
    }

    Connections {
        target: overlay.gridModel
        function onStructureChanged() { overlay.geometryEpoch += 1 }
        function onGeometryChanged() { overlay.geometryEpoch += 1 }
    }

    function split(first, last, frozenCount) {
        const result = []
        const frozenLast = Math.min(last, frozenCount - 1)
        if (first <= frozenLast) result.push([first, frozenLast])
        const scrollingFirst = Math.max(first, frozenCount)
        if (scrollingFirst <= last) result.push([scrollingFirst, last])
        return result
    }

    // AGENT-NOTE: panes scroll independently (EditableGrid's note on panes),
    // and the overlay draws above every pane, so each band's outline is
    // clipped to its own pane: a band scrolled under a frozen pane is cut
    // there, border included. gridRangeSelection_<index> keeps the whole
    // band in overlay coordinates (tests read it); it draws nothing itself.
    Repeater {
        model: overlay.rectangles
        delegate: Item {
            id: band
            required property var modelData
            required property int index
            objectName: "gridRangeSelection_" + index
            x: modelData.x
            y: modelData.y
            width: Math.max(0, modelData.width)
            height: Math.max(0, modelData.height)

            Item {
                id: pane
                objectName: "gridRangePane_" + band.index
                x: band.modelData.paneX - band.x
                y: band.modelData.paneY - band.y
                width: band.modelData.paneWidth >= 0 ? band.modelData.paneWidth
                                                     : Math.max(0, overlay.width - band.modelData.paneX)
                height: band.modelData.paneHeight >= 0 ? band.modelData.paneHeight
                                                       : Math.max(0, overlay.height - band.modelData.paneY)
                clip: true

                Rectangle {
                    objectName: "gridRangeBorder_" + band.index
                    x: -pane.x
                    y: -pane.y
                    width: band.width
                    height: band.height
                    color: Tk.Theme.alpha(Tk.Theme.color.accent, Tk.Theme.opacity.hidden)
                    border.width: Tk.Theme.size.focusRing
                    border.color: Tk.Theme.color.accent
                }
            }
        }
    }
}
