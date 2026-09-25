// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "AssetDependencyView"
    when: windowShown
    visible: true
    width: 700
    height: 440

    Tk.AssetDependencyIndex { id: graph }
    Component {
        id: viewComponent
        Tk.AssetDependencyView { width: 660; height: 400; index: graph }
    }
    Component { id: spyComponent; SignalSpy {} }
    Component { id: projectIndexComponent; Tk.AssetDependencyIndex {} }

    function initTestCase() {
        verify(graph.upsertAsset("input", "Input asset", "input.svg", true))
        verify(graph.upsertAsset("output", "Generated output", "output.png", true))
        const token = graph.beginScan("output")
        verify(graph.commitScan("output", token, ["input", "lost.png"]))
    }

    function test_navigationAndRelinkRequest() {
        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        view.selectedId = "output"
        compare(view.selectedDependencies.length, 2)
        waitForRendering(view)
        const input = findChild(view, "dependencyInput_input")
        verify(input)
        mouseClick(input)
        compare(view.selectedId, "input")
        compare(view.selectedImpact.length, 1)
        compare(view.selectedImpact[0], "output")

        view.selectedId = "lost.png"
        compare(view.selectedDiagnostics.length, 1)
        const spy = createTemporaryObject(spyComponent, root,
                                          {target: view, signalName: "relinkRequested"})
        const relink = findChild(view, "dependencyRelink")
        verify(spy && relink)
        mouseClick(relink)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "lost.png")

        view.query = "generated"
        compare(view.visibleAssets.length, 1)
        compare(view.visibleAssets[0].id, "output")
    }

    function test_referencesLinesAndRelinkCandidates() {
        const project = createTemporaryObject(projectIndexComponent, root)
        project.projectRoot = Qt.resolvedUrl("../fixtures/asset_dependencies")
        compare(project.lastError, "")
        verify(project.scanQmlFile("Main.qml"))
        const view = createTemporaryObject(viewComponent, root, {index: project})
        verify(view)
        view.selectedId = "Main.qml"
        waitForRendering(view)
        const lostRow = findChild(view, "dependencyInput_images/lost.png")
        verify(lostRow)
        compare(lostRow.secondaryText, "line 10 · reference · Missing")
        const importRow = findChild(view, "dependencyInput_components/Panel.qml")
        verify(importRow)
        compare(importRow.secondaryText, "line 3 · import")

        view.selectedId = "images/lost.png"
        compare(view.relinkCandidates.length, 0)   // walked on a worker, not in a binding
        tryVerify(() => view.relinkCandidates.length === 1)
        compare(view.relinkCandidates[0], "archive/lost.png")
        compare(view.selectedDiagnostics[0].message,
                "Main.qml line 10 needs missing images/lost.png")
        waitForRendering(view)
        const spy = createTemporaryObject(spyComponent, root,
                                          {target: view, signalName: "relinkChosen"})
        const candidate = findChild(view, "dependencyCandidate_archive/lost.png")
        verify(candidate)
        // The row may sit below the detail pane's fold; emit its click
        // directly rather than depend on the scroll position.
        candidate.clicked()
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "images/lost.png")
        compare(spy.signalArguments[0][1], "archive/lost.png")
        const plan = spy.signalArguments[0][2]
        compare(plan.length, 1)
        compare(plan[0].sourceId, "Main.qml")
        compare(plan[0].line, 10)
        compare(plan[0].replacement, "archive/lost.png")
    }

    function chainIndex() {
        // output -> mid -> gone.png (missing); output and other -> input.
        const index = createTemporaryObject(projectIndexComponent, root)
        verify(index.upsertAsset("input", "Input", "input.svg", true))
        verify(index.upsertAsset("mid", "Mid", "mid.png", true, "output"))
        verify(index.upsertAsset("output", "Output", "output.png", true, "output"))
        verify(index.upsertAsset("other", "Other", "other.png", true, "output"))
        verify(index.commitScan("mid", index.beginScan("mid"), ["gone.png"]))
        verify(index.commitScan("output", index.beginScan("output"), ["mid", "input"]))
        verify(index.commitScan("other", index.beginScan("other"), ["input"]))
        return index
    }

    function test_rebuildGatedOnTransitiveInputs() {
        const view = createTemporaryObject(viewComponent, root, {index: chainIndex()})
        verify(view)
        view.selectedId = "output"
        waitForRendering(view)
        compare(view.selectedDiagnostics.length, 0)   // nothing wrong next to it
        compare(view.rebuildBlockers.length, 1)        // but two steps down
        const rebuild = findChild(view, "dependencyRebuild")
        verify(rebuild)
        compare(rebuild.available, false)
        compare(findChild(view, "dependencyRebuildBlocked").text,
                "Rebuild blocked: mid needs missing gone.png")
        const spy = createTemporaryObject(spyComponent, root,
                                          {target: view, signalName: "rebuildRequested"})
        mouseClick(rebuild)
        compare(spy.count, 0)

        view.selectedId = "other"
        compare(rebuild.available, true)
        mouseClick(rebuild)
        compare(spy.count, 1)

        view.selectedId = "input"
        compare(view.rebuildPlan.valid, ["other"])
        compare(view.rebuildPlan.blocked.length, 1)
        compare(findChild(view, "dependencyRebuildPlan").text,
                "Replacing it rebuilds other · 1 blocked")
        const affected = createTemporaryObject(spyComponent, root,
                                               {target: view, signalName: "rebuildAffectedRequested"})
        mouseClick(findChild(view, "dependencyRebuildAffected"))
        compare(affected.count, 1)
        compare(affected.signalArguments[0][0], "input")
        compare(affected.signalArguments[0][1], ["other"])
    }

    function test_graphModeNavigatesWithNodeGraph() {
        const view = createTemporaryObject(viewComponent, root, {index: chainIndex()})
        verify(view)
        view.selectedId = "output"
        const mode = findChild(view, "dependencyDetailMode")
        verify(mode)
        mode.currentIndex = 1
        waitForRendering(view)
        const graphView = findChild(view, "assetDependencyGraph")
        verify(graphView && graphView.visible)
        compare(findChild(view, "dependencyDetailScroll").visible, false)
        const ids = graphView.neighbourhood.nodes.map(n => n.nodeId).sort()
        compare(ids, ["input", "mid", "output"])
        compare(graphView.neighbourhood.edges.length, 2)
        const mid = graphView.neighbourhood.nodes.find(n => n.nodeId === "mid")
        const output = graphView.neighbourhood.nodes.find(n => n.nodeId === "output")
        verify(mid.x < output.x)   // inputs left of the selected asset
        // The whole neighbourhood is fitted inside the view, below the toolbar.
        const toolbar = findChild(graphView, "nodeGraphToolbar")
        for (const record of graphView.neighbourhood.nodes) {
            const item = findChild(graphView, "node_" + record.nodeId)
            verify(item)
            const box = item.mapToItem(graphView, 0, 0, item.width, item.height)
            verify(box.x >= 0 && box.x + box.width <= graphView.width + 0.5, record.nodeId)
            verify(box.y >= toolbar.y + toolbar.height, record.nodeId)
            verify(box.y + box.height <= graphView.height + 0.5, record.nodeId)
        }
        const node = findChild(view, "node_mid")
        verify(node)
        mouseClick(node, node.width / 2, Tk.Theme.size.header / 2)
        compare(view.selectedId, "mid")
        tryVerify(() => graphView.neighbourhood.nodes.some(n => n.nodeId === "gone.png"))
        verify(graphView.neighbourhood.nodes.find(n => n.nodeId === "gone.png")
               .title.endsWith("(missing)"))
        // Too narrow to fit even at the smallest zoom: the selection stays in view.
        view.width = 300
        waitForRendering(view)
        const selected = findChild(graphView, "node_mid")
        const box = selected.mapToItem(graphView, 0, 0, selected.width, selected.height)
        verify(box.x >= 0 && box.x + box.width <= graphView.width + 0.5)
    }
}
