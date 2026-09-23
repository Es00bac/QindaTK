// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: plot and table consume the same full-resolution model.
// Numeric angles and ordered categories are separate axis modes; neither
// the delegate nor the CSV path invents angular units or category order.
T.Control {
    id: root
    objectName: "polarView"
    property Tk.PolarDataModel model: null
    property alias angleMode: plot.angleMode
    property alias angleUnit: plot.angleUnit
    property alias categories: plot.categories
    property alias radialMinimum: plot.radialMinimum
    property alias radialMaximum: plot.radialMaximum
    property alias radialUnit: plot.radialUnit
    property alias startAngleDegrees: plot.startAngleDegrees
    property alias clockwise: plot.clockwise
    property alias plotItem: plot
    property bool showDataTable: false
    property string tooltip: ""
    readonly property bool valid: plot.valid
    readonly property string error: plot.error
    readonly property string selectedSeriesId: plot.selectedSeriesId
    readonly property string selectedPointId: plot.selectedPointId
    signal pointSelected(string seriesId, string pointId)
    signal exportRequested(string format, var image)

    function project(angle, radius) { return plot.project(angle, radius) }
    function projectCategory(category, radius) { return plot.projectCategory(category, radius) }
    function unproject(x, y) { return plot.unproject(x, y) }
    function hitTest(x, y) { return plot.hitTest(x, y) }
    function selectAt(x, y) { return plot.selectAt(x, y) }
    function selectedPoint() { return plot.selectedPoint() }
    function csvData() { return plot.csv() }
    function renderImage(size) { return plot.renderImage(size) }
    function exportPng(path, size) { return plot.exportPng(path, size) }
    function exportSvg(path, size) { return plot.exportSvg(path, size) }
    function navigateSelection(key) { return plot.navigateSelection(key) }

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: 0
    activeFocusOnTab: enabled
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip.length ? tooltip : qsTr("Polar plot")
    Accessible.description: showDataTable
        ? qsTr("Full-resolution polar data table")
        : qsTr("Arrow keys inspect points. The data table exposes exact angles, categories and radial values.")
    Accessible.focusable: enabled
    Accessible.focused: activeFocus || plot.activeFocus
    Keys.onPressed: function(event) {
        if (plot.navigateSelection(event.key)) event.accepted = true
    }

    background: Rectangle {
        color: Tk.Theme.color.canvas
        border.color: Tk.Theme.color.border
        border.width: Tk.Theme.size.border
    }
    contentItem: Item {
        Tk.PolarRenderer {
            id: plot
            objectName: "polarPlot"
            anchors.fill: parent
            visible: !root.showDataTable
            model: root.model
            backgroundColor: Tk.Theme.color.canvas
            gridColor: Tk.Theme.color.border
            textColor: Tk.Theme.color.textMuted
            defaultSeriesColor: Tk.Theme.color.accent
            seriesColors: [Tk.Theme.color.accent, Tk.Theme.color.warning,
                           Tk.Theme.color.info, Tk.Theme.color.success,
                           Tk.Theme.color.danger, Tk.Theme.color.textMuted]
            fillOpacity: Tk.Theme.opacity.disabled
            metrics: ({
                margin: Tk.Theme.size.fieldWidth / 2,
                gridWidth: Tk.Theme.size.border,
                lineWidth: Tk.Theme.size.border * 2,
                pointRadius: Tk.Theme.size.iconSm / 3,
                hitRadius: Tk.Theme.size.controlSm / 2,
                selectionWidth: Tk.Theme.size.focusRing,
                selectionGap: Tk.Theme.space.xs,
                labelGap: Tk.Theme.space.sm,
                labelWidth: Tk.Theme.size.fieldWidth,
                fontSize: Tk.Theme.font.caption,
                legendGap: Tk.Theme.space.md,
                legendSwatch: Tk.Theme.size.iconSm
            })
            onPointSelected: function(seriesId, pointId) {
                root.pointSelected(seriesId, pointId)
            }
            onExportImageReady: function(image) { root.exportRequested("image", image) }
        }
        Rectangle {
            objectName: "polarFocusRing"
            anchors.fill: plot
            radius: Tk.Theme.radius.sm
            color: Tk.Theme.alpha(Tk.Theme.color.canvas, Tk.Theme.opacity.hidden)
            border.color: Tk.Theme.color.focus
            border.width: Tk.Theme.size.focusRing
            visible: root.activeFocus || plot.activeFocus
        }
        Tk.DataTable {
            objectName: "polarDataTable"
            anchors.fill: parent
            visible: root.showDataTable
            model: root.model
            tooltip: root.tooltip + qsTr(" data")
            columns: [
                Tk.TableColumn { key: "seriesName"; title: qsTr("Series"); width: Tk.Theme.size.fieldWidth },
                Tk.TableColumn { key: "pointId"; title: qsTr("Point ID"); width: Tk.Theme.size.fieldWidth },
                Tk.TableColumn { key: "angle"; title: root.angleUnit === Tk.PolarRenderer.Radians
                    ? qsTr("Angle (rad)") : qsTr("Angle (deg)"); width: Tk.Theme.size.fieldWidth; mono: true },
                Tk.TableColumn { key: "category"; title: qsTr("Category"); width: Tk.Theme.size.fieldWidth },
                Tk.TableColumn { key: "radius"; title: root.radialUnit.length
                    ? qsTr("Radius (%1)").arg(root.radialUnit) : qsTr("Radius");
                    width: Tk.Theme.size.fieldWidth; mono: true },
                Tk.TableColumn { key: "kind"; title: qsTr("Mark"); flex: 1;
                    formatter: function(value) {
                        const names = [qsTr("Points"), qsTr("Line"), qsTr("Radar"), qsTr("Radial bar")]
                        return names[Number(value)] || ""
                    } }
            ]
        }
    }
}
