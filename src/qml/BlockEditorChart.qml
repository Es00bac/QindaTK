// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Internal part of Tk.BlockEditor: a chart block drawn by Tk.ChartView from
// a table block's columns (NotebookDocument.chartSeries). Point IDs are the
// table's stable row IDs, so a selection survives row edits.
Tk.Flex {
    id: root
    property var editor: null // the owning Tk.BlockEditor (var: a typed property would make a load cycle)
    property var block: ({})
    readonly property string blockId: block && block.id ? block.id : ""
    readonly property var kinds: ["line", "bar", "scatter"]
    // The revision argument makes the binding re-read the series after any
    // committed edit, including edits to a source table on another page.
    readonly property var series: seriesFor(blockId, editor && editor.notebook ? editor.notebook.revision : "")

    function seriesFor(id, revision) {
        if (!editor || !editor.notebook || id.length === 0)
            return { valid: false, error: qsTr("Charts read their data from a notebook table"), points: [] }
        return editor.notebook.chartSeries(id)
    }
    function datasetKind(kind) {
        return kind === "bar" ? Tk.ChartDataset.Bar : kind === "scatter" ? Tk.ChartDataset.Scatter : Tk.ChartDataset.Line
    }
    onSeriesChanged: dataset.replacePoints(series.points || [])
    Component.onCompleted: dataset.replacePoints(series.points || [])

    direction: Tk.Flex.Column
    align: Tk.Flex.Stretch
    gap: Tk.Theme.space.xs
    Accessible.role: Accessible.Grouping
    Accessible.name: series.valid ? qsTr("Chart of %1 by %2").arg(series.name).arg(series.xName) : qsTr("Chart block")
    Accessible.description: series.valid ? "" : String(series.error || "")

    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        Tk.Label {
            text: root.series.valid ? qsTr("%1 by %2").arg(root.series.name).arg(root.series.xName) : qsTr("Chart")
            muted: true
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }
        Tk.ComboBox {
            objectName: "blockChartKind_" + root.blockId
            model: [qsTr("Line"), qsTr("Bar"), qsTr("Scatter")]
            currentIndex: Math.max(0, root.kinds.indexOf(root.block && root.block.data ? root.block.data.kind : "line"))
            tooltip: qsTr("Chart type")
            enabled: !!root.editor && root.editor.capabilities.canEdit && !(root.block && root.block.locked)
            onActivated: function(index) { root.editor.submitData(root.block, { kind: root.kinds[index] }) }
        }
    }
    Tk.ChartView {
        id: chart
        objectName: "blockChart_" + root.blockId
        visible: root.series.valid === true
        implicitHeight: Tk.Theme.size.panelMinHeight
        tooltip: qsTr("Chart of %1").arg(root.series.name || qsTr("table data"))
        xAxis.minimum: root.series.xMin || 0
        xAxis.maximum: root.series.xMax || 1
        yAxis.minimum: root.series.yMin || 0
        yAxis.maximum: root.series.yMax || 1
        Tk.ChartDataset {
            id: dataset
            seriesId: root.blockId
            name: root.series.name || ""
            kind: root.datasetKind(root.series.kind)
        }
    }
    Tk.Caption {
        objectName: "blockChartError_" + root.blockId
        visible: root.series.valid !== true
        text: root.series.error || ""
        color: Tk.Theme.color.warning
        wrapMode: Text.WordWrap
    }
}
