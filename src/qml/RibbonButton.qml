// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// One ribbon command at one of three weights (D-297):
//   "large" — the group's primary command: icon over a one-line label, as
//             tall as the two stacked rows beside it (`bandHeight`);
//   "small" — icon + one-line label at control height (stacked two high);
//   "icon"  — a square icon-only button (the label becomes the tooltip).
// `hasMenu` adds a chevron; with `split` the chevron is its own target that
// emits `menuRequested` while the body emits `clicked`.
//
// AGENT-CONTRACT (Ribbon.qml, RibbonGroup.qml): `largeWidth`, `smallWidth`
// and `iconWidth` are this button's width at each weight, computed from the
// label metrics alone — never from the current `size` — so the ribbon can
// plan its squeeze level without laying out every weight (no binding loop).
T.AbstractButton {
    id: control
    Tk.Flex.shrink: 0

    property string size: "small"
    property string iconName: ""
    property string tooltip: ""
    property string shortcutText: ""
    property bool hasMenu: false
    property bool split: false
    property real bandHeight: Tk.Theme.size.control * 2 + Tk.Theme.space.xs
    readonly property bool menuHovered: split && hasMenu && chevronHover.hovered
    signal menuRequested()

    // The chevron plus the row gap before it (Flex `gap: space.sm`).
    readonly property real _chevron: hasMenu ? Tk.Theme.size.iconSm + Tk.Theme.space.sm : 0
    readonly property real largeWidth: Math.ceil(Math.max(Tk.Theme.size.chip + Tk.Theme.space.sm,
        Math.min(Tk.Theme.size.labelWidth, labelMetrics.advanceWidth) + _chevron + Tk.Theme.space.md * 2))
    readonly property real smallWidth: Math.ceil((iconName.length > 0 ? Tk.Theme.size.icon + Tk.Theme.space.sm : 0)
        + smallMetrics.advanceWidth + Tk.Theme.space.sm * 2 + _chevron)
    readonly property real iconWidth: iconName.length > 0 ? Tk.Theme.size.control + _chevron : smallWidth
    readonly property string plainLabel: String(text).replace(/&&/g, "\u0001").replace(/&/g, "").replace(/\u0001/g, "&")

    implicitWidth: size === "large" ? largeWidth : size === "icon" ? iconWidth : smallWidth
    implicitHeight: size === "large" ? bandHeight : Tk.Theme.size.control
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    opacity: enabled ? Tk.Theme.opacity.full : Tk.Theme.opacity.disabled

    Accessible.role: hasMenu && !split ? Accessible.ButtonMenu : Accessible.Button
    Accessible.name: plainLabel.length > 0 ? plainLabel : tooltip
    Accessible.description: tooltip.length > 0 && tooltip !== plainLabel ? tooltip : shortcutText
    Accessible.focusable: enabled
    Accessible.checkable: checkable
    Accessible.checked: checked
    Accessible.onPressAction: if (enabled) control.click()

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Down && hasMenu) { event.accepted = true; control.menuRequested(); return }
        if (event.key !== Qt.Key_Space && event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter) return
        event.accepted = true
        if (enabled && !event.isAutoRepeat) control.click()
    }

    TextMetrics { id: labelMetrics; font.family: Tk.Theme.font.family; font.pixelSize: Tk.Theme.font.caption; text: control.plainLabel }
    TextMetrics { id: smallMetrics; font.family: Tk.Theme.font.family; font.pixelSize: Tk.Theme.font.small; text: control.plainLabel }

    Tk.ToolTip {
        text: {
            const head = control.size === "icon" ? control.plainLabel : (control.tooltip !== control.plainLabel ? control.tooltip : "")
            const tail = control.shortcutText.length > 0 ? control.shortcutText : ""
            return [head.length > 0 ? head : (tail.length > 0 ? control.plainLabel : ""), tail].filter(Boolean).join("  ")
        }
        visible: text.length > 0 && control.hovered
        delay: Tk.Theme.motion.tooltipDelay
    }

    background: Rectangle {
        objectName: "ribbonButtonBackground"
        radius: Tk.Theme.radius.md
        color: control.down ? Tk.Theme.color.pressed
             : control.checked ? Tk.Theme.color.accentSubtle
             : control.hovered ? Tk.Theme.color.hover
             : Tk.Theme.alpha(Tk.Theme.color.hover, Tk.Theme.opacity.hidden)
        border.width: control.checked ? Tk.Theme.size.border : 0
        border.color: Tk.Theme.color.controlActiveBorder
        Behavior on color { ColorAnimation { duration: Tk.Theme.motion.fast } }
        Rectangle {
            visible: control.split && control.hasMenu && control.hovered
            color: Tk.Theme.color.divider
            width: control.size === "large" ? parent.width - Tk.Theme.space.md : Tk.Theme.size.border
            height: control.size === "large" ? Tk.Theme.size.border : parent.height - Tk.Theme.space.md
            x: control.size === "large" ? Tk.Theme.space.md / 2 : parent.width - control._chevron
            y: control.size === "large" ? parent.height / 2 + Tk.Theme.space.xs : Tk.Theme.space.md / 2
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Tk.Theme.size.border
            radius: parent.radius + Tk.Theme.size.border
            color: Tk.Theme.alpha(Tk.Theme.color.bg, Tk.Theme.opacity.hidden)
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
            visible: control.visualFocus
        }
    }

    readonly property color _ink: !enabled ? Tk.Theme.color.textDisabled
                                : checked ? Tk.Theme.color.accentText
                                : Tk.Theme.color.text
    readonly property color _iconInk: !enabled ? Tk.Theme.color.textDisabled
                                    : checked ? Tk.Theme.color.accent
                                    : Tk.Theme.color.textMuted

    contentItem: Item {
        implicitWidth: control.implicitWidth
        implicitHeight: control.implicitHeight
        // ---- large: icon over a one-line caption (+ chevron) ----
        // AGENT-NOTE: anchors, not a Flex column: two stacked rows of
        // controls leave room for exactly one caption line under a 20px
        // icon, so the caption elides instead of wrapping.
        Item {
            visible: control.size === "large"
            anchors.fill: parent
            Tk.Icon {
                id: largeIcon
                objectName: "ribbonButtonIcon"
                visible: control.iconName.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.verticalCenter
                anchors.bottomMargin: -Tk.Theme.space.xs
                name: control.iconName
                size: Tk.Theme.size.iconXl
                color: control._iconInk
            }
            Text {
                id: largeLabel
                objectName: "ribbonButtonLabel"
                anchors.top: parent.verticalCenter
                anchors.topMargin: Tk.Theme.space.sm
                x: (parent.width - width - control._chevron) / 2
                width: Math.min(implicitWidth, parent.width - Tk.Theme.space.sm * 2 - control._chevron)
                text: control.plainLabel
                color: control._ink
                font.family: Tk.Theme.font.family
                font.pixelSize: Tk.Theme.font.caption
                elide: Text.ElideRight
            }
            Tk.Icon {
                visible: control.hasMenu
                anchors.verticalCenter: largeLabel.verticalCenter
                x: largeLabel.x + largeLabel.width + Tk.Theme.space.xs
                name: "chevron-down"
                size: Tk.Theme.size.iconSm
                color: control._iconInk
            }
        }
        // ---- small / icon: one row ----
        Tk.Flex {
            visible: control.size !== "large"
            anchors.fill: parent
            anchors.leftMargin: control.size === "icon" ? 0 : Tk.Theme.space.sm
            anchors.rightMargin: control.size === "icon" ? 0 : Tk.Theme.space.sm
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            justify: control.size === "icon" ? Tk.Flex.Center : Tk.Flex.Start
            gap: Tk.Theme.space.sm
            Tk.Icon {
                visible: control.iconName.length > 0
                name: control.iconName
                size: Tk.Theme.size.icon
                color: control._iconInk
            }
            Text {
                visible: control.size === "small" || control.iconName.length === 0
                Tk.Flex.grow: 1
                text: control.plainLabel
                color: control._ink
                font.family: Tk.Theme.font.family
                font.pixelSize: Tk.Theme.font.small
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Tk.Icon {
                visible: control.hasMenu
                name: "chevron-down"
                size: Tk.Theme.size.iconSm
                color: control._iconInk
            }
        }
    }

    // The split chevron is its own pointer target; it never fires `clicked`.
    Item {
        objectName: "ribbonButtonMenuArea"
        visible: control.split && control.hasMenu
        z: 1
        x: control.size === "large" ? 0 : control.width - control._chevron - Tk.Theme.space.xs
        y: control.size === "large" ? control.height / 2 + Tk.Theme.space.xs : 0
        width: control.size === "large" ? control.width : control._chevron + Tk.Theme.space.xs
        height: control.size === "large" ? control.height / 2 - Tk.Theme.space.xs : control.height
        HoverHandler { id: chevronHover }
        TapHandler { enabled: control.enabled; onTapped: control.menuRequested() }
    }
    onClicked: if (hasMenu && !split) control.menuRequested()
}
