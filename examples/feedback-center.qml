// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Preview the persistent notification model alongside task and history states.
Rectangle {
    width: 1000
    height: 760
    color: Tk.Theme.color.bg

    Tk.NotificationModel {
        id: notifications
        Component.onCompleted: {
            post(qsTr("Media needs attention"), qsTr("The source file is offline."),
                 "warning", qsTr("Import"), [{ id: "media.relink", label: qsTr("Relink") }])
            post(qsTr("Export complete"), qsTr("The delivery file is ready."),
                 "success", qsTr("Export"), [{ id: "result.open", label: qsTr("Open") }])
            post(qsTr("Autosave paused"), qsTr("A destination is read-only."),
                 "error", qsTr("Workspace"))
        }
    }
    Tk.JobModel { id: jobs }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg

        Tk.Overline { title: qsTr("FEEDBACK AND HISTORY"); wide: true; color: Tk.Theme.color.accent }
        Tk.Caption { text: qsTr("Persistent alerts retain stable action IDs; tasks and history bind to their authoritative services.") }
        Tk.Toast {
            width: Tk.Theme.size.fieldWidth * 14
            title: qsTr("Background task finished")
            message: qsTr("Peak analysis is ready.")
            actionText: qsTr("Open")
            actionId: "result.open"
        }
        Tk.NotificationCenter {
            width: parent.width
            notificationModel: notifications
            listHeight: Tk.Theme.size.fieldWidth * 4
        }
        Tk.TaskCenter { width: parent.width; jobModel: jobs }
        Tk.HistoryView { width: parent.width }
    }
}
