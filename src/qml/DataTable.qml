// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: a dense, virtualised, sortable table. `columns` is a list
// of Tk.TableColumn. `model` is a JS array of objects, or any model that
// exposes `get(index)` returning one record (QML ListModel does; a C++ model
// needs a Q_INVOKABLE QVariantMap get(int)). Rows are Theme.size.row tall and
// only the visible ones exist, so a table of ten thousand processes costs the
// same as a table of thirty.
//
// AGENT-NOTE: records are fetched with recordAt(index) rather than read from
// the delegate's `model`/`modelData` context properties, because those differ
// by model kind -- a JS array delegate gets `modelData` and no `model`, a
// QAbstractItemModel delegate gets role properties and no `modelData` -- and
// a cell that has to work for formatters, ramps and custom delegates alike
// cannot branch on which one happens to exist. get() is only ever called for
// rows that are actually on screen.
//
// AGENT-GUARD: the table never sorts or filters. Clicking a header emits
// `sortRequested(key, order)` and nothing moves until the owner applies it
// to its model. A table that sorted a JS array in place would silently
// reorder the caller's data and disagree with a C++ proxy's own ordering.
//
// objectNames: tableHeader, tableBody, headerCell_<key>, tableRow.
Item {
    id: table

    // AGENT-NOTE: a typed list, not `var`. Assigning an array of QML objects
    // to a `var` property fails with "Cannot assign multiple values to a
    // singular property"; `list<T>` is what accepts inline TableColumn items.
    property list<Tk.TableColumn> columns
    property var model: null
    property string sortKey: ""
    property int sortOrder: Qt.AscendingOrder
    property int currentIndex: -1
    property bool showHeader: true
    property bool alternatingRows: false
    property real rowHeight: Tk.Theme.size.row
    property string emptyText: ""
    property string tooltip: ""
    // Per-column width overrides written by a header drag, keyed by column key.
    property var columnWidths: ({})

    readonly property int count: table.model == null ? 0
                               : table.model.count !== undefined ? table.model.count
                               : table.model.rowCount !== undefined ? table.model.rowCount()
                               : table.model.length !== undefined ? table.model.length : 0
    readonly property var visibleColumns: {
        const out = []
        // AGENT-NOTE: .length, never Array.isArray -- a C++ list property
        // arrives as a sequence that indexes and has length but fails
        // isArray(), which would empty the table (see TabStrip).
        const source = table.columns
        const n = source != null && source.length !== undefined ? source.length : 0
        for (let i = 0; i < n; ++i) {
            if (source[i].visible) {
                out.push(source[i])
            }
        }
        return out
    }

    signal sortRequested(string key, int order)
    signal activated(int index)
    signal rowRightClicked(int index, var point)
    signal columnResized(string key, real width)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Table
    Accessible.name: table.tooltip.length > 0 ? table.tooltip : qsTr("Table")

    Tk.ToolTip {
        text: table.tooltip
        visible: table.tooltip.length > 0 && tableHover.hovered
        delay: 600
    }
    HoverHandler { id: tableHover }

    function widthOf(column) {
        const override = table.columnWidths[column.key]
        return override !== undefined ? override : column.width
    }

    // Fixed columns keep their width; elastic ones share the remainder.
    function resolvedWidth(column, available) {
        if (column.flex <= 0) {
            return Math.max(column.minWidth, table.widthOf(column))
        }
        let fixed = 0
        let flexTotal = 0
        for (const c of table.visibleColumns) {
            if (c.flex > 0) {
                flexTotal += c.flex
            } else {
                fixed += Math.max(c.minWidth, table.widthOf(c))
            }
        }
        const gaps = Tk.Theme.space.sm * Math.max(0, table.visibleColumns.length - 1)
        const spare = Math.max(0, available - fixed - gaps)
        return Math.max(column.minWidth, spare * (column.flex / Math.max(1, flexTotal)))
    }

    function toggleSort(column) {
        if (!column.sortable) {
            return
        }
        let order = column.descendingFirst ? Qt.DescendingOrder : Qt.AscendingOrder
        if (table.sortKey === column.key) {
            order = table.sortOrder === Qt.AscendingOrder ? Qt.DescendingOrder
                                                          : Qt.AscendingOrder
        }
        table.sortRequested(column.key, order)
    }

    function cellOf(row, key) {
        return row === undefined || row === null ? undefined : row[key]
    }

    // One record, whatever kind of model is attached.
    function recordAt(index) {
        const source = table.model
        if (source == null || index < 0) {
            return undefined
        }
        if (typeof source.get === "function") {
            return source.get(index)
        }
        if (source.length !== undefined) {
            return source[index]
        }
        return undefined
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0

        Tk.Box {
            objectName: "tableHeader"
            visible: table.showHeader
            Tk.Flex.shrink: 0
            implicitWidth: table.width
            implicitHeight: Tk.Theme.size.header
            color: Tk.Theme.color.headerBg
            borderBottom: Tk.Theme.size.border
            borderColor: Tk.Theme.color.divider
            paddingLeft: Tk.Theme.space.sm
            paddingRight: Tk.Theme.space.sm

            Tk.Flex {
                anchors.fill: parent
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm

                Repeater {
                    model: table.visibleColumns
                    delegate: Item {
                        id: headerCell
                        required property var modelData
                        required property int index
                        objectName: "headerCell_" + headerCell.modelData.key
                        implicitWidth: table.resolvedWidth(headerCell.modelData,
                                                           table.width - Tk.Theme.space.md)
                        implicitHeight: Tk.Theme.size.header
                        readonly property bool active: table.sortKey === headerCell.modelData.key

                        Tk.Flex {
                            anchors.fill: parent
                            align: Tk.Flex.Center
                            justify: headerCell.modelData.align === Qt.AlignRight ? Tk.Flex.End
                                   : headerCell.modelData.align === Qt.AlignHCenter ? Tk.Flex.Center
                                   : Tk.Flex.Start
                            gap: 2

                            Tk.Overline {
                                text: headerCell.modelData.title
                                color: headerCell.active ? Tk.Theme.color.accent
                                                         : Tk.Theme.color.headerText
                                Tk.Flex.shrink: 1
                                Tk.Flex.minWidth: 0
                            }
                            Tk.Icon {
                                visible: headerCell.active
                                name: table.sortOrder === Qt.AscendingOrder ? "chevron-up"
                                                                            : "chevron-down"
                                size: Tk.Theme.size.iconSm
                                color: Tk.Theme.color.accent
                                Tk.Flex.shrink: 0
                            }
                        }

                        TapHandler {
                            enabled: headerCell.modelData.sortable
                            // AGENT-GUARD: the default DragThreshold policy
                            // takes only a PASSIVE grab, so the press keeps
                            // travelling and every other header cell's
                            // handler fires from the same click -- one click
                            // sorted by three columns in a row. ReleaseWithin-
                            // Bounds grabs exclusively and consumes it.
                            gesturePolicy: TapHandler.ReleaseWithinBounds
                            onTapped: table.toggleSort(headerCell.modelData)
                        }
                        Tk.ToolTip {
                            text: headerCell.modelData.tooltip
                            visible: hover.hovered && headerCell.modelData.tooltip.length > 0
                            delay: 600
                        }
                        HoverHandler { id: hover }

                        // The seam that drags this column wider or narrower.
                        Rectangle {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: Tk.Theme.size.border
                            height: Math.round(parent.height * 0.6)
                            visible: headerCell.modelData.resizable
                                     && headerCell.modelData.flex <= 0
                            color: seam.active ? Tk.Theme.color.accent : Tk.Theme.color.divider

                            MouseArea {
                                id: seam
                                anchors.centerIn: parent
                                width: Tk.Theme.size.seam
                                height: parent.height
                                cursorShape: Qt.SizeHorCursor
                                property real pressX: 0
                                property real pressWidth: 0
                                readonly property bool active: pressed
                                onPressed: function(mouse) {
                                    pressX = mouse.x
                                    pressWidth = table.widthOf(headerCell.modelData)
                                }
                                onPositionChanged: function(mouse) {
                                    if (!pressed) {
                                        return
                                    }
                                    const column = headerCell.modelData
                                    const next = Math.max(column.minWidth,
                                                          pressWidth + (mouse.x - pressX))
                                    // Reassigning the whole map is what makes
                                    // the binding on widthOf() re-evaluate.
                                    const widths = Object.assign({}, table.columnWidths)
                                    widths[column.key] = next
                                    table.columnWidths = widths
                                    table.columnResized(column.key, next)
                                }
                            }
                        }
                    }
                }
            }
        }

        Tk.Scroll {
            objectName: "tableBody"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            implicitWidth: table.width
            overflowX: Tk.Scroll.Hidden

            ListView {
                id: rows
                width: table.width
                height: Math.max(0, table.height
                                 - (table.showHeader ? Tk.Theme.size.header : 0))
                model: table.model
                currentIndex: table.currentIndex
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0

                delegate: Item {
                    id: rowItem
                    required property int index
                    objectName: "tableRow"
                    width: rows.width
                    height: table.rowHeight

                    readonly property var record: table.recordAt(rowItem.index)
                    readonly property bool selected: table.currentIndex === rowItem.index

                    Rectangle {
                        anchors.fill: parent
                        color: rowItem.selected ? Tk.Theme.color.selection
                             : rowHover.hovered ? Tk.Theme.color.hover
                             : (table.alternatingRows && rowItem.index % 2 === 1)
                               ? Tk.Theme.color.panelAlt : "transparent"
                    }
                    HoverHandler { id: rowHover }
                    TapHandler {
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: {
                            table.currentIndex = rowItem.index
                            table.activated(rowItem.index)
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: function(eventPoint) {
                            table.currentIndex = rowItem.index
                            table.rowRightClicked(rowItem.index, eventPoint.position)
                        }
                    }

                    Tk.Flex {
                        anchors.fill: parent
                        anchors.leftMargin: Tk.Theme.space.sm
                        anchors.rightMargin: Tk.Theme.space.sm
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm

                        Repeater {
                            model: table.visibleColumns
                            delegate: Item {
                                id: cell
                                required property var modelData
                                readonly property var column: cell.modelData
                                readonly property var value:
                                    table.cellOf(rowItem.record, cell.column.key)
                                implicitWidth: table.resolvedWidth(
                                    cell.column, table.width - Tk.Theme.space.md)
                                implicitHeight: table.rowHeight

                                // AGENT-CONTRACT: a cell delegate reads its
                                // data from its PARENT -- parent.value,
                                // parent.row, parent.column. A delegate
                                // declared in another file cannot see these
                                // ids, and a Loader does not initialise a
                                // loaded item's required properties from its
                                // own same-named ones (verified on Qt 6.11:
                                // "Required property value was not
                                // initialized"), so parent is the only
                                // binding path that works across files.
                                Loader {
                                    anchors.fill: parent
                                    active: cell.column.delegate !== null
                                    sourceComponent: cell.column.delegate
                                    property var value: cell.value
                                    property var row: rowItem.record
                                    property var column: cell.column
                                }
                                Tk.Label {
                                    anchors.fill: parent
                                    visible: cell.column.delegate === null
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: cell.column.align
                                    text: cell.column.display(cell.value, rowItem.record)
                                    mono: cell.column.mono
                                    muted: cell.column.muted && !rowItem.selected
                                    color: cell.column.ramp !== null
                                           ? cell.column.ramp.forValue(Number(cell.value),
                                                                       cell.column.rampFrom,
                                                                       cell.column.rampTo)
                                           : (cell.column.muted && !rowItem.selected
                                              ? Tk.Theme.color.textMuted
                                              : Tk.Theme.color.text)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Tk.EmptyState {
        anchors.centerIn: parent
        visible: table.count === 0 && table.emptyText.length > 0
        title: table.emptyText
    }

    Keys.onUpPressed: table.currentIndex = Math.max(0, table.currentIndex - 1)
    Keys.onDownPressed: table.currentIndex = Math.min(table.count - 1, table.currentIndex + 1)
    Keys.onReturnPressed: table.activated(table.currentIndex)
}
