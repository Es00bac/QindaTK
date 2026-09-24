// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-041 visual fixture: document-owned shapes with host-supplied guides.
Rectangle {
    width: 900
    height: 600
    color: Tk.Theme.color.bg

    Tk.CanvasDocument {
        id: document
        pageRect: Qt.rect(0, 0, 600, 400)
        Component.onCompleted: {
            addRect(70, 55, 180, 125, "#4a81a1")
            addRect(300, 160, 195, 140, "#a98655")
        }
    }
    Tk.CanvasView {
        id: view
        objectName: "guideFixtureCanvas"
        anchors.fill: parent
        document: document
        backend: "software"
        fitMode: "page"
        viewRotation: 17
        accessibleName: qsTr("Guide geometry fixture")
        Tk.GuideLayer {
            objectName: "guideFixtureLayer"
            anchors.fill: parent
            surface: view
            guideData: [
                { id: "vertical-220", orientation: "vertical", position: 220,
                  label: qsTr("Vertical composition guide") },
                { id: "horizontal-190", orientation: "horizontal", position: 190,
                  label: qsTr("Horizontal composition guide") }
            ]
        }
    }
}
