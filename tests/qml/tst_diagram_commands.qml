// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-083 audit: DiagramCanvas creates cards and ports from stencils, groups
// and ungroups through the document, and aligns/distributes the selection.
TestCase {
    id: testCase
    name: "DiagramCommands"
    when: windowShown
    visible: true
    width: 760
    height: 420

    Component { id: documentComponent; Tk.DiagramDocument {} }

    Tk.DiagramCanvas {
        id: canvas
        width: 760
        height: 420
        stencils: [
            { type: "filter", title: "Filter",
              inputs: [{ label: "In", type: "rows" }], outputs: [{ label: "Out", type: "rows" }] },
            { type: "sink", title: "Sink", inputs: [{ label: "Rows", type: "rows" }] }
        ]
    }

    function makeDiagram() {
        const doc = createTemporaryObject(documentComponent, testCase)
        const a = doc.addNode("A", 10, 40)
        const b = doc.addNode("B", 260, 10)
        const c = doc.addNode("C", 500, 100)
        canvas.document = doc
        canvas.snapStep = 0
        canvas.clearSelection()
        canvas.forceActiveFocus()
        return { doc: doc, a: a, b: b, c: c }
    }

    function nodeRow(doc, id) {
        for (let i = 0; i < doc.nodes.length; ++i)
            if (doc.nodes[i].nodeId === id) return doc.nodes[i]
        return null
    }

    function test_stencils_add_cards_with_ports() {
        const d = makeDiagram()
        const graph = canvas.graphView
        compare(graph.nodeTypes.length, 2)
        verify(findChild(canvas, "nodeGraphAddButton").enabled)
        const before = d.doc.revision
        graph.requestAddNode("filter") // what the Add menu item triggers
        compare(d.doc.nodes.length, 4)
        verify(d.doc.revision > before)
        const card = nodeRow(d.doc, canvas.selectedNodeId)
        compare(card.title, "Filter")
        compare(card.stencilId, "filter")
        compare(card.inputs.length, 1)
        compare(card.outputs[0].type, "rows")
        verify(d.doc.undo()) // card and ports in one step
        compare(d.doc.nodes.length, 3)
        verify(d.doc.redo())
        compare(canvas.addStencil("missing", 0, 0), "")
        compare(d.doc.nodes.length, 4)
        // Inspector port buttons add ports to the focused card.
        graph.nodeSelected(d.a)
        waitForRendering(canvas)
        const addOutput = findChild(canvas, "diagramAddOutput")
        verify(addOutput.visible)
        mouseClick(addOutput)
        compare(nodeRow(d.doc, d.a).outputs.length, 1)
        compare(nodeRow(d.doc, d.a).outputs[0].label, "Out 1")
        canvas.addPort("input")
        compare(nodeRow(d.doc, d.a).inputs.length, 1)
        // A new port is immediately connectable.
        const out = nodeRow(d.doc, d.a).outputs[0].portId
        const filter = d.doc.nodes[3]
        verify(d.doc.connectPorts(d.a, out, filter.nodeId, filter.inputs[0].portId) !== "")
    }

    function test_group_button_and_shortcuts() {
        const d = makeDiagram()
        const graph = canvas.graphView
        graph.nodeSelectionToggled(d.a)
        graph.nodeSelectionToggled(d.b)
        const group = findChild(canvas, "nodeGraphGroupButton")
        verify(group.enabled)
        mouseClick(group)
        compare(d.doc.groups.length, 1)
        compare(d.doc.groups[0].nodeIds, [d.a, d.b])
        compare(canvas.selectedGroupId, d.doc.groups[0].groupId)
        compare(graph.groups.length, 1)
        // Dragging or nudging the group moves its members in one step.
        const revision = d.doc.revision
        graph.groupMoveRequested(canvas.selectedGroupId, 20, 30)
        compare(d.doc.revision, revision + 1)
        compare(nodeRow(d.doc, d.a).x, 30)
        compare(nodeRow(d.doc, d.b).y, 40)
        compare(nodeRow(d.doc, d.c).x, 500)
        graph.forceActiveFocus()
        keyClick(Qt.Key_G, Qt.ControlModifier | Qt.ShiftModifier) // ungroup
        compare(d.doc.groups.length, 0)
        compare(d.doc.nodes.length, 3)
        verify(d.doc.undo())
        compare(d.doc.groups.length, 1)
        // Ctrl+G on a card that is already grouped is refused and reported.
        graph.nodeSelected(d.a)
        graph.forceActiveFocus()
        keyClick(Qt.Key_G, Qt.ControlModifier)
        compare(d.doc.groups.length, 1)
        compare(graph.linkStatusState, "error")
        verify(graph.linkStatusText.indexOf("Group 1") >= 0)
        graph.nodeSelected(d.c)
        keyClick(Qt.Key_G, Qt.ControlModifier)
        compare(d.doc.groups.length, 2)
        compare(graph.linkStatusState, "info")
    }

    function test_align_and_distribute_selection() {
        const d = makeDiagram()
        const graph = canvas.graphView
        graph.nodeSelectionToggled(d.a)
        graph.nodeSelectionToggled(d.b)
        graph.nodeSelectionToggled(d.c)
        compare(canvas.selectionCount, 3)
        waitForRendering(canvas)
        const top = findChild(canvas, "diagramArrange_top")
        verify(top !== null && top.visible)
        verify(!findChild(canvas, "diagramTitleEditor").visible)
        mouseClick(top)
        compare(nodeRow(d.doc, d.a).y, 10)
        compare(nodeRow(d.doc, d.c).y, 10)
        verify(d.doc.undo())
        const revision = d.doc.revision
        verify(canvas.distributeSelection("horizontal"))
        compare(nodeRow(d.doc, d.b).x, 255)
        compare(d.doc.revision, revision + 1)
        verify(canvas.alignSelection("centerY"))
        compare(nodeRow(d.doc, d.a).y, 55)
        // Two cards cannot be distributed: the button is disabled and the
        // command reports the reason.
        graph.nodeSelectionToggled(d.c)
        compare(canvas.selectionCount, 2)
        waitForRendering(canvas)
        verify(!findChild(canvas, "diagramArrange_horizontal").enabled)
        verify(!canvas.distributeSelection("horizontal"))
        compare(graph.linkStatusState, "error")
    }
}
