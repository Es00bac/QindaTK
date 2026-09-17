// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// Drop-target geometry for Tk.DockHost drags. Pure functions over the
// host (for frames, size and the model); DockHost.qml calls them and
// DockDropOverlay draws the result.

// A band of a frame maps to a DockModel.dockBeside placement. "before"/
// "after" follow the slot order inside the lane; "lane-before" is a new
// lane nearer the host edge, "lane-after" one nearer the canvas.
function placementFor(zone, band) {
    if (band === "center") {
        return "tab"
    }
    switch (zone) {
    case "left":
        return band === "top" ? "before" : band === "bottom" ? "after" : band === "left" ? "lane-before" : "lane-after"
    case "right":
        return band === "top" ? "before" : band === "bottom" ? "after" : band === "right" ? "lane-before" : "lane-after"
    case "top":
    case "center":
        return band === "left" ? "before" : band === "right" ? "after" : band === "top" ? "lane-before" : "lane-after"
    case "bottom":
        return band === "left" ? "before" : band === "right" ? "after" : band === "bottom" ? "lane-before" : "lane-after"
    }
    return "tab"
}

function edgeTargets(host, panelId, edge) {
    const info = host.panelInfo(panelId)
    const extent = info.extent !== undefined ? info.extent : 280
    const w = host.width
    const h = host.height
    const all = [
        { "zone": "left", "rect": Qt.rect(0, 0, edge, h), "preview": Qt.rect(0, 0, extent, h) },
        { "zone": "right", "rect": Qt.rect(w - edge, 0, edge, h), "preview": Qt.rect(w - extent, 0, extent, h) },
        { "zone": "top", "rect": Qt.rect(0, 0, w, edge), "preview": Qt.rect(0, 0, w, extent) },
        { "zone": "bottom", "rect": Qt.rect(0, h - edge, w, edge), "preview": Qt.rect(0, h - extent, w, extent) }
    ]
    return all.filter(function(t) { return host.model.acceptsZone(panelId, t.zone) })
}

function inside(p, r) {
    return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height
}

function previewFor(host, frame, r, placement) {
    const side = frame.zone === "left" || frame.zone === "right"
    if (placement === "tab") {
        return r
    }
    if (placement === "before") {
        return side ? Qt.rect(r.x, r.y, r.width, r.height / 2) : Qt.rect(r.x, r.y, r.width / 2, r.height)
    }
    if (placement === "after") {
        return side ? Qt.rect(r.x, r.y + r.height / 2, r.width, r.height / 2)
                    : Qt.rect(r.x + r.width / 2, r.y, r.width / 2, r.height)
    }
    const laneItem = frame.parent
    const l = laneItem ? laneItem.mapToItem(host, 0, 0, laneItem.width, laneItem.height) : r
    const info = host.panelInfo(host.dragId)
    const extent = info.extent !== undefined ? info.extent : 280
    const towardEdge = placement === "lane-before"
    switch (frame.zone) {
    case "left":
        return towardEdge ? Qt.rect(l.x - extent, l.y, extent, l.height) : Qt.rect(l.x + l.width, l.y, extent, l.height)
    case "right":
        return towardEdge ? Qt.rect(l.x + l.width, l.y, extent, l.height) : Qt.rect(l.x - extent, l.y, extent, l.height)
    case "bottom":
        return towardEdge ? Qt.rect(l.x, l.y + l.height, l.width, extent) : Qt.rect(l.x, l.y - extent, l.width, extent)
    default:
        return towardEdge ? Qt.rect(l.x, l.y - extent, l.width, extent) : Qt.rect(l.x, l.y + l.height, l.width, extent)
    }
}

// The target under host-local point p: a host-edge zone first, then a
// 5-way band of a docked frame (25% bands, centre = tab), else null.
function hitTest(host, p) {
    for (const t of host.edgeTargets) {
        if (inside(p, t.rect)) {
            return { "kind": "zone", "zone": t.zone, "preview": t.preview }
        }
    }
    const seen = []
    for (const id in host.frames) {
        const frame = host.frames[id]
        if (!frame || seen.indexOf(frame) >= 0) {
            continue
        }
        seen.push(frame)
        if (frame.panelIds.indexOf(host.dragId) >= 0 || !host.model.acceptsZone(host.dragId, frame.zone)) {
            continue
        }
        const r = frame.mapToItem(host, 0, 0, frame.width, frame.height)
        if (!inside(p, r)) {
            continue
        }
        const rx = (p.x - r.x) / Math.max(r.width, 1)
        const ry = (p.y - r.y) / Math.max(r.height, 1)
        const band = 0.25
        const which = rx < band ? "left" : rx > 1 - band ? "right" : ry < band ? "top" : ry > 1 - band ? "bottom" : "center"
        const placement = placementFor(frame.zone, which)
        return { "kind": "beside", "target": frame.activeId, "placement": placement,
                 "preview": previewFor(host, frame, r, placement) }
    }
    return null
}
