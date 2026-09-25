// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

function nodeIndex(nodes, nodeId) {
    const source = nodes || []
    for (let i = 0; i < source.length; ++i)
        if (String(source[i].nodeId) === String(nodeId)) return i
    return -1
}

function nodeById(nodes, nodeId) {
    const index = nodeIndex(nodes, nodeId)
    return index < 0 ? null : nodes[index]
}

function portById(nodes, nodeId, portId, side) {
    const node = nodeById(nodes, nodeId)
    if (!node) return null
    const ports = side === "output" ? node.outputs || [] : node.inputs || []
    for (let i = 0; i < ports.length; ++i)
        if (String(ports[i].portId) === String(portId)) return ports[i]
    return null
}

function maxPortCount(nodes) {
    let count = 1
    const source = nodes || []
    for (let i = 0; i < source.length; ++i)
        count = Math.max(count, (source[i].inputs || []).length,
                         (source[i].outputs || []).length)
    return count
}

function portRole(type) {
    const key = String(type || "").toLowerCase()
    if (key.indexOf("audio") >= 0) return "info"
    if (key.indexOf("mask") >= 0 || key.indexOf("depth") >= 0) return "warning"
    if (key.indexOf("video") >= 0 || key.indexOf("image") >= 0) return "accent"
    if (key.indexOf("color") >= 0) return "success"
    return "textMuted"
}

function visibleNode(record, nodeWidth, nodeHeight, zoom, panX, panY, width, height) {
    const w = nodeWidth * zoom
    const h = nodeHeight * zoom
    const x = panX + Number(record.x || 0) * zoom
    const y = panY + Number(record.y || 0) * zoom
    return x + w >= 0 && y + h >= 0 && x <= width && y <= height
}

function edgePoint(nodes, nodeWidth, headerHeight, rowHeight, zoom, panX, panY,
                   nodeId, portId, side) {
    const node = nodeById(nodes, nodeId)
    if (!node) return Qt.point(0, 0)
    const ports = side === "output" ? node.outputs || [] : node.inputs || []
    let index = 0
    for (let i = 0; i < ports.length; ++i)
        if (String(ports[i].portId) === String(portId)) { index = i; break }
    const x = Number(node.x || 0) + (side === "output" ? nodeWidth : 0)
    const y = Number(node.y || 0) + headerHeight + rowHeight * (index + 0.5)
    return Qt.point(panX + x * zoom, panY + y * zoom)
}

// Deterministic source-to-target route. Reverse links travel above both cards
// so the path never crosses either endpoint card's interior.
function paintEdge(ctx, a, b, mode, stub, clearance) {
    ctx.moveTo(a.x, a.y)
    if (mode !== "orthogonal") {
        const dx = Math.max(stub, Math.abs(b.x - a.x) * 0.45)
        ctx.bezierCurveTo(a.x + dx, a.y, b.x - dx, b.y, b.x, b.y)
    } else if (b.x >= a.x + stub * 2) {
        const middle = (a.x + b.x) / 2
        ctx.lineTo(middle, a.y)
        ctx.lineTo(middle, b.y)
        ctx.lineTo(b.x, b.y)
    } else {
        const laneY = Math.min(a.y, b.y) - clearance
        ctx.lineTo(a.x + stub, a.y)
        ctx.lineTo(a.x + stub, laneY)
        ctx.lineTo(b.x - stub, laneY)
        ctx.lineTo(b.x - stub, b.y)
        ctx.lineTo(b.x, b.y)
    }
}

// Document-space polyline (DiagramDocument.routeEdges) mapped into the view.
function paintRoute(ctx, points, zoom, panX, panY) {
    for (let i = 0; i < points.length; ++i) {
        const x = panX + Number(points[i].x) * zoom
        const y = panY + Number(points[i].y) * zoom
        if (i === 0) ctx.moveTo(x, y)
        else ctx.lineTo(x, y)
    }
}

// Middle of a routed polyline's middle segment, else of the endpoints.
function edgeMidpoint(route, a, b, zoom, panX, panY) {
    const points = route && route.points ? route.points : []
    if (points.length < 2) return Qt.point((a.x + b.x) / 2, (a.y + b.y) / 2)
    const index = Math.floor((points.length - 2) / 2)
    const p = points[index], q = points[index + 1]
    return Qt.point(panX + (Number(p.x) + Number(q.x)) / 2 * zoom,
                    panY + (Number(p.y) + Number(q.y)) / 2 * zoom)
}

function groupById(groups, groupId) {
    const source = groups || []
    for (let i = 0; i < source.length; ++i)
        if (String(source[i].groupId) === String(groupId)) return source[i]
    return null
}

function groupContains(groups, groupId, nodeId) {
    const group = groupById(groups, groupId)
    return Boolean(group) && (group.nodeIds || []).map(String).indexOf(String(nodeId)) >= 0
}

// Document-space frame around a group's member cards, with a title strip.
function groupBounds(nodes, group, nodeWidth, nodeHeight, padding, header) {
    const ids = group ? group.nodeIds || [] : []
    let left = Infinity, top = Infinity, right = -Infinity, bottom = -Infinity
    for (let i = 0; i < ids.length; ++i) {
        const node = nodeById(nodes, ids[i])
        if (!node) continue
        const x = Number(node.x || 0)
        const y = Number(node.y || 0)
        left = Math.min(left, x)
        top = Math.min(top, y)
        right = Math.max(right, x + nodeWidth)
        bottom = Math.max(bottom, y + nodeHeight)
    }
    if (left === Infinity) return null
    return { x: left - padding, y: top - padding - header,
             width: right - left + padding * 2, height: bottom - top + padding * 2 + header }
}

// Case-insensitive match on title, type and ID, in snapshot order.
function searchNodes(nodes, text) {
    const query = String(text || "").trim().toLowerCase()
    const result = []
    const source = nodes || []
    for (let i = 0; query && i < source.length; ++i) {
        const node = source[i]
        const haystack = [node.title, node.type, node.nodeId].join(" ").toLowerCase()
        if (haystack.indexOf(query) >= 0) result.push(String(node.nodeId))
    }
    return result
}

// Keyboard link target: first free input of the armed type, then any free
// input, then the first input. The host still validates the request.
function targetInput(nodes, edges, nodeId, type) {
    const node = nodeById(nodes, nodeId)
    const inputs = node ? node.inputs || [] : []
    const occupied = function(port) {
        return (edges || []).some(function(edge) {
            return String(edge.toNodeId) === String(nodeId)
                && String(edge.toPortId) === String(port.portId)
        })
    }
    const free = inputs.filter(function(port) { return !occupied(port) })
    const typed = free.filter(function(port) { return String(port.type) === String(type) })
    const choice = typed.length ? typed[0] : free.length ? free[0] : inputs[0]
    return choice ? String(choice.portId) : ""
}
