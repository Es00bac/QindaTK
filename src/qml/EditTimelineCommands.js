// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// Keyboard and assistive clip commands for Tk.EditTimeline. Pure functions
// over the timeline (its model, viewport, selection and current IDs); every
// document change is one TimelineModel.applyEdit call, so each command is one
// undo step and a rejected command changes nothing.

function clipRecord(timeline, id) {
    return timeline.model && id.length > 0 ? timeline.model.clipData(id) : ({})
}

function selectionOrCurrent(timeline) {
    if (timeline.selectedClipIds.length > 0) return timeline.selectedClipIds
    return timeline.currentClipId.length > 0 ? [timeline.currentClipId] : []
}

function anchorOf(timeline, ids) {
    return ids.indexOf(timeline.currentClipId) >= 0 ? timeline.currentClipId : ids[0]
}

// Keyboard steps follow the snap grid; with snapping off they use frames.
function stepGrid(timeline) {
    return timeline.snapGrid === "none" ? "frames" : timeline.snapGrid
}

// Shift+click selects the clips between the anchor and the target across the
// tracks in between, in visible order, through SelectionModel.extendTo.
function selectClip(timeline, id, modifiers, shiftModifier, controlModifier) {
    const selection = timeline.selectionModel
    if (!selection || id.length === 0) return
    const anchor = selection.anchorId
    if (modifiers & controlModifier) {
        selection.toggle(id)
    } else if ((modifiers & shiftModifier) && anchor.length > 0 && timeline.model.hasClip(anchor)) {
        const a = clipRecord(timeline, anchor), b = clipRecord(timeline, id)
        const model = timeline.model
        const start = model.compareTicks(a.start, b.start) <= 0 ? a.start : b.start
        const end = model.compareTicks(a.end, b.end) >= 0 ? a.end : b.end
        const inside = model.clipIdsInRect(a.trackId, b.trackId, start, end)
                            .filter(function(other) { return other !== anchor && other !== id })
        selection.extendTo(id, [anchor].concat(inside, [id]))
    } else {
        selection.select(id)
    }
    timeline.setCurrentClip(id, false)
}

function moveCurrentTrack(timeline, delta) {
    const model = timeline.model
    const ids = model.visibleTrackIds()
    if (ids.length === 0) return
    const index = Math.max(0, Math.min(ids.length - 1, ids.indexOf(timeline.currentTrackId) + delta))
    timeline.currentTrackId = ids[index]
    timeline.viewport.ensureTrackVisible(timeline.currentTrackId)
    const reference = timeline.currentClipId.length > 0 ? clipRecord(timeline, timeline.currentClipId).start
                                                        : timeline.effectivePlayheadTicks
    const next = model.nearestClip(timeline.currentTrackId, reference)
    if (next.length > 0) {
        timeline.selectionModel.select(next)
        timeline.setCurrentClip(next, true)
    } else {
        timeline.currentClipId = ""
    }
}

// With no current clip, direction 1 goes to the first clip and -1 to the last.
function moveCurrentClip(timeline, direction, extend, shiftModifier) {
    const model = timeline.model
    if (timeline.currentTrackId.length === 0) timeline.currentTrackId = model.visibleTrackIds()[0] || ""
    const next = timeline.currentClipId.length > 0 ? model.adjacentClip(timeline.currentClipId, direction)
                                                   : model.edgeClip(timeline.currentTrackId, direction < 0)
    if (next.length === 0) return
    timeline.selectClip(next, extend ? shiftModifier : 0)
    timeline.setCurrentClip(next, true)
}

function nudgeSelection(timeline, steps, trackDelta) {
    const ids = selectionOrCurrent(timeline)
    if (!timeline.model || ids.length === 0) return false
    const anchor = anchorOf(timeline, ids)
    const start = clipRecord(timeline, anchor).start
    const target = steps === 0 ? start : timeline.model.stepTicks(start, stepGrid(timeline), steps)
    const ok = timeline.report(timeline.model.applyEdit(
        { kind: "move", clipIds: ids, anchorClipId: anchor, grabTicks: start, ripple: timeline.rippleMode },
        { pointerTicks: target, trackDelta: trackDelta || 0, snap: false }))
    if (ok) timeline.setCurrentClip(anchor, true)
    return ok
}

function trimSelection(timeline, edge, steps) {
    const ids = selectionOrCurrent(timeline)
    if (!timeline.model || ids.length === 0) return false
    const anchor = anchorOf(timeline, ids)
    const clip = clipRecord(timeline, anchor)
    const from = edge === "start" ? clip.start : clip.end
    return timeline.report(timeline.model.applyEdit(
        { kind: edge === "start" ? "trimStart" : "trimEnd", clipIds: ids, anchorClipId: anchor,
          grabTicks: from, ripple: timeline.rippleMode },
        { pointerTicks: timeline.model.stepTicks(from, stepGrid(timeline), steps), snap: false }))
}

function splitAtPlayhead(timeline) {
    const ids = selectionOrCurrent(timeline)
    if (!timeline.model || ids.length === 0) return false
    return timeline.report(timeline.model.applyEdit({ kind: "split", clipIds: ids },
                                                    { pointerTicks: timeline.effectivePlayheadTicks, snap: false }))
}

function deleteSelection(timeline, ripple) {
    const ids = selectionOrCurrent(timeline)
    if (!timeline.model || ids.length === 0) return false
    const next = timeline.model.adjacentClip(timeline.currentClipId, 1)
    if (!timeline.report(timeline.model.applyEdit({ kind: "delete", clipIds: ids, ripple: ripple }))) return false
    if (next.length > 0 && timeline.model.hasClip(next)) timeline.selectClip(next, 0)
    return true
}

function reorderCurrentTrack(timeline, delta) {
    if (!timeline.model || timeline.currentTrackId.length === 0) return false
    const ids = timeline.model.trackIds()
    const index = ids.indexOf(timeline.currentTrackId)
    const target = index + (delta > 0 ? 2 : -1)
    if (index < 0 || target < 0 || (delta > 0 && index + 1 >= ids.length)) return false
    const ok = timeline.report(timeline.model.applyEdit({ kind: "reorderTrack", trackId: timeline.currentTrackId,
                                                          beforeTrackId: target < ids.length ? ids[target] : "" }))
    timeline.viewport.ensureTrackVisible(timeline.currentTrackId)
    return ok
}
