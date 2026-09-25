// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "TraceView"
    when: windowShown
    visible: true
    width: 760
    height: 430
    Tk.TraceIndex { id: trace }
    Component { id: viewComponent; Tk.TraceView { width: 740; height: 400; index: trace } }
    Component { id: spyComponent; SignalSpy {} }

    function loadTrace() {
        verify(trace.setClockDomains([
            {id: "mono", nanosPerTick: "1000", offsetNs: "0"},
            {id: "gpu", nanosPerTick: "100", offsetNs: "100000"}
        ]))
        trace.ingestAsync([
            {id: "root", kind: "span", name: "Render frame", threadId: "ui",
             clockDomain: "mono", startTicks: "100", endTicks: "500"},
            {id: "gpuWork", kind: "span", name: "Composite", threadId: "gpu",
             parentId: "root", clockDomain: "gpu", startTicks: "2000", endTicks: "3000"}
        ])
        tryCompare(trace, "count", 2)
        tryCompare(trace, "busy", false)
    }

    function test_zoomSelectionAndExport() {
        loadTrace()
        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        waitForRendering(view)
        compare(view.visibleRecords.length, 2)
        const zoom = findChild(view, "traceZoomIn")
        verify(zoom)
        mouseClick(zoom)
        compare(view.zoom, 2)
        const bar = findChild(view, "traceRecord_gpuWork")
        verify(bar)
        mouseClick(bar)
        compare(view.selectedId, "gpuWork")
        compare(view.selectedRecord.startNs, "300000")
        const spy = createTemporaryObject(spyComponent, root,
                                          {target: view, signalName: "exportRequested"})
        mouseClick(findChild(view, "traceExport"))
        compare(spy.count, 1)
        verify(spy.signalArguments[0][0].includes("gpuWork"))
    }

    function test_rulerAndExactInspector() {
        loadTrace()
        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        waitForRendering(view)
        const ruler = findChild(view, "traceRuler")
        verify(ruler)
        compare(view.totalMicros, 400)   // 100,000 ns to 500,000 ns
        compare(ruler.originUnits, 0)
        compare(ruler.playheadUnits, -1)
        view.zoomBy(2)
        compare(view.pan, 0.25)
        compare(ruler.originUnits, 100)
        view.selectedId = "gpuWork"
        compare(ruler.playheadUnits, 200)  // 300,000 ns is 200 µs after the start
        compare(view.selectedRecord.durationNs, "100000")
        compare(view.selectedAncestors, ["root"])
        compare(view.selectedChildren.length, 0)
        const detail = findChild(view, "traceInspectorDetail")
        verify(detail)
        compare(detail.text, "100000 ns long · depth 1 · path root › gpuWork · 0 children")
        view.selectedId = "root"
        compare(view.selectedChildren.length, 1)
        compare(detail.text, "400000 ns long · depth 0 · path root · 1 children")
    }

    function test_cancelKeepsSnapshot() {
        loadTrace()
        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        const spy = createTemporaryObject(spyComponent, root,
                                          {target: trace, signalName: "analysisDiscarded"})
        trace.ingestAsync([
            {id: "later", kind: "event", name: "Later", threadId: "ui",
             clockDomain: "mono", startTicks: "900"}
        ])
        const cancel = findChild(view, "traceCancel")
        verify(cancel)
        compare(cancel.visible, true)
        cancel.clicked()
        compare(trace.busy, false)
        compare(cancel.visible, false)
        tryCompare(spy, "count", 1)
        compare(trace.count, 2)
        compare(view.visibleRecords.length, 2)
    }
}
