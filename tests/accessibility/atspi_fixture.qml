// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QindaTK as Tk

Window {
    width: 720
    height: 420
    visible: true
    title: "QindaTK AT-SPI qualification fixture"

    Item {
        anchors.fill: parent

        Tk.AccessibilityAdapter {
            id: grid
            objectName: "atspiGrid"
            x: 20; y: 20; width: 210; height: 110
            accessibleName: "Spreadsheet grid"
            Accessible.role: Accessible.Pane
            Accessible.name: accessibleName
            Component.onCompleted: setNodes([
                { id: "grid-A1", name: "A1: 12", description: "Row 1, column 1", role: Accessible.Cell, bounds: Qt.rect(0, 0, 80, 24), value: 12 },
                { id: "grid-B1", name: "B1: 17", description: "Row 1, column 2", role: Accessible.Cell, bounds: Qt.rect(80, 0, 80, 24), value: 17, selected: true }
            ])
        }

        Tk.AccessibilityAdapter {
            id: canvas
            objectName: "atspiCanvas"
            x: 250; y: 20; width: 210; height: 110
            accessibleName: "Canvas objects"
            Accessible.role: Accessible.Pane
            Accessible.name: accessibleName
            Component.onCompleted: setNodes([
                { id: "canvas-circle", name: "Blue circle", description: "Circle centered at 40, 40, radius 12", role: Accessible.Graphic, bounds: Qt.rect(28, 28, 24, 24) }
            ])
        }

        Tk.AccessibilityAdapter {
            id: timeline
            objectName: "atspiTimeline"
            x: 480; y: 20; width: 210; height: 110
            accessibleName: "Timeline clips"
            Accessible.role: Accessible.Pane
            Accessible.name: accessibleName
            Component.onCompleted: setNodes([
                { id: "timeline-intro", name: "Intro", description: "Track 1, start 0 seconds, duration 4 seconds", role: Accessible.ListItem, bounds: Qt.rect(0, 0, 100, 24), value: 1, minimum: 0, maximum: 10, step: 1, readOnly: false, commands: { press: "atspi.increment" } },
                { id: "timeline-verse", name: "Verse", description: "Track 1, start 4 seconds, duration 8 seconds", role: Accessible.ListItem, bounds: Qt.rect(100, 0, 100, 24), value: "4–12 s", offscreen: true }
            ])
        }
    }
}
