// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 900
    height: 560
    Tk.CanvasDocument { id: document; pageRect: Qt.rect(0, 0, 720, 420) }
    Tk.ToolController { document: document }
    Tk.CanvasView {
        id: canvas
        anchors.fill: parent
        document: document
        zoom: 0.9
        centerX: 360
        centerY: 210
        viewRotation: -8
        Tk.ArtboardView {
            anchors.fill: parent
            surface: canvas
            boardData: [
                { id: "cover", name: "Cover", rect: Qt.rect(40, 50, 260, 320),
                  background: Tk.Theme.color.panel },
                { id: "spread", name: "Spread", rect: Qt.rect(340, 50, 340, 320),
                  background: Tk.Theme.color.surface }
            ]
            activeBoardId: "cover"
        }
    }
}
