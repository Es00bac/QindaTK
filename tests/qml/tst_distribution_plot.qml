// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "DistributionPlot"
    visible: true
    width: 800
    height: 500

    Tk.DistributionModel { id: samples }
    Tk.DistributionPlot {
        id: plot
        anchors.fill: parent
        distributionModel: samples
        tooltip: "Distribution fixture"
    }
    SignalSpy { id: selectionSpy; target: plot; signalName: "groupSelected" }

    function test_selection_tables_and_identity() {
        verify(samples.replaceGroups([
            {id: "a", label: "Alpha", unit: "ms", values: [1, 2, 3, 4, 5],
             interval: {low: 1.5, high: 4.5, estimate: 3, meaning: "95% confidence interval of mean"}},
            {id: "b", label: "Beta", unit: "ms", values: [2, 3, 4, 5, 6],
             interval: {low: 2.5, high: 5.5, estimate: 4, meaning: "95% confidence interval of mean"}}
        ]))
        compare(plot.selectedGroupId, "a")
        verify(plot.navigate(1))
        compare(plot.selectedGroupId, "b")
        compare(selectionSpy.count, 1)
        const surface = findChild(plot, "distributionCanvas")
        verify(surface)
        mouseClick(surface, surface.width * 0.25, surface.height / 2)
        compare(plot.selectedGroupId, "a")
        mouseClick(surface, surface.width * 0.75, surface.height / 2)
        compare(plot.selectedGroupId, "b")
        compare(selectionSpy.count, 3)
        plot.selectedGroupId = "a"
        compare(selectionSpy.count, 3)
        plot.selectedGroupId = "b"
        plot.mode = "violin"
        compare(plot.mode, "violin")
        plot.mode = "interval"
        verify(plot.legendText.indexOf("95% confidence interval") >= 0)
        compare(plot.sourceRows.length, 0)
        plot.showDataTable = true
        verify(plot.showDataTable)
        compare(findChild(plot, "distributionSummaryTable").visible, true)
        plot.showDataTable = false
        plot.showSourceTable = true
        compare(findChild(plot, "distributionSourceTable").visible, true)
        compare(plot.sourceRows.length, 5)
        compare(plot.sourceRows[0].value, 2)
        verify(samples.replaceGroups([
            {id: "b", label: "Beta", unit: "ms", values: [2, 3, 4, 5, 6]},
            {id: "a", label: "Alpha", unit: "ms", values: [1, 2, 3, 4, 5]}
        ]))
        compare(plot.selectedGroupId, "b")
        compare(plot.sourceRows.length, 5)
        plot.distributionModel = null
        compare(plot.selectedGroupId, "")
        plot.distributionModel = samples
        compare(plot.selectedGroupId, "b")
    }
}
