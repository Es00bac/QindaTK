// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// "+k more" link of CalendarGridView (month cells and the all-day lane).
// Internal part; the owner names it calendarGridMore_<date> and handles
// `clicked` by emitting moreRequested(date).
T.AbstractButton {
    id: control

    property int count: 0
    property string tooltip: qsTr("Show all %n events", "", count)

    implicitWidth: label.implicitWidth + leftPadding + rightPadding
    implicitHeight: Tk.Theme.size.controlSm
    leftPadding: Tk.Theme.space.sm
    rightPadding: Tk.Theme.space.sm
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    text: qsTr("+%1 more").arg(count)

    Accessible.role: Accessible.Button
    Accessible.name: tooltip

    background: Rectangle {
        radius: Tk.Theme.radius.sm
        color: control.down ? Tk.Theme.color.pressed : control.hovered ? Tk.Theme.color.hover : "transparent"
        border.width: control.activeFocus ? Tk.Theme.size.focusRing : 0
        border.color: Tk.Theme.color.focus
    }
    contentItem: Tk.Label {
        id: label
        text: control.text
        font.pixelSize: Tk.Theme.font.caption
        font.weight: Font.DemiBold
        color: Tk.Theme.color.textMuted
    }
    Tk.ToolTip { text: control.tooltip; visible: control.hovered }
}
