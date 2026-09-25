// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// UV snapshots use normalized [0,1] coordinates with v=0 at the bottom.
// AGENT-CONTRACT: this view emits stable face/corner/vertex IDs; the host
// applies UV edits and publishes the resulting snapshot.
T.Control {
    id: control
    objectName: "uvEditor"
    property var faces: []
    property var vertices: []
    property var uvIslands: []
    property string selectedFaceId: ""
    property bool seamMode: false
    // In seam mode, stitch clicks weld a seam edge instead of toggling it.
    property bool stitchMode: false
    // Island rectangles from MeshRigDocument.packUvLayout(packMargin), drawn
    // as a preview outline; the host usually binds it while packHovered.
    property var packPreview: []
    property real packMargin: 0.02
    readonly property bool packHovered: packButton.hovered
    property string tooltip: qsTr("UV editor: drag a corner, or drag a face to move its island")
    property string dragFaceId: ""
    property int dragCorner: -1
    property point dragStart: Qt.point(0, 0)
    property point dragDelta: Qt.point(0, 0)
    signal faceSelected(string faceId)
    signal uvCornerMoveRequested(string faceId, int corner, point normalizedUv)
    signal uvIslandTranslateRequested(string faceId, point delta)
    signal seamToggleRequested(string vertexA, string vertexB)
    signal seamStitchRequested(string vertexA, string vertexB)
    signal uvIslandTransformRequested(string faceId, point translation, real rotationDegrees, real scale)
    signal uvUnwrapRequested(string faceId)
    signal uvPackRequested(real margin)

    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.waveformHeight * 6
    padding: Tk.Theme.space.md
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Canvas
    Accessible.name: tooltip
    Accessible.description: qsTr("Triangle UV layout; v=0 is at the bottom. Seams and edits are host-owned.")
    Accessible.focusable: enabled
    Accessible.focused: activeFocus

    function uvToPoint(uv) {
        const side = Math.max(1, Math.min(surface.width, surface.height) - Tk.Theme.space.xl * 2)
        const ox = (surface.width - side) / 2 + Tk.Theme.space.xl
        const oy = (surface.height - side) / 2 + Tk.Theme.space.xl
        return Qt.point(ox + Number(uv.x) * side, oy + (1 - Number(uv.y)) * side)
    }
    function pointToUv(point) {
        const side = Math.max(1, Math.min(surface.width, surface.height) - Tk.Theme.space.xl * 2)
        const ox = (surface.width - side) / 2 + Tk.Theme.space.xl
        const oy = (surface.height - side) / 2 + Tk.Theme.space.xl
        return Qt.point(Math.max(0, Math.min(1, (point.x - ox) / side)),
                        Math.max(0, Math.min(1, 1 - (point.y - oy) / side)))
    }
    function faceAt(point) {
        for (const face of (faces || [])) {
            if (!face.uv || face.uv.length !== 3) continue
            const a = uvToPoint(face.uv[0]), b = uvToPoint(face.uv[1]), c = uvToPoint(face.uv[2])
            const d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y)
            if (Math.abs(d) < 0.001) continue
            const u = ((b.y - c.y) * (point.x - c.x) + (c.x - b.x) * (point.y - c.y)) / d
            const v = ((c.y - a.y) * (point.x - c.x) + (a.x - c.x) * (point.y - c.y)) / d
            if (u >= 0 && v >= 0 && u + v <= 1) return face
        }
        return null
    }
    function closestCorner(face, point) {
        if (!face) return -1
        let selected = -1, best = Tk.Theme.size.controlSm
        for (let i = 0; i < face.uv.length; ++i) {
            const p = uvToPoint(face.uv[i])
            const distance = Math.hypot(point.x - p.x, point.y - p.y)
            if (distance <= best) { selected = i; best = distance }
        }
        return selected
    }
    function closestEdge(face, point) {
        let selected = 0, best = Infinity
        for (let i = 0; i < 3; ++i) {
            const a = uvToPoint(face.uv[i]), b = uvToPoint(face.uv[(i + 1) % 3])
            const dx = b.x - a.x, dy = b.y - a.y
            const t = Math.max(0, Math.min(1, ((point.x - a.x) * dx + (point.y - a.y) * dy) / Math.max(0.001, dx * dx + dy * dy)))
            const distance = Math.hypot(point.x - a.x - t * dx, point.y - a.y - t * dy)
            if (distance < best) { best = distance; selected = i }
        }
        return selected
    }
    function selectAt(point) {
        const face = faceAt(point)
        if (!face) return
        faceSelected(String(face.faceId))
        if (seamMode) {
            const i = closestEdge(face, point)
            const a = String(face.vertexIds[i]), b = String(face.vertexIds[(i + 1) % 3])
            if (stitchMode && face.seams && face.seams[i]) seamStitchRequested(a, b)
            else if (!stitchMode) seamToggleRequested(a, b)
        }
    }
    function rotateSelectedIsland(degrees) {
        if (!selectedFaceId) return false
        uvIslandTransformRequested(selectedFaceId, Qt.point(0, 0), degrees, 1)
        return true
    }
    function visibleUv(face, index) {
        if (dragFaceId === String(face.faceId) && dragCorner === index
                && (dragDelta.x !== 0 || dragDelta.y !== 0))
            return Qt.point(Math.max(0, Math.min(1, Number(dragStart.x) + dragDelta.x)),
                            Math.max(0, Math.min(1, Number(dragStart.y) + dragDelta.y)))
        return face.uv[index]
    }

    background: Rectangle { color: Tk.Theme.color.panel; border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border; border.width: Tk.Theme.size.border; radius: Tk.Theme.radius.md }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Overline { text: qsTr("UV layout · %1 islands").arg(control.uvIslands ? control.uvIslands.length : 0) }
            Tk.Spacer { size: Tk.Theme.space.sm }
            Tk.Button {
                objectName: "uvEditMode"
                text: control.seamMode ? qsTr("Seams") : qsTr("UVs")
                variant: control.seamMode ? "accent" : "default"
                small: true
                tooltip: qsTr("Toggle between UV corner editing and seam marking")
                onClicked: control.seamMode = !control.seamMode
            }
            Tk.Button {
                objectName: "uvStitchMode"
                visible: control.seamMode
                text: qsTr("Stitch")
                small: true
                checkable: true
                checked: control.stitchMode
                tooltip: qsTr("Click a seam edge to weld its two UV islands")
                onClicked: control.stitchMode = !control.stitchMode
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Button {
                objectName: "uvRotate"
                text: qsTr("Rotate")
                small: true
                enabled: control.selectedFaceId.length > 0
                tooltip: qsTr("Rotate the selected UV island 90 degrees (R)")
                onClicked: control.rotateSelectedIsland(90)
            }
            Tk.Button {
                objectName: "uvUnwrap"
                text: qsTr("Unwrap")
                small: true
                enabled: control.selectedFaceId.length > 0
                tooltip: qsTr("Project the selected UV island flat onto its best-fit plane")
                onClicked: control.uvUnwrapRequested(control.selectedFaceId)
            }
            Tk.Button {
                id: packButton
                objectName: "uvPack"
                text: qsTr("Pack")
                small: true
                enabled: control.uvIslands && control.uvIslands.length > 0
                tooltip: qsTr("Pack all UV islands into texture space; hover to preview")
                onClicked: control.uvPackRequested(control.packMargin)
            }
        }
        Item {
            id: surface
            objectName: "uvSurface"
            Tk.Flex.flex: 1
            implicitWidth: Tk.Theme.size.panelMinWidth * 2
            implicitHeight: Tk.Theme.size.panelMinWidth * 2
            Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas; border.color: Tk.Theme.color.border; border.width: Tk.Theme.size.border }
            Canvas {
                id: uvCanvas
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    for (const face of (control.faces || [])) {
                        if (!face.uv || face.uv.length !== 3) continue
                        const ps = [0, 1, 2].map(i => control.uvToPoint(control.visibleUv(face, i)))
                        ctx.beginPath(); ctx.moveTo(ps[0].x, ps[0].y); ctx.lineTo(ps[1].x, ps[1].y); ctx.lineTo(ps[2].x, ps[2].y); ctx.closePath()
                        ctx.fillStyle = String(face.faceId) === control.selectedFaceId ? Tk.Theme.color.selection : Tk.Theme.color.panelAlt
                        ctx.fill(); ctx.lineWidth = Tk.Theme.size.border; ctx.strokeStyle = Tk.Theme.color.controlBorder; ctx.stroke()
                        for (let i = 0; i < 3; ++i) {
                            if (!face.seams || !face.seams[i]) continue
                            ctx.beginPath(); ctx.moveTo(ps[i].x, ps[i].y); ctx.lineTo(ps[(i + 1) % 3].x, ps[(i + 1) % 3].y)
                            ctx.lineWidth = Tk.Theme.size.border * 2; ctx.strokeStyle = Tk.Theme.color.danger; ctx.stroke()
                        }
                        for (const p of ps) {
                            ctx.beginPath(); ctx.arc(p.x, p.y, Tk.Theme.space.sm, 0, Math.PI * 2)
                            ctx.fillStyle = Tk.Theme.color.accent; ctx.fill()
                        }
                    }
                    ctx.setLineDash([Tk.Theme.space.xs, Tk.Theme.space.xs])
                    ctx.lineWidth = Tk.Theme.size.border
                    ctx.strokeStyle = Tk.Theme.color.warning
                    for (const island of (control.packPreview || [])) {
                        const r = island.bounds
                        if (!r) continue
                        const a = control.uvToPoint(Qt.point(r.x, r.y + r.height))
                        const b = control.uvToPoint(Qt.point(r.x + r.width, r.y))
                        ctx.strokeRect(a.x, a.y, b.x - a.x, b.y - a.y)
                    }
                    ctx.setLineDash([])
                }
            }
            TapHandler { onTapped: function(eventPoint) { control.selectAt(eventPoint.position) } }
            DragHandler {
                target: null
                onActiveChanged: {
                    if (active) {
                        const start = centroid.pressPosition
                        const face = control.faceAt(start)
                        control.dragFaceId = face ? String(face.faceId) : ""
                        control.dragCorner = control.seamMode ? -1 : control.closestCorner(face, start)
                        control.dragStart = face && control.dragCorner >= 0 ? face.uv[control.dragCorner] : control.pointToUv(start)
                        control.dragDelta = Qt.point(0, 0)
                    } else if (control.dragFaceId) {
                        if (control.dragCorner >= 0) {
                            const uv = Qt.point(Math.max(0, Math.min(1, control.dragStart.x + control.dragDelta.x)), Math.max(0, Math.min(1, control.dragStart.y + control.dragDelta.y)))
                            control.uvCornerMoveRequested(control.dragFaceId, control.dragCorner, uv)
                        } else if (!control.seamMode) {
                            control.uvIslandTranslateRequested(control.dragFaceId, control.dragDelta)
                        }
                        control.dragFaceId = ""; control.dragCorner = -1; control.dragDelta = Qt.point(0, 0)
                    }
                }
                onTranslationChanged: {
                    control.dragDelta = Qt.point(translation.x / Math.max(1, Math.min(surface.width, surface.height) - Tk.Theme.space.xl * 2),
                                                -translation.y / Math.max(1, Math.min(surface.width, surface.height) - Tk.Theme.space.xl * 2))
                    uvCanvas.requestPaint()
                }
            }
        }
        Tk.Caption {
            text: control.seamMode
                ? (control.stitchMode ? qsTr("Click a red seam edge to weld the two UV islands across it.")
                                      : qsTr("Click a face edge to cut or clear a seam."))
                : qsTr("Drag a corner to move it; drag elsewhere in a face to move its UV island.")
            wrapMode: Text.Wrap
        }
    }
    onFacesChanged: uvCanvas.requestPaint()
    onSelectedFaceIdChanged: uvCanvas.requestPaint()
    onPackPreviewChanged: uvCanvas.requestPaint()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_S) { seamMode = !seamMode; event.accepted = true }
        else if (event.key === Qt.Key_R && rotateSelectedIsland(90)) event.accepted = true
    }
}
