// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#rich-text-and-paginated-documents): the
// contextual text-format bar for a RichTextEditor, RichDocumentView or
// Tk.RichTextArea (`target`). Values come from the area's formatState
// (resolved, with mixed states); every action invokes one command ID, which
// is one document transaction and one undo step. Buttons never take focus
// and field commits return focus to the editor, so the selection survives.
Item {
    id: bar

    property Item target: null
    readonly property Tk.RichTextArea area: bar.target === null ? null
        : bar.target.area !== undefined ? bar.target.area : bar.target
    readonly property var formatState: area ? area.formatState : null
    property var fontProvider: fontCatalog
    property var colors: [
        { name: qsTr("Text"), color: Tk.Theme.color.text },
        { name: qsTr("Muted"), color: Tk.Theme.color.textMuted },
        { name: qsTr("Accent"), color: Tk.Theme.color.accent },
        { name: qsTr("Info"), color: Tk.Theme.color.info },
        { name: qsTr("Success"), color: Tk.Theme.color.success },
        { name: qsTr("Warning"), color: Tk.Theme.color.warning },
        { name: qsTr("Danger"), color: Tk.Theme.color.danger }
    ]
    property bool compact: false
    property string tooltip: qsTr("Formatting")
    readonly property bool editable: formatState !== null && formatState.editable

    implicitWidth: toolbar.implicitWidth
    implicitHeight: toolbar.implicitHeight
    Accessible.role: Accessible.ToolBar
    Accessible.name: bar.tooltip

    function invoke(commandId, args) {
        if (!bar.area) return false
        const ok = bar.area.invokeCommand(commandId, args === undefined ? {} : args)
        bar.area.forceActiveFocus()
        return ok
    }

    Tk.FontCatalog { id: fontCatalog }

    component Toggle: Tk.IconButton {
        id: toggle
        property string commandId: ""
        property int checkState: 0
        small: bar.compact
        enabled: bar.editable
        checked: checkState === Qt.Checked
        focusPolicy: Qt.NoFocus
        Accessible.checkable: true
        Accessible.checkStateMixed: checkState === Qt.PartiallyChecked
        onClicked: bar.invoke(toggle.commandId)
        Rectangle {
            objectName: "formatBarMixedMarker"
            visible: toggle.checkState === Qt.PartiallyChecked
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Tk.Theme.space.xs / 2
            width: parent.width / 2
            height: Tk.Theme.size.border * 2
            radius: height / 2
            color: Tk.Theme.color.accent
        }
    }

    component Action: Tk.IconButton {
        id: action
        property string commandId: ""
        small: bar.compact
        enabled: bar.editable
        focusPolicy: Qt.NoFocus
        onClicked: bar.invoke(action.commandId)
    }

    Tk.ToolBar {
        id: toolbar
        anchors.fill: parent
        compact: bar.compact

        Tk.ComboBox {
            id: family
            objectName: "formatBarFontFamily"
            small: bar.compact
            enabled: bar.editable
            editable: true
            implicitWidth: Tk.Theme.size.fieldWidth + Tk.Theme.size.control
            model: bar.fontProvider ? bar.fontProvider.families : []
            textRole: "family"
            placeholderText: bar.formatState && bar.formatState.fontFamilyMixed ? qsTr("Mixed") : ""
            tooltip: bar.formatState && bar.formatState.fontMissing
                     ? qsTr("%1 is not installed; text uses a fallback font").arg(bar.formatState.fontFamily)
                     : qsTr("Font")
            // Typing breaks a binding, so the shown family is pushed on every
            // selection change instead (unless the user is editing it).
            function sync() {
                if (family.activeFocus) return
                family.editText = bar.formatState && !bar.formatState.fontFamilyMixed ? bar.formatState.fontFamily : ""
            }
            Component.onCompleted: sync()
            onActiveFocusChanged: sync()
            onActivated: index => bar.invoke("text.fontFamily", { value: family.textAt(index) })
            onAccepted: bar.invoke("text.fontFamily", { value: family.editText })
            Connections {
                target: bar.formatState
                function onChanged() { family.sync() }
            }
        }
        Tk.Icon {
            objectName: "formatBarFontMissing"
            visible: bar.formatState !== null && bar.formatState.fontMissing
            name: "triangle-alert"
            color: Tk.Theme.color.warning
            size: Tk.Theme.size.icon
            Accessible.role: Accessible.Indicator
            Accessible.name: qsTr("Font not installed")
        }
        Tk.NumberField {
            id: size
            objectName: "formatBarFontSize"
            small: bar.compact
            enabled: bar.editable
            implicitWidth: Tk.Theme.size.fieldWidth * 2 / 3
            value: bar.formatState ? bar.formatState.fontSize : 0
            mixed: bar.formatState !== null && bar.formatState.fontSizeMixed
            from: 1
            to: 1638
            stepSize: 1
            decimals: 1
            suffix: qsTr(" pt")
            tooltip: qsTr("Font size")
            onValueModified: value => bar.invoke("text.fontSize", { value: value })
        }
        Tk.ToolSeparator {}
        Toggle { objectName: "formatBarBold"; iconName: "bold"; commandId: "text.bold"; tooltip: qsTr("Bold"); checkState: bar.formatState ? bar.formatState.bold : 0 }
        Toggle { objectName: "formatBarItalic"; iconName: "italic"; commandId: "text.italic"; tooltip: qsTr("Italic"); checkState: bar.formatState ? bar.formatState.italic : 0 }
        Toggle { objectName: "formatBarUnderline"; iconName: "underline"; commandId: "text.underline"; tooltip: qsTr("Underline"); checkState: bar.formatState ? bar.formatState.underline : 0 }
        Toggle { objectName: "formatBarStrike"; iconName: "strikethrough"; commandId: "text.strike"; tooltip: qsTr("Strikethrough"); checkState: bar.formatState ? bar.formatState.strike : 0 }
        Toggle { objectName: "formatBarSuperscript"; iconName: "superscript"; commandId: "text.superscript"; tooltip: qsTr("Superscript"); checkState: bar.formatState ? bar.formatState.superscript : 0 }
        Toggle { objectName: "formatBarSubscript"; iconName: "subscript"; commandId: "text.subscript"; tooltip: qsTr("Subscript"); checkState: bar.formatState ? bar.formatState.subscript : 0 }
        Tk.ColorButton {
            objectName: "formatBarColor"
            small: bar.compact
            enabled: bar.editable
            focusPolicy: Qt.NoFocus
            colors: bar.colors
            color: bar.formatState && bar.formatState.color.a > 0 ? bar.formatState.color : Tk.Theme.color.text
            tooltip: bar.formatState && bar.formatState.colorMixed ? qsTr("Text colour (mixed)") : qsTr("Text colour")
            onColorSelected: color => bar.invoke("text.color", { value: color.toString() })
        }
        Action { objectName: "formatBarClear"; iconName: "eraser"; commandId: "text.clearFormatting"; tooltip: qsTr("Clear formatting") }
        Tk.ToolSeparator {}
        Tk.Segmented {
            id: alignment
            objectName: "formatBarAlignment"
            small: true
            enabled: bar.editable
            tooltip: qsTr("Alignment")
            model: [
                { iconName: "text-align-start", value: "start", tooltip: qsTr("Align start") },
                { iconName: "text-align-center", value: "center", tooltip: qsTr("Centre") },
                { iconName: "text-align-end", value: "end", tooltip: qsTr("Align end") },
                { iconName: "text-align-justify", value: "justify", tooltip: qsTr("Justify") }
            ]
            currentIndex: {
                if (!bar.formatState || bar.formatState.alignmentMixed) return -1
                return ["start", "center", "end", "justify"].indexOf(bar.formatState.alignment)
            }
            onActivated: index => bar.invoke("paragraph.align", { value: ["start", "center", "end", "justify"][index] })
        }
        Tk.ComboBox {
            id: heading
            objectName: "formatBarHeading"
            small: bar.compact
            enabled: bar.editable
            implicitWidth: Tk.Theme.size.fieldWidth
            model: [qsTr("Body text"), qsTr("Heading 1"), qsTr("Heading 2"), qsTr("Heading 3"),
                    qsTr("Heading 4"), qsTr("Heading 5"), qsTr("Heading 6")]
            placeholderText: qsTr("Mixed")
            tooltip: qsTr("Paragraph style")
            currentIndex: bar.formatState && !bar.formatState.headingMixed ? bar.formatState.heading : -1
            onActivated: index => bar.invoke("paragraph.heading", { value: index })
        }
        Toggle {
            objectName: "formatBarBulletList"; iconName: "list"; commandId: "paragraph.bulletList"; tooltip: qsTr("Bulleted list")
            checkState: !bar.formatState ? 0 : bar.formatState.list === "bullet" ? Qt.Checked : bar.formatState.list === "mixed" ? Qt.PartiallyChecked : Qt.Unchecked
        }
        Toggle {
            objectName: "formatBarNumberedList"; iconName: "list-ordered"; commandId: "paragraph.numberedList"; tooltip: qsTr("Numbered list")
            checkState: !bar.formatState ? 0 : bar.formatState.list === "number" ? Qt.Checked : bar.formatState.list === "mixed" ? Qt.PartiallyChecked : Qt.Unchecked
        }
        Action {
            objectName: "formatBarOutdent"; iconName: "list-indent-decrease"; commandId: "paragraph.outdent"; tooltip: qsTr("Decrease list level")
            enabled: bar.editable && bar.formatState.list !== "none"
        }
        Action {
            objectName: "formatBarIndent"; iconName: "list-indent-increase"; commandId: "paragraph.indent"; tooltip: qsTr("Increase list level")
            enabled: bar.editable && bar.formatState.list !== "none"
        }
        Tk.ToolSeparator {}
        Tk.IconButton {
            id: linkButton
            objectName: "formatBarLink"
            small: bar.compact
            enabled: bar.editable
            focusPolicy: Qt.NoFocus
            iconName: "link"
            checked: bar.formatState !== null && bar.formatState.link.length > 0
            tooltip: qsTr("Link")
            onClicked: {
                linkField.text = bar.formatState ? bar.formatState.link : ""
                linkPopover.open()
            }
        }
        Action { objectName: "formatBarUndo"; iconName: "undo-2"; commandId: "edit.undo"; tooltip: qsTr("Undo"); enabled: bar.area !== null && bar.area.document !== null && bar.area.document.canUndo }
        Action { objectName: "formatBarRedo"; iconName: "redo-2"; commandId: "edit.redo"; tooltip: qsTr("Redo"); enabled: bar.area !== null && bar.area.document !== null && bar.area.document.canRedo }
    }

    Tk.Popover {
        id: linkPopover
        objectName: "formatBarLinkPopover"
        anchorItem: linkButton
        placement: "bottom"
        onOpened: linkField.forceActiveFocus()
        onClosed: if (bar.area) bar.area.forceActiveFocus()
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.TextField {
                id: linkField
                objectName: "formatBarLinkField"
                implicitWidth: Tk.Theme.size.fieldWidth * 2
                placeholderText: qsTr("https://")
                tooltip: qsTr("Link address")
                onAccepted: { bar.invoke("text.link", { href: linkField.text }); linkPopover.close() }
            }
            Tk.Button {
                objectName: "formatBarLinkApply"
                text: qsTr("Apply")
                onClicked: { bar.invoke("text.link", { href: linkField.text }); linkPopover.close() }
            }
            Tk.IconButton {
                objectName: "formatBarLinkRemove"
                iconName: "unlink"
                tooltip: qsTr("Remove link")
                onClicked: { bar.invoke("text.link", { href: "" }); linkPopover.close() }
            }
        }
    }
}
