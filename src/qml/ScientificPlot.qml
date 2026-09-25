// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A 2D scientific figure: traces and scatter series with X and asymmetric Y
// error bars, an optional sampled surface (colour map), numeric annotations,
// calibrated linear/log axes, keyboard/pointer selection, a data table and a
// text summary as the textual alternative, and PNG/SVG export with provenance.
// AGENT-NOTE: `dataset` is the WP-085 first-slice single-series API; it is
// series 0 when set, ahead of `series`. Painting, sampling and export live in
// ScientificPlotRenderer (C++), see docs/scientific-plot.md.
Item {
    id: root
    property Tk.ScientificDataset dataset: null
    property list<Tk.ScientificDataset> series
    property Tk.ScientificGrid surface: null
    property int surfaceInterpolation: Tk.ScientificGrid.Bilinear
    property Tk.ChartAnnotationModel annotations: null
    property alias xAxis: horizontalAxis
    property alias yAxis: verticalAxis
    property string xLabel: ""
    property string yLabel: ""
    property string title: qsTr("Scientific plot")
    property string tooltip: title
    property bool showDataTable: false
    // Series index shown by the data table; -1 shows the surface lattice.
    property int tableSource: 0
    property int mode: Tk.ScientificPlotRenderer.ErrorBars
    property int paintPointLimit: 2048
    readonly property alias renderer: plot
    readonly property var allSeries: {
        const out = root.dataset ? [root.dataset] : []
        for (let i = 0; i < root.series.length; ++i) out.push(root.series[i])
        return out
    }
    readonly property int selectedSeries: plot.selectedSeries
    readonly property string selectedSampleId: plot.selectedSampleId
    readonly property var selectedSample: plot.selectedSample
    readonly property string problem: plot.problem
    readonly property string summary: plot.summary
    // First-slice signal (any series), plus the series-qualified form.
    signal sampleSelected(string id)
    signal seriesSampleSelected(int series, string id)
    function dataAt(x, y) { return plot.dataAt(x, y) }
    // projected(id) reads series 0 (first-slice form); projected(series, id) any series.
    function projected(a, b) { return b === undefined ? plot.projected(0, a) : plot.projected(a, b) }
    function selectId(id) { return plot.selectSample(0, id) }
    function selectSample(seriesIndex, id) { return plot.selectSample(seriesIndex, id) }
    function navigateSelection(key) { return plot.navigateSelection(key) }
    function setViewport(xs, xe, ys, ye) { plot.setViewport(xs, xe, ys, ye) }
    function zoom(factor, xAnchor, yAnchor) { plot.zoom(factor, xAnchor, yAnchor) }
    function pan(dx, dy) { plot.pan(dx, dy) }
    function csvData() { return plot.csvData() }
    function textSummary() { return plot.textSummary() }
    function metadataJson(width, height) { return plot.metadataJson(width, height) }
    function exportPng(path, size) { return plot.exportPng(path, size) }
    function exportSvg(path, size) { return plot.exportSvg(path, size) }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    objectName: "scientificPlot"
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: plot.summary + " " + qsTr("Selected %1.").arg(selectedSampleId || qsTr("none"))
    Accessible.focusable: true
    Accessible.focused: activeFocus || plot.activeFocus
    Keys.onPressed: function(event) {
        if (plot.navigateSelection(event.key)) event.accepted = true
    }

    readonly property var _unitSource: allSeries.length ? allSeries[0] : surface
    Tk.ChartAxis { id: horizontalAxis; unit: root._unitSource ? root._unitSource.xUnit : "" }
    Tk.ChartAxis { id: verticalAxis; unit: root._unitSource ? root._unitSource.yUnit : "" }
    Tk.ScientificPlotRenderer {
        id: plot
        objectName: "scientificPlotCanvas"
        anchors.fill: parent
        visible: !root.showDataTable
        series: root.allSeries
        surface: root.surface
        surfaceInterpolation: root.surfaceInterpolation
        annotations: root.annotations
        xAxis: horizontalAxis
        yAxis: verticalAxis
        mode: root.mode
        title: root.title
        xLabel: root.xLabel
        yLabel: root.yLabel
        paintPointLimit: root.paintPointLimit
        backgroundColor: Tk.Theme.color.canvas
        gridColor: Tk.Theme.color.border
        textColor: Tk.Theme.color.textMuted
        missingColor: Tk.Theme.alpha(Tk.Theme.color.warning, Tk.Theme.opacity.muted)
        selectionColor: Tk.Theme.color.focus
        annotationColor: Tk.Theme.color.info
        seriesColors: [Tk.Theme.color.accent, Tk.Theme.color.warning, Tk.Theme.color.success,
                       Tk.Theme.color.danger, Tk.Theme.color.info, Tk.Theme.color.textMuted]
        surfaceColors: Tk.Theme.ramp.thermal.colors
        metrics: ({ leftMargin: Tk.Theme.size.fieldWidth / 2 + Tk.Theme.font.body * 2,
                    rightMargin: Tk.Theme.space.sm,
                    topMargin: root.title ? Tk.Theme.font.body + Tk.Theme.space.md : Tk.Theme.space.sm,
                    bottomMargin: Tk.Theme.font.body * 3 + Tk.Theme.space.md,
                    fontFamily: Tk.Theme.font.family,
                    fontSize: Tk.Theme.font.caption,
                    labelGap: Tk.Theme.space.xs,
                    tickLabelWidth: Tk.Theme.size.fieldWidth / 2,
                    lineWidth: Tk.Theme.size.border,
                    pointRadius: Tk.Theme.size.iconSm / 3,
                    capHalfWidth: Tk.Theme.size.iconSm / 2,
                    hitRadius: Tk.Theme.size.controlSm / 2,
                    colorBarWidth: Tk.Theme.size.iconSm,
                    bandOpacity: Tk.Theme.opacity.disabled })
        onSampleSelected: function(seriesIndex, id) {
            root.sampleSelected(id)
            root.seriesSampleSelected(seriesIndex, id)
        }
    }
    Rectangle {
        objectName: "scientificPlotFocusRing"
        anchors.fill: plot
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.alpha(Tk.Theme.color.canvas, Tk.Theme.opacity.hidden)
        border.color: Tk.Theme.color.focus
        border.width: Tk.Theme.size.focusRing
        visible: (root.activeFocus || plot.activeFocus) && !root.showDataTable
    }
    Tk.DataTable {
        objectName: "scientificPlotDataTable"
        anchors.fill: parent
        visible: root.showDataTable && root.tableSource >= 0
        model: root.tableSource >= 0 && root.tableSource < root.allSeries.length ? root.allSeries[root.tableSource] : null
        tooltip: root.tooltip + qsTr(" samples")
        columns: [
            Tk.TableColumn { key: "id"; title: qsTr("Sample"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "x"; title: qsTr("X"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "y"; title: qsTr("Y"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "sigmaY"; title: qsTr("±1σ Y"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "sigmaYLow"; title: qsTr("−σ Y"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "sigmaYHigh"; title: qsTr("+σ Y"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "sigmaXLow"; title: qsTr("−σ X"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "sigmaXHigh"; title: qsTr("+σ X"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "missingY"; title: qsTr("Missing"); width: Tk.Theme.size.controlLg },
            Tk.TableColumn { key: "xUnit"; title: qsTr("X unit"); width: Tk.Theme.size.controlLg },
            Tk.TableColumn { key: "yUnit"; title: qsTr("Y unit"); width: Tk.Theme.size.controlLg }
        ]
    }
    Tk.DataTable {
        objectName: "scientificPlotSurfaceTable"
        anchors.fill: parent
        visible: root.showDataTable && root.tableSource < 0
        model: root.surface
        tooltip: root.tooltip + qsTr(" surface nodes")
        columns: [
            Tk.TableColumn { key: "id"; title: qsTr("Node"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "x"; title: qsTr("X"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "y"; title: qsTr("Y"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "z"; title: qsTr("Z"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "missing"; title: qsTr("Missing"); width: Tk.Theme.size.controlLg },
            Tk.TableColumn { key: "zUnit"; title: qsTr("Z unit"); width: Tk.Theme.size.controlLg }
        ]
    }
    HoverHandler { id: hover }
    Tk.ToolTip { text: root.tooltip; visible: hover.hovered && !root.showDataTable }
}
