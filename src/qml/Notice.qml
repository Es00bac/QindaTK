// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// An inline message bar: tinted background, a 2px left edge and an icon in
// the status colour of `variant` ("info", "success", "warning", "danger"),
// optional title, wrapping text, an `actions` slot and an optional
// dismiss button. Never modal; place it where the message applies.
Tk.Box {
    id: notice

    property string text: ""
    property string title: ""
    property string variant: "info"
    property string iconName: ""
    property bool dismissible: false
    property alias actions: actionsHost.data

    signal dismissed()

    // D-328: the width at which the title and the text each fit on one line
    // (icon, actions, dismiss button, padding and border included). The
    // text column has a zero flex basis, so `implicitWidth` leaves the text
    // out; a host that sizes a notice from its content (Tk.Toast) reads this.
    readonly property real naturalWidth: Math.ceil(row.implicitWidth
        + Math.max(notice.title.length > 0 ? titleMetrics.advanceWidth : 0,
                   notice.text.length > 0 ? textMetrics.advanceWidth : 0))
        + insetLeft + insetRight

    readonly property color statusColor: notice.variant === "danger" ? Tk.Theme.color.danger
                                       : notice.variant === "warning" ? Tk.Theme.color.warning
                                       : notice.variant === "success" ? Tk.Theme.color.success
                                       : Tk.Theme.color.info
    readonly property string statusIcon: notice.iconName.length > 0 ? notice.iconName
                                       : notice.variant === "danger" ? "circle-x"
                                       : notice.variant === "warning" ? "triangle-alert"
                                       : notice.variant === "success" ? "circle-check"
                                       : "info"

    color: notice.variant === "danger" ? Tk.Theme.color.dangerSubtle
         : notice.variant === "info" ? Tk.Theme.color.accentSubtle
         : Tk.Theme.alpha(notice.statusColor, 0.15)
    borderLeft: 2
    borderColor: notice.statusColor
    padding: Tk.Theme.space.sm
    paddingLeft: Tk.Theme.space.md

    Accessible.role: Accessible.AlertMessage
    Accessible.name: notice.title.length > 0 ? notice.title + ". " + notice.text : notice.text

    TextMetrics { id: titleMetrics; font: titleLabel.font; text: notice.title }
    TextMetrics { id: textMetrics; font: textLabel.font; text: notice.text }

    Tk.Flex {
        id: row
        direction: Tk.Flex.Row
        align: Tk.Flex.Start
        gap: Tk.Theme.space.sm

        Tk.Icon {
            name: notice.statusIcon
            size: Tk.Theme.size.icon
            color: notice.statusColor
            Tk.Flex.shrink: 0
            Tk.Flex.alignSelf: Tk.Flex.Start
        }
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minWidth: 0

            Tk.Label {
                id: titleLabel
                objectName: "noticeTitle"
                visible: notice.title.length > 0
                text: notice.title
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
            Tk.Label {
                id: textLabel
                objectName: "noticeText"
                visible: notice.text.length > 0
                text: notice.text
                font.pixelSize: Tk.Theme.font.small
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
        }
        Tk.Flex {
            id: actionsHost
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            visible: children.length > 0
            Tk.Flex.shrink: 0
        }
        Tk.IconButton {
            objectName: "noticeDismiss"
            visible: notice.dismissible
            small: true
            iconName: "x"
            tooltip: qsTr("Dismiss")
            Tk.Flex.shrink: 0
            Tk.Flex.alignSelf: Tk.Flex.Start
            onClicked: notice.dismissed()
        }
    }
}
