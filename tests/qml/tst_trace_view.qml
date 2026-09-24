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

    function test_zoomSelectionAndExport() {
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
}
