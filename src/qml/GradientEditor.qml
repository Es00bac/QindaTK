// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
pragma ComponentBehavior: Bound

// Host-owned gradient stop presentation. The host supplies stable stop records
// { id, position, color }; this item owns only the active pointer preview.
T.Control {
    id: control
    objectName: "gradientEditor"

    property var stops: []
    property var selectedStopId: null
    property string tooltip: qsTr("Gradient stops")
    property real trackInset: Tk.Theme.size.iconSm

    readonly property bool dragging: drag.active
    readonly property var previewStop: drag.active ? drag.snapshot : null

    signal selectionRequested(var stopId)
    signal addStopRequested(real position)
    signal movePreviewRequested(var stopId, real position)
    signal moveFinished(var stopId, real position)
    signal moveCanceled(var stopId)
    signal deleteStopRequested(var stopId)

    implicitWidth: Tk.Theme.size.fieldWidth
    implicitHeight: Tk.Theme.size.controlLg + Tk.Theme.space.lg
    focus: true
    activeFocusOnTab: enabled
    hoverEnabled: true
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    function clampedPosition(x) {
        const usable = Math.max(1, width - 2 * trackInset)
        return Math.max(0, Math.min(1, (x - trackInset) / usable))
    }

    function stopAt(x, y) {
        let best = null
        let distance = Tk.Theme.size.controlSm
        for (let i = 0; i < stops.length; ++i) {
            const s = stops[i]
            const sx = trackInset + s.position * (width - 2 * trackInset)
            const d = Math.hypot(x - sx, y - handleY)
            if (d <= distance) {
                best = s
                distance = d
            }
        }
        return best
    }

    function beginDrag(stop, x) {
        if (!stop)
            return
        selectionRequested(stop.id)
        drag.active = true
        drag.snapshot = { id: stop.id, position: stop.position, color: stop.color }
        drag.position = stop.position
        drag.offset = x - (trackInset + stop.position * (width - 2 * trackInset))
        drag.moved = false
    }

    function updateDrag(x) {
        if (!drag.active)
            return
        const next = clampedPosition(x - drag.offset)
        drag.position = next
        if (Math.abs(next - drag.snapshot.position) > 0.000001)
            drag.moved = true
        if (drag.moved)
            movePreviewRequested(drag.snapshot.id, next)
    }

    function finishDrag() {
        if (!drag.active)
            return
        const id = drag.snapshot.id
        if (drag.moved)
            moveFinished(id, drag.position)
        drag.active = false
        drag.snapshot = null
    }

    function cancelDrag() {
        if (!drag.active)
            return
        const id = drag.snapshot.id
        moveCanceled(id)
        drag.active = false
        drag.snapshot = null
    }

    readonly property real handleY: Tk.Theme.size.controlSm + Tk.Theme.space.xs

    Accessible.role: Accessible.Slider
    Accessible.name: tooltip.length > 0 ? tooltip : qsTr("Gradient stops")
    Accessible.description: drag.active
        ? qsTr("Moving stop to %1 percent").arg(Math.round(drag.position * 100))
        : qsTr("%1 color stops").arg(stops.length)

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered && !pointer.pressed
    }

    Rectangle {
        objectName: "gradientTrack"
        x: control.trackInset
        y: Tk.Theme.size.controlSm
        width: Math.max(1, control.width - 2 * control.trackInset)
        height: Tk.Theme.size.controlSm - Tk.Theme.space.xs
        radius: Tk.Theme.radius.sm
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.controlBorder
        color: Tk.Theme.color.panel
        Canvas {
            id: gradientPreview
            objectName: "gradientPreview"
            anchors.fill: parent
            anchors.margins: Tk.Theme.size.border
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const preview = ctx.createLinearGradient(0, 0, width, 0)
                const source = control.stops || []
                for (let i = 0; i < source.length; ++i)
                    preview.addColorStop(Math.max(0, Math.min(1, Number(source[i].position))),
                                         String(source[i].color))
                ctx.fillStyle = preview
                ctx.fillRect(0, 0, width, height)
            }
            Connections {
                target: control
                function onStopsChanged() { gradientPreview.requestPaint() }
                function onWidthChanged() { gradientPreview.requestPaint() }
                function onHeightChanged() { gradientPreview.requestPaint() }
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.focus
            visible: control.activeFocus
        }
    }

    Repeater {
        model: control.stops
        delegate: Rectangle {
            required property var modelData
            objectName: "gradientStop_" + modelData.id
            readonly property bool previewed: drag.active && drag.snapshot !== null
                && drag.snapshot.id === modelData.id
            readonly property real stopPosition: previewed ? drag.position : modelData.position
            x: control.trackInset + stopPosition * (control.width - 2 * control.trackInset) - width / 2
            y: control.handleY - height / 2
            width: Tk.Theme.size.controlSm - Tk.Theme.space.xs
            height: width
            radius: Tk.Theme.radius.xs
            color: previewed ? drag.snapshot.color : modelData.color
            border.width: modelData.id === control.selectedStopId
                ? Tk.Theme.size.focusRing : Tk.Theme.size.border
            border.color: modelData.id === control.selectedStopId
                ? Tk.Theme.color.focus : Tk.Theme.color.controlBorder
            Accessible.role: Accessible.ListItem
            Accessible.name: qsTr("Gradient stop at %1 percent").arg(Math.round(modelData.position * 100))
            Accessible.description: qsTr("Color %1").arg(String(modelData.color))
        }
    }

    QtObject {
        id: drag
        property bool active: false
        property var snapshot: null
        property real position: 0
        property real offset: 0
        property bool moved: false
    }

    MouseArea {
        id: pointer
        objectName: "gradientPointer"
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function(mouse) {
            control.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                const stop = control.stopAt(mouse.x, mouse.y)
                if (stop)
                    control.deleteStopRequested(stop.id)
                mouse.accepted = true
                return
            }
            const stop = control.stopAt(mouse.x, mouse.y)
            if (stop) {
                control.beginDrag(stop, mouse.x)
            } else if (mouse.y >= control.handleY - Tk.Theme.size.controlSm
                       && mouse.y <= control.handleY + Tk.Theme.size.controlSm) {
                control.addStopRequested(control.clampedPosition(mouse.x))
            }
        }
        onPositionChanged: function(mouse) {
            if (pressed && drag.active)
                control.updateDrag(mouse.x)
        }
        onReleased: control.finishDrag()
        onCanceled: control.cancelDrag()
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape && drag.active) {
            control.cancelDrag()
            event.accepted = true
            return
        }
        if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)
            && control.selectedStopId !== null) {
            control.deleteStopRequested(control.selectedStopId)
            event.accepted = true
        }
    }
}
