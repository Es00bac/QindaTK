// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk
import "../../src/qml/NodeGraphGeometry.js" as Geometry

TestCase {
    id: testCase
    name: "DiagramCanvas"
    when: windowShown
    visible: true
    width: 600
    height: 360

    property string sourceNode: ""
    property string targetNode: ""
    property string sourcePort: ""
    property string targetPort: ""

    Tk.DiagramDocument { id: diagram }
    Tk.DiagramCanvas {
        id: canvas
        width: parent.width
        height: parent.height
        document: diagram
    }

    function initTestCase() {
        sourceNode = diagram.addNode("Source", 10, 20)
        targetNode = diagram.addNode("Target", 320, 20)
        sourcePort = diagram.addPort(sourceNode, "Out", "output")
        targetPort = diagram.addPort(targetNode, "In", "input")
    }

    function test_link_and_move_reach_document() {
        const graph = canvas.graphView
        compare(graph.wireRouting, "orthogonal")
        compare(graph.nodes.length, 2)
        graph.beginOrFinishLink(sourceNode, sourcePort, "output")
        graph.beginOrFinishLink(targetNode, targetPort, "input")
        compare(diagram.edges.length, 1)
        compare(graph.linkStatusState, "accepted")
        graph.nodeMoveRequested(sourceNode, 54, 66)
        compare(diagram.nodes[0].x, 54)
        compare(diagram.nodes[0].y, 66)
        compare(diagram.edges[0].fromPortId, sourcePort)
    }

    function test_orthogonal_routes_are_stable() {
        const points = []
        const pen = {
            moveTo: function(x, y) { points.push([x, y]) },
            lineTo: function(x, y) { points.push([x, y]) }
        }
        Geometry.paintEdge(pen, Qt.point(20, 30), Qt.point(120, 70),
                           "orthogonal", 16, 24)
        compare(JSON.stringify(points), JSON.stringify([
            [20, 30], [70, 30], [70, 70], [120, 70]]))
        points.length = 0
        Geometry.paintEdge(pen, Qt.point(120, 70), Qt.point(20, 30),
                           "orthogonal", 16, 24)
        compare(JSON.stringify(points), JSON.stringify([
            [120, 70], [136, 70], [136, 6], [4, 6], [4, 30], [20, 30]]))
    }
}
