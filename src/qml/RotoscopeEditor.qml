// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: control
    objectName: "rotoscopeEditor"
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Animated mask editor")
    Accessible.description: maskStatus.text

    // Masks follow TrackingRotoModel.masks. Coordinates are normalized to the
    // displayed source frame; host remains authoritative for keyframe edits.
    property var masks: []
    property string maskId: ""
    property string currentFrame: "0"
    property string sourceFrame: String((mask.keyframes || []).length ? mask.keyframes[0].frame : currentFrame)
    property string sourceFrameUrl: ""
    property real sourceAspectRatio: 1
    property string trackingTrackId: ""
    property var propagationPreview: ({})
    property string tooltip: qsTr("Edit the selected mask path on this source frame")
    property bool generatedPreview: false
    property real feather: Number(mask.feather || 0)
    readonly property var mask: masks.find(function(item) { return String(item.maskId) === control.maskId }) || ({})
    readonly property var keyframe: (mask.keyframes || []).find(function(item) { return String(item.frame) === String(control.currentFrame) }) || ({})
    readonly property var points: keyframe.points || []
    readonly property bool previewVisible: generatedPreview || propagationPreview.state === "preview"
    property var _previewPoint: null
    property bool _dragging: false
    property string _dragPointId: ""
    property int _serial: 0

    signal maskSelected(string maskId)
    signal propagateRequested(string maskId, string sourceFrame, string targetFrame, string trackId)
    signal acceptPreviewRequested(string maskId, string frame, var points)
    signal cancelPreviewRequested(string maskId, string frame)
    signal pointEditPreviewRequested(string maskId, string frame, var points)
    signal keyframeEditRequested(string maskId, string frame, var points)
    signal keyframeRemoveRequested(string maskId, string frame)
    signal featherChangeRequested(string maskId, real feather)
    signal editCancelled(string maskId, string frame)

    function _requestId() { return "roto:" + String(mask.maskId || "") + ":" + currentFrame + ":" + (++_serial) }
    function _pointsForEdit(pointId, x, y) {
        return points.map(function(point) {
            const p = {pointId: String(point.pointId), x: Number(point.x), y: Number(point.y)}
            if (p.pointId === pointId) { p.x = x; p.y = y }
            return p
        })
    }
    function movePoint(pointId, x, y) {
        if (!mask.maskId || !pointId || points.length < 3) return false
        const cx = Math.max(0, Math.min(1, Number(x)))
        const cy = Math.max(0, Math.min(1, Number(y)))
        _previewPoint = {pointId: pointId, x: cx, y: cy}
        keyframeEditRequested(String(mask.maskId), String(currentFrame), _pointsForEdit(pointId, cx, cy))
        path.requestPaint()
        return true
    }
    function previewPoint(pointId, x, y) {
        if (!mask.maskId || !pointId || points.length < 3) return false
        const cx = Math.max(0, Math.min(1, Number(x)))
        const cy = Math.max(0, Math.min(1, Number(y)))
        _dragPointId = pointId
        _previewPoint = {pointId: pointId, x: cx, y: cy}
        pointEditPreviewRequested(String(mask.maskId), String(currentFrame), _pointsForEdit(pointId, cx, cy))
        path.requestPaint()
        return true
    }
    function finishDrag() {
        if (!_dragging) return false
        _dragging = false
        if (_previewPoint) keyframeEditRequested(String(mask.maskId), String(currentFrame),
                                                  _pointsForEdit(_dragPointId, _previewPoint.x, _previewPoint.y))
        _previewPoint = null; _dragPointId = ""
        path.requestPaint()
        return true
    }
    function addPoint(x, y) {
        if (!mask.maskId || points.length >= 256) return false
        const next = points.map(function(p) { return {pointId: String(p.pointId), x: Number(p.x), y: Number(p.y)} })
        next.push({pointId: _requestId(), x: Math.max(0, Math.min(1, Number(x))), y: Math.max(0, Math.min(1, Number(y)))})
        keyframeEditRequested(String(mask.maskId), String(currentFrame), next)
        return true
    }
    function addPointAt(x, y) {
        if (frameSurface.width <= 0 || frameSurface.height <= 0) return false
        const px = Math.max(0, Math.min(1, Number(x))) * frameSurface.width
        const py = Math.max(0, Math.min(1, Number(y))) * frameSurface.height
        const hitRadius = Tk.Theme.size.controlSm / 2
        for (let i = 0; i < points.length; ++i) {
            const dx = px - Number(points[i].x) * frameSurface.width
            const dy = py - Number(points[i].y) * frameSurface.height
            if (dx * dx + dy * dy <= hitRadius * hitRadius) return false
        }
        return addPoint(x, y)
    }
    function removePoint(pointId) {
        if (!mask.maskId || points.length <= 3) return false
        const next = points.filter(function(p) { return String(p.pointId) !== pointId })
        if (next.length === points.length) return false
        keyframeEditRequested(String(mask.maskId), String(currentFrame), next)
        return true
    }
    function requestPropagation() {
        if (!mask.maskId || !trackingTrackId || !sourceFrame || !currentFrame) return false
        propagateRequested(String(mask.maskId), String(sourceFrame), String(currentFrame), String(trackingTrackId))
        return true
    }
    function acceptPreview() {
        const previewPoints = propagationPreview.points || []
        if (propagationPreview.state !== "preview" || !mask.maskId || previewPoints.length < 3
                || propagationPreview.ok === false) return false
        acceptPreviewRequested(String(mask.maskId), String(currentFrame), previewPoints)
        return true
    }
    function cancelPreview() {
        if (propagationPreview.state !== "preview" || !mask.maskId) return false
        cancelPreviewRequested(String(mask.maskId), String(currentFrame))
        return true
    }
    function cancelEdit() {
        if (!_dragging) return false
        _dragging = false; _previewPoint = null; _dragPointId = ""
        editCancelled(String(mask.maskId), String(currentFrame))
        path.requestPaint()
        return true
    }

    implicitWidth: Tk.Theme.size.controlLg * 16
    implicitHeight: Tk.Theme.size.controlLg * 12
    padding: Tk.Theme.space.md
    HoverHandler { id: hover }
    Tk.ToolTip { visible: hover.hovered && control.tooltip.length > 0; text: control.tooltip }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape && control.cancelEdit()) event.accepted = true
    }
    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("Rotoscope"); wide: true }
            Tk.ComboBox {
                objectName: "rotoMaskSelect"
                model: control.masks.map(function(m) { return {text: String(m.label || m.maskId), value: String(m.maskId)} })
                textRole: "text"
                currentIndex: Math.max(0, control.masks.findIndex(function(m) { return String(m.maskId) === control.maskId }))
                tooltip: qsTr("Select the mask to edit")
                onActivated: function(index) { control.maskSelected(String(control.masks[index].maskId)) }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption { id: maskStatus; text: control.previewVisible ? qsTr("GENERATED PREVIEW") : qsTr("EDITABLE MASK"); color: control.previewVisible ? Tk.Theme.color.warning : Tk.Theme.color.textMuted }
            Tk.Button {
                objectName: "rotoPropagate"
                text: qsTr("Propagate")
                tooltip: qsTr("Preview the mask translated by accepted tracking motion")
                enabled: Boolean(control.mask.maskId && control.trackingTrackId)
                onClicked: control.requestPropagation()
            }
            Tk.Button {
                objectName: "rotoAcceptPreview"
                text: qsTr("Accept preview")
                tooltip: qsTr("Commit the generated mask path as a keyframe")
                visible: control.propagationPreview.state === "preview"
                enabled: control.propagationPreview.ok !== false && (control.propagationPreview.points || []).length >= 3
                onClicked: control.acceptPreview()
            }
            Tk.Button {
                objectName: "rotoCancelPreview"
                text: qsTr("Discard")
                variant: "ghost"
                tooltip: qsTr("Discard the generated mask preview")
                visible: control.propagationPreview.state === "preview"
                onClicked: control.cancelPreview()
            }
            Tk.Button { objectName: "rotoRemoveKeyframe"; text: qsTr("Remove key"); tooltip: qsTr("Remove this mask keyframe"); enabled: control.keyframe.frame !== undefined; onClicked: control.keyframeRemoveRequested(String(control.mask.maskId), String(control.currentFrame)) }
        }
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Frame %1").arg(control.currentFrame) }
            Tk.Caption { text: qsTr("%1 points").arg(control.points.length) }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption { text: qsTr("Feather") }
            Tk.Slider {
                objectName: "rotoFeather"
                from: 0; to: 64; value: control.feather
                tooltip: qsTr("Mask edge feather in source pixels")
                onValueModified: function(value) { control.featherChangeRequested(String(control.mask.maskId), value) }
            }
            Tk.Mono { text: Number(control.feather).toFixed(1) }
        }
        Item {
            id: canvasBox
            objectName: "rotoCanvas"
            width: parent.width
            height: Math.max(Tk.Theme.size.waveformHeight * 3, parent.height - Tk.Theme.size.control * 2)
            implicitHeight: height
            clip: true
            Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
            Item {
                id: frameSurface
                objectName: "rotoFrameSurface"
                anchors.centerIn: parent
                width: Math.min(canvasBox.width, canvasBox.height * Math.max(0.1, control.sourceAspectRatio))
                height: width / Math.max(0.1, control.sourceAspectRatio)
                Image {
                    anchors.fill: parent
                    source: control.sourceFrameUrl
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    Accessible.ignored: true
                }
                Canvas {
                    id: path
                    objectName: "rotoPath"
                    anchors.fill: parent
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const preview = control.propagationPreview.points || []
                        if (preview.length > 2) {
                            ctx.beginPath()
                            for (let i = 0; i < preview.length; ++i) {
                                if (i === 0) ctx.moveTo(preview[i].x * width, preview[i].y * height)
                                else ctx.lineTo(preview[i].x * width, preview[i].y * height)
                            }
                            ctx.closePath(); ctx.strokeStyle = Tk.Theme.color.warning
                            ctx.lineWidth = Tk.Theme.size.border * 2; ctx.setLineDash([Tk.Theme.size.controlSm / 4, Tk.Theme.space.xs]); ctx.stroke(); ctx.setLineDash([])
                        }
                        const pts = control.points
                        if (pts.length < 2) return
                        ctx.beginPath()
                        for (let i = 0; i < pts.length; ++i) {
                            const p = control._previewPoint && control._previewPoint.pointId === String(pts[i].pointId)
                                      ? control._previewPoint : pts[i]
                            if (i === 0) ctx.moveTo(p.x * width, p.y * height)
                            else ctx.lineTo(p.x * width, p.y * height)
                        }
                        ctx.closePath()
                        ctx.fillStyle = control.previewVisible ? Tk.Theme.color.warning : Tk.Theme.color.accent
                        ctx.globalAlpha = 0.18; ctx.fill(); ctx.globalAlpha = 1
                        ctx.strokeStyle = ctx.fillStyle; ctx.lineWidth = Tk.Theme.size.border * 2; ctx.stroke()
                    }
                    Connections {
                        target: control
                        function onMaskChanged() { path.requestPaint() }
                        function onCurrentFrameChanged() { path.requestPaint() }
                        function onPointsChanged() { path.requestPaint() }
                        function onPropagationPreviewChanged() { path.requestPaint() }
                    }
                }
                Repeater {
                    model: control.points
                    delegate: Item {
                        required property var modelData
                        objectName: "rotoPoint_" + String(modelData.pointId)
                        x: (control._previewPoint && control._previewPoint.pointId === String(modelData.pointId) ? control._previewPoint.x : Number(modelData.x)) * frameSurface.width - width / 2
                        y: (control._previewPoint && control._previewPoint.pointId === String(modelData.pointId) ? control._previewPoint.y : Number(modelData.y)) * frameSurface.height - height / 2
                        width: Tk.Theme.size.controlSm
                        height: width
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Mask point %1").arg(String(modelData.pointId))
                        Accessible.description: qsTr("Drag to edit. Press Delete to remove this point.")
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width / 2
                            height: width
                            radius: Tk.Theme.radius.full
                            color: Tk.Theme.color.accent
                            border.color: parent.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.text
                            border.width: Tk.Theme.size.border
                        }
                        DragHandler {
                            target: null
                            onActiveChanged: {
                                if (active) control._dragging = true
                                else if (control._dragging) control.finishDrag()
                            }
                            onCentroidChanged: {
                                if (!active || frameSurface.width <= 0 || frameSurface.height <= 0) return
                                const p = mapToItem(frameSurface, centroid.position.x, centroid.position.y)
                                control.previewPoint(String(modelData.pointId), p.x / frameSurface.width, p.y / frameSurface.height)
                            }
                        }
                        Keys.onPressed: function(event) {
                            if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)
                                    && control.removePoint(String(modelData.pointId))) event.accepted = true
                        }
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: function(eventPoint) {
                        const p = eventPoint.position
                        control.addPointAt(p.x / frameSurface.width, p.y / frameSurface.height)
                    }
                }
            }
        }
    }
}
