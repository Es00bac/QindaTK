// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A repeated tiny native texture, independent of the document dimensions.
Item {
    id: checker
    property color lightColor: Tk.Theme.color.controlBg
    property color darkColor: Tk.Theme.color.border
    property real cellSize: Tk.Theme.space.md
    Canvas {
        id: tile
        visible: false
        width: checker.cellSize * 2
        height: width
        onPaint: {
            const c = getContext("2d")
            c.fillStyle = checker.lightColor
            c.fillRect(0, 0, width, height)
            c.fillStyle = checker.darkColor
            c.fillRect(0, 0, width / 2, height / 2)
            c.fillRect(width / 2, height / 2, width / 2, height / 2)
            pattern.source = toDataURL()
        }
        Component.onCompleted: requestPaint()
    }
    onLightColorChanged: tile.requestPaint()
    onDarkColorChanged: tile.requestPaint()
    onCellSizeChanged: tile.requestPaint()
    Image {
        id: pattern
        objectName: "rasterCheckerboard"
        anchors.fill: parent
        fillMode: Image.Tile
        smooth: false
    }
}
