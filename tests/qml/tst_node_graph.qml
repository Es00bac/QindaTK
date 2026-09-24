// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "NodeGraph"
    when: windowShown
    visible: true
    width: 480
    height: 360

    Component {
        id: graphComponent
        Tk.NodeGraph {
            width: 440
            height: 320
            nodes: [
                { nodeId: "source", title: "Source", x: 0, y: 0,
                  inputs: [], outputs: [{ portId: "out", label: "Frame", type: "video" }] },
                { nodeId: "audio", title: "Audio", x: 260, y: 0,
                  inputs: [{ portId: "in", label: "Sound", type: "audio" }], outputs: [] }
            ]
        }
    }

    Component {
        id: compositeComponent
        Tk.NodeGraph {
            width: 440
            height: 320
            nodes: [
                { nodeId: "plate", title: "Plate", type: "solid", x: 0, y: 0, inputs: [],
                  outputs: [{ portId: "image", label: "Image", type: "image" }] },
                { nodeId: "matte", title: "Matte", type: "mask", x: 0, y: 130, inputs: [],
                  outputs: [{ portId: "mask", label: "Mask", type: "mask" }] },
                { nodeId: "blend", title: "Blend", type: "blend", x: 240, y: 40,
                  inputs: [{ portId: "background", label: "Background", type: "image" },
                           { portId: "foreground", label: "Foreground", type: "image" },
                           { portId: "mask", label: "Mask", type: "mask", optional: true }],
                  outputs: [] }
            ]
            edges: [{ edgeId: "e1", fromNodeId: "plate", fromPortId: "image",
                      toNodeId: "blend", toPortId: "background" }]
            groups: [{ groupId: "g1", title: "Inputs", nodeIds: ["plate", "matte"] }]
            panY: 48
        }
    }

    SignalSpy { id: requestSpy }
    SignalSpy { id: secondSpy }

    function test_keyboard_connection_request_and_host_rejection() {
        const graph = createTemporaryObject(graphComponent, this)
        verify(graph !== null)
        requestSpy.target = graph
        requestSpy.signalName = "linkConnectRequested"
        requestSpy.clear()

        graph.selectedNodeId = "source"
        graph.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(graph.keyboardConnectSourceNodeId, "source")
        graph.selectedNodeId = "audio"
        keyClick(Qt.Key_Return)
        compare(requestSpy.count, 1)
        const args = requestSpy.signalArguments[0]
        compare(args[1], "source")
        compare(args[2], "out")
        compare(args[3], "audio")
        compare(args[4], "in")
        verify(graph.acknowledgeLink(args[0], "rejected", "Host rejected video → audio"))
        compare(graph.linkStatusState, "rejected")
        compare(graph.linkStatusText, "Host rejected video → audio")
    }

    function args(spy, index) {
        return Array.prototype.slice.call(spy.signalArguments[index])
    }

    function spyOn(spy, graph, signalName) {
        spy.target = graph
        spy.signalName = signalName
        spy.clear()
    }

    function test_search_finds_and_reveals_nodes() {
        const graph = createTemporaryObject(compositeComponent, this)
        spyOn(requestSpy, graph, "nodeSelected")
        graph.searchText = "MASK"
        compare(graph.searchMatches, ["matte"])
        const field = findChild(graph, "nodeGraphSearch")
        compare(field.text, "MASK")
        graph.forceActiveFocus()
        keyClick(Qt.Key_F3)
        compare(requestSpy.count, 1)
        compare(requestSpy.signalArguments[0][0], "matte")
        fuzzyCompare(graph.panX + graph.nodeWidth / 2 * graph.zoom, graph.width / 2, 0.5)
        fuzzyCompare(graph.panY + (130 + graph.nodeHeight / 2) * graph.zoom, graph.height / 2, 0.5)
        compare(graph.linkStatusText, "Match 1 of 1")
        compare(findChild(graph, "node_matte").border.color, Tk.Theme.color.warning)
        keyClick(Qt.Key_F, Qt.ControlModifier)
        verify(field.activeFocus)
        graph.searchText = "missing"
        compare(graph.searchMatches.length, 0)
        graph.forceActiveFocus()
        keyClick(Qt.Key_F3)
        compare(requestSpy.count, 1)
        compare(graph.linkStatusText, "No nodes match “missing”")
    }

    function test_groups_frame_members_and_request_edits() {
        const graph = createTemporaryObject(compositeComponent, this)
        const frame = findChild(graph, "nodeGroup_g1")
        verify(frame !== null)
        const box = frame.mapToItem(graph, 0, 0, frame.width, frame.height)
        for (const nodeId of ["plate", "matte"]) {
            const card = findChild(graph, "node_" + nodeId)
            const rect = card.mapToItem(graph, 0, 0, card.width, card.height)
            verify(box.x < rect.x && box.y < rect.y && box.x + box.width > rect.x + rect.width
                   && box.y + box.height > rect.y + rect.height, nodeId)
        }
        spyOn(requestSpy, graph, "groupSelected")
        mouseClick(findChild(graph, "nodeGroupHeader_g1"))
        compare(requestSpy.signalArguments[0][0], "g1")

        graph.forceActiveFocus()
        graph.selectedNodeIds = ["plate"]
        graph.selectedNodeId = "blend"
        spyOn(requestSpy, graph, "groupRequested")
        keyClick(Qt.Key_G, Qt.ControlModifier)
        compare(requestSpy.signalArguments[0][0], ["plate", "blend"])
        graph.selectedGroupId = "g1"
        spyOn(requestSpy, graph, "groupMoveRequested")
        keyClick(Qt.Key_Right, Qt.ShiftModifier)
        compare(args(requestSpy, 0), ["g1", graph.nudgeStep, 0])
        spyOn(requestSpy, graph, "ungroupRequested")
        keyClick(Qt.Key_G, Qt.ControlModifier | Qt.ShiftModifier)
        compare(requestSpy.signalArguments[0][0], "g1")

        graph.selectedGroupId = ""
        spyOn(requestSpy, graph, "nodeMoveRequested")
        keyClick(Qt.Key_Down, Qt.ShiftModifier)
        compare(args(requestSpy, 0), ["blend", 240, 40 + graph.nudgeStep])
        spyOn(requestSpy, graph, "nodeSelectionToggled")
        keyClick(Qt.Key_Space, Qt.ControlModifier)
        compare(requestSpy.signalArguments[0][0], "blend")
        spyOn(requestSpy, graph, "nodeRemoveRequested")
        keyClick(Qt.Key_Delete)
        compare(requestSpy.signalArguments[0][0], "blend")
        spyOn(requestSpy, graph, "nodeAddRequested")
        graph.requestAddNode("blend")
        compare(args(requestSpy, 0), ["blend",
            (graph.width / 2 - graph.panX) / graph.zoom - graph.nodeWidth / 2,
            (graph.height / 2 - graph.panY) / graph.zoom - graph.nodeHeight / 2])
    }

    function test_escape_cancels_a_drag_and_group_drags_move() {
        const graph = createTemporaryObject(compositeComponent, this)
        spyOn(requestSpy, graph, "nodeMoveRequested")
        spyOn(secondSpy, graph, "groupMoveRequested")
        graph.forceActiveFocus()
        const card = findChild(graph, "node_blend")
        mousePress(card, 60, 8)
        mouseMove(card, 80, 20, -1, Qt.LeftButton)
        mouseMove(card, 110, 40, -1, Qt.LeftButton)
        keyClick(Qt.Key_Escape)
        mouseRelease(card, 110, 40, Qt.LeftButton)
        wait(30)
        compare(requestSpy.count, 0)
        mousePress(card, 60, 8)
        mouseMove(card, 80, 20, -1, Qt.LeftButton)
        mouseMove(card, 110, 40, -1, Qt.LeftButton)
        mouseRelease(card, 110, 40, Qt.LeftButton)
        tryCompare(requestSpy, "count", 1)
        compare(requestSpy.signalArguments[0][0], "blend")
        verify(requestSpy.signalArguments[0][1] > 240 && requestSpy.signalArguments[0][2] > 40)

        const header = findChild(graph, "nodeGroupHeader_g1")
        mousePress(header, 20, 8)
        mouseMove(header, 40, 20, -1, Qt.LeftButton)
        mouseMove(header, 70, 40, -1, Qt.LeftButton)
        mouseRelease(header, 70, 40, Qt.LeftButton)
        tryCompare(secondSpy, "count", 1)
        compare(secondSpy.signalArguments[0][0], "g1")
        verify(secondSpy.signalArguments[0][1] > 0 && secondSpy.signalArguments[0][2] > 0)
    }

    function test_ctrl_wheel_zooms_about_the_pointer() {
        const graph = createTemporaryObject(compositeComponent, this)
        const worldX = (300 - graph.panX) / graph.zoom
        mouseWheel(graph, 300, 150, 0, 120, Qt.NoButton, Qt.ControlModifier)
        fuzzyCompare(graph.zoom, 1.15, 0.001)
        fuzzyCompare((300 - graph.panX) / graph.zoom, worldX, 0.01)
    }

    function test_link_preview_and_typed_keyboard_target() {
        const graph = createTemporaryObject(compositeComponent, this)
        spyOn(requestSpy, graph, "linkConnectRequested")
        const preview = findChild(graph, "nodeGraphLinkPreview")
        graph.forceActiveFocus()
        graph.selectedNodeId = "matte"
        keyClick(Qt.Key_Space)
        verify(preview.visible)
        graph.selectedNodeId = "blend"
        keyClick(Qt.Key_Return)
        verify(!preview.visible)
        compare(args(requestSpy, 0).slice(1), ["matte", "mask", "blend", "mask"])
        graph.selectedNodeId = "plate"
        keyClick(Qt.Key_Space)
        graph.selectedNodeId = "blend"
        keyClick(Qt.Key_Return)
        compare(args(requestSpy, 1).slice(1), ["plate", "image", "blend", "foreground"])

        graph.selectedNodeId = "plate"
        keyClick(Qt.Key_Space)
        keyClick(Qt.Key_Escape)
        verify(!preview.visible)
        compare(graph.linkStatusText, "Canceled")
        mouseClick(findChild(graph, "nodePort_plate_image"))
        verify(preview.visible)
        graph.nodes = graph.nodes.filter(function(node) { return node.nodeId !== "plate" })
        verify(!preview.visible)
        compare(graph.linkStatusText, "The connection source was removed")
        mouseClick(findChild(graph, "nodePort_blend_foreground"))
        compare(requestSpy.count, 2)
    }
}
