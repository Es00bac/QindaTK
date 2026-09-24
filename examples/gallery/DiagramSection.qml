// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Diagram"
    note: "DiagramCanvas · SankeyDiagram · DiagramDocument (docs/diagram.md)"

    Tk.DiagramDocument {
        id: diagram
        Component.onCompleted: {
            const a = addNode(qsTr("Input"), 10, 55)
            const b = addNode(qsTr("Review"), 320, 55)
            const out = addPort(a, qsTr("Work"), "output")
            const input = addPort(b, qsTr("Work"), "input")
            setEdgeFlow(connectPorts(a, out, b, input), 4)
        }
    }
    GalleryRow {
        label: "SankeyDiagram"
        Tk.SankeyDiagram {
            objectName: "gallerySankeyDiagram"
            implicitWidth: 660
            implicitHeight: 140
            document: diagram
            tooltip: qsTr("Flow bands")
        }
    }
    GalleryRow {
        label: "DiagramCanvas"
        Tk.DiagramCanvas {
            objectName: "galleryDiagramCanvas"
            implicitWidth: 660
            implicitHeight: 225
            document: diagram
            tooltip: qsTr("Editable diagram")
        }
    }
}
