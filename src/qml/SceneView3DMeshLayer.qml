// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (D-179): SceneView3D's CPU mesh layer. It draws the
// selected face and the posed bones from MeshRigDocument.cameraOverlay (the
// exact inverse of meshRay), and while SceneView3D.meshTool is "select" or
// "paint" it owns left-button gestures on the mesh: selection emits stable
// face/bone IDs, paint emits the camera ray through the pixel under the
// pointer. It needs no RHI renderer, so selection sync and camera painting
// behave the same under every backend. Right/middle drags still reach the
// renderer's camera controls below.
Item {
    id: layer
    objectName: "sceneMeshLayer3D"
    property var view: null
    readonly property var overlay: {
        const doc = view ? view.meshDocument : null
        if (!doc || width <= 0 || height <= 0) return ({face: [], bones: []})
        // revision NOTIFYs on every document change (edits, poses, playhead).
        return doc.revision.length >= 0
            ? doc.cameraOverlay(view.cameraPosition, view.cameraEulerDegrees, view.cameraFieldOfView,
                                Qt.size(width, height), view.selectedMeshFaceId)
            : ({face: [], bones: []})
    }
    readonly property bool stroking: gesture.stroking

    // The posed bone whose projected segment is within reach of (x, y).
    function boneAt(x, y) {
        if (!view || !view.showMeshBones) return ""
        let best = "", bestDistance = Tk.Theme.space.md
        for (const bone of (overlay.bones || [])) {
            const dx = bone.tail.x - bone.head.x, dy = bone.tail.y - bone.head.y
            const length2 = dx * dx + dy * dy
            const t = length2 > 0 ? Math.max(0, Math.min(1, ((x - bone.head.x) * dx + (y - bone.head.y) * dy) / length2)) : 0
            const distance = Math.hypot(x - bone.head.x - t * dx, y - bone.head.y - t * dy)
            if (distance <= bestDistance) { best = String(bone.boneId); bestDistance = distance }
        }
        return best
    }
    function cancelStroke() {
        if (!gesture.stroking) return false
        gesture.stroking = false
        view.meshStrokeCancelled()
        return true
    }
    onOverlayChanged: canvas.requestPaint()
    Connections {
        target: layer.view
        function onSelectedBoneIdChanged() { canvas.requestPaint() }
        function onShowMeshBonesChanged() { canvas.requestPaint() }
    }

    Canvas {
        id: canvas
        objectName: "sceneMeshOverlay3D"
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const face = layer.overlay.face || []
            if (face.length === 3) {
                ctx.beginPath()
                ctx.moveTo(face[0].x, face[0].y); ctx.lineTo(face[1].x, face[1].y); ctx.lineTo(face[2].x, face[2].y)
                ctx.closePath()
                ctx.fillStyle = Tk.Theme.color.selection
                ctx.fill()
                ctx.lineWidth = Tk.Theme.size.border * 2
                ctx.strokeStyle = Tk.Theme.color.accent
                ctx.stroke()
            }
            if (!layer.view || !layer.view.showMeshBones) return
            for (const bone of (layer.overlay.bones || [])) {
                const chosen = String(bone.boneId) === layer.view.selectedBoneId
                ctx.lineWidth = Tk.Theme.size.border * (chosen ? 3 : 2)
                ctx.strokeStyle = chosen ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                ctx.fillStyle = ctx.strokeStyle
                ctx.beginPath(); ctx.moveTo(bone.head.x, bone.head.y); ctx.lineTo(bone.tail.x, bone.tail.y); ctx.stroke()
                ctx.beginPath(); ctx.arc(bone.head.x, bone.head.y, Tk.Theme.space.xs + 1, 0, Math.PI * 2); ctx.fill()
            }
        }
    }
    MouseArea {
        id: gesture
        objectName: "sceneMeshGesture3D"
        anchors.fill: parent
        enabled: Boolean(layer.view && layer.view.meshDocument)
                 && (layer.view.meshTool === "select" || layer.view.meshTool === "paint")
        acceptedButtons: Qt.LeftButton
        cursorShape: layer.view && layer.view.meshTool === "paint" ? Qt.CrossCursor : Qt.ArrowCursor
        property bool stroking: false
        function sendRay(x, y) {
            const ray = layer.view.meshRay(x, y)
            if (ray.origin) layer.view.meshPaintRayRequested(ray.origin, ray.direction)
            return Boolean(ray.origin)
        }
        onPressed: function(mouse) {
            layer.view.forceActiveFocus()
            if (layer.view.meshTool === "paint") {
                stroking = true
                layer.view.meshStrokeBegan()
                sendRay(mouse.x, mouse.y)
                return
            }
            const bone = layer.boneAt(mouse.x, mouse.y)
            if (bone.length) { layer.view.meshBonePicked(bone); return }
            const face = layer.view.pickMeshFace(mouse.x, mouse.y)
            if (face.faceId) layer.view.meshFacePicked(face.faceId, face.uv)
        }
        onPositionChanged: function(mouse) { if (stroking) sendRay(mouse.x, mouse.y) }
        onReleased: {
            if (!stroking) return
            stroking = false
            layer.view.meshStrokeFinished()
        }
        onCanceled: layer.cancelStroke()
    }
}
