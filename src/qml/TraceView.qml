// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (WP-087): TraceIndex owns exact times, lanes and the
// aggregated layout (trace_layout.cpp); this view projects normalized
// coordinates onto one canvas and never edits trace data.
// AGENT-GUARD (EXT-11): no per-record delegates. Everything in a window is
// drawn from layoutWindow's marks, whose count is bounded by rows × pixel
// columns; a Repeater over records would bring back the 500-record cap.
Item {
    id: root
    objectName: "traceView"
    property Tk.TraceIndex index: null
    property string tooltip: qsTr("Trace explorer")
    property string selectedId: ""
    property string threadFilter: ""
    property string textFilter: ""
    property real zoom: 1
    property real pan: 0
    property int dataRevision: 0
    property string pendingExport: ""
    readonly property real windowSize: 1 / zoom
    // Deepest zoom leaves a 50 ns window, so any record can get its own pixels.
    readonly property real maxZoom: {
        if (dataRevision < 0 || !index || index.count === 0) return 1
        return Math.max(1, (Number(index.endNs) - Number(index.startNs)) / 50)
    }
    readonly property var tracks: dataRevision >= 0 && index ? index.tracks(threadFilter) : []
    readonly property int columns: Math.max(1, Math.floor(timeline.trackWidth))
    readonly property int firstRow: Math.max(0, Math.floor(timeline.contentY / timeline.rowHeight))
    readonly property int visibleRows: Math.ceil(timeline.height / timeline.rowHeight) + 1
    // {rows, matched, drawn, tiles: [{row, x0, x1, count, id, kind, name, threadId}]}
    readonly property var layout: {
        if (dataRevision < 0 || !index || timeline.trackWidth <= 0)
            return ({rows: 0, matched: 0, drawn: 0, tiles: []})
        return index.layoutWindow(pan, pan + windowSize, columns, threadFilter, textFilter,
                                  firstRow, visibleRows)
    }
    readonly property int mergedMarks: layout.tiles.filter(tile => tile.count > 1).length
    readonly property var selectedRecord: {
        if (dataRevision < 0 || !index || !selectedId) return ({})
        return index.record(selectedId)
    }
    // Nearest parent first, as TraceIndex.ancestorsOf returns them.
    readonly property var selectedAncestors: {
        if (dataRevision < 0 || !index || !selectedId) return []
        return index.ancestorsOf(selectedId)
    }
    readonly property var selectedChildren: {
        if (dataRevision < 0 || !index || !selectedId) return []
        return index.childrenOf(selectedId)
    }
    // Display-only span of the snapshot in microseconds for the ruler.
    readonly property real totalMicros: {
        if (dataRevision < 0 || !index || index.count === 0) return 1
        return Math.max(0.001, (Number(index.endNs) - Number(index.startNs)) / 1000)
    }
    signal exportRequested(string json)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Connections {
        target: root.index
        function onChanged() { ++root.dataRevision }
        function onExportReady(requestId, json) {
            if (requestId !== root.pendingExport) return
            root.pendingExport = ""
            root.exportRequested(json)
        }
    }
    onLayoutChanged: canvas.requestPaint()
    onSelectedIdChanged: canvas.requestPaint()

    function zoomBy(factor) {
        const midpoint = pan + windowSize / 2
        zoom = Math.max(1, Math.min(maxZoom, zoom * factor))
        pan = Math.max(0, Math.min(1 - windowSize, midpoint - windowSize / 2))
    }
    // Zooms onto [x0, x1] of the current window (fractions), with a margin.
    function zoomToWindow(x0, x1) {
        const from = pan + x0 * windowSize, to = pan + x1 * windowSize
        const margin = Math.max((to - from) * 0.1, 1e-12)
        const midpoint = (from + to) / 2
        zoom = Math.max(1, Math.min(maxZoom, 1 / (to - from + 2 * margin)))
        pan = Math.max(0, Math.min(1 - windowSize, midpoint - windowSize / 2))
    }
    // Canvas coordinates → layout row and pixel column.
    function rowAt(y) { return Math.floor((y + timeline.contentY) / timeline.rowHeight) }
    function columnAt(x) { return Math.floor(x - timeline.labelWidth) }
    function tileAt(x, y) {
        const row = rowAt(y), column = columnAt(x)
        for (const tile of layout.tiles)
            if (tile.row === row && column >= Math.floor(tile.x0 * columns)
                && column < Math.max(Math.floor(tile.x0 * columns) + 1, Math.ceil(tile.x1 * columns)))
                return tile
        return null
    }
    function selectAt(x, y) {
        if (!index || x < timeline.labelWidth) return ""
        const id = index.recordAt(pan, pan + windowSize, columns, rowAt(y), columnAt(x),
                                  threadFilter, textFilter)
        if (id) selectedId = id
        return id
    }
    function exportTrace() {
        if (index && !pendingExport) pendingExport = index.exportJsonAsync()
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("TRACE"); Tk.Flex.grow: 1 }
            Tk.Caption {
                objectName: "traceCounts"
                text: !root.index ? ""
                      : root.index.busy ? qsTr("Loading trace…")
                      : qsTr("%1 records · %2 dropped · %3 rejected")
                        .arg(root.index.count).arg(root.index.truncatedCount)
                        .arg(root.index.rejectedCount)
            }
            Tk.Button {
                objectName: "traceCancel"
                visible: !!root.index && root.index.busy
                text: qsTr("Cancel"); tooltip: qsTr("Stop loading this trace")
                onClicked: root.index.cancel()
            }
            Tk.Button {
                objectName: "traceZoomOut"
                text: "−"; tooltip: qsTr("Zoom out")
                onClicked: root.zoomBy(0.5)
            }
            Tk.Button {
                objectName: "traceZoomIn"
                text: "+"; tooltip: qsTr("Zoom in")
                onClicked: root.zoomBy(2)
            }
            Tk.Button {
                objectName: "traceExport"
                text: root.pendingExport ? qsTr("Exporting…") : qsTr("Export")
                enabled: !root.pendingExport
                tooltip: qsTr("Export trace as JSON")
                onClicked: root.exportTrace()
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.ComboBox {
                objectName: "traceThreadFilter"
                model: [qsTr("All threads")].concat(root.index ? root.index.threads : [])
                tooltip: qsTr("Filter trace by thread")
                onCurrentIndexChanged: root.threadFilter = currentIndex > 0 ?
                                           root.index.threads[currentIndex - 1] : ""
            }
            Tk.SearchField {
                objectName: "traceSearch"
                Tk.Flex.grow: 1
                placeholderText: qsTr("Filter spans and events")
                tooltip: qsTr("Filter trace by name or category")
                onSearched: function(text) { root.textFilter = text }
            }
        }
        Tk.Caption {
            objectName: "traceError"
            visible: !!root.index && root.index.lastError.length > 0
            text: root.index ? root.index.lastError : ""
            color: Tk.Theme.color.danger
            elide: Text.ElideRight
        }
        Tk.Slider {
            objectName: "tracePan"
            from: 0
            to: Math.max(0, 1 - root.windowSize)
            value: root.pan
            enabled: root.zoom > 1
            tooltip: qsTr("Pan through trace time")
            onValueModified: function(value) { root.pan = value }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            Item {
                implicitWidth: timeline.labelWidth
                implicitHeight: Tk.Theme.size.timeRuler
            }
            // Shared ruler pattern: units are microseconds from the trace start.
            Tk.TimeRuler {
                id: ruler
                objectName: "traceRuler"
                Tk.Flex.flex: 1
                Tk.Flex.minWidth: 0
                tooltip: qsTr("Trace time")
                pixelsPerUnit: width * root.zoom / root.totalMicros
                originUnits: root.pan * root.totalMicros
                playheadUnits: root.selectedRecord.startNs === undefined || !root.index ? -1
                    : (Number(root.selectedRecord.startNs) - Number(root.index.startNs)) / 1000
                formatUnit: function(micros) {
                    return micros >= 1000 ? Number((micros / 1000).toPrecision(6)) + qsTr(" ms")
                                          : Number(micros.toPrecision(6)) + qsTr(" µs")
                }
            }
        }
        Flickable {
            id: timeline
            objectName: "traceTimeline"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: Tk.Theme.size.panelMinHeight
            implicitWidth: root.implicitWidth
            implicitHeight: Tk.Theme.size.panelMinHeight
            clip: true
            contentWidth: width
            contentHeight: Math.max(height, root.layout.rows * rowHeight)
            boundsBehavior: Flickable.StopAtBounds
            readonly property real labelWidth: Tk.Theme.size.panelMinWidth / 2
            readonly property real rowHeight: Tk.Theme.size.row
            readonly property real trackWidth: Math.max(0, width - labelWidth)
            // Ctrl+wheel zooms, Shift+wheel pans; a plain wheel scrolls the tracks.
            WheelHandler {
                target: null
                acceptedModifiers: Qt.ControlModifier
                onWheel: function(event) { root.zoomBy(event.angleDelta.y > 0 ? 2 : 0.5) }
            }
            WheelHandler {
                target: null
                acceptedModifiers: Qt.ShiftModifier
                onWheel: function(event) {
                    const step = root.windowSize * (event.angleDelta.y > 0 ? -0.1 : 0.1)
                    root.pan = Math.max(0, Math.min(1 - root.windowSize, root.pan + step))
                }
            }
            onContentYChanged: canvas.requestPaint()
            Canvas {
                id: canvas
                objectName: "traceTracks"
                y: timeline.contentY
                width: timeline.width
                height: timeline.height
                Accessible.role: Accessible.Chart
                Accessible.name: qsTr("Trace tracks, %1 records in view").arg(root.layout.matched)
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const rowHeight = timeline.rowHeight, left = timeline.labelWidth
                    const offset = timeline.contentY
                    const inset = Tk.Theme.space.xs
                    ctx.fillStyle = Tk.Theme.color.surface
                    ctx.fillRect(0, 0, width, height)
                    ctx.font = Tk.Theme.font.caption + "px \"" + Tk.Theme.font.family + "\""
                    ctx.textBaseline = "middle"
                    // Thread labels and track separators.
                    for (const track of root.tracks) {
                        const top = track.firstRow * rowHeight - offset
                        const bottom = top + track.lanes * rowHeight
                        if (bottom < 0 || top > height) continue
                        ctx.fillStyle = Tk.Theme.color.divider
                        ctx.fillRect(0, bottom - 1, width, 1)
                        ctx.save()
                        ctx.beginPath()
                        ctx.rect(0, top, left - inset, rowHeight)
                        ctx.clip()
                        ctx.fillStyle = Tk.Theme.color.textMuted
                        ctx.fillText(track.threadId, Tk.Theme.space.sm, top + rowHeight / 2)
                        ctx.restore()
                    }
                    // Whole pixel columns, as layoutWindow and recordAt count them.
                    const trackWidth = root.columns
                    for (const tile of root.layout.tiles) {
                        const x = left + tile.x0 * trackWidth
                        const w = Math.max(1, (tile.x1 - tile.x0) * trackWidth)
                        const y = tile.row * rowHeight - offset + inset
                        const h = rowHeight - 2 * inset
                        ctx.fillStyle = tile.count === 1 && tile.id === root.selectedId
                            ? Tk.Theme.color.accent
                            : tile.count > 1 ? Tk.Theme.color.info
                            : tile.kind === "event" ? Tk.Theme.color.warning
                            : Tk.Theme.color.selection
                        ctx.fillRect(x, y, w, h)
                        if (w < Tk.Theme.font.caption * 3) continue
                        ctx.save()
                        ctx.beginPath()
                        ctx.rect(x, y, w, h)
                        ctx.clip()
                        ctx.fillStyle = Tk.Theme.color.text
                        ctx.fillText(tile.count > 1 ? qsTr("%1 records").arg(tile.count) : tile.name,
                                     x + inset, y + h / 2)
                        ctx.restore()
                    }
                }
                MouseArea {
                    objectName: "traceTracksInput"
                    anchors.fill: parent
                    // Click selects the record under the pointer; double-click
                    // on a merged mark zooms until its records separate.
                    onClicked: function(mouse) { root.selectAt(mouse.x, mouse.y) }
                    onDoubleClicked: function(mouse) {
                        const tile = root.tileAt(mouse.x, mouse.y)
                        if (tile && tile.count > 1) root.zoomToWindow(tile.x0, tile.x1)
                    }
                }
            }
        }
        Tk.Caption {
            objectName: "traceWindowSummary"
            text: root.mergedMarks > 0
                  ? qsTr("%1 in view · %2 merged marks, double-click one to zoom in")
                    .arg(root.layout.matched).arg(root.mergedMarks)
                  : qsTr("%1 in view").arg(root.layout.matched)
            color: Tk.Theme.color.textMuted
            elide: Text.ElideRight
        }
        Tk.Caption {
            objectName: "traceInspector"
            text: root.selectedId && root.selectedRecord.id === undefined
                  ? qsTr("%1 is not in this trace").arg(root.selectedId)
                  : root.selectedId ? qsTr("%1 · %2 · %3 ns to %4 ns · %5 · parent %6")
                  .arg(root.selectedRecord.name).arg(root.selectedRecord.threadId)
                  .arg(root.selectedRecord.startNs).arg(root.selectedRecord.endNs)
                  .arg(root.selectedRecord.clockDomain)
                  .arg(root.selectedRecord.parentId || qsTr("none"))
                  : qsTr("Select a span or event to inspect exact times")
            elide: Text.ElideRight
        }
        Tk.Caption {
            objectName: "traceInspectorDetail"
            visible: !!root.selectedId && root.selectedRecord.id !== undefined
            text: qsTr("%1 ns long · depth %2 · path %3 · %4 children")
                  .arg(root.selectedRecord.durationNs || "").arg(root.selectedAncestors.length)
                  .arg(root.selectedAncestors.slice().reverse().concat([root.selectedId])
                       .join(" › "))
                  .arg(root.selectedChildren.length)
            elide: Text.ElideRight
        }
    }
}
