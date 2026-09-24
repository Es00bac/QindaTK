// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: fixture
    width: 1180
    height: 720
    Tk.CadDocument { id: sketch }

    Component.onCompleted: {
        const a = sketch.addPoint(0, 0)
        const b = sketch.addPoint(120, 0)
        const c = sketch.addPoint(120, 80)
        const d = sketch.addPoint(0, 80)
        const top = sketch.addSegment(a, b)
        const right = sketch.addSegment(b, c)
        const bottom = sketch.addSegment(c, d)
        const left = sketch.addSegment(d, a)
        sketch.addConstraint("horizontal", top, "", 0)
        sketch.addConstraint("vertical", right, "", 0)
        sketch.addConstraint("horizontal", bottom, "", 0)
        sketch.addConstraint("vertical", left, "", 0)
        sketch.addConstraint("distance", a, b, 120)
        sketch.addConstraint("distance", b, c, 80)
        for (const xy of [[25, 25], [95, 25], [25, 55], [95, 55]]) {
            const center = sketch.addPoint(xy[0], xy[1])
            sketch.addCircle(center, 6)
        }
        sketch.addArc(sketch.addPoint(60, 40), 18, 20, 140)
        viewport.selectedEntityId = top
    }

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("CAD sketch"); font.pixelSize: Tk.Theme.font.title }
            Tk.Badge { text: qsTr("Constrained geometry"); variant: "accent" }
            Item { Tk.Flex.grow: 1 }
            Tk.Button {
                text: qsTr("Select")
                tooltip: qsTr("Select and drag a point")
                onClicked: viewport.mode = "select"
            }
            Tk.Button {
                text: qsTr("Point")
                tooltip: qsTr("Place a point with grid and point snapping")
                onClicked: viewport.mode = "point"
            }
            Tk.Button {
                text: qsTr("Line")
                tooltip: qsTr("Choose two endpoints for a line")
                onClicked: viewport.mode = "line"
            }
            Tk.Button {
                text: qsTr("Circle")
                tooltip: qsTr("Choose a center and radius")
                onClicked: viewport.mode = "circle"
            }
            Tk.Button {
                text: qsTr("Arc")
                tooltip: qsTr("Choose a center, start and end for an arc")
                onClicked: viewport.mode = "arc"
            }
            Tk.Button {
                text: sketch.units === "mm" ? qsTr("Millimeters") : qsTr("Inches")
                tooltip: qsTr("Switch display and input units")
                onClicked: sketch.changeUnits(sketch.units === "mm" ? "in" : "mm")
            }
            Tk.Button {
                text: qsTr("Undo")
                tooltip: qsTr("Undo the last sketch edit")
                enabled: sketch.canUndo
                onClicked: sketch.undo()
            }
            Tk.Button {
                text: qsTr("Redo")
                tooltip: qsTr("Redo the last sketch edit")
                enabled: sketch.canRedo
                onClicked: sketch.redo()
            }
        }
        Tk.Label {
            text: qsTr("Mounting plate · 120 × 80 mm · four 12 mm bores and a clearance arc. Drag a point or select a dimension to edit the constrained sketch.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.CadViewport {
            id: viewport
            objectName: "referenceCadViewport"
            Tk.Flex.flex: 1
            document: sketch
            tooltip: qsTr("Mounting plate CAD sketch")
            zoom: 5
            panX: 230
            panY: 70
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label { text: viewport.statusText; muted: Boolean(sketch.lastError) === false }
            Item { Tk.Flex.grow: 1 }
            Tk.Label {
                text: {
                    const m = sketch.measure(viewport.selectedEntityId)
                    if (m.kind === "segment") return qsTr("Selected length: %1 %2")
                        .arg(Number(m.length).toFixed(3)).arg(m.units)
                    if (m.kind === "circle") return qsTr("Selected radius: %1 %2")
                        .arg(Number(m.radius).toFixed(3)).arg(m.units)
                    return qsTr("Mode: %1 · Revision %2").arg(viewport.mode).arg(sketch.revision)
                }
                muted: true
            }
        }
    }
}
