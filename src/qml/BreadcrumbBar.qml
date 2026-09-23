// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Stable-ID hierarchy navigation. Horizontal overflow is a real viewport;
// labels are presentation only and never participate in identity lookup.
Item {
    id: bar
    property var path: []
    property string tooltip: qsTr("Breadcrumb navigation")
    property real itemMaximumWidth: Tk.Theme.size.fieldWidth
    property string siblingParentId: ""
    property var siblingItems: []
    property bool siblingPending: false
    property string siblingRequestId: ""
    property Item siblingAnchor: null
    property int _siblingSerial: 0
    signal navigateRequested(string id)
    signal siblingsRequested(string id, string requestId)

    function hasSiblingAction(segment) {
        return (segment.siblings && segment.siblings.length > 0)
            || segment.siblingProvider === true
    }

    function naturalPathWidth() {
        let result = 0
        for (let i = 0; i < path.length; ++i) {
            const segment = path[i]
            const label = String(segment.label || segment.id)
            result += Math.min(itemMaximumWidth, label.length * Tk.Theme.font.caption * 0.6 + Tk.Theme.space.md * 2)
            if (hasSiblingAction(segment)) result += Tk.Theme.size.control
            if (i < path.length - 1) {
                result += Tk.Theme.font.caption * 0.6 + Tk.Theme.space.xs * 2
            }
        }
        return result
    }

    implicitWidth: viewport.implicitWidth
    implicitHeight: viewport.height
    Accessible.role: Accessible.ToolBar
    Accessible.name: tooltip
    function activate(id) {
        if (String(id).length === 0) return false
        navigateRequested(String(id))
        return true
    }
    function showSiblings(segment, anchor) {
        if (!segment || !hasSiblingAction(segment)) return false
        siblingAnchor = anchor || null
        siblingParentId = String(segment.id)
        if (segment.siblings && segment.siblings.length) {
            siblingItems = segment.siblings
            siblingPending = false
            siblingRequestId = ""
            siblingMenu.popup(siblingAnchor)
        } else {
            siblingItems = []
            siblingPending = true
            siblingRequestId = "breadcrumb-" + (++_siblingSerial)
            siblingsRequested(siblingParentId, siblingRequestId)
        }
        return true
    }
    function supplySiblings(id, requestId, items) {
        if (!siblingPending || String(id) !== siblingParentId
            || String(requestId) !== siblingRequestId) return false
        siblingPending = false
        siblingRequestId = ""
        siblingItems = items && typeof items.length === "number" ? items : []
        if (siblingItems.length > 0) siblingMenu.popup(siblingAnchor)
        return true
    }
    onPathChanged: {
        // A path replacement can recycle the popup anchor even when an ID is
        // reused. A late result must not open against the new hierarchy.
        if (siblingMenu && siblingMenu.visible) siblingMenu.close()
        if (siblingPending) {
            siblingPending = false
            siblingRequestId = ""
            siblingItems = []
            siblingAnchor = null
        }
    }

    Tk.Scroll {
        id: viewport
        objectName: "breadcrumbViewport"
        width: bar.width
        height: Tk.Theme.size.control
        overflowX: Tk.Scroll.Auto
        overflowY: Tk.Scroll.Hidden
        accessibleName: bar.tooltip
        Item {
            id: rowHost
            anchors.left: parent.left
            anchors.top: parent.top
            width: Math.max(viewport.viewportWidth, bar.naturalPathWidth())
            implicitWidth: bar.naturalPathWidth()
            implicitHeight: row.implicitHeight
        Tk.Flex {
            id: row
            anchors.fill: parent
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Repeater {
                model: bar.path
                delegate: Item {
                    id: segmentSlot
                    required property int index
                    required property var modelData
                    objectName: "breadcrumbSegment_" + modelData.id
                    Tk.Flex.shrink: 0
                    readonly property real buttonWidth: Math.min(bar.itemMaximumWidth,
                        String(modelData.label || modelData.id).length * Tk.Theme.font.caption * 0.6 + Tk.Theme.space.md * 2)
                    readonly property real separatorWidth: index < bar.path.length - 1
                        ? Tk.Theme.font.caption * 0.6 + Tk.Theme.space.xs * 2 : 0
                    implicitWidth: buttonWidth + (bar.hasSiblingAction(modelData) ? Tk.Theme.size.control : 0) + separatorWidth
                    Tk.Flex.minWidth: buttonWidth + (bar.hasSiblingAction(modelData) ? Tk.Theme.size.control : 0) + separatorWidth
                    implicitHeight: Tk.Theme.size.control
                    T.Button {
                        id: segmentButton
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: segmentSlot.buttonWidth
                        leftPadding: Tk.Theme.space.md
                        rightPadding: Tk.Theme.space.md
                        text: String(modelData.label || modelData.id)
                        enabled: modelData.enabled !== false && index < bar.path.length - 1
                        focusPolicy: Qt.TabFocus
                        objectName: "breadcrumbSelect_" + modelData.id
                        Accessible.role: Accessible.Button
                        Accessible.name: text
                        Accessible.description: modelData.id
                        Accessible.focusable: enabled
                        onClicked: bar.activate(modelData.id)
                        contentItem: Tk.Label { id: label; text: segmentButton.text; elide: Text.ElideRight; color: segmentButton.enabled ? Tk.Theme.color.text : Tk.Theme.color.textMuted }
                        background: Rectangle { color: segmentButton.hovered ? Tk.Theme.color.hover : "transparent"; radius: Tk.Theme.radius.sm }
                        Tk.ToolTip { text: segmentButton.text; visible: segmentButton.hovered }
                    }
                    Tk.IconButton {
                        id: siblingButton
                        objectName: "breadcrumbSiblings_" + modelData.id
                        visible: bar.hasSiblingAction(modelData)
                        anchors.left: segmentButton.right
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "chevron-down"
                        tooltip: qsTr("Choose a sibling of %1").arg(modelData.label || modelData.id)
                        Accessible.name: tooltip
                        onClicked: bar.showSiblings(modelData, siblingButton)
                    }
                    Tk.Label {
                        id: separator
                        visible: index < bar.path.length - 1
                        anchors.left: siblingButton.visible ? siblingButton.right : segmentButton.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: "/"
                        color: Tk.Theme.color.textMuted
                    }
                }
            }
        }
        }
    }
    Tk.Menu {
        id: siblingMenu
        objectName: "breadcrumbSiblingMenu"
        Repeater {
            model: bar.siblingItems
            delegate: Tk.MenuItem {
                required property var modelData
                text: modelData.label || modelData.id
                checked: modelData.id === bar.siblingParentId
                onTriggered: bar.activate(modelData.id)
            }
        }
    }
}
