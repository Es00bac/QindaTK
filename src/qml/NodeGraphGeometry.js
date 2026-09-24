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
