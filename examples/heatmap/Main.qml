// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 900
    height: 580
    color: Tk.Theme.color.bg
    Tk.Heatmap {
        id: heatmap
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.lg
        tooltip: qsTr("Sensor matrix")
        model: Tk.HeatmapModel {
            id: matrix
            xMinimum: 20
            xMaximum: 20000
            xUnit: "Hz"
            yMinimum: 0
            yMaximum: 60
            yUnit: "s"
            valueUnit: "dB"
            colorMinimum: -60
            colorMaximum: 0
            Component.onCompleted: {
                setDimensions(24, 36)
                for (let row = 0; row < 24; ++row)
                    for (let col = 0; col < 36; ++col)
                        if ((row + col) % 11 !== 0)
                            setCell(row, col, "sample-" + row + "-" + col,
                                    -60 + 60 * (row + col) / 58)
                setCell(0, 0, "extreme-low", -60)
                setCell(23, 35, "extreme-high", 0)
            }
        }
    }
}
