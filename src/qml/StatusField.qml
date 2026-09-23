// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// One entry of a StatusBar: optional icon and caption text. Actionable fields
// use a real template button; readouts can opt out and remain static text.
Item {
    id: field
    // AGENT-NOTE: CSS gives buttons `min-width: auto`; Flex gives 0. Refusing
    // to shrink keeps the label whole and lets the row overflow instead.
    Tk.Flex.shrink: 0

    property string text: ""
    property string iconName: ""
    property bool muted: true
    property string tooltip: ""
    // Retain the historical click behavior by default; read-only status values
    // can set actionable false to leave both Tab order and actions entirely.
    property bool actionable: true
    property bool _actionFocused: false
    readonly property bool hovered: hover.hovered

    signal clicked()

    implicitWidth: row.implicitWidth + Tk.Theme.space.xs * 2
    // Preserve the 22px Compact row, then give actionable status entries the
    // existing control target role when the app opts into Touch density.
    implicitHeight: field.actionable && Tk.Density.modeName === "touch"
                    ? Tk.Theme.size.control : Tk.Theme.size.statusBar

    Accessible.role: Accessible.StaticText
    Accessible.name: field.text
    Accessible.ignored: field.actionable

    Tk.ToolTip {
        text: field.tooltip
        visible: field.tooltip.length > 0 && field.hovered
        delay: Tk.Theme.motion.tooltipDelay
    }

    Rectangle {
        anchors.fill: parent
        radius: Tk.Theme.radius.xs
        color: field.hovered ? Tk.Theme.color.hover : "transparent"
    }

    Tk.Flex {
        id: row
        anchors.fill: parent
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        paddingLeft: Tk.Theme.space.xs
        paddingRight: Tk.Theme.space.xs

        Tk.Icon {
            visible: field.iconName.length > 0
            name: field.iconName.length > 0 ? field.iconName : "circle"
            size: Tk.Theme.size.iconSm
            color: field.muted && !field.hovered ? Tk.Theme.color.textMuted : Tk.Theme.color.text
            Tk.Flex.shrink: 0
        }
        Tk.Caption {
            text: field.text
            muted: field.muted && !field.hovered
        }
    }

    // AGENT-CONTRACT (docs/control-interaction.md): actionable status fields
    // put all pointer, keyboard and assistive activation on this template
    // button, then emit the public clicked signal once through its click path.
    Loader {
        id: actionLoader
        anchors.fill: parent
        active: field.actionable
        sourceComponent: Component {
            T.AbstractButton {
                objectName: "statusAction"
                anchors.fill: parent
                enabled: field.actionable
                focusPolicy: enabled ? Qt.StrongFocus : Qt.NoFocus
                hoverEnabled: true
                background: null
                contentItem: Item { visible: false }

                Accessible.role: Accessible.Button
                Accessible.name: field.text.length > 0 ? field.text : field.tooltip
                Accessible.description: field.tooltip
                Accessible.onPressAction: if (enabled) click()

                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) {
                    if (!enabled || (event.key !== Qt.Key_Space
                                     && event.key !== Qt.Key_Return
                                     && event.key !== Qt.Key_Enter)) return
                    event.accepted = true
                    click()
                }

                onClicked: field.clicked()
                onActiveFocusChanged: field._actionFocused = activeFocus
            }
        }
    }

    Rectangle {
        objectName: "statusFocusRing"
        anchors.fill: parent
        anchors.margins: -Tk.Theme.size.focusRing
        radius: Tk.Theme.radius.xs
        color: "transparent"
        border.width: Tk.Theme.size.focusRing
        border.color: Tk.Theme.color.focus
        visible: field.enabled && field.actionable && field._actionFocused
    }

    HoverHandler { id: hover }
}
