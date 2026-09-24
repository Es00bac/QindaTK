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

    SignalSpy { id: requestSpy }

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
}
