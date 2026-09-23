// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "Uncertainty"
    when: windowShown
    visible: true
    width: 480
    height: 320
    Component {
        id: viewComponent
        Tk.UncertaintyView {
            width: 400
            height: 240
            xAxis.minimum: 0
            xAxis.maximum: 4
            yAxis.minimum: 0
            yAxis.maximum: 10
            model: Tk.UncertaintyModel {
                sourceDatasetId: "trial"
                xUnit: "day"
                yUnit: "kg"
                meaning: Tk.UncertaintyModel.Credible
                coverage: 0.8
                Component.onCompleted: replaceIntervals([
                    {id: "first", sourceId: "raw-1", x: 1, estimate: 5, low: 3, high: 7},
                    {id: "gap", sourceId: "raw-2", x: 2, estimate: 6},
                    {id: "last", sourceId: "raw-3", x: 3, estimate: 5, low: 2, high: 8}
                ])
            }
        }
    }
    function test_modes_selection_and_metadata() {
        const view = createTemporaryObject(viewComponent, this)
        verify(view !== null)
        compare(view.model.count, 3)
        compare(view.model.missingCount, 1)
        compare(view.model.get(1).status, "missing")
        compare(view.model.get(0).sourceId, "raw-1")
        verify(view.csvData().includes("credible"))
        verify(view.csvData().includes("raw-3"))
        verify(view.selectId("first"))
        compare(view.selectedId, "first")
        verify(view.navigateSelection(Qt.Key_Right))
        compare(view.selectedId, "gap")
        view.mode = Tk.UncertaintyRenderer.Band
        verify(view.projected("last").x > view.projected("first").x)
        view.zoom(2, 0.5, 0.5)
        verify(view.horizontalViewFraction < 1)
        view.showDataTable = true
        const table = findChild(view, "uncertaintyDataTable")
        verify(table !== null)
        verify(table.visible)
    }
}
