// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-043 fixture: host-owned knots and samples rendered at the active density.
Rectangle {
    width: 900
    height: 600
    color: Tk.Theme.color.bg

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Overline {
            title: qsTr("CURVE PRESENTATION")
            objectName: "curveSceneTitle"
        }
        Tk.Caption {
            text: qsTr("Normalized preview; the host owns interpolation, constraints and saved values.")
        }
        Tk.CurveEditor {
            objectName: "curveSceneEditor"
            width: 720
            height: 420
            Tk.Flex.grow: 1
            Tk.Flex.flex: 1
            accessibleName: qsTr("Exposure response curve")
            tooltip: qsTr("Select and drag knots. Escape cancels the active preview.")
            knots: [
                { id: "black", x: 0, y: 0, label: qsTr("Black point") },
                { id: "shadow", x: 0.23, y: 0.16, label: qsTr("Shadow point") },
                { id: "midtone", x: 0.55, y: 0.52, label: qsTr("Midtone point") },
                { id: "white", x: 1, y: 1, label: qsTr("White point") }
            ]
            samples: [
                { x: 0, y: 0 }, { x: 0.1, y: 0.05 }, { x: 0.23, y: 0.16 },
                { x: 0.38, y: 0.34 }, { x: 0.55, y: 0.52 },
                { x: 0.72, y: 0.72 }, { x: 0.87, y: 0.9 }, { x: 1, y: 1 }
            ]
        }
    }
}
