// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// Constraint and tool commands for CadViewport (docs/cad.md). Every function
// takes the CadDocument; nothing here keeps state.

function entityKind(document, id) {
    if (!document || !id) return ""
    return String(document.measure(String(id)).kind || "")
}

// Kinds the current selection can take, in toolbar order.
// AGENT-CONTRACT (CadDocument::addConstraint): the reference shapes here
// must match the kinds the document accepts, or buttons offer rejected edits.
function constraintOptions(document, ids) {
    const kinds = (ids || []).map(function(id) { return entityKind(document, id) })
    if (kinds.length === 1 && kinds[0] === "segment")
        return ["horizontal", "vertical", "distance", "angle"]
    if (kinds.length === 2 && kinds[0] === "point" && kinds[1] === "point")
        return ["coincident", "distance"]
    if (kinds.length === 2 && kinds.indexOf("segment") >= 0 && kinds.indexOf("circle") >= 0)
        return ["tangent"]
    return []
}

// Adds `kind` for the selection. Distance and angle fix the current measured
// value, so the sketch does not jump; setDimension edits it afterwards.
function applyConstraint(document, ids, kind) {
    if (constraintOptions(document, ids).indexOf(kind) < 0) return ""
    const first = String(ids[0])
    if (kind === "tangent") {
        const segment = entityKind(document, first) === "segment" ? first : String(ids[1])
        const circle = segment === first ? String(ids[1]) : first
        return document.addConstraint("tangent", segment, circle, 0)
    }
    if (ids.length === 1) {
        const measured = document.measure(first)
        if (kind === "distance") {
            const segment = segmentById(document, first)
            return document.addConstraint("distance", segment.startPointId,
                                          segment.endPointId, measured.length)
        }
        return document.addConstraint(kind, first, "", kind === "angle" ? measured.angleDegrees : 0)
    }
    const a = document.measure(first), b = document.measure(String(ids[1]))
    return document.addConstraint(kind, first, String(ids[1]),
                                  kind === "distance" ? Math.hypot(b.x - a.x, b.y - a.y) : 0)
}

function segmentById(document, id) {
    const source = document.segments
    for (let i = 0; i < source.length; ++i)
        if (String(source[i].entityId) === String(id)) return source[i]
    return null
}

// Constraints that reference the selection, for the inspector's remove list.
// A segment also owns the distance between its endpoints.
function constraintsFor(document, ids) {
    if (!document) return []
    const wanted = {}
    for (let i = 0; i < (ids || []).length; ++i) {
        wanted[String(ids[i])] = true
        const segment = segmentById(document, ids[i])
        if (segment) wanted[segment.startPointId + "|" + segment.endPointId] = true
    }
    return document.constraints.filter(function(c) {
        return wanted[String(c.firstId)] || wanted[String(c.secondId)]
            || wanted[c.firstId + "|" + c.secondId] || wanted[c.secondId + "|" + c.firstId]
    })
}

// Creates the pending first location (if it is not an existing point) and
// the finished entity in one document transaction: one undo step, and no
// orphan point when the entity is rejected.
function finishTool(document, pending, build) {
    document.beginTransaction()
    const centerId = pending.pointId ? String(pending.pointId)
                                     : document.addPoint(pending.x, pending.y)
    const id = centerId ? build(centerId) : ""
    if (id) document.commitTransaction()
    else document.cancelTransaction()
    return id || ""
}
