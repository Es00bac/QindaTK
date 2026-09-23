// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Persistent notices are owned by the injected bounded model. Action IDs are
// emitted to the host for command/context validation rather than invoked here.
Tk.Box {
    id: control
    objectName: "notificationCenter"
    property var notificationModel: null
    property bool showRead: true
    property real listHeight: Tk.Theme.size.fieldWidth * 3
    signal actionRequested(string notificationId, string actionId)

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Notifications")

    function count() {
        if (!notificationModel) return 0
        if (notificationModel.count !== undefined) return notificationModel.count
        if (notificationModel.notifications !== undefined) return notificationModel.notifications.length
        return 0
    }
    function markRead(notificationId) {
        return notificationModel && notificationModel.markRead ? notificationModel.markRead(notificationId) : false
    }
    function dismissNotification(notificationId) {
        return notificationModel && notificationModel.dismiss ? notificationModel.dismiss(notificationId) : false
    }
    function activate(notificationId, actionId) {
        actionRequested(notificationId, actionId)
        markRead(notificationId)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        width: parent ? parent.width : Tk.Theme.size.fieldWidth
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("Notifications"); wide: true; Tk.Flex.grow: 1 }
            Tk.Badge {
                objectName: "notificationCount"
                text: String(control.count())
                variant: control.count() > 0 ? "accent" : "default"
                tooltip: qsTr("Notification count")
            }
        }
        ListView {
            id: notificationList
            objectName: "notificationList"
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Notifications")
            model: control.notificationModel
            clip: true
            spacing: Tk.Theme.space.xs
            implicitHeight: Math.min(Math.max(contentHeight, Tk.Theme.size.control), control.listHeight)
            height: implicitHeight
            Tk.Flex.shrink: 0
            boundsBehavior: Flickable.StopAtBounds
            delegate: Tk.Box {
                id: notificationRow
                required property string notificationId
                required property string severity
                required property string title
                required property string message
                required property string source
                required property string timestamp
                required property bool read
                required property var actions
                required property var model
                readonly property int repeatCount: model.repeatCount === undefined ? 1 : Number(model.repeatCount)
                objectName: "notification_" + notificationId
                width: notificationList.width
                visible: control.showRead || !read
                implicitHeight: visible ? notice.implicitHeight : 0
                Accessible.role: Accessible.ListItem
                Accessible.name: (read ? qsTr("Read") : qsTr("Unread")) + ", " + title + ". " + message
                                 + (repeatCount > 1 ? qsTr(" Repeated %1 times.").arg(repeatCount) : "")

                Tk.Notice {
                    id: notice
                    objectName: "notificationNotice_" + notificationRow.notificationId
                    anchors.fill: parent
                    title: notificationRow.repeatCount > 1
                           ? qsTr("%1 (%2)").arg(notificationRow.title).arg(notificationRow.repeatCount)
                           : notificationRow.title
                    text: notificationRow.message
                    variant: notificationRow.severity === "error" ? "danger" : notificationRow.severity
                    dismissible: true
                    onDismissed: control.dismissNotification(notificationRow.notificationId)
                    actions: Tk.Flex {
                        direction: Tk.Flex.Column
                        align: Tk.Flex.End
                        gap: Tk.Theme.space.xs
                        Tk.Label {
                            objectName: "notificationMeta"
                            visible: notificationRow.source.length > 0 || notificationRow.timestamp.length > 0
                            text: [notificationRow.source, notificationRow.timestamp].filter(function(value) { return value.length > 0 }).join(" · ")
                            color: Tk.Theme.color.textMuted
                            font.pixelSize: Tk.Theme.font.micro
                        }
                        Tk.Flex {
                            direction: Tk.Flex.Row
                            gap: Tk.Theme.space.xs
                            Repeater {
                                model: notificationRow.actions || []
                                delegate: Tk.Button {
                                    required property var modelData
                                    required property int index
                                    objectName: "notificationAction_" + notificationRow.notificationId + "_" + (modelData.id || index)
                                    text: modelData.label || modelData.id || qsTr("Action")
                                    tooltip: text
                                    variant: "ghost"
                                    onClicked: control.activate(notificationRow.notificationId, String(modelData.id || ""))
                                }
                            }
                            Tk.Button {
                                objectName: "notificationMarkRead_" + notificationRow.notificationId
                                visible: !notificationRow.read
                                text: qsTr("Mark read")
                                tooltip: qsTr("Mark this notification as read")
                                variant: "ghost"
                                onClicked: control.markRead(notificationRow.notificationId)
                            }
                        }
                    }
                }
            }
        }
        Tk.EmptyState {
            objectName: "notificationEmpty"
            visible: control.count() === 0
            title: qsTr("No notifications")
            text: qsTr("Application notices will appear here.")
        }
    }
}
