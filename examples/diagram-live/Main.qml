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
        const reworkDrop = diagram.addPort(rework, qsTr("Withdraw"), "output", "work")
        const withdrawn = diagram.addNode(qsTr("Withdrawn"), 940, 300, "output")
        const withdrawnIn = diagram.addPort(withdrawn, qsTr("Closed"), "input", "work")
        // Every step conserves its flow: triage 12 + 3 in = 10 + 5 out.
        diagram.setEdgeFlow(diagram.connectPorts(request, requestOut, triage, triageIn), 12)
        diagram.setEdgeFlow(diagram.connectPorts(triage, triageYes, approved, approvedIn), 10)
        diagram.setEdgeFlow(diagram.connectPorts(triage, triageNo, rework, reworkIn), 5)
        diagram.setEdgeFlow(diagram.connectPorts(rework, reworkOut, triage, triageIn), 3)
        diagram.setEdgeFlow(diagram.connectPorts(rework, reworkDrop, withdrawn, withdrawnIn), 2)
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
            text: qsTr("Wire width shows request volume; the Sankey view places the same quantities as bands. Drag cards (Ctrl+click to multi-select) or choose Auto layout. Activate output then input to connect; middle drag pans.")
            muted: true
            wrapMode: Text.Wrap
        }
        Tk.DiagramCanvas {
            id: canvas
            objectName: "liveDiagramCanvas"
            Tk.Flex.flex: 1
            document: diagram
            flowMode: true
            snapStep: Tk.Theme.space.md
            tooltip: qsTr("Request flow diagram")
        }
        Tk.SankeyDiagram {
            objectName: "liveSankeyDiagram"
            implicitHeight: Tk.Theme.size.fieldWidth * 1.5
            document: diagram
            tooltip: qsTr("Request volume by stage")
        }
        Tk.Label {
            text: diagram.lastError || qsTr("%1 nodes · %2 connections · %3 unbalanced · revision %4")
                .arg(diagram.nodes.length).arg(diagram.edges.length)
                .arg(diagram.revision >= 0 ? diagram.flowImbalances().length : 0).arg(diagram.revision)
            muted: true
        }
    }
}
