// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "Heatmap"
    when: windowShown
    visible: true
    width: 360
    height: 240
    Component {
        id: fixture
        Tk.Heatmap {
            width: 320
            height: 200
            tooltip: "Spectral matrix"
            model: Tk.HeatmapModel {
                id: matrix
                xCategories: ["low", "high"]
                yCategories: ["early", "late"]
                valueUnit: "dB"
                colorMinimum: -10
                colorMaximum: 10
                Component.onCompleted: {
                    setDimensions(2, 2)
                    setCell(0, 0, "cold", -10)
                    setCell(0, 1, "hot", 10)
                    setCell(1, 1, "missing", null)
                }
            }
        }
    }
    function test_selection_zoom_and_accessible_cell() {
        const view = createTemporaryObject(fixture, this)
        verify(view !== null)
        compare(view.model.rows, 2)
        verify(view.selectCell(0, 1))
        compare(view.selectedCell.id, "hot")
        verify(view.navigateSelection(Qt.Key_Down))
        compare(view.selectedCell.id, "missing")
        verify(view.selectRegion(0, 2, 0, 2))
        compare(view.selectedRegion.lastRow - view.selectedRegion.firstRow, 2)
        compare(view.selectedRegion.lastColumn - view.selectedRegion.firstColumn, 2)
        compare(view.selectedCell.missing, true)
        view.zoom(2, 0.5, 0.5)
        verify(view.horizontalViewFraction < 1)
        verify(view.paintBinCount <= 4096)
        compare(view.objectName, "heatmap")
    }
}
