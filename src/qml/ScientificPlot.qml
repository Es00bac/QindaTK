// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    property Tk.ScientificDataset dataset: null
    property alias xAxis: plot.xAxis
    property alias yAxis: plot.yAxis
    property string title: qsTr("Scientific plot")
    property string tooltip: title
    property bool showDataTable: false
    property int mode: Tk.UncertaintyRenderer.ErrorBars
    readonly property string selectedSampleId: plot.selectedId
    readonly property var selectedSample: dataset && selectedSampleId
        ? dataset.sampleById(selectedSampleId) : ({})
    signal sampleSelected(string id)
    function dataAt(x, y) { return plot.dataAt(x, y) }
    function projected(id) { return plot.projected(id) }
    function selectId(id) { return plot.selectId(id) }
    function csvData() { return dataset ? dataset.csvData() : "" }
    function exportPng(path, size) { return plot.exportPng(path, size) }
    function exportSvg(path, size) { return plot.exportSvg(path, size) }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    objectName: "scientificPlot"
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: dataset ? qsTr("%1 float64 samples, %2 missing. X %3; Y %4. Selected %5.")
        .arg(dataset.count).arg(dataset.missingCount).arg(dataset.xUnit).arg(dataset.yUnit)
        .arg(selectedSampleId || qsTr("none")) : qsTr("No data")

    Tk.UncertaintyView {
        id: plot
        objectName: "scientificPlotCanvas"
        anchors.fill: parent
        model: root.dataset ? root.dataset.intervals : null
        mode: root.mode
        tooltip: root.tooltip
        title: root.title
        visible: !root.showDataTable
        onIntervalSelected: function(id) { root.sampleSelected(id) }
    }
    Tk.DataTable {
        objectName: "scientificPlotDataTable"
        anchors.fill: plot
        visible: root.showDataTable
        model: root.dataset
        tooltip: root.tooltip + qsTr(" samples")
        columns: [
            Tk.TableColumn { key: "id"; title: qsTr("Sample"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "x"; title: qsTr("X"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "y"; title: qsTr("Y"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "sigmaY"; title: qsTr("±1σ Y"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "missingY"; title: qsTr("Missing"); width: Tk.Theme.size.controlLg },
            Tk.TableColumn { key: "xUnit"; title: qsTr("X unit"); width: Tk.Theme.size.controlLg },
            Tk.TableColumn { key: "yUnit"; title: qsTr("Y unit"); width: Tk.Theme.size.controlLg }
        ]
    }
}
