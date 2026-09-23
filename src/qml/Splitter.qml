// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Resizable panes with a 4px seam (Theme.size.seam) that lights in the
// accent when hovered or dragged. Children size themselves with the
// template's attached properties (`SplitView.preferredWidth`,
// `SplitView.minimumWidth`, `SplitView.fillWidth`; import
// `QtQuick.Templates as T` or `QtQuick.Controls as QQC` for the attached
// type). `saveState()`/`restoreState()` come from the template.
T.SplitView {
    id: splitter

    property real handleSize: Tk.Theme.size.seam
    property string tooltip: ""

    handle: Rectangle {
        id: seam
        objectName: "splitterHandle"
        implicitWidth: splitter.orientation === Qt.Horizontal ? splitter.handleSize : splitter.width
        implicitHeight: splitter.orientation === Qt.Horizontal ? splitter.height : splitter.handleSize
        color: T.SplitHandle.pressed || T.SplitHandle.hovered ? Tk.Theme.color.seamHover
                                                              : Tk.Theme.color.seam
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        activeFocusOnTab: splitter.enabled
        Accessible.role: Accessible.Splitter
        Accessible.name: splitter.tooltip.length > 0
            ? splitter.tooltip : qsTr("Resize pane %1").arg(seam.paneNumber())
        Accessible.description: splitter.orientation === Qt.Horizontal
            ? qsTr("Left and Right resize adjacent panes")
            : qsTr("Up and Down resize adjacent panes")
        Accessible.focusable: splitter.enabled
        Accessible.focused: seam.activeFocus
        Accessible.onIncreaseAction: seam.adjust(Tk.Theme.space.lg)
        Accessible.onDecreaseAction: seam.adjust(-Tk.Theme.space.lg)

        // AGENT-NOTE: SplitView does not publish a handle index. Match this
        // handle to the preceding visible pane by its boundary, then update
        // Qt's documented SplitView.preferredWidth/Height attached property.
        // Both supported Qt versions expose the attached object to QML JS as
        // pane["T.SplitView"]; tst_splitter_targets covers the contract.
        function adjacentPanes() {
            const horizontal = splitter.orientation === Qt.Horizontal
            // Reading x/y directly also makes accessible naming re-evaluate
            // after SplitView places a newly created handle.
            const handleAxis = horizontal ? seam.x : seam.y
            let previous = null
            let next = null
            let number = 0
            let distance = Infinity
            const visible = []
            for (let i = 0; i < splitter.count; ++i) {
                const pane = splitter.itemAt(i)
                if (pane && pane.visible) visible.push(pane)
            }
            for (let i = 0; i + 1 < visible.length; ++i) {
                const pane = visible[i]
                const edge = pane.mapToItem(splitter,
                                            horizontal ? pane.width : 0,
                                            horizontal ? 0 : pane.height)
                const difference = Math.abs((horizontal ? edge.x : edge.y) - handleAxis)
                if (difference < distance) {
                    previous = pane
                    next = visible[i + 1]
                    number = i + 1
                    distance = difference
                }
            }
            return { previous: previous, next: next, number: number }
        }
        function paneNumber() { return adjacentPanes().number }
        function adjust(delta) {
            if (!splitter.enabled || !seam.enabled || !isFinite(delta)) return false
            const pair = adjacentPanes()
            if (!pair.previous || !pair.next) return false
            const horizontal = splitter.orientation === Qt.Horizontal
            const first = pair.previous["T.SplitView"]
            const second = pair.next["T.SplitView"]
            if (!first || !second) return false
            const size = horizontal ? pair.previous.width : pair.previous.height
            const adjacentSize = horizontal ? pair.next.width : pair.next.height
            const minimum = Math.max(horizontal ? first.minimumWidth : first.minimumHeight,
                                     size + adjacentSize - (horizontal ? second.maximumWidth : second.maximumHeight))
            const maximum = Math.min(horizontal ? first.maximumWidth : first.maximumHeight,
                                     size + adjacentSize - (horizontal ? second.minimumWidth : second.minimumHeight))
            const target = Math.max(minimum, Math.min(maximum, size + delta))
            if (!isFinite(target) || Math.abs(target - size) < 0.01) return false
            if (horizontal) first.preferredWidth = target
            else first.preferredHeight = target
            return true
        }
        Keys.onPressed: function(event) {
            const horizontal = splitter.orientation === Qt.Horizontal
            const direction = horizontal
                ? (event.key === Qt.Key_Left ? -1 : event.key === Qt.Key_Right ? 1 : 0)
                : (event.key === Qt.Key_Up ? -1 : event.key === Qt.Key_Down ? 1 : 0)
            if (direction !== 0 && seam.adjust(direction * Tk.Theme.space.lg))
                event.accepted = true
        }

        Rectangle {
            objectName: "splitterFocusRing"
            anchors.fill: parent
            anchors.margins: -Tk.Theme.size.focusRing
            color: Qt.rgba(Tk.Theme.color.focus.r, Tk.Theme.color.focus.g,
                           Tk.Theme.color.focus.b, Tk.Theme.opacity.hidden)
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
            visible: seam.activeFocus && splitter.enabled
        }
        Tk.ToolTip {
            text: seam.Accessible.name
            visible: splitter.tooltip.length > 0 && T.SplitHandle.hovered
        }

        // AGENT-CONTRACT: SplitView filters pointer events delivered to this
        // child; its handle lookup uses the matching containment mask. The
        // child gives Qt's delivery agent actual bounds outside the seam.
        containmentMask: targetArea
        MouseArea {
            id: targetArea
            objectName: "splitterHitTarget"
            x: splitter.orientation === Qt.Horizontal ? (seam.width - width) / 2 : 0
            y: splitter.orientation === Qt.Horizontal ? 0 : (seam.height - height) / 2
            width: splitter.orientation === Qt.Horizontal ? Math.max(seam.width, Tk.Theme.size.seamTarget) : seam.width
            height: splitter.orientation === Qt.Horizontal ? seam.height : Math.max(seam.height, Tk.Theme.size.seamTarget)
            acceptedButtons: Qt.LeftButton
        }
    }
}
