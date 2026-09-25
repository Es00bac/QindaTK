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
        compare(view.layout.matched, 2)
        const zoom = findChild(view, "traceZoomIn")
        verify(zoom)
        mouseClick(zoom)
        compare(view.zoom, 2)
        // Window 0.25..0.75: gpuWork (0.5..0.75 of the trace) fills the
        // right half of the gpu track, row 0.
        const canvas = findChild(view, "traceTracks")
        const timeline = findChild(view, "traceTimeline")
        verify(canvas && timeline)
        mouseClick(canvas, timeline.labelWidth + 0.75 * timeline.trackWidth,
                   timeline.rowHeight / 2)
        compare(view.selectedId, "gpuWork")
        compare(view.selectedRecord.startNs, "300000")
        const spy = createTemporaryObject(spyComponent, root,
                                          {target: view, signalName: "exportRequested"})
        mouseClick(findChild(view, "traceExport"))
        compare(spy.count, 0)   // serialized on a worker, delivered later
        tryCompare(spy, "count", 1)
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
        // An ID the trace does not hold says so instead of printing undefined.
        view.selectedId = "gone"
        compare(findChild(view, "traceInspector").text, "gone is not in this trace")
        compare(detail.visible, false)
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
        compare(view.layout.matched, 2)
    }

    function test_clippedSpanKeepsItsRealEnd() {
        loadTrace()
        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        // root covers 0..1 of the trace; gpuWork 0.5..0.75. At pan 0.6 and
        // zoom 2.5 the window is 0.6..1.0, so gpuWork is clipped on the left
        // and must end 37.5% of the way across, not run past its end.
        view.zoom = 2.5
        view.pan = 0.6
        const tile = view.layout.tiles.find(t => t.id === "gpuWork")
        verify(tile)
        compare(tile.x0, 0)
        fuzzyCompare(tile.x1, 0.375, 1e-9)
    }

    function test_largeTraceWithoutPerRecordDelegates() {
        verify(trace.setClockDomains([{id: "mono", nanosPerTick: "1000", offsetNs: "0"}]))
        const rows = [{id: "frame", kind: "span", name: "Frame", threadId: "gpu",
                       clockDomain: "mono", startTicks: "0", endTicks: "19998"}]
        for (let i = 0; i < 19999; ++i)
            rows.push({id: "e" + i, kind: "event", name: "Tick " + i, threadId: "ui",
                       clockDomain: "mono", startTicks: String(i)})
        trace.ingestAsync(rows)
        tryCompare(trace, "count", 20000, 10000)
        tryCompare(trace, "busy", false)
        const view = createTemporaryObject(viewComponent, root)
        verify(view)
        waitForRendering(view)
        compare(view.layout.matched, 20000)   // nothing hidden behind a cap
        let sum = 0
        for (const tile of view.layout.tiles) sum += tile.count
        compare(sum, 20000)
        verify(view.layout.tiles.length <= 2 * view.columns)
        verify(view.mergedMarks > 0)
        verify(findChild(view, "traceWindowSummary").text.startsWith("20000 in view"))
        compare(findChild(view, "traceRecord_e0"), null)

        // Double-click merged marks until e12345 has a mark of its own.
        const canvas = findChild(view, "traceTracks")
        const timeline = findChild(view, "traceTimeline")
        const target = 12345 / 19998
        for (let step = 0; step < 12; ++step) {
            const x = timeline.labelWidth
                      + (target - view.pan) / view.windowSize * timeline.trackWidth
            const tile = view.tileAt(x, 1.5 * timeline.rowHeight)
            verify(tile)
            if (tile.count === 1) break
            mouseDoubleClickSequence(canvas, x, 1.5 * timeline.rowHeight)
        }
        verify(view.zoom > 20)
        // Click the first pixel of the mark as drawn; a sub-pixel event
        // fills the pixel column it starts in.
        const mark = view.layout.tiles.find(t => t.id === "e12345")
        verify(mark)
        compare(mark.count, 1)
        const x = timeline.labelWidth + Math.floor(mark.x0 * view.columns)
        // Event timestamps are synthetic: delay this click past the double-click
        // interval, or it completes another double-click instead of selecting.
        mouseClick(canvas, x, 1.5 * timeline.rowHeight, Qt.LeftButton, Qt.NoModifier,
                   Qt.styleHints.mouseDoubleClickInterval + 50)
        compare(view.selectedId, "e12345")
        compare(view.selectedRecord.startNs, "12345000")
        view.zoom = 1
        view.pan = 0
        compare(view.selectedRecord.startNs, "12345000")   // exact after zooming out
    }
}
