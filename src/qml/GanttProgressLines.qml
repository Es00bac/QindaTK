// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Internal part of GanttView (D-313): progress lines. `view.progressLines`
// is [{utc, role}] (role: a Tk.Theme.color role, default danger). For each
// line and each task row in view the polyline runs from the line's instant
// at the row's top to the task's progress point at its middle and back at
// its bottom, so a task behind schedule peaks left and one ahead peaks
// right. A row keeps the line straight when it is a group, a summary,
// complete, or not started and due after the line (MS Project's rule).
Canvas {
    id: lines
    // AGENT-NOTE: `var`, not the owner's type: a part typed with the control
    // that instantiates it is a cyclic type, which qmllint reports as
    // "cannot assign X to X" (the GridCellDelegate / GridFillHandle precedent).
    required property var view
    property real contentY: 0
    objectName: "ganttProgressLines"
    visible: view.count > 0 && (view.progressLines || []).length > 0

    // Track x of a row's progress point against a line at `statusUtc`, or
    // the line's own x when the row keeps it straight.
    function pointFor(record, statusUtc, statusX) {
        const v = view
        const kind = v.kindOf(record)
        const progress = Math.max(0, Math.min(1, Number(record.progress || 0)))
        if (kind === "group" || kind === "summary" || progress >= 1) return statusX
        if (progress <= 0 && Number(record.startUtc) >= Number(statusUtc)) return statusX
        if (record.progressUtc) return v.xFor(record.progressUtc)
        const start = v.xFor(record.startUtc)
        if (kind === "milestone") return start
        const pieces = record.segments && record.segments.length > 0 ? record.segments : [] // a sequence, not an Array
        if (pieces.length === 0) return start + Math.max(0, v.xFor(v.endFor(record)) - start) * progress
        let total = 0
        for (let i = 0; i < pieces.length; ++i)
            total += Math.max(0, v.xFor(pieces[i].finishUtc) - v.xFor(pieces[i].startUtc))
        let left = total * progress
        for (let j = 0; j < pieces.length; ++j) {
            const from = v.xFor(pieces[j].startUtc)
            const span = Math.max(0, v.xFor(pieces[j].finishUtc) - from)
            if (left <= span) return from + left
            left -= span
        }
        return v.xFor(v.endFor(record))
    }
    // The vertices of line `index` over the rows in view (canvas coordinates).
    function pointsFor(index) {
        const v = view
        const line = (v.progressLines || [])[index]
        if (!v.taskModel || !line || !line.utc) return []
        const statusUtc = String(line.utc)
        const statusX = v.xFor(statusUtc)
        const rowHeight = Math.max(1, v.rowHeight)
        const first = Math.max(0, Math.floor(contentY / rowHeight))
        const last = Math.min(v.count, Math.ceil((contentY + height) / rowHeight))
        const out = []
        for (let i = first; i < last; ++i) {
            const top = i * rowHeight - contentY
            const x = pointFor(v.taskModel.displayGet(i), statusUtc, statusX)
            out.push({ x: statusX, y: top })
            if (Math.abs(x - statusX) > 0.5) out.push({ x: x, y: top + rowHeight / 2 })
            out.push({ x: statusX, y: top + rowHeight })
        }
        return out
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const count = (view.progressLines || []).length
        ctx.lineWidth = Tk.Theme.size.focusRing
        ctx.lineJoin = "round"
        for (let n = 0; n < count; ++n) {
            const pts = pointsFor(n)
            if (pts.length < 2) continue
            const role = String(view.progressLines[n].role || "danger")
            ctx.strokeStyle = Tk.Theme.color[role] || Tk.Theme.color.danger
            ctx.beginPath()
            ctx.moveTo(pts[0].x, pts[0].y)
            for (let k = 1; k < pts.length; ++k) ctx.lineTo(pts[k].x, pts[k].y)
            ctx.stroke()
        }
    }
    Connections {
        target: lines.view.taskModel
        function onPreviewChanged() { lines.requestPaint() }
        function onRowsChanged() { lines.requestPaint() }
    }
    Connections {
        target: lines.view
        function onVisibleStartUtcChanged() { lines.requestPaint() }
        function onPixelsPerDayChanged() { lines.requestPaint() }
        function onPixelsPerWorkingMinuteChanged() { lines.requestPaint() }
        function onTimeScaleChanged() { lines.requestPaint() }
        function onRowHeightChanged() { lines.requestPaint() }
        function onCalendarRevisionChanged() { lines.requestPaint() }
        function onProgressLinesChanged() { lines.requestPaint() }
    }
    Connections {
        target: Tk.Theme
        function onChanged() { lines.requestPaint() }
    }
    onContentYChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
}
