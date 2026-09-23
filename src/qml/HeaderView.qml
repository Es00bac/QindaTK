// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: section positions, order and size come from EditableGridModel.
// The view receives viewport geometry and never keeps a parallel size map.
T.Control {
    id: header

    property Tk.EditableGridModel gridModel: null
    property int orientation: Qt.Horizontal
    property real scrollX: 0
    property real scrollY: 0
    property real rowHeaderWidth: Tk.Theme.size.tableColumn
    property var sectionLabel: function(id, index) { return String(index + 1) }
    property string tooltip: orientation === Qt.Horizontal
                             ? qsTr("Grid column headers") : qsTr("Grid row headers")
    property int geometryEpoch: 0

    readonly property bool horizontal: orientation === Qt.Horizontal
    readonly property var sections: sectionRecords()

    function sectionRecords() {
        const epoch = geometryEpoch
        const result = []
        if (!gridModel || width <= 0 || height <= 0) return result
        const regions = gridModel.visibleRegions(
                    horizontal ? width : Math.max(rowHeaderWidth,
                                                   gridModel.defaultColumnSize * 2),
                    horizontal ? gridModel.defaultRowSize * 2 : height,
                    horizontal ? scrollX : 0, horizontal ? 0 : scrollY)
        const seen = ({})
        for (let ri = 0; ri < regions.length; ++ri) {
            const region = regions[ri]
            const first = horizontal ? region.firstColumn : region.firstRow
            const count = horizontal ? region.columnCount : region.rowCount
            for (let i = first; i < first + count; ++i) {
                if (seen[i]) continue
                seen[i] = true
                const rect = horizontal ? gridModel.cellRect(0, i, scrollX, 0)
                                        : gridModel.cellRect(i, 0, 0, scrollY)
                result.push({ index: i,
                              id: horizontal ? gridModel.columnIdAt(i) : gridModel.rowIdAt(i),
                              label: sectionLabel(horizontal ? gridModel.columnIdAt(i)
                                                             : gridModel.rowIdAt(i), i),
                              position: horizontal ? rect.x : rect.y,
                              size: horizontal ? rect.width : rect.height,
                              frozen: horizontal ? i < gridModel.frozenColumns
                                                 : i < gridModel.frozenRows })
            }
        }
        return result
    }

    function requestResize(section, size) {
        if (!gridModel || !section) return false
        const minimum = Tk.Theme.size.control
        return horizontal ? gridModel.requestColumnResize(section.id, Math.max(minimum, size))
                          : gridModel.requestRowResize(section.id, Math.max(minimum, size))
    }

    implicitWidth: horizontal ? Tk.Theme.size.panelMinWidth : rowHeaderWidth
    implicitHeight: horizontal ? Tk.Theme.size.header : Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.focusable: false
    clip: true

    Connections {
        target: header.gridModel
        ignoreUnknownSignals: true
        function onStructureChanged() { header.geometryEpoch += 1 }
        function onGeometryChanged() { header.geometryEpoch += 1 }
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.headerBg
        border.color: Tk.Theme.color.divider
        border.width: Tk.Theme.size.border
    }

    Repeater {
        model: header.sections
        delegate: Item {
            id: section
            required property var modelData
            required property int index
            objectName: (header.horizontal ? "gridColumnHeader_" : "gridRowHeader_")
                        + section.modelData.id
            x: header.horizontal ? section.modelData.position : 0
            y: header.horizontal ? 0 : section.modelData.position
            width: header.horizontal ? section.modelData.size : header.width
            height: header.horizontal ? header.height : section.modelData.size
            z: section.modelData.frozen ? 2 : 1
            Accessible.role: header.horizontal ? Accessible.ColumnHeader
                                               : Accessible.RowHeader
            Accessible.name: String(section.modelData.label)
            Accessible.focusable: false

            Rectangle {
                anchors.fill: parent
                color: section.modelData.frozen ? Tk.Theme.color.headerBg
                                                : Tk.Theme.color.panelAlt
                border.width: Tk.Theme.size.border
                border.color: Tk.Theme.color.divider
            }
            Tk.Label {
                anchors.fill: parent
                anchors.leftMargin: Tk.Theme.space.xs
                anchors.rightMargin: Tk.Theme.space.xs
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: header.horizontal ? Text.AlignHCenter : Text.AlignLeft
                text: String(section.modelData.label)
                color: Tk.Theme.color.textMuted
                font.family: Tk.Theme.font.monoFamily
                font.pixelSize: Tk.Theme.font.small
                elide: Text.ElideRight
            }
            MouseArea {
                id: headerDrag
                x: 0
                y: 0
                width: Math.max(0, parent.width - Tk.Theme.size.seamTarget)
                height: parent.height
                acceptedButtons: Qt.LeftButton
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property real originX: 0
                property real originY: 0
                onPressed: function(mouse) { originX = mouse.x; originY = mouse.y }
                onReleased: function(mouse) {
                    const delta = header.horizontal ? mouse.x - originX : mouse.y - originY
                    if (Math.abs(delta) < Tk.Theme.space.sm) return
                    const coordinate = header.horizontal ? section.modelData.position + mouse.x
                                                          : section.modelData.position + mouse.y
                    const records = header.sections
                    let beforeId = ""
                    for (let i = 0; i < records.length; ++i) {
                        if (coordinate < records[i].position + records[i].size / 2) {
                            beforeId = records[i].id
                            break
                        }
                    }
                    if (header.horizontal)
                        header.gridModel.requestColumnMove(section.modelData.id, beforeId)
                    else
                        header.gridModel.requestRowMove(section.modelData.id, beforeId)
                }
            }
            MouseArea {
                id: resizeHandle
                objectName: (header.horizontal ? "gridColumnResize_" : "gridRowResize_")
                            + section.modelData.id
                x: header.horizontal ? Math.max(0, parent.width - width) : 0
                y: header.horizontal ? 0 : Math.max(0, parent.height - height)
                width: header.horizontal ? Math.min(Tk.Theme.size.seamTarget, parent.width)
                                         : parent.width
                height: header.horizontal ? parent.height
                                          : Math.min(Tk.Theme.size.seamTarget, parent.height)
                acceptedButtons: Qt.LeftButton
                cursorShape: header.horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
                property real startCoordinate: 0
                property real startSize: 0
                onPressed: function(mouse) {
                    const point = resizeHandle.mapToItem(null, mouse.x, mouse.y)
                    startCoordinate = header.horizontal ? point.x : point.y
                    startSize = section.modelData.size
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const point = resizeHandle.mapToItem(null, mouse.x, mouse.y)
                    const coordinate = header.horizontal ? point.x : point.y
                    header.requestResize(section.modelData,
                                         startSize + coordinate - startCoordinate)
                }
            }
        }
    }
}
