// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Heatmaps"
    note: "Heatmap · sparse model · missing cells · region selection"
    Tk.HeatmapModel { id: matrix }
    Component.onCompleted: {
        matrix.setDimensions(7, 12)
        matrix.xUnit = "column"
        matrix.yUnit = "row"
        matrix.valueUnit = "intensity"
        for (let row = 0; row < 7; ++row)
            for (let column = 0; column < 12; ++column)
                if ((row + column) % 4 !== 0)
                    matrix.setCell(row, column, "gallery-" + row + "-" + column,
                                   (row * 12 + column) / 83)
    }
    Tk.Heatmap {
        objectName: "galleryHeatmap"
        width: 940
        height: 250
        Tk.Flex.shrink: 0
        model: matrix
        tooltip: qsTr("Gallery intensity heatmap")
    }
}
