// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The controls for one timeline track: name, kind, mute/solo/arm/lock/hide,
// routing summary, a resize seam and a reorder grip.
//
// AGENT-CONTRACT: every value here is owner state from TimelineModel through
// TimelineViewport track rows. Toggles, rename, resize and reorder emit
// requests keyed by trackId; nothing changes until the model applies them,
// so a rejected request leaves the header showing the committed state.
// With gestures: false the header leaves press, drag and double-click on its
// background and resize seam to the owner (EditTimelineHeaders handles them
// for the whole column, so a drag outlives a recycled header delegate).
Item {
    id: header

    property string trackId: ""
    property string name: ""
    property string kind: ""
    property bool muted: false
    property bool solo: false
    property bool armed: false
    property bool locked: false
    property bool hidden: false
    property string routing: ""
    property string trackColor: ""
    property int clipCount: 0
    property bool current: false
    property bool preview: false
    property bool resizable: true
    property bool gestures: true
    // Set by an owner that runs the resize gesture itself (gestures: false).
    property bool resizing: false
    property string tooltip: header.name.length > 0 ? header.name : qsTr("Track")
    readonly property bool editingName: nameEditor.visible
    readonly property string kindIcon: header.kind === "video" ? "film"
                                     : header.kind === "midi" ? "music"
                                     : header.kind === "audio" ? "audio-lines" : "layers"

    signal propertyRequested(string trackId, string property, var value)
    signal renameRequested(string trackId, string name)
    signal selectRequested(string trackId)
    signal heightStepRequested(string trackId, int steps)
    // Starts on the first pointer movement; a press without one emits nothing.
    signal resizeStarted(string trackId)
    signal resizeMoved(string trackId, real delta)
    signal resizeFinished(string trackId)
    signal resizeCanceled(string trackId)
    // Reorder positions are pointer y in the parent's coordinates, because the
    // header itself moves while the model previews the new order.
    signal reorderStarted(string trackId, real y)
    signal reorderMoved(string trackId, real y)
    signal reorderFinished(string trackId)
    signal reorderCanceled(string trackId)

    function startRename() {
        nameEditor.text = header.name
        nameEditor.visible = true
        nameEditor.forceActiveFocus()
        nameEditor.selectAll()
    }
    function finishRename(accept) {
        if (!nameEditor.visible) return
        const text = nameEditor.text.trim()
        nameEditor.visible = false
        if (accept && text.length > 0 && text !== header.name) header.renameRequested(header.trackId, text)
    }

    objectName: "trackHeader_" + header.trackId
    implicitWidth: Tk.Theme.size.labelWidth * 2
    implicitHeight: Tk.Theme.size.rowLg + Tk.Theme.size.waveformHeight

    Accessible.role: Accessible.Grouping
    Accessible.name: header.tooltip
    Accessible.description: [header.kind, header.muted ? qsTr("muted") : "", header.solo ? qsTr("solo") : "",
                             header.armed ? qsTr("armed for recording") : "",
                             header.locked ? qsTr("locked") : "", qsTr("%n clips", "", header.clipCount),
                             header.routing].filter(function(part) { return part.length > 0 }).join(", ")

    Tk.Box {
        objectName: "trackHeaderFrame"
        anchors.fill: parent
        color: header.current ? Tk.Theme.color.controlHoverBg : Tk.Theme.color.headerBg
        borderColor: Tk.Theme.color.divider
        borderRight: Tk.Theme.size.border
        borderBottom: Tk.Theme.size.border
        paddingLeft: Tk.Theme.space.sm
        paddingRight: Tk.Theme.space.xs
        paddingTop: Tk.Theme.space.xs

        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs

            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs

                Rectangle {
                    objectName: "trackColor"
                    implicitWidth: Tk.Theme.size.seam
                    implicitHeight: Tk.Theme.size.iconSm
                    radius: Tk.Theme.radius.xs
                    color: header.trackColor.length > 0 ? header.trackColor : Tk.Theme.color.accent
                }
                Tk.Icon {
                    objectName: "trackKindIcon"
                    name: header.kindIcon
                    size: Tk.Theme.size.iconSm
                    color: Tk.Theme.color.textMuted
                }
                Item {
                    Tk.Flex.grow: 1
                    implicitHeight: Tk.Theme.size.controlSm
                    implicitWidth: Tk.Theme.size.icon
                    Tk.Label {
                        objectName: "trackName"
                        anchors.fill: parent
                        visible: !nameEditor.visible
                        verticalAlignment: Text.AlignVCenter
                        text: header.name
                        color: header.hidden ? Tk.Theme.color.textMuted : Tk.Theme.color.text
                    }
                    Tk.TextField {
                        id: nameEditor
                        objectName: "trackNameEditor"
                        anchors.fill: parent
                        visible: false
                        small: true
                        tooltip: qsTr("Track name")
                        onAccepted: header.finishRename(true)
                        onActiveFocusChanged: if (!activeFocus) header.finishRename(false)
                        Keys.onEscapePressed: function(event) {
                            event.accepted = true
                            header.finishRename(false)
                        }
                    }
                }
                Tk.Button {
                    objectName: "trackMute"
                    text: qsTr("M")
                    small: true
                    checked: header.muted
                    tooltip: header.muted ? qsTr("Unmute %1").arg(header.tooltip) : qsTr("Mute %1").arg(header.tooltip)
                    Accessible.name: qsTr("Mute")
                    Accessible.checkable: true
                    onClicked: header.propertyRequested(header.trackId, "muted", !header.muted)
                }
                Tk.Button {
                    objectName: "trackSolo"
                    text: qsTr("S")
                    small: true
                    checked: header.solo
                    tooltip: header.solo ? qsTr("Unsolo %1").arg(header.tooltip) : qsTr("Solo %1").arg(header.tooltip)
                    Accessible.name: qsTr("Solo")
                    Accessible.checkable: true
                    onClicked: header.propertyRequested(header.trackId, "solo", !header.solo)
                }
                Tk.Button {
                    objectName: "trackArm"
                    text: qsTr("R")
                    small: true
                    checked: header.armed
                    variant: header.armed ? "danger" : "default"
                    tooltip: header.armed ? qsTr("Disarm %1").arg(header.tooltip) : qsTr("Arm %1 for recording").arg(header.tooltip)
                    Accessible.name: qsTr("Record arm")
                    Accessible.checkable: true
                    onClicked: header.propertyRequested(header.trackId, "armed", !header.armed)
                }
                Tk.IconButton {
                    objectName: "trackLock"
                    iconName: header.locked ? "lock" : "lock-open"
                    small: true
                    ghost: true
                    checked: header.locked
                    tooltip: header.locked ? qsTr("Unlock %1").arg(header.tooltip) : qsTr("Lock %1").arg(header.tooltip)
                    Accessible.checkable: true
                    onClicked: header.propertyRequested(header.trackId, "locked", !header.locked)
                }
                Tk.IconButton {
                    objectName: "trackVisibility"
                    iconName: header.hidden ? "eye-off" : "eye"
                    small: true
                    ghost: true
                    tooltip: qsTr("Hide %1").arg(header.tooltip)
                    onClicked: header.propertyRequested(header.trackId, "hidden", !header.hidden)
                }
            }
            Tk.Caption {
                objectName: "trackRouting"
                visible: header.routing.length > 0 && header.height >= Tk.Theme.size.control * 2
                text: header.routing
                elide: Text.ElideRight
            }
        }
    }

    // Reorder grip: the frame background behind the buttons.
    MouseArea {
        objectName: "trackReorderArea"
        anchors.fill: parent
        z: -1
        enabled: header.gestures
        property real pressY: 0
        property bool reordering: false
        preventStealing: reordering
        onPressed: function(mouse) {
            pressY = mouse.y
            reordering = false
            header.selectRequested(header.trackId)
        }
        onPositionChanged: function(mouse) {
            if (!pressed) return
            const y = header.parent ? mapToItem(header.parent, mouse.x, mouse.y).y : mouse.y
            if (!reordering && Math.abs(mouse.y - pressY) >= Qt.styleHints.startDragDistance) {
                reordering = true
                header.reorderStarted(header.trackId, y)
            }
            if (reordering) header.reorderMoved(header.trackId, y)
        }
        onReleased: if (reordering) { reordering = false; header.reorderFinished(header.trackId) }
        onCanceled: if (reordering) { reordering = false; header.reorderCanceled(header.trackId) }
        onDoubleClicked: header.startRename()
    }

    Item {
        objectName: "trackResize"
        visible: header.resizable
        width: header.width
        height: Tk.Theme.size.seamTarget / 2
        y: header.height - height
        activeFocusOnTab: false
        Accessible.role: Accessible.Splitter
        Accessible.name: qsTr("Resize %1").arg(header.tooltip)
        Accessible.onIncreaseAction: header.heightStepRequested(header.trackId, 1)
        Accessible.onDecreaseAction: header.heightStepRequested(header.trackId, -1)

        Rectangle {
            objectName: "trackResizeSeam"
            visible: resizeArea.containsMouse || resizeArea.pressed || header.resizing
            width: parent.width
            height: Tk.Theme.size.border * 2
            y: parent.height - height
            color: Tk.Theme.color.seamHover
        }
        MouseArea {
            id: resizeArea
            anchors.fill: parent
            hoverEnabled: true
            // Without gestures only the cursor and hover seam stay here.
            acceptedButtons: header.gestures ? Qt.LeftButton : Qt.NoButton
            cursorShape: Qt.SizeVerCursor
            preventStealing: true
            property real pressY: 0
            property bool resizing: false
            onPressed: function(mouse) {
                pressY = mapToItem(header, mouse.x, mouse.y).y
                resizing = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const delta = mapToItem(header, mouse.x, mouse.y).y - pressY
                if (!resizing) {
                    if (delta === 0) return
                    resizing = true
                    header.resizeStarted(header.trackId)
                }
                header.resizeMoved(header.trackId, delta)
            }
            onReleased: if (resizing) { resizing = false; header.resizeFinished(header.trackId) }
            onCanceled: if (resizing) { resizing = false; header.resizeCanceled(header.trackId) }
        }
    }
}
