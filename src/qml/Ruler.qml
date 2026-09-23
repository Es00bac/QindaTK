// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: a tick ruler for canvases and timelines. `origin` is the
// pixel position (along the ruler) of unit 0, `pixelsPerUnit` the pixels per unit;
// major ticks every `majorEvery` units carry micro labels, minor ticks
// every `minorEvery`. `cursorPosition` (px, -1 none) draws the accent
// cursor line. It repaints when the theme changes so colours never go
// stale. 22px thick.
//
// Markers (A7): `markers` is a list of {id, position (px along the
// ruler), kind: "indent-first" | "indent-left" | "indent-right" | "tab",
// draggable}. A first-line marker hangs from the top edge, the indents
// and tabs stand on the bottom edge. Dragging a draggable marker emits
// markerMoved(id, position) for every move (the host writes the value
// back and the marker follows through `markers`); a double-click on the
// bottom half where no marker sits emits tabAdded(position); dragging a
// tab off the ruler (below it) emits tabRemoved(id) on release. `band`
// shades the surface the ruler measures (paper) and `activeBand` the
// part of it that is live (the text column), both as [start, end] px.
Item {
    id: ruler

    property int orientation: Qt.Horizontal
    property real origin: 0
    property real pixelsPerUnit: 10
    property string unit: ""
    property real majorEvery: 10
    property real minorEvery: 1
    property bool labels: true
    property real cursorPosition: -1
    property real minimumMinorSpacing: Tk.Theme.space.unit
    property var markers: []
    property var band: []
    property var activeBand: []
    readonly property real markerSize: Tk.Theme.size.rulerMarker
    signal markerMoved(string id, real position)
    signal tabAdded(real position)
    signal tabRemoved(string id)

    // A focus id survives a marker-model replacement initiated by the host
    // in response to markerMoved.
    property var _markerDelegates: ({})
    property string _markerFocusRestoreId: ""

    readonly property bool horizontal: ruler.orientation === Qt.Horizontal

    implicitWidth: horizontal ? Tk.Theme.size.rulerLength : Tk.Theme.size.row
    implicitHeight: horizontal ? Tk.Theme.size.row : Tk.Theme.size.rulerLength

    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Ruler")

    function css(c) {
        return "rgba(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + ","
               + Math.round(c.b * 255) + "," + c.a + ")"
    }
    function format(value) {
        const rounded = Math.round(value * 100) / 100
        return (Number.isInteger(rounded) ? rounded.toString() : rounded.toFixed(2))
               + (ruler.unit.length > 0 && ruler.horizontal ? ruler.unit : "")
    }
    function moveMarkerTo(markerData, position) {
        if (!ruler.enabled || !markerData || markerData.draggable === false || !isFinite(position))
            return false
        const next = Math.max(0, Math.min(ruler.width, position))
        if (next === markerData.position)
            return false
        ruler.markerMoved(markerData.id, next)
        return true
    }
    function moveMarkerBy(markerData, direction, sourceItem) {
        if (!ruler.enabled || !markerData || markerData.draggable === false)
            return false
        const next = Math.max(0, Math.min(ruler.width,
                                           markerData.position + direction * Tk.Theme.space.unit))
        if (next === markerData.position)
            return false
        const restoreFocus = sourceItem && sourceItem.activeFocus
        if (restoreFocus)
            ruler._markerFocusRestoreId = markerData.id
        const changed = ruler.moveMarkerTo(markerData, next)
        if (restoreFocus && changed) {
            const markerId = markerData.id
            Qt.callLater(function() {
                if (ruler._markerFocusRestoreId !== markerId)
                    return
                const replacement = ruler._markerDelegates[markerId]
                if (replacement && ruler.enabled)
                    replacement.forceActiveFocus(Qt.OtherFocusReason)
                ruler._markerFocusRestoreId = ""
            })
        }
        return changed
    }
    function removeTab(markerData) {
        if (!ruler.enabled || !markerData || markerData.draggable === false || markerData.kind !== "tab")
            return false
        ruler.tabRemoved(markerData.id)
        return true
    }
    function markerName(kind) {
        switch (kind) {
        case "indent-first": return qsTr("First-line indent")
        case "indent-left": return qsTr("Left indent")
        case "indent-right": return qsTr("Right indent")
        case "tab": return qsTr("Tab stop")
        default: return qsTr("Ruler marker")
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.surface
        // The measured surface and its live part (a page and its text
        // column): panelAlt for the surface, inputBg for the live part.
        Rectangle {
            objectName: "rulerBand"
            visible: ruler.band.length === 2 && ruler.band[1] > ruler.band[0]
            x: ruler.horizontal && visible ? ruler.band[0] : 0
            y: !ruler.horizontal && visible ? ruler.band[0] : 0
            width: ruler.horizontal ? (visible ? ruler.band[1] - ruler.band[0] : 0) : parent.width
            height: ruler.horizontal ? parent.height : (visible ? ruler.band[1] - ruler.band[0] : 0)
            color: Tk.Theme.color.panelAlt
        }
        Rectangle {
            objectName: "rulerActiveBand"
            visible: ruler.activeBand.length === 2 && ruler.activeBand[1] > ruler.activeBand[0]
            x: ruler.horizontal && visible ? ruler.activeBand[0] : 0
            y: !ruler.horizontal && visible ? ruler.activeBand[0] : 0
            width: ruler.horizontal ? (visible ? ruler.activeBand[1] - ruler.activeBand[0] : 0) : parent.width
            height: ruler.horizontal ? parent.height : (visible ? ruler.activeBand[1] - ruler.activeBand[0] : 0)
            color: Tk.Theme.color.inputBg
        }
        Rectangle {
            visible: ruler.horizontal
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Tk.Theme.size.border
            color: Tk.Theme.color.divider
        }
        Rectangle {
            visible: !ruler.horizontal
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: Tk.Theme.size.border
            color: Tk.Theme.color.divider
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            if (ruler.pixelsPerUnit <= 0 || ruler.minorEvery <= 0) {
                return
            }
            const length = ruler.horizontal ? width : height
            const thickness = ruler.horizontal ? height : width
            const tickColor = ruler.css(Tk.Theme.color.textMuted)
            const labelColor = ruler.css(Tk.Theme.color.textMuted)
            ctx.strokeStyle = tickColor
            ctx.fillStyle = labelColor
            ctx.lineWidth = Tk.Theme.size.border
            ctx.font = Tk.Theme.font.micro + "px \"" + Tk.Theme.font.monoFamily + "\""
            ctx.textBaseline = "top"
            // Thin out minor ticks that would land closer than the minimum.
            let minor = ruler.minorEvery
            while (minor * ruler.pixelsPerUnit < ruler.minimumMinorSpacing) {
                minor *= 2
            }
            const major = Math.max(ruler.majorEvery, minor)
            // With a band set, ticks cover the measured surface only (a
            // ruler numbered into the void beside a page is noise).
            const hasBand = ruler.band.length === 2 && ruler.band[1] > ruler.band[0]
            const from = hasBand ? Math.max(0, ruler.band[0]) : 0
            const to = hasBand ? Math.min(length, ruler.band[1]) : length
            const first = Math.ceil((from - ruler.origin) / ruler.pixelsPerUnit / minor) * minor
            const last = Math.floor((to - ruler.origin) / ruler.pixelsPerUnit / minor) * minor
            ctx.beginPath()
            for (let u = first; u <= last; u += minor) {
                const p = Math.round(ruler.origin + u * ruler.pixelsPerUnit) + 0.5
                const isMajor = Math.abs(u / major - Math.round(u / major)) < 1e-6
                const len = isMajor ? thickness * 0.55 : thickness * 0.25
                if (ruler.horizontal) {
                    ctx.moveTo(p, thickness)
                    ctx.lineTo(p, thickness - len)
                } else {
                    ctx.moveTo(thickness, p)
                    ctx.lineTo(thickness - len, p)
                }
                if (isMajor && ruler.labels) {
                    if (ruler.horizontal) {
                        ctx.fillText(ruler.format(u), p + Tk.Theme.space.xs, Tk.Theme.size.border)
                    } else {
                        ctx.save()
                        ctx.translate(Tk.Theme.space.xs, p - Tk.Theme.space.xs)
                        ctx.rotate(-Math.PI / 2)
                        ctx.fillText(ruler.format(u), 0, 0)
                        ctx.restore()
                    }
                }
            }
            ctx.stroke()
        }
    }

    Rectangle {
        visible: ruler.cursorPosition >= 0
        x: ruler.horizontal ? Math.round(ruler.cursorPosition) : 0
        y: ruler.horizontal ? 0 : Math.round(ruler.cursorPosition)
        width: ruler.horizontal ? Tk.Theme.size.border : parent.width
        height: ruler.horizontal ? parent.height : Tk.Theme.size.border
        color: Tk.Theme.color.accent
    }

    // ---- markers (horizontal rulers) ----
    Repeater {
        model: ruler.horizontal ? ruler.markers : []
        delegate: Canvas {
            id: marker
            required property var modelData
            readonly property bool hanging: modelData.kind === "indent-first"
            readonly property bool tab: modelData.kind === "tab"
            readonly property bool draggable: modelData.draggable !== false
            readonly property real keyboardStep: Tk.Theme.space.unit
            objectName: "rulerMarker_" + modelData.id
            x: Math.round(modelData.position - ruler.markerSize / 2)
            y: hanging ? Tk.Theme.size.border : ruler.height - ruler.markerSize - Tk.Theme.size.border
            width: ruler.markerSize
            height: ruler.markerSize
            activeFocusOnTab: ruler.enabled && draggable

            Accessible.role: draggable ? Accessible.Slider : Accessible.StaticText
            Accessible.name: ruler.markerName(modelData.kind)
            Accessible.description: qsTr("Position %1 pixels%2")
                .arg(Math.round(modelData.position))
                .arg(draggable ? qsTr(". Use Left and Right to move by %1 pixels.").arg(keyboardStep) : "")
            Accessible.focusable: ruler.enabled && draggable
            Accessible.focused: marker.activeFocus
            Accessible.onIncreaseAction: ruler.moveMarkerBy(modelData, 1, marker)
            Accessible.onDecreaseAction: ruler.moveMarkerBy(modelData, -1, marker)

            Keys.onPressed: function(event) {
                if (!ruler.enabled || !draggable)
                    return
                if (event.key === Qt.Key_Left) {
                    ruler.moveMarkerBy(modelData, -1, marker)
                    event.accepted = true
                } else if (event.key === Qt.Key_Right) {
                    ruler.moveMarkerBy(modelData, 1, marker)
                    event.accepted = true
                } else if (event.key === Qt.Key_Delete && tab) {
                    ruler.removeTab(modelData)
                    event.accepted = true
                }
            }

            Component.onCompleted: {
                ruler._markerDelegates[modelData.id] = marker
                if (ruler._markerFocusRestoreId === modelData.id) {
                    const markerId = modelData.id
                    Qt.callLater(function() {
                        if (ruler._markerFocusRestoreId !== markerId)
                            return
                        const replacement = ruler._markerDelegates[markerId]
                        if (replacement && ruler.enabled)
                            replacement.forceActiveFocus(Qt.OtherFocusReason)
                    })
                }
            }
            Component.onDestruction: {
                if (ruler._markerDelegates[modelData.id] === marker)
                    delete ruler._markerDelegates[modelData.id]
            }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.clearRect(0, 0, width, height)
                ctx.fillStyle = ruler.css(Tk.Theme.color.accent)
                ctx.strokeStyle = ruler.css(Tk.Theme.color.accentText)
                ctx.lineWidth = Tk.Theme.size.border
                ctx.beginPath()
                if (tab) {
                    ctx.rect(width / 2 - Tk.Theme.size.border, 0, Tk.Theme.space.xs, height)
                    ctx.rect(width / 2 - Tk.Theme.size.border, height - Tk.Theme.space.xs,
                             width / 2 + Tk.Theme.size.border, Tk.Theme.space.xs)
                } else if (hanging) {
                    ctx.moveTo(0, 0); ctx.lineTo(width, 0); ctx.lineTo(width / 2, height)
                } else {
                    ctx.moveTo(0, height); ctx.lineTo(width, height); ctx.lineTo(width / 2, 0)
                }
                ctx.closePath()
                ctx.fill()
                ctx.stroke()
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -Tk.Theme.size.focusRing
                color: "transparent"
                border.width: Tk.Theme.size.focusRing
                border.color: Tk.Theme.color.focus
                visible: marker.activeFocus
                z: 2
            }
            Tk.ToolTip {
                text: marker.Accessible.name + " · " + marker.Accessible.description
                visible: marker.activeFocus && marker.draggable
                delay: 600
            }

            Connections {
                target: Tk.Theme
                function onChanged() { marker.requestPaint() }
            }
        }
    }
    MouseArea {
        id: markerArea
        objectName: "rulerMarkerArea"
        anchors.fill: parent
        enabled: ruler.horizontal && ruler.enabled && ruler.markers.length > 0
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        property var dragging: null
        readonly property real markerHitRadius: Tk.Density.modeName === "touch"
            ? Tk.Theme.size.controlSm / 2 : ruler.markerSize
        function markerAt(px, py) {
            let best = null
            let bestDistance = markerHitRadius
            for (let i = 0; i < ruler.markers.length; ++i) {
                const m = ruler.markers[i]
                const onTop = m.kind === "indent-first"
                if ((py < ruler.height / 2) !== onTop) {
                    continue
                }
                const distance = Math.abs(m.position - px)
                // AGENT-GUARD: overlapping touch targets choose the nearest
                // marker; strict improvement leaves exact ties with the
                // earlier model entry instead of making array order jitter.
                if (distance <= markerHitRadius &&
                    (best === null || distance < bestDistance)) {
                    best = m
                    bestDistance = distance
                }
            }
            return best
        }
        cursorShape: dragging !== null || markerAt(mouseX, mouseY) !== null ? Qt.SizeHorCursor : Qt.ArrowCursor
        onPressed: function(mouse) {
            const m = markerAt(mouse.x, mouse.y)
            dragging = m !== null && m.draggable !== false ? m : null
            if (dragging !== null) {
                const delegate = ruler._markerDelegates[dragging.id]
                if (delegate)
                    delegate.forceActiveFocus(Qt.MouseFocusReason)
            }
            mouse.accepted = true   // a refused press never gets its double-click
        }
        onPositionChanged: function(mouse) {
            if (dragging !== null) {
                ruler.moveMarkerTo(dragging, mouse.x)
            }
        }
        onReleased: function(mouse) {
            if (dragging !== null && dragging.kind === "tab" && mouse.y > ruler.height + ruler.markerSize) {
                ruler.removeTab(dragging)
            }
            dragging = null
        }
        onDoubleClicked: function(mouse) {
            if (mouse.y >= ruler.height / 2 && markerAt(mouse.x, mouse.y) === null) {
                ruler.tabAdded(mouse.x)
            }
        }
    }

    onBandChanged: canvas.requestPaint()
    onOriginChanged: canvas.requestPaint()
    onPixelsPerUnitChanged: canvas.requestPaint()
    onScaleChanged: canvas.requestPaint()
    onOrientationChanged: canvas.requestPaint()
    onMajorEveryChanged: canvas.requestPaint()
    onMinorEveryChanged: canvas.requestPaint()
    onMinimumMinorSpacingChanged: canvas.requestPaint()
    onLabelsChanged: canvas.requestPaint()
    onUnitChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()

    Connections {
        target: Tk.Theme
        function onChanged() { canvas.requestPaint() }
    }
}
