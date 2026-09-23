// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/diff-view.md): model computes aligned, read-only rows.
// Buttons only request a decision through DiffModel's host adapter.
T.Control {
    id: diff
    objectName: "diffView"
    property Tk.DiffModel diffModel: null
    property bool sideBySide: true
    property string tooltip: qsTr("Differences")
    property string currentHunkId: ""
    property Component leftDelegate: null
    property Component rightDelegate: null
    readonly property int hunkCount: diffModel ? diffModel.hunkCount : 0
    signal navigationRequested(string hunkId)
    signal decisionRequested(string requestId, string hunkId, string action)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: qsTr("Use Up and Down to navigate changes. Accept and Reject request a host transaction.")
    Accessible.focusable: enabled
    Accessible.focused: activeFocus

    function navigate(step) {
        if (!diffModel || hunkCount === 0) return false
        let current = -1
        for (let i = 0; i < hunkCount; ++i)
            if (diffModel.hunkAt(i).id === currentHunkId) current = i
        const next = current < 0 ? (step < 0 ? hunkCount - 1 : 0)
                                 : (current + step + hunkCount) % hunkCount
        const hunk = diffModel.hunkAt(next)
        currentHunkId = hunk.id
        rows.positionViewAtIndex(hunk.row, ListView.Center)
        navigationRequested(currentHunkId)
        return true
    }
    function request(action) {
        if (!diffModel || !currentHunkId) return ""
        return diffModel.requestDecision(currentHunkId, action,
                                         diffModel.leftRevision, diffModel.rightRevision)
    }
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Right)
            event.accepted = navigate(1)
        else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left)
            event.accepted = navigate(-1)
    }
    Connections {
        target: diff.diffModel
        function onStateChanged() {
            if (!diff.diffModel) return
            const currentRow = diff.diffModel.hunkRow(diff.currentHunkId)
            if (currentRow >= 0) {
                rows.positionViewAtIndex(currentRow, ListView.Center)
                return
            }
            diff.currentHunkId = diff.diffModel.hunkCount > 0
                ? diff.diffModel.hunkAt(0).id : ""
        }
        function onDecisionRequested(requestId, hunkId, action) {
            diff.decisionRequested(requestId, hunkId, action)
        }
    }
    Component.onCompleted: {
        if (diffModel && diffModel.hunkCount > 0)
            currentHunkId = diffModel.hunkAt(0).id
    }

    Component {
        id: textCell
        Tk.Label {
            text: parent && parent.value !== undefined ? String(parent.value) : ""
            mono: true
            anchors.fill: parent
        }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Rectangle {
            objectName: "diffToolbar"
            color: Tk.Theme.color.panel
            implicitHeight: Tk.Theme.size.chip
            Tk.Flex.shrink: 0
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                padding: Tk.Theme.space.xs
                Tk.Button {
                    text: qsTr("Previous")
                    small: true
                    tooltip: qsTr("Previous change")
                    available: diff.hunkCount > 0
                    onClicked: diff.navigate(-1)
                }
                Tk.Button {
                    text: qsTr("Next")
                    small: true
                    tooltip: qsTr("Next change")
                    available: diff.hunkCount > 0
                    onClicked: diff.navigate(1)
                }
                Tk.Label {
                    text: qsTr("%1 changes").arg(diff.hunkCount)
                    muted: true
                    Tk.Flex.grow: 1
                }
                Tk.Button {
                    text: diff.sideBySide ? qsTr("Inline") : qsTr("Side by side")
                    small: true
                    tooltip: qsTr("Switch diff layout")
                    onClicked: diff.sideBySide = !diff.sideBySide
                }
                Tk.Button {
                    text: qsTr("Reject")
                    small: true
                    tooltip: qsTr("Request rejection of selected change")
                    available: !!diff.currentHunkId && !!diff.diffModel
                    onClicked: diff.request("reject")
                }
                Tk.Button {
                    text: qsTr("Accept")
                    small: true
                    variant: "accent"
                    tooltip: qsTr("Request acceptance of selected change")
                    available: !!diff.currentHunkId && !!diff.diffModel
                    onClicked: diff.request("accept")
                }
            }
        }

        ListView {
            id: rows
            objectName: "diffRows"
            model: diff.diffModel
            clip: true
            Tk.Flex.grow: 1
            boundsBehavior: Flickable.StopAtBounds
            delegate: Item {
                id: region
                width: rows.width
                implicitHeight: diff.sideBySide || record.kind === "equal"
                    ? Tk.Theme.size.row : Tk.Theme.size.row * 2 + Tk.Theme.size.border
                height: implicitHeight
                property var record: diff.diffModel.get(index)
                readonly property bool selected: record.hunkId !== ""
                    && record.hunkId === diff.currentHunkId
                objectName: "diffRegion"
                Accessible.role: Accessible.ListItem
                Accessible.name: record.kind === "equal"
                    ? qsTr("Unchanged line %1: %2").arg(record.leftLine).arg(record.leftText)
                    : qsTr("Change %1, left line %2: %3, right line %4: %5")
                          .arg(record.hunkId).arg(record.leftLine).arg(record.leftText)
                          .arg(record.rightLine).arg(record.rightText)
                Accessible.onPressAction: {
                    if (record.hunkId) diff.currentHunkId = record.hunkId
                }
                Rectangle {
                    anchors.fill: parent
                    color: region.selected ? Tk.Theme.color.selection
                        : region.record.kind === "change" ? Tk.Theme.color.panelAlt
                        : Tk.Theme.color.surface
                }
                Tk.Flex {
                    anchors.fill: parent
                    direction: diff.sideBySide ? Tk.Flex.Row : Tk.Flex.Column
                    Item {
                        objectName: "diffLeftCell"
                        Tk.Flex.flex: 1
                        implicitHeight: Tk.Theme.size.row
                        property var value: region.record.leftValue
                        property var row: region.record
                        property string hunkId: region.record.hunkId || ""
                        Loader {
                            anchors.fill: parent
                            sourceComponent: diff.leftDelegate || textCell
                            property var value: parent.value
                            property var row: parent.row
                            property string hunkId: parent.hunkId
                        }
                    }
                    Rectangle {
                        color: Tk.Theme.color.border
                        implicitWidth: Tk.Theme.size.border
                        implicitHeight: Tk.Theme.size.border
                        visible: !diff.sideBySide && region.record.kind === "change"
                    }
                    Item {
                        objectName: "diffRightCell"
                        Tk.Flex.flex: 1
                        implicitHeight: Tk.Theme.size.row
                        visible: diff.sideBySide || region.record.kind === "change"
                        property var value: region.record.rightValue
                        property var row: region.record
                        property string hunkId: region.record.hunkId || ""
                        Loader {
                            anchors.fill: parent
                            sourceComponent: diff.rightDelegate || textCell
                            property var value: parent.value
                            property var row: parent.row
                            property string hunkId: parent.hunkId
                        }
                    }
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    width: Tk.Theme.size.border
                    height: parent.height
                    color: Tk.Theme.color.border
                    visible: diff.sideBySide && region.record.kind === "change"
                }
                TapHandler {
                    onTapped: if (region.record.hunkId) diff.currentHunkId = region.record.hunkId
                }
            }
        }
        Tk.Label {
            objectName: "diffError"
            text: diff.diffModel ? diff.diffModel.error : qsTr("No diff model")
            color: Tk.Theme.color.danger
            visible: text.length > 0
            Tk.Flex.shrink: 0
        }
    }
}
