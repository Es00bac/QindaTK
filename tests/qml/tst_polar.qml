// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "PolarView"
    when: windowShown
    visible: true
    width: 420
    height: 380

    Component {
        id: radarFixture
        Tk.PolarView {
            width: 320
            height: 320
            tooltip: "Risk radar"
            angleMode: Tk.PolarRenderer.Category
            categories: ["North", "East", "South", "West"]
            radialMaximum: 10
            radialUnit: "score"
            Tk.PolarDataModel {
                id: points
                Component.onCompleted: replacePoints([
                    { seriesId: "score", seriesName: "Risk", pointId: "s", category: "South",
                      radius: 7, kind: Tk.PolarDataModel.Polygon },
                    { seriesId: "score", seriesName: "Risk", pointId: "n", category: "North",
                      radius: 6, kind: Tk.PolarDataModel.Polygon },
                    { seriesId: "score", seriesName: "Risk", pointId: "w", category: "West",
                      radius: 8, kind: Tk.PolarDataModel.Polygon },
                    { seriesId: "score", seriesName: "Risk", pointId: "e", category: "East",
                      radius: 5, kind: Tk.PolarDataModel.Polygon }
                ])
            }
            model: points
        }
    }

    function test_category_projection_hit_table_export() {
        const view = createTemporaryObject(radarFixture, this)
        verify(view !== null)
        compare(view.model.count, 4)
        verify(view.valid)
        const east = view.projectCategory("East", 5)
        verify(east.x > view.width / 2)
        verify(Math.abs(east.y - view.height / 2) < 0.001)
        compare(view.hitTest(east.x, east.y).pointId, "e")
        verify(view.selectAt(east.x, east.y))
        compare(view.selectedPointId, "e")
        verify(view.csvData().includes("\"East\",\"1\""))
        view.showDataTable = true
        verify(findChild(view, "polarDataTable").visible)
        compare(findChild(view, "polarDataTable").count, 4)
        view.categories = ["North", "East"]
        verify(!view.valid)
    }
}
