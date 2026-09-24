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
