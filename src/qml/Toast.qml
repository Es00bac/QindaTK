// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Transient alert that remains until timeout or explicit dismissal. Focus or
// pointer hover pauses the themed countdown so its action stays reachable.
FocusScope {
    id: control
    objectName: "toast"
    property string title: ""
    property string message: ""
    property string variant: "info"
    property string actionText: ""
    property string actionId: ""
    property string notificationId: ""
    property Tk.NotificationModel notificationModel: null
    property int duration: Tk.Theme.motion.toastTimeout
    property int remaining: duration
    property bool paused: pointer.hovered || activeFocus
    readonly property int tickInterval: Math.max(Tk.Theme.motion.fast, Tk.Theme.motion.base)
    signal dismissed()
    signal actionRequested(string actionId)

    visible: false
    implicitWidth: notice.implicitWidth
    implicitHeight: notice.implicitHeight
    Accessible.role: Accessible.AlertMessage
    Accessible.name: control.title.length > 0 ? control.title + ". " + control.message : control.message
    Accessible.description: control.actionText.length > 0
        ? qsTr("Action available: %1").arg(control.actionText) : ""

    function show() {
        remaining = duration
        visible = true
    }
    function announce(titleText, messageText, severity, source, actions) {
        if (!notificationModel) return ""
        return notificationModel.post(titleText, messageText, severity || "info",
                                      source || "", actions || [])
    }
    function presentNext() {
        if (visible || !notificationModel) return false
        const next = notificationModel.takeNextToast()
        if (!next || !next.notificationId) return false
        notificationId = next.notificationId
        title = next.title || ""
        message = next.message || ""
        variant = next.severity === "error" ? "danger" : next.severity
        const action = next.actions && next.actions.length > 0 ? next.actions[0] : null
        actionId = action ? String(action.id || "") : ""
        actionText = action ? String(action.label || "") : ""
        show()
        return true
    }
    function dismiss() {
        if (!visible) return
        visible = false
        countdown.stop()
        dismissed()
    }
    function activateAction() {
        if (actionText.length === 0) return
        actionRequested(actionId)
        dismiss()
    }
    onDismissed: Qt.callLater(function() { control.presentNext() })
    onNotificationModelChanged: Qt.callLater(function() { control.presentNext() })
    Component.onCompleted: Qt.callLater(function() { control.presentNext() })
    Connections {
        target: control.notificationModel
        function onPosted() { control.presentNext() }
    }

    HoverHandler { id: pointer }
    Timer {
        id: countdown
        interval: control.tickInterval
        repeat: true
        running: control.visible && !control.paused && control.duration > 0 && interval > 0
        onTriggered: {
            control.remaining = Math.max(0, control.remaining - interval)
            if (control.remaining === 0) control.dismiss()
        }
    }
    // AGENT-CONTRACT: a newly presented toast announces its message and
    // available action through Qt's live accessibility event, including
    // toasts posted by NotificationModel while focus remains elsewhere.
    onVisibleChanged: if (visible) {
        remaining = duration
        if (Accessible.name.length > 0)
            Accessible.announce(Accessible.name + (Accessible.description.length > 0
                ? ". " + Accessible.description : ""))
    }
    onDurationChanged: if (visible) remaining = duration

    Tk.Notice {
        id: notice
        objectName: "toastNotice"
        anchors.fill: parent
        title: control.title
        text: control.message
        variant: control.variant
        dismissible: true
        onDismissed: control.dismiss()
        actions: Tk.Button {
            id: actionControl
            objectName: "toastAction"
            visible: control.actionText.length > 0
            text: control.actionText
            variant: "ghost"
            tooltip: control.actionText
            onClicked: control.activateAction()
            Keys.onEscapePressed: control.dismiss()
        }
    }
}
