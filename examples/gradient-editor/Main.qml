// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 520
    height: 190
    property var demoStops: [
        { id: "warm", position: 0, color: "#e35d6a" },
        { id: "sun", position: 0.38, color: "#f6c85f" },
        { id: "cool", position: 1, color: "#4385c5" }
    ]

    Tk.GradientEditor {
        objectName: "gradientEditor"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Tk.Theme.space.lg
        stops: parent.demoStops
        selectedStopId: "sun"
        tooltip: qsTr("Color gradient stops")
        onAddStopRequested: function(position) {
            const next = parent.demoStops.slice()
            next.push({ id: "new-" + next.length, position: position, color: "#ffffff" })
            parent.demoStops = next
        }
    }
}
