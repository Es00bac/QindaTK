// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Internal part of EditableGrid (D-312): a band over every visible row the
// host marks as a group header (`grid.groupHeader(rowId, index)`): chevron,
// name and summary at the viewport's left edge, and per-column summary
// values right-aligned in their columns. Bands sit above the cells and under
// the selection and the editor; a body row's band is clipped below the
// frozen rows like the cells it covers. A tap selects the row's cell under
// the pointer; the chevron asks the host to collapse or expand the group.
Item {
    id: bands
    // AGENT-NOTE: `var`, not the owner's type: a part typed with the control
    // that instantiates it is a cyclic type, which qmllint reports as
    // "cannot assign X to X" (the GridCellDelegate / GridFillHandle precedent).
    required property var grid
    readonly property size frozen: grid.model && grid.dataEpoch >= 0 ? grid.model.frozenExtent() : Qt.size(0, 0)
    readonly property var groups: collect()

    // The host's group header for a row (grid.groupHeader), or null.
    function info(rowId, index) {
        const provider = grid.groupHeader
        if (typeof provider !== "function" || !rowId) return null
        const header = provider(rowId, index === undefined && grid.model ? grid.model.rowIndex(rowId) : index)
        return header && typeof header === "object" ? header : null
    }
    // Emits grid.groupToggled when the request changes the group's state.
    function request(rowId, collapsed) {
        const header = info(rowId)
        if (!header || (header.collapsed === true) === collapsed) return false
        grid.groupToggled(rowId, collapsed)
        return true
    }
    function toggle(rowId) {
        const header = info(rowId)
        return !!header && request(rowId, header.collapsed !== true)
    }

    // One entry per visible group row: {rowId, row, y, height, frozenRow, info, cells}.
    function collect() {
        const records = grid.visibleCellRecords
        if (typeof grid.groupHeader !== "function" || !records || records.length === 0) return []
        const byRow = ({})
        const out = []
        for (let i = 0; i < records.length; ++i) {
            const record = records[i]
            let entry = byRow[record.rowId]
            if (entry === undefined) {
                const header = info(record.rowId, record.row)
                entry = header ? { rowId: record.rowId, row: record.row, y: record.y, height: record.height,
                                   frozenRow: record.frozenRow === true, info: header, cells: [] } : null
                byRow[record.rowId] = entry
                if (entry) out.push(entry)
            }
            if (entry) entry.cells.push(record)
        }
        return out
    }

    component Band: Rectangle {
        id: band
        required property var modelData
        property real offsetY: 0
        readonly property var info: modelData.info
        readonly property bool collapsed: info.collapsed === true
        objectName: "gridGroupHeader_" + modelData.rowId
        x: 0
        y: modelData.y - offsetY
        width: bands.width
        height: modelData.height
        color: Tk.Theme.color.headerBg
        Accessible.role: Accessible.Row
        Accessible.name: [String(info.label || ""), String(info.summary || "")]
            .filter(function(part) { return part.length > 0 }).join(", ")
        Accessible.description: collapsed ? qsTr("Group, collapsed. Enter expands.") : qsTr("Group, expanded. Enter collapses.")

        Rectangle {
            y: parent.height - height
            width: parent.width
            height: Tk.Theme.size.border
            color: Tk.Theme.color.divider
        }
        TapHandler {
            onTapped: function(point) {
                let column = 0
                for (let i = 0; i < band.modelData.cells.length; ++i) {
                    const cell = band.modelData.cells[i]
                    if (point.position.x >= cell.x && point.position.x < cell.x + cell.width) column = cell.column
                }
                bands.grid.setCurrent(band.modelData.row, column, false)
                bands.grid.forceActiveFocus()
            }
        }
        Tk.Icon {
            id: chevron
            objectName: "gridGroupChevron"
            x: Tk.Theme.space.xs + Math.max(0, Math.floor(Number(band.info.level || 0))) * Tk.Theme.space.lg
            y: (parent.height - height) / 2
            name: band.collapsed ? "chevron-right" : "chevron-down"
            size: Tk.Theme.size.iconSm
            color: Tk.Theme.color.textMuted
            TapHandler {
                margin: Tk.Theme.space.xs
                onTapped: bands.grid.toggleGroup(band.modelData.rowId)
            }
        }
        Tk.Label {
            id: label
            objectName: "gridGroupLabel"
            x: chevron.x + chevron.width + Tk.Theme.space.xs
            width: Math.min(implicitWidth, Math.max(0, band.width - x))
            height: band.height
            text: String(band.info.label || "")
            font.bold: true
        }
        Tk.Caption {
            id: summary
            objectName: "gridGroupSummary"
            x: label.x + label.width + Tk.Theme.space.sm
            width: Math.min(implicitWidth, Math.max(0, band.width - x))
            height: band.height
            text: String(band.info.summary || "")
            elide: Text.ElideRight
        }
        // Per-column values, hidden where they would run under the title.
        Repeater {
            model: band.modelData.cells
            delegate: Tk.Label {
                required property var modelData
                readonly property var values: band.info.values || ({})
                readonly property string value: values[modelData.columnId] === undefined
                                                ? "" : String(values[modelData.columnId])
                objectName: "gridGroupValue_" + modelData.columnId
                visible: value.length > 0 && x >= summary.x + summary.width + Tk.Theme.space.sm
                x: modelData.x + Tk.Theme.space.sm
                width: Math.max(0, modelData.width - Tk.Theme.space.sm * 2)
                height: band.height
                horizontalAlignment: Text.AlignRight
                font.pixelSize: Tk.Theme.font.small
                text: value
            }
        }
    }

    // Frozen group rows stay put; body rows scroll and are clipped below them.
    Repeater {
        model: bands.groups.filter(function(entry) { return entry.frozenRow })
        delegate: Band { }
    }
    Item {
        y: bands.frozen.height
        width: bands.width
        height: Math.max(0, bands.height - y)
        clip: true
        Repeater {
            model: bands.groups.filter(function(entry) { return !entry.frozenRow })
            delegate: Band { offsetY: bands.frozen.height }
        }
    }
}
