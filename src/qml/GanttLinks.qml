// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk
import "GanttScale.js" as GanttScale

// Internal part of GanttView (D-303): dependency links for the successors
// in view. `linkTypes` picks the anchors — FS end→start, SS start→start,
// FF end→end, SF start→end — and each link ends in an arrowhead at the
// successor; a link joining two critical tasks is drawn in `danger`.
Canvas {
    id: links
    required property Tk.GanttView view
    property real contentY: 0
    objectName: "ganttDependencies"
    visible: view.count > 0

    // Paint-time geometry of one row's interval (milestones span their diamond).
    function endpoints(record) {
        const v = view
        const start = v.xFor(record.startUtc)
        const milestone = v.kindOf(record) === "milestone"
        const half = milestone ? Math.round(v.rowHeight * 0.29) : 0
        const end = milestone ? start : v.xFor(v.endFor(record))
        return { start: start - half, end: Math.max(start, end) + half }
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const v = view
        if (!v.taskModel) return
        const rowHeight = Math.max(1, v.rowHeight)
        const gap = Tk.Theme.space.md
        const head = Tk.Theme.space.sm
        const first = Math.max(0, Math.floor(contentY / rowHeight))
        const last = Math.min(v.count, Math.ceil((contentY + height) / rowHeight))
        ctx.lineWidth = Tk.Theme.size.border
        ctx.lineJoin = "round"
        for (let i = first; i < last; ++i) {
            const task = v.taskModel.displayGet(i)
            const deps = task.dependencies || []
            const types = task.linkTypes || []
            const to = endpoints(task)
            const y2 = i * rowHeight - contentY + rowHeight / 2
            for (let k = 0; k < deps.length; ++k) {
                const p = v.taskModel.indexOfId(String(deps[k]))
                if (p < 0) continue
                const predecessor = v.taskModel.displayGet(p)
                const from = endpoints(predecessor)
                const type = String(types[k] || "FS")
                const exitRight = type === "FS" || type === "FF"
                const enterLeft = type === "FS" || type === "SS"
                const y1 = p * rowHeight - contentY + rowHeight / 2
                const route = GanttScale.linkRoute(
                    { x: exitRight ? from.end : from.start, y: y1 },
                    { x: enterLeft ? to.start : to.end, y: y2 },
                    exitRight, enterLeft, gap, rowHeight)
                const tone = task.critical === true && predecessor.critical === true
                    ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                ctx.strokeStyle = tone
                ctx.fillStyle = tone
                ctx.beginPath()
                const pts = route.points
                ctx.moveTo(pts[0].x, pts[0].y)
                for (let j = 1; j < pts.length - 1; ++j) ctx.lineTo(pts[j].x, pts[j].y)
                const end = pts[pts.length - 1]
                ctx.lineTo(end.x + (route.arrowRight ? -head : head), end.y)
                ctx.stroke()
                ctx.beginPath()
                const dir = route.arrowRight ? 1 : -1
                ctx.moveTo(end.x, end.y)
                ctx.lineTo(end.x - dir * head, end.y - head / 2 - Tk.Theme.size.border)
                ctx.lineTo(end.x - dir * head, end.y + head / 2 + Tk.Theme.size.border)
                ctx.closePath()
                ctx.fill()
            }
        }
    }
    Connections {
        target: links.view.taskModel
        function onPreviewChanged() { links.requestPaint() }
        function onRowsChanged() { links.requestPaint() }
    }
    Connections {
        target: links.view
        function onVisibleStartUtcChanged() { links.requestPaint() }
        function onPixelsPerDayChanged() { links.requestPaint() }
        function onPixelsPerWorkingMinuteChanged() { links.requestPaint() }
        function onTimeScaleChanged() { links.requestPaint() }
        function onRowHeightChanged() { links.requestPaint() }
        function onCalendarRevisionChanged() { links.requestPaint() }
    }
    onContentYChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
}
