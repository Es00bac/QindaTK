// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk
import "FormControls.js" as FormControls

// Internal part of FormDesigner (D-311): one form control as design view
// draws it — what it shows (caption, bound field "[name]", "=expression",
// items, subform name, pages, image source) in a frame of its design size.
// It takes no input: FormControlCanvas selects, moves and resizes it. With
// `live` it draws the static types (label, line, rectangle, image) of a
// previewed form, without the design frame.
Item {
    id: shape
    required property var control
    property bool live: false
    property int currentPage: 0
    readonly property string type: String(control.type || "")
    readonly property bool inputLike: type === "textBox" || type === "comboBox" || type === "listBox"
    readonly property string boundText: control.expression ? String(control.expression)
        : control.fieldName ? "[" + control.fieldName + "]" : qsTr("Unbound")
    readonly property var items: control.items || []
    readonly property var pages: control.pages || []

    Accessible.role: Accessible.Graphic
    Accessible.name: String(control.name || type)

    // The frame of the boxed types.
    Rectangle {
        objectName: "formShapeFrame"
        anchors.fill: parent
        visible: shape.type !== "line" && (!shape.live || shape.type === "rectangle")
        radius: shape.type === "commandButton" ? Tk.Theme.radius.sm : 0
        color: shape.inputLike ? Tk.Theme.color.inputBg
             : shape.type === "commandButton" ? Tk.Theme.color.controlBg
             : shape.type === "rectangle" || shape.type === "label" ? "transparent" : Tk.Theme.color.panelAlt
        border.width: Tk.Theme.size.border
        border.color: shape.type === "label" ? Tk.Theme.color.divider
                    : shape.type === "rectangle" ? Tk.Theme.color.borderStrong : Tk.Theme.color.inputBorder
    }
    // A line from corner to corner (slant "down": top-left to bottom-right).
    Rectangle {
        objectName: "formShapeLine"
        visible: shape.type === "line"
        readonly property bool up: shape.control.slant === "up"
        x: 0
        y: up ? shape.height : 0
        width: Math.max(Tk.Theme.size.border, Math.sqrt(shape.width * shape.width + shape.height * shape.height))
        height: Tk.Theme.size.border
        transformOrigin: Item.TopLeft
        rotation: Math.atan2(up ? -shape.height : shape.height, Math.max(0.0001, shape.width)) * 180 / Math.PI
        color: Tk.Theme.color.borderStrong
    }
    Tk.Label {
        objectName: "formShapeCaption"
        visible: shape.type === "label" || shape.type === "commandButton"
        anchors.fill: parent
        anchors.leftMargin: Tk.Theme.space.xs
        anchors.rightMargin: Tk.Theme.space.xs
        text: String(shape.control.caption || "")
        horizontalAlignment: shape.type === "commandButton" ? Text.AlignHCenter : Text.AlignLeft
        font.pixelSize: Tk.Theme.font.small
    }
    // Bound or calculated text, combo and list boxes.
    Tk.Label {
        objectName: "formShapeBinding"
        visible: shape.inputLike && !shape.live
        x: Tk.Theme.space.xs
        width: Math.max(0, parent.width - x * 2 - (shape.type === "comboBox" ? Tk.Theme.size.iconSm : 0))
        height: Math.min(parent.height, Tk.Theme.size.control)
        text: shape.boundText
        mono: !!shape.control.expression
        muted: !shape.control.fieldName && !shape.control.expression
        font.pixelSize: Tk.Theme.font.small
    }
    Tk.Icon {
        visible: shape.type === "comboBox" && !shape.live
        anchors.right: parent.right
        anchors.rightMargin: Tk.Theme.space.xs
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        size: Tk.Theme.size.iconSm
        color: Tk.Theme.color.textMuted
    }
    Column {
        visible: shape.type === "listBox" && !shape.live
        x: Tk.Theme.space.xs
        y: Tk.Theme.size.control
        width: Math.max(0, parent.width - x * 2)
        height: Math.max(0, parent.height - y)
        clip: true
        Repeater {
            model: shape.items.slice(0, 8)
            delegate: Tk.Caption { required property var modelData; text: String(modelData) }
        }
    }
    // Check box: the box, then the caption.
    Rectangle {
        visible: shape.type === "checkBox"
        x: Tk.Theme.space.xs
        anchors.verticalCenter: parent.verticalCenter
        width: Tk.Theme.size.iconSm
        height: width
        radius: Tk.Theme.radius.xs
        color: Tk.Theme.color.inputBg
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.inputBorder
    }
    Tk.Label {
        visible: shape.type === "checkBox"
        x: Tk.Theme.space.xs * 2 + Tk.Theme.size.iconSm
        width: Math.max(0, parent.width - x)
        height: parent.height
        text: String(shape.control.caption || "") + (shape.control.fieldName ? "  [" + shape.control.fieldName + "]" : "")
        font.pixelSize: Tk.Theme.font.small
    }
    // Subform: its child form's name.
    Tk.Flex {
        visible: shape.type === "subform"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.sm
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Overline { title: qsTr("Subform") }
        Tk.Label { objectName: "formShapeSubform"; text: String(shape.control.formName || qsTr("No form")); font.pixelSize: Tk.Theme.font.small }
        Tk.Caption {
            visible: text.length > 0
            text: shape.control.linkMasterFields ? qsTr("Linked by %1").arg(shape.control.linkMasterFields) : ""
        }
    }
    // Tab control: its page tabs over the page frame.
    Row {
        objectName: "formShapePages"
        visible: shape.type === "tabControl"
        x: Tk.Theme.space.xs
        spacing: Tk.Theme.size.border
        Repeater {
            model: shape.pages
            delegate: Rectangle {
                id: pageTab
                required property var modelData
                required property int index
                width: pageLabel.implicitWidth + Tk.Theme.space.md * 2
                height: Tk.Theme.size.tab
                color: index === shape.currentPage ? Tk.Theme.color.panel : Tk.Theme.color.headerBg
                border.width: Tk.Theme.size.border
                border.color: Tk.Theme.color.divider
                Tk.Caption { id: pageLabel; anchors.centerIn: parent; text: String(pageTab.modelData) }
            }
        }
    }
    Rectangle {
        visible: shape.type === "tabControl"
        y: Tk.Theme.size.tab
        width: parent.width
        height: Math.max(0, parent.height - y)
        color: "transparent"
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.divider
    }
    // Image: the picture when it is local data, else its source.
    Image {
        id: picture
        objectName: "formShapeImage"
        visible: shape.type === "image" && status === Image.Ready
        anchors.fill: parent
        anchors.margins: Tk.Theme.size.border
        source: shape.type === "image" && FormControls.drawableImage(shape.control.source) ? String(shape.control.source) : ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }
    Tk.Flex {
        visible: shape.type === "image" && picture.status !== Image.Ready
        anchors.centerIn: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        Tk.Icon { name: "image"; size: Tk.Theme.size.iconLg; color: Tk.Theme.color.textMuted }
        Tk.Caption { text: String(shape.control.source || qsTr("No picture")); elide: Text.ElideMiddle; Tk.Flex.maxWidth: shape.width }
    }
}
