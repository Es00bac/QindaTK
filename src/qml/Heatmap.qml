// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: heatmap
    property Tk.HeatmapModel model: null
    property string tooltip: qsTr("Heatmap")
    readonly property var selectedCell: renderer.selectedCell
    readonly property var selectedRegion: renderer.selectedRegion
    readonly property real horizontalViewFraction: renderer.horizontalViewFraction
    readonly property real verticalViewFraction: renderer.verticalViewFraction
    readonly property int paintBinCount: renderer.paintBinCount
    signal cellSelected(string cellId)
    signal regionSelected(var region)
    function setViewport(x0, x1, y0, y1) { renderer.setViewport(x0, x1, y0, y1) }
    function pan(x, y) { renderer.pan(x, y) }
    function zoom(factor, xAnchor, yAnchor) { renderer.zoom(factor, xAnchor, yAnchor) }
    function selectCell(row, column) { return renderer.selectCell(row, column) }
    function selectRegion(firstRow, lastRow, firstColumn, lastColumn) {
        return renderer.selectRegion(firstRow, lastRow, firstColumn, lastColumn)
    }
    function navigateSelection(key, extend) { return renderer.navigateSelection(key, extend || false) }
    function renderImage(size) { return renderer.renderImage(size) }
    function exportPng(path, size) { return renderer.exportPng(path, size) }
    function exportSvg(path, size) { return renderer.exportSvg(path, size) }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    objectName: "heatmap"
    activeFocusOnTab: true
    Accessible.role: Accessible.Table
    Accessible.name: tooltip
    function accessibleCellDescription() {
        if (!model) return qsTr("No matrix")
        if (!model.valid) return model.error
        return qsTr("%1 rows, %2 columns. Arrow keys select cells. %3")
        .arg(model.rows).arg(model.columns).arg(selectedCell.id === undefined ? qsTr("No cell selected")
        : qsTr("Selected %1 at X %2 %3, Y %4 %5: %6 %7")
            .arg(selectedCell.id).arg(selectedCell.x).arg(model.xUnit)
            .arg(selectedCell.y).arg(model.yUnit)
            .arg(selectedCell.missing ? qsTr("Missing") : selectedCell.value)
            .arg(model.valueUnit) + (selectedRegion.lastRow - selectedRegion.firstRow > 1
                || selectedRegion.lastColumn - selectedRegion.firstColumn > 1
                ? qsTr(" Region %1 rows by %2 columns.")
                    .arg(selectedRegion.lastRow - selectedRegion.firstRow)
                    .arg(selectedRegion.lastColumn - selectedRegion.firstColumn) : ""))
    }
    Accessible.description: accessibleCellDescription()
    Accessible.focusable: true
    Accessible.focused: activeFocus || renderer.activeFocus
    Keys.onPressed: function(event) {
        if (renderer.navigateSelection(event.key, (event.modifiers & Qt.ShiftModifier) !== 0))
            event.accepted = true
    }

    Tk.HeatmapRenderer {
        id: renderer
        objectName: "heatmapPlot"
        anchors.fill: parent
        model: heatmap.model
        lowColor: Tk.Theme.color.surface
        highColor: Tk.Theme.color.accent
        missingColor: Tk.Theme.color.border
        selectionColor: Tk.Theme.color.focus
        textColor: Tk.Theme.color.textMuted
        backgroundColor: Tk.Theme.color.canvas
        categoryColors: [Tk.Theme.color.accent, Tk.Theme.color.warning,
                         Tk.Theme.color.success, Tk.Theme.color.danger,
                         Tk.Theme.color.info]
        metrics: ({ leftMargin: Tk.Theme.size.labelWidth / 2,
                    rightMargin: Tk.Theme.space.sm,
                    topMargin: Tk.Theme.font.body + Tk.Theme.space.sm,
                    bottomMargin: Tk.Theme.font.body + Tk.Theme.space.sm,
                    fontSize: Tk.Theme.font.caption,
                    selectionWidth: Tk.Theme.size.focusRing,
                    legendSwatch: Tk.Theme.size.iconSm / 2,
                    legendGap: Tk.Theme.space.xs })
        Accessible.role: Accessible.Cell
        Accessible.name: heatmap.selectedCell.id === undefined
            ? qsTr("No cell selected") : heatmap.selectedCell.id
        Accessible.description: heatmap.accessibleCellDescription()
        onCellSelected: function(id) { heatmap.cellSelected(id) }
        onRegionSelected: function(region) { heatmap.regionSelected(region) }
    }
    Rectangle {
        objectName: "heatmapFocusRing"
        anchors.fill: parent
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.alpha(Tk.Theme.color.canvas, Tk.Theme.opacity.hidden)
        border.color: Tk.Theme.color.focus
        border.width: Tk.Theme.size.focusRing
        visible: heatmap.activeFocus || renderer.activeFocus
    }
}
