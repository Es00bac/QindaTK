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
}
