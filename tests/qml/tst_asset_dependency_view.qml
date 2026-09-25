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
        compare(view.relinkCandidates.length, 1)
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
}
