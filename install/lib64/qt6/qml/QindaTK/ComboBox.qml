// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): the 24px (small 20) picker on inputBg
// with a chevron, whose popup (objectName "comboPopup") is a Menu-styled
// list of 22px rows, at most 12 visible before it scrolls. `textRole`
// and `iconRole` read the model; a JS array of strings works with no
// roles. `placeholderText` shows while currentIndex is -1. `editable`
// turns the content into a text input, as T.ComboBox defines.
T.ComboBox {
    id: control

    property bool small: false
    property string iconRole: ""
    property string placeholderText: ""
    property string tooltip: ""

    readonly property real controlHeight: small ? Tk.Theme.size.controlSm : Tk.Theme.size.control
    readonly property real edgePadding: Tk.Theme.space.sm + 2
    readonly property string roleName: control.textRole.length > 0 ? control.textRole : "modelData"

    function textFor(model) {
        if (model === undefined || model === null) return ""
        const value = model[control.roleName]
        if (value !== undefined) return String(value)
        if (model.display !== undefined) return String(model.display)
        return typeof model === "string" ? model : ""
    }
    function iconFor(model) {
        if (control.iconRole.length === 0 || model === undefined || model === null) return ""
        const value = model[control.iconRole]
        return value === undefined ? "" : String(value)
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    leftPadding: edgePadding
    rightPadding: edgePadding + indicator.width + Tk.Theme.space.xs
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.ComboBox
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : control.displayText

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered && !control.popup.visible
        delay: 600
    }

    delegate: T.ItemDelegate {
        id: row
        required property var model
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        height: Tk.Theme.size.menuItem
        text: control.textFor(model)
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        padding: 0
        leftPadding: Tk.Theme.space.md
        rightPadding: Tk.Theme.space.md
        Accessible.role: Accessible.ListItem
        Accessible.name: text

        background: Rectangle {
            radius: Tk.Theme.radius.xs
            color: row.highlighted ? Tk.Theme.color.hover : "transparent"
        }
        contentItem: Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs + 2
            Tk.Icon {
                readonly property string iconName: control.iconFor(row.model)
                visible: iconName.length > 0
                name: iconName
                size: Tk.Theme.size.icon
                color: row.index === control.currentIndex ? Tk.Theme.color.accentText : Tk.Theme.color.textMuted
                Tk.Flex.shrink: 0
            }
            Tk.Label {
                text: row.text
                color: row.index === control.currentIndex ? Tk.Theme.color.accentText : Tk.Theme.color.text
                font.pixelSize: Tk.Theme.font.small
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Icon {
                visible: row.index === control.currentIndex
                name: "check"
                size: Tk.Theme.size.iconSm
                color: Tk.Theme.color.accentText
                Tk.Flex.shrink: 0
            }
        }
    }

    indicator: Tk.Icon {
        x: control.width - width - control.edgePadding
        y: control.topPadding + (control.availableHeight - height) / 2
        name: "chevron-down"
        size: control.small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
        color: control.hovered || control.popup.visible ? Tk.Theme.color.text : Tk.Theme.color.textMuted
    }

    contentItem: T.TextField {
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse
        verticalAlignment: Text.AlignVCenter
        color: control.enabled ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
        selectionColor: Tk.Theme.color.selection
        selectedTextColor: Tk.Theme.color.text
        font.family: Tk.Theme.font.family
        font.pixelSize: control.small ? Tk.Theme.font.small : Tk.Theme.font.body
        renderType: Text.NativeRendering
        padding: 0
    }

    background: Rectangle {
        implicitWidth: Tk.Theme.size.fieldWidth
        implicitHeight: control.controlHeight
        radius: Tk.Theme.radius.sm
        color: control.down || control.popup.visible ? Tk.Theme.color.controlHoverBg : Tk.Theme.color.inputBg
        border.width: Tk.Theme.size.border
        border.color: control.activeFocus || control.popup.visible ? Tk.Theme.color.inputFocusBorder
                    : control.hovered ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.inputBorder
        Behavior on border.color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: Tk.Theme.color.focus
            visible: control.visualFocus
        }
        Tk.Label {
            visible: control.currentIndex < 0 && !control.editable && control.placeholderText.length > 0
            x: control.leftPadding
            width: Math.max(0, parent.width - control.leftPadding - control.rightPadding)
            height: parent.height
            text: control.placeholderText
            color: Tk.Theme.color.textMuted
            font.pixelSize: control.small ? Tk.Theme.font.caption : Tk.Theme.font.small
        }
    }

    popup: T.Popup {
        objectName: "comboPopup"
        y: control.height + Tk.Theme.space.xs
        width: control.width
        implicitHeight: Math.min(contentHeight, Tk.Theme.size.menuItem * 12) + topPadding + bottomPadding
        padding: Tk.Theme.space.xs
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: Tk.ScrollBar { }
        }

        background: Rectangle {
            color: Tk.Theme.color.popoverBg
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.popoverBorder
            radius: Tk.Theme.radius.md
        }

        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Tk.Theme.motion.fast } }
        exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Tk.Theme.motion.fast } }
    }
}
