// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (WP-076, docs/scene-3d.md#transform-gizmo): `view` owns the
// projection. It provides projectWorldPosition(vector3d) -> vector3d(x, y,
// depth) in this item's pixels (negative depth: not visible) and
// viewRay(x, y) -> {origin, direction} in world space; SceneView3D is the
// standard adapter and calls refresh() when its camera or renderer changes.
// SceneModel3D owns the transaction, world/local basis, snapping, parent
// inversion and undo. This item only draws handles and turns one drag into
// beginTransform, previews measured from the press, then commit or cancel.
Item {
    id: root
    objectName: "transformGizmo3D"
    property Tk.SceneModel3D scene: null
    property Item view: null
    property string mode: "translate" // translate, rotate, scale
    property string space: "world" // world, local
    property real translationSnap: 0
    property real rotationSnap: 0
    property real scaleSnap: 0
    // Handles keep this screen length at every camera distance and zoom.
    property real armLength: Tk.Theme.size.action * 2
    property real hitTolerance: Tk.Theme.size.handle / 2
    property string tooltip: qsTr("Transform handles: drag an arrow or square to move, a ring to rotate or the circle to scale; Escape cancels")
    readonly property var handles: state.geometry
    readonly property string activeHandle: state.gesture ? state.gesture.handle : ""
    readonly property string hoveredHandle: state.hovered

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("3D transform handles")
    Accessible.description: tooltip

    QtObject {
        id: state
        property var geometry: ({visible: false})
        property string signature: ""
        property var gesture: null
        property string hovered: ""
        property bool awaitingFrame: false
    }

    // Deferred relayout plus one after the next frame: Qt Quick 3D may only
    // apply a camera or size change to its projection when it synchronizes.
    function refresh() {
        state.awaitingFrame = true
        Qt.callLater(root.relayout)
    }
    function relayout() {
        const geometry = computeGeometry()
        const signature = JSON.stringify(geometry)
        if (signature === state.signature) return
        state.signature = signature
        state.geometry = geometry
        canvas.requestPaint()
    }
    function rotated(q, v) {
        // v + 2w(u x v) + 2u x (u x v) for the unit quaternion (w, u).
        const cx = q.y * v.z - q.z * v.y, cy = q.z * v.x - q.x * v.z, cz = q.x * v.y - q.y * v.x
        return Qt.vector3d(v.x + 2 * (q.scalar * cx + q.y * cz - q.z * cy),
                           v.y + 2 * (q.scalar * cy + q.z * cx - q.x * cz),
                           v.z + 2 * (q.scalar * cz + q.x * cy - q.y * cx))
    }
    function polygonArea(points) {
        let area = 0
        for (let i = 0; i < points.length; ++i) {
            const a = points[i], b = points[(i + 1) % points.length]
            area += a.x * b.y - b.x * a.y
        }
        return Math.abs(area) / 2
    }
    function computeGeometry() {
        const hidden = {visible: false}
        if (!visible || !scene || !view || !scene.selectedId.length || width <= 0 || height <= 0)
            return hidden
        const pose = scene.worldTransform(scene.selectedId)
        if (!pose.position) return hidden
        const pivot = pose.position
        const center = view.projectWorldPosition(pivot)
        if (!(center.z >= 0) || !isFinite(center.x) || !isFinite(center.y)) return hidden
        const rayA = view.viewRay(center.x, center.y), rayB = view.viewRay(center.x + 1, center.y)
        if (!rayA.direction || !rayB.direction) return hidden
        // World units per pixel at the pivot's depth, for perspective or orthographic views.
        const depth = pivot.minus(rayA.origin).dotProduct(rayA.direction)
        const perPixel = rayB.origin.plus(rayB.direction.times(depth)).minus(pivot).length()
        if (!(perPixel > 0) || !isFinite(perPixel)) return hidden
        const length = armLength * perPixel
        const units = [Qt.vector3d(1, 0, 0), Qt.vector3d(0, 1, 0), Qt.vector3d(0, 0, 1)]
        const axes = space === "local" ? units.map(u => rotated(pose.rotation, u)) : units
        const names = ["x", "y", "z"]
        const project = function(point) {
            const p = view.projectWorldPosition(point)
            return {x: p.x, y: p.y, front: p.z >= 0 && isFinite(p.x) && isFinite(p.y)}
        }
        const g = {visible: true, mode: mode, space: space, length: length,
                   origin: {x: center.x, y: center.y}, pivot: {x: pivot.x, y: pivot.y, z: pivot.z},
                   axes: [], planes: [], rings: []}
        for (let i = 0; i < 3; ++i) {
            const end = project(pivot.plus(axes[i].times(length)))
            g.axes.push({name: names[i], direction: {x: axes[i].x, y: axes[i].y, z: axes[i].z},
                         end: {x: end.x, y: end.y},
                         // An axis within about 11 degrees of the sight line has no usable drag direction.
                         usable: end.front && Math.hypot(end.x - center.x, end.y - center.y) >= armLength * 0.2,
                         towardViewer: axes[i].dotProduct(rayA.direction) <= 0})
        }
        if (mode === "translate") {
            const pairs = [[0, 1, "xy"], [0, 2, "xz"], [1, 2, "yz"]]
            for (const pair of pairs) {
                const a = axes[pair[0]].times(length), b = axes[pair[1]].times(length)
                const corners = [[0.25, 0.25], [0.5, 0.25], [0.5, 0.5], [0.25, 0.5]]
                    .map(c => project(pivot.plus(a.times(c[0])).plus(b.times(c[1]))))
                g.planes.push({name: pair[2], corners: corners.map(c => ({x: c.x, y: c.y})),
                               usable: corners.every(c => c.front)
                                   && polygonArea(corners) >= Math.pow(armLength * 0.25, 2) * 0.2})
            }
        } else if (mode === "rotate") {
            const segments = 48
            for (let i = 0; i < 3; ++i) {
                // Ring i spans the other two axes in cyclic order, so +theta is a positive turn.
                const u = axes[(i + 1) % 3].times(length), v = axes[(i + 2) % 3].times(length)
                const points = []
                for (let s = 0; s < segments; ++s) {
                    const angle = 2 * Math.PI * s / segments
                    points.push(project(pivot.plus(u.times(Math.cos(angle))).plus(v.times(Math.sin(angle)))))
                }
                g.rings.push({name: names[i], points: points.map(p => ({x: p.x, y: p.y})),
                              // An almost edge-on ring (under ~6 degrees) cannot be dragged around its centre.
                              usable: points.every(p => p.front)
                                  && polygonArea(points) >= Math.PI * armLength * armLength * 0.1})
            }
        }
        return g
    }
    function segmentDistance(px, py, a, b) {
        const dx = b.x - a.x, dy = b.y - a.y, squared = dx * dx + dy * dy
        const t = squared > 0 ? Math.max(0, Math.min(1, ((px - a.x) * dx + (py - a.y) * dy) / squared)) : 0
        return Math.hypot(px - a.x - t * dx, py - a.y - t * dy)
    }
    function inside(points, x, y) {
        let result = false
        for (let i = 0, j = points.length - 1; i < points.length; j = i++) {
            const a = points[i], b = points[j]
            if ((a.y > y) !== (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
                result = !result
        }
        return result
    }
    // Returns x/y/z (axis or ring), xy/xz/yz (plane), uniform (scale) or "".
    function handleAt(x, y) {
        const g = state.geometry
        // AGENT-GUARD: the centre stays with the object so SceneView3D can still
        // pick and drag it; handles begin at 30% of the arm length.
        if (!g.visible || Math.hypot(x - g.origin.x, y - g.origin.y) < armLength * 0.3) return ""
        let best = "", bestDistance = hitTolerance
        if (g.mode === "translate") {
            for (const plane of g.planes)
                if (plane.usable && inside(plane.corners, x, y)) return plane.name
            for (const axis of g.axes) {
                if (!axis.usable) continue
                const dx = axis.end.x - g.origin.x, dy = axis.end.y - g.origin.y
                const reach = 1 + Tk.Theme.size.grip / Math.hypot(dx, dy)
                const d = segmentDistance(x, y, g.origin, {x: g.origin.x + dx * reach, y: g.origin.y + dy * reach})
                if (d <= bestDistance) { best = axis.name; bestDistance = d }
            }
        } else if (g.mode === "rotate") {
            for (const ring of g.rings) {
                if (!ring.usable) continue
                for (let i = 0; i < ring.points.length; ++i) {
                    const d = segmentDistance(x, y, ring.points[i], ring.points[(i + 1) % ring.points.length])
                    if (d <= bestDistance) { best = ring.name; bestDistance = d }
                }
            }
        } else if (Math.abs(Math.hypot(x - g.origin.x, y - g.origin.y) - armLength) <= bestDistance) {
            best = "uniform"
        }
        return best
    }
    function planeHit(x, y, pivot, normal) {
        const ray = view ? view.viewRay(x, y) : ({})
        if (!ray.direction) return null
        const denominator = ray.direction.dotProduct(normal)
        if (Math.abs(denominator) < 1e-4) return null
        return ray.origin.plus(ray.direction.times(pivot.minus(ray.origin).dotProduct(normal) / denominator))
    }
    function preview(x, y) {
        const gesture = state.gesture
        // Escape or a host cancel ended the transaction; ignore the rest of the drag.
        if (!gesture || !gesture.scene.editing) return
        const g = gesture.geometry, target = gesture.scene
        const axis = g.axes.find(a => a.name === gesture.handle)
        if (gesture.mode === "translate" && axis) {
            // The pointer's travel along the drawn arrow, in world units at the pivot.
            const dx = axis.end.x - g.origin.x, dy = axis.end.y - g.origin.y, pixels = Math.hypot(dx, dy)
            const along = ((x - gesture.x) * dx + (y - gesture.y) * dy) / pixels
            target.previewAxisTranslation(along / pixels * g.length, axis.name, gesture.space, translationSnap)
        } else if (gesture.mode === "translate") {
            const n = g.axes.find(a => gesture.handle.indexOf(a.name) < 0).direction
            const normal = Qt.vector3d(n.x, n.y, n.z), pivot = Qt.vector3d(g.pivot.x, g.pivot.y, g.pivot.z)
            const from = planeHit(gesture.x, gesture.y, pivot, normal), to = planeHit(x, y, pivot, normal)
            if (from && to)
                target.previewConstrainedTranslation(to.minus(from), gesture.handle, gesture.space, translationSnap)
        } else if (gesture.mode === "rotate" && axis) {
            // Screen y points down, so a counter-clockwise sweep as seen is a negative angle step.
            const angle = Math.atan2(y - g.origin.y, x - g.origin.x)
            let step = angle - gesture.angle
            if (step > Math.PI) step -= 2 * Math.PI
            else if (step < -Math.PI) step += 2 * Math.PI
            gesture.angle = angle
            gesture.turned += step
            target.previewRotation(-gesture.turned * 180 / Math.PI * (axis.towardViewer ? 1 : -1),
                                   axis.name, gesture.space, rotationSnap)
        } else if (gesture.mode === "scale") {
            const start = Math.max(1, Math.hypot(gesture.x - g.origin.x, gesture.y - g.origin.y))
            target.previewUniformScale(Math.max(0.001, Math.hypot(x - g.origin.x, y - g.origin.y) / start), scaleSnap)
        }
    }
    function finishGesture(commit) {
        const gesture = state.gesture
        state.gesture = null
        if (gesture && gesture.scene.editing) {
            if (commit) gesture.scene.commitTransform()
            else gesture.scene.cancelTransform()
        }
        canvas.requestPaint()
    }
    function cancelGesture() {
        if (state.gesture) finishGesture(false)
    }
    // Assistive and scripted path: one committed step on a visible handle.
    function nudge(handle, steps) {
        if (!scene || !scene.selectedId.length || scene.editing || !targetFor(handle)
            || !scene.beginTransform(scene.selectedId)) return false
        let ok = false
        if (mode === "translate")
            ok = scene.previewAxisTranslation(steps * (translationSnap > 0 ? translationSnap : 1), handle, space, 0)
        else if (mode === "rotate")
            ok = scene.previewRotation(steps * (rotationSnap > 0 ? rotationSnap : 15), handle, space, 0)
        else
            ok = scene.previewUniformScale(Math.pow(1.25, steps), 0)
        if (ok) return scene.commitTransform()
        scene.cancelTransform()
        return false
    }
    function targetFor(name) {
        const g = state.geometry
        if (!g.visible) return null
        if (g.mode === "scale")
            return name === "uniform" ? {x: g.origin.x + armLength, y: g.origin.y, label: qsTr("Scale uniformly")} : null
        const axis = g.axes.find(a => a.name === name)
        if (!axis) return null
        if (g.mode === "rotate") {
            const ring = g.rings.find(r => r.name === name)
            return ring.usable ? {x: ring.points[6].x, y: ring.points[6].y,
                                  label: qsTr("Rotate around %1").arg(name.toUpperCase())} : null
        }
        return axis.usable ? {x: axis.end.x, y: axis.end.y, label: qsTr("Move along %1").arg(name.toUpperCase())} : null
    }
    function axisColor(name) {
        if (name === "x" || name === "yz") return Tk.Theme.color.danger
        if (name === "y" || name === "xz") return Tk.Theme.color.success
        return Tk.Theme.color.info
    }
    function paintHandles(context) {
        context.clearRect(0, 0, canvas.width, canvas.height)
        const g = state.geometry
        if (!g.visible) return
        const lit = activeHandle.length ? activeHandle : hoveredHandle
        context.lineWidth = Tk.Theme.size.focusRing
        context.lineCap = "round"
        if (g.mode === "translate") {
            for (const plane of g.planes) {
                if (!plane.usable) continue
                context.globalAlpha = plane.name === lit ? Tk.Theme.opacity.muted : Tk.Theme.opacity.disabled
                context.fillStyle = plane.name === lit ? Tk.Theme.color.text : axisColor(plane.name)
                context.beginPath()
                context.moveTo(plane.corners[0].x, plane.corners[0].y)
                for (let i = 1; i < 4; ++i) context.lineTo(plane.corners[i].x, plane.corners[i].y)
                context.closePath()
                context.fill()
            }
            context.globalAlpha = Tk.Theme.opacity.full
            for (const axis of g.axes) {
                if (!axis.usable) continue
                const dx = axis.end.x - g.origin.x, dy = axis.end.y - g.origin.y, pixels = Math.hypot(dx, dy)
                const ux = dx / pixels, uy = dy / pixels, tip = Tk.Theme.size.grip
                context.strokeStyle = context.fillStyle = axis.name === lit ? Tk.Theme.color.text : axisColor(axis.name)
                context.beginPath()
                context.moveTo(g.origin.x, g.origin.y)
                context.lineTo(axis.end.x, axis.end.y)
                context.stroke()
                context.beginPath()
                context.moveTo(axis.end.x + ux * tip, axis.end.y + uy * tip)
                context.lineTo(axis.end.x - uy * tip / 2, axis.end.y + ux * tip / 2)
                context.lineTo(axis.end.x + uy * tip / 2, axis.end.y - ux * tip / 2)
                context.closePath()
                context.fill()
            }
        } else if (g.mode === "rotate") {
            for (const ring of g.rings) {
                if (!ring.usable) continue
                context.strokeStyle = ring.name === lit ? Tk.Theme.color.text : axisColor(ring.name)
                context.beginPath()
                context.moveTo(ring.points[0].x, ring.points[0].y)
                for (let i = 1; i < ring.points.length; ++i) context.lineTo(ring.points[i].x, ring.points[i].y)
                context.closePath()
                context.stroke()
            }
        } else {
            context.strokeStyle = lit === "uniform" ? Tk.Theme.color.text : Tk.Theme.color.accent
            context.beginPath()
            context.arc(g.origin.x, g.origin.y, armLength, 0, 2 * Math.PI)
            context.stroke()
        }
        context.fillStyle = Tk.Theme.color.accent
        context.beginPath()
        context.arc(g.origin.x, g.origin.y, Tk.Theme.space.xs, 0, 2 * Math.PI)
        context.fill()
    }

    onVisibleChanged: { if (!visible) cancelGesture(); refresh() }
    onSceneChanged: { cancelGesture(); refresh() }
    onViewChanged: refresh()
    onModeChanged: refresh()
    onSpaceChanged: refresh()
    onArmLengthChanged: refresh()
    onWidthChanged: refresh()
    onHeightChanged: refresh()
    Component.onCompleted: refresh()
    Component.onDestruction: cancelGesture()
    Connections {
        target: root.scene
        function onChanged() { root.refresh() }
        function onSelectionChanged() { root.refresh() }
    }
    Connections {
        target: root.Window.window
        enabled: state.awaitingFrame
        function onFrameSwapped() {
            state.awaitingFrame = false
            root.relayout()
        }
    }
    Connections {
        target: Tk.Theme
        function onChanged() { canvas.requestPaint() }
    }

    Canvas {
        id: canvas
        objectName: "transformGizmoCanvas3D"
        anchors.fill: parent
        onPaint: root.paintHandles(getContext("2d"))
    }
    MouseArea {
        id: pointer
        objectName: "transformGizmoPointer3D"
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: root.activeHandle.length || root.hoveredHandle.length ? Qt.SizeAllCursor : Qt.ArrowCursor
        onPressed: function(mouse) {
            const handle = root.handleAt(mouse.x, mouse.y)
            const target = root.scene
            // Anything but a handle falls through to the view's pick, drag and camera gestures.
            if (!handle.length || !target || target.editing || !target.beginTransform(target.selectedId)) {
                mouse.accepted = false
                return
            }
            if (root.view) root.view.forceActiveFocus()
            const g = state.geometry
            state.gesture = {scene: target, handle: handle, geometry: g, mode: g.mode, space: g.space,
                             x: mouse.x, y: mouse.y, turned: 0,
                             angle: Math.atan2(mouse.y - g.origin.y, mouse.x - g.origin.x)}
            canvas.requestPaint()
        }
        onPositionChanged: function(mouse) {
            if (state.gesture) {
                root.preview(mouse.x, mouse.y)
                return
            }
            const hovered = root.handleAt(mouse.x, mouse.y)
            if (hovered === state.hovered) return
            state.hovered = hovered
            canvas.requestPaint()
        }
        onReleased: root.finishGesture(true)
        onCanceled: root.finishGesture(false)
        onExited: {
            if (state.gesture || !state.hovered.length) return
            state.hovered = ""
            canvas.requestPaint()
        }
    }
    Repeater {
        model: ["x", "y", "z", "uniform"]
        delegate: Item {
            id: target
            required property string modelData
            readonly property var anchor: root.handles.visible ? root.targetFor(modelData) : null
            objectName: "transformGizmoHandle3D_" + modelData
            visible: anchor !== null
            width: root.hitTolerance * 2
            height: width
            x: anchor ? anchor.x - width / 2 : 0
            y: anchor ? anchor.y - height / 2 : 0
            Accessible.role: Accessible.Button
            Accessible.name: anchor ? anchor.label : ""
            Accessible.description: root.tooltip
            Accessible.onPressAction: root.nudge(target.modelData, 1)
            Accessible.onIncreaseAction: root.nudge(target.modelData, 1)
            Accessible.onDecreaseAction: root.nudge(target.modelData, -1)
        }
    }
}
