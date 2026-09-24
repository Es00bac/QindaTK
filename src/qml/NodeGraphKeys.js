// SPDX-License-Identifier: LGPL-3.0-or-later
.pragma library

// Keyboard alternatives for every NodeGraph pointer gesture (docs/controls.md).
// Returns true when the key was handled; NodeGraph accepts it then.
function handle(graph, event) {
    const modifiers = event.modifiers & ~Qt.KeypadModifier
    if (modifiers & (Qt.AltModifier | Qt.MetaModifier)) return false
    const shift = (modifiers & Qt.ShiftModifier) !== 0
    const key = event.key
    if (modifiers & Qt.ControlModifier) {
        if (key === Qt.Key_F && !shift) return graph.focusSearch()
        if (key === Qt.Key_G) return shift ? graph.requestUngroup() : graph.requestGroup()
        if (key === Qt.Key_Space && !shift && graph.nodeById(graph.selectedNodeId)) {
            graph.nodeSelectionToggled(graph.selectedNodeId)
            return true
        }
        return false
    }
    if (key === Qt.Key_F3) return graph.findNext(shift)
    if (key === Qt.Key_Plus || (key === Qt.Key_Equal && !shift)) {
        graph.setZoom(graph.zoom * 1.15)
        return true
    }
    const step = graph.nudgeStep
    const arrows = [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down]
    const arrow = arrows.indexOf(key)
    if (shift) {
        if (arrow < 0) return false
        return graph.nudge(arrow === 0 ? -step : arrow === 1 ? step : 0,
                           arrow === 2 ? -step : arrow === 3 ? step : 0)
    }
    if (key === Qt.Key_Minus) {
        graph.setZoom(graph.zoom / 1.15)
        return true
    }
    if (key === Qt.Key_Escape) {
        graph.cancelInteraction()
        return true
    }
    if (key === Qt.Key_Space) return graph.armKeyboardLink()
    if (key === Qt.Key_Return || key === Qt.Key_Enter) return graph.finishKeyboardLink()
    if (key === Qt.Key_Delete || key === Qt.Key_Backspace) return graph.requestRemoveSelection()
    if (arrow >= 0) return graph.selectAdjacent(arrow === 0 || arrow === 2 ? -1 : 1)
    return false
}
