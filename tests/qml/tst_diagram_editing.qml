// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-083: DiagramCanvas selection, snapping, routing, property editing and
// shortcuts, plus SankeyDiagram band placement and conservation display.
TestCase {
    id: testCase
    name: "DiagramEditing"
    when: windowShown
    visible: true
    width: 640
    height: 400

    Component { id: documentComponent; Tk.DiagramDocument {} }

    Tk.DiagramCanvas {
        id: canvas
        width: 640
        height: 260
    }
    Tk.SankeyDiagram {
        id: sankey
        y: 260
        width: 400
        height: 140
    }

    function makeDiagram() {
        const doc = createTemporaryObject(documentComponent, testCase)
        const a = doc.addNode("A", 10, 20)
        const b = doc.addNode("B", 320, 20)
        const c = doc.addNode("C", 10, 200)
        const out = doc.addPort(a, "Out", "output")
        const input = doc.addPort(b, "In", "input")
        const edge = doc.connectPorts(a, out, b, input)
        canvas.document = doc
        canvas.snapStep = 0
        canvas.clearSelection()
        return { doc: doc, a: a, b: b, c: c, out: out, input: input, edge: edge }
    }

    function nodeRow(doc, id) {
        for (let i = 0; i < doc.nodes.length; ++i)
            if (doc.nodes[i].nodeId === id) return doc.nodes[i]
        return null
    }

    function test_group_move_is_one_step() {
        const d = makeDiagram()
        const graph = canvas.graphView
        graph.nodeSelectionToggled(d.a)
        graph.nodeSelectionToggled(d.b)
        compare(canvas.selectedNodeIds.length, 2)
        compare(canvas.selectedNodeId, d.b)
        const revision = d.doc.revision
        graph.nodeMoveRequested(d.a, 40, 50)
        compare(d.doc.revision, revision + 1)
        compare(nodeRow(d.doc, d.a).x, 40)
        compare(nodeRow(d.doc, d.a).y, 50)
        compare(nodeRow(d.doc, d.b).x, 350)
        compare(nodeRow(d.doc, d.b).y, 50)
        compare(nodeRow(d.doc, d.c).x, 10)
        compare(d.doc.edges[0].fromPortId, d.out)
        verify(d.doc.undo())
        compare(nodeRow(d.doc, d.a).x, 10)
        compare(nodeRow(d.doc, d.b).x, 320)
        // A card outside the multi-selection moves alone.
        graph.nodeSelected(d.c)
        graph.nodeMoveRequested(d.c, 30, 220)
        compare(nodeRow(d.doc, d.c).x, 30)
        compare(nodeRow(d.doc, d.a).x, 10)
        graph.nodeSelectionToggled(d.b)
        compare(canvas.selectedNodeIds.length, 1)
        compare(canvas.selectedNodeIds[0], d.a)
    }

    function test_snap_step_rounds_moves() {
        const d = makeDiagram()
        canvas.snapStep = 8
        canvas.graphView.nodeSelected(d.c)
        canvas.graphView.nodeMoveRequested(d.c, 13, 205)
        compare(nodeRow(d.doc, d.c).x, 16)
        compare(nodeRow(d.doc, d.c).y, 208)
        canvas.snapStep = 0
    }

    function test_delete_removes_selection_in_one_step() {
        const d = makeDiagram()
        const graph = canvas.graphView
        graph.nodeSelectionToggled(d.a)
        graph.nodeSelectionToggled(d.c)
        compare(canvas.selectedNodeId, d.c)
        verify(graph.requestRemoveSelection())
        compare(d.doc.nodes.length, 1)
        compare(d.doc.nodes[0].nodeId, d.b)
        compare(d.doc.edges.length, 0)
        compare(canvas.selectedNodeIds.length, 0)
        verify(d.doc.undo())
        compare(d.doc.nodes.length, 3)
        compare(d.doc.edges[0].edgeId, d.edge)
    }

    function test_canvas_draws_document_routes() {
        const d = makeDiagram()
        const routes = canvas.graphView.edgeRoutes
        const expected = d.doc.routeEdges(canvas.routeMetrics)
        verify(routes[d.edge] !== undefined)
        compare(routes[d.edge].points.length, expected[d.edge].points.length)
        verify(routes[d.edge].clear)
        const first = routes[d.edge].points[0]
        compare(first.x, 10 + canvas.graphView.nodeWidth)
        verify(d.doc.moveNode(d.b, 520, 20))
        compare(canvas.graphView.edgeRoutes[d.edge].points[
                    canvas.graphView.edgeRoutes[d.edge].points.length - 1].x, 520)
    }

    function test_inspector_edits_title_and_flow() {
        const d = makeDiagram()
        const graph = canvas.graphView
        const inspector = findChild(canvas, "diagramInspector")
        const titleEditor = findChild(canvas, "diagramTitleEditor")
        const flowEditor = findChild(canvas, "diagramFlowEditor")
        verify(!inspector.visible)
        graph.nodeSelected(d.a)
        verify(inspector.visible)
        verify(titleEditor.visible)
        compare(titleEditor.text, "A")
        titleEditor.text = "Alpha"
        titleEditor.accepted()
        compare(nodeRow(d.doc, d.a).title, "Alpha")
        titleEditor.text = "   "
        titleEditor.accepted()
        compare(nodeRow(d.doc, d.a).title, "Alpha")
        compare(titleEditor.text, "Alpha")
        graph.edgeSelected(d.edge)
        verify(flowEditor.visible)
        verify(!titleEditor.visible)
        flowEditor.valueModified(7)
        compare(d.doc.edges[0].flow, 7)
        verify(d.doc.undo())
        compare(d.doc.edges[0].flow, 1)
        compare(flowEditor.value, 1)
    }

    function test_shortcuts_and_cancel() {
        const d = makeDiagram()
        const graph = canvas.graphView
        graph.forceActiveFocus()
        verify(graph.activeFocus)
        graph.nodeSelected(d.c)
        graph.nodeMoveRequested(d.c, 90, 210)
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(nodeRow(d.doc, d.c).x, 10)
        keyClick(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
        compare(nodeRow(d.doc, d.c).x, 90)
        graph.beginOrFinishLink(d.a, d.out, "output")
        compare(graph.pendingNode, d.a)
        keyClick(Qt.Key_Escape)
        compare(graph.pendingNode, "")
        compare(d.doc.edges.length, 1)
    }

    function makeFlow() {
        const doc = createTemporaryObject(documentComponent, testCase)
        const s = doc.addNode("Supply", 0, 0)
        const m = doc.addNode("Mill", 0, 0)
        const x = doc.addNode("Export", 0, 0)
        const y = doc.addNode("Local", 0, 0)
        const so = doc.addPort(s, "out", "output")
        const mi = doc.addPort(m, "in", "input")
        const mo = doc.addPort(m, "out", "output")
        const xi = doc.addPort(x, "in", "input")
        const yi = doc.addPort(y, "in", "input")
        doc.setEdgeFlow(doc.connectPorts(s, so, m, mi), 10)
        doc.setEdgeFlow(doc.connectPorts(m, mo, x, xi), 6)
        const my = doc.connectPorts(m, mo, y, yi)
        doc.setEdgeFlow(my, 4)
        sankey.document = doc
        return { doc: doc, s: s, m: m, x: x, y: y, my: my }
    }

    function test_sankey_places_bands_from_document() {
        const f = makeFlow()
        verify(sankey.layout.valid)
        compare(findChild(sankey, "sankeyEmpty").visible, false)
        const nodes = sankey.layout.nodes
        for (let i = 0; i < nodes.length; ++i) {
            const bar = findChild(sankey, "sankeyNode_" + nodes[i].nodeId)
            verify(bar !== null)
            compare(bar.x, sankey.margin + nodes[i].x)
            compare(bar.y, sankey.margin + nodes[i].y)
            compare(bar.height, Math.max(Tk.Theme.size.border, nodes[i].height))
        }
        const mill = findChild(sankey, "sankeyNode_" + f.m)
        const export_ = findChild(sankey, "sankeyNode_" + f.x)
        const local = findChild(sankey, "sankeyNode_" + f.y)
        // Mill value 10 = Export 6 + Local 4, so heights are conserved on one scale.
        fuzzyCompare(mill.height, export_.height + local.height, 1e-9)
        verify(findChild(sankey, "sankeyLink_" + f.my) !== null)
        verify(Qt.colorEqual(mill.color, Tk.Theme.color.accent))
        compare(sankey.imbalances.length, 0)

        verify(f.doc.setEdgeFlow(f.my, 3))
        compare(sankey.imbalances.length, 1)
        verify(Qt.colorEqual(findChild(sankey, "sankeyNode_" + f.m).color, Tk.Theme.color.warning))
        verify(findChild(sankey, "sankeyNode_" + f.m).tooltip.indexOf("imbalance 1") >= 0)
    }

    function test_sankey_empty_document() {
        sankey.document = createTemporaryObject(documentComponent, testCase)
        verify(!sankey.layout.valid)
        verify(findChild(sankey, "sankeyEmpty").visible)
    }
}
