// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: fixture
    width: 1180
    height: 720

    Tk.DiagramDocument { id: diagram }

    Component.onCompleted: {
        const request = diagram.addNode(qsTr("Request"), 32, 120, "input")
        const triage = diagram.addNode(qsTr("Triage"), 355, 120, "process")
        const approved = diagram.addNode(qsTr("Approved"), 690, 46, "output")
        const rework = diagram.addNode(qsTr("Rework"), 690, 300, "process")
        const requestOut = diagram.addPort(request, qsTr("Packet"), "output", "work")
        const triageIn = diagram.addPort(triage, qsTr("Packet"), "input", "work")
        const triageYes = diagram.addPort(triage, qsTr("Approved"), "output", "work")
        const triageNo = diagram.addPort(triage, qsTr("Needs work"), "output", "work")
        const approvedIn = diagram.addPort(approved, qsTr("Done"), "input", "work")
        const reworkIn = diagram.addPort(rework, qsTr("Revise"), "input", "work")
        const reworkOut = diagram.addPort(rework, qsTr("Resubmit"), "output", "work")
        diagram.setEdgeFlow(diagram.connectPorts(request, requestOut, triage, triageIn), 12)
        diagram.setEdgeFlow(diagram.connectPorts(triage, triageYes, approved, approvedIn), 7)
        diagram.setEdgeFlow(diagram.connectPorts(triage, triageNo, rework, reworkIn), 5)
        diagram.setEdgeFlow(diagram.connectPorts(rework, reworkOut, triage, triageIn), 3)
    }

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        gap: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label { text: qsTr("Diagram canvas"); font.pixelSize: Tk.Theme.font.title }
            Tk.Badge { text: qsTr("Persistent graph"); variant: "accent" }
            Item { Tk.Flex.grow: 1 }
            Tk.Button {
                text: qsTr("Auto layout")
                tooltip: qsTr("Arrange the diagram by connection direction")
                onClicked: canvas.autoLayout()
            }
            Tk.Button {
                text: qsTr("Undo")
                enabled: diagram.canUndo
                tooltip: qsTr("Undo the last diagram edit")
                onClicked: diagram.undo()
            }
            Tk.Button {
                text: qsTr("Redo")
                enabled: diagram.canRedo
                tooltip: qsTr("Redo the last diagram edit")
                onClicked: diagram.redo()
            }
        }
        Tk.Label {
            text: qsTr("Wire width shows request volume. Drag cards freely or choose Auto layout. Activate output then input to connect; middle drag pans.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.DiagramCanvas {
            id: canvas
            objectName: "liveDiagramCanvas"
            Tk.Flex.flex: 1
            document: diagram
            flowMode: true
            tooltip: qsTr("Request flow diagram")
        }
        Tk.Label {
            text: diagram.lastError || qsTr("%1 nodes · %2 connections · revision %3")
                .arg(diagram.nodes.length).arg(diagram.edges.length).arg(diagram.revision)
            muted: true
        }
    }
}
