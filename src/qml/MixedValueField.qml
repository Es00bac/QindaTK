// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Shared state and explicit transaction helpers for typed editors. Merely
// focusing the child never changes a mixed value; callers commit deliberately.
T.Control {
    id: field
    default property alias content: editorHost.data
    property Tk.PropertyBinding binding: null
    property string tooltip: ""
    property string label: ""
    property string helpText: ""
    property string mixedText: qsTr("Multiple values")
    property bool showReset: true
    readonly property bool mixed: binding ? binding.mixed : false
    readonly property bool unset: binding ? binding.value === undefined : true
    readonly property bool readOnly: !binding || !binding.writable
    readonly property string errorText: binding ? (binding.validationError || binding.availabilityError) : ""

    function beginEdit() { return binding ? binding.begin() : false }
    function updateEdit(value) { return binding ? binding.update(value) : false }
    function commitEdit() { return binding ? binding.commit() : false }
    function cancelEdit() { if (binding) binding.cancel() }
    function reset() { return binding ? binding.reset() : false }

    implicitWidth: editorHost.childrenRect.width + Tk.Theme.space.sm
    implicitHeight: column.implicitHeight
    enabled: !readOnly
    hoverEnabled: true
    Accessible.role: Accessible.Grouping
    Accessible.name: label || tooltip
    Accessible.description: errorText || (mixed ? mixedText : helpText)
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Tk.ToolTip { text: field.tooltip; visible: field.tooltip.length > 0 && field.hovered }

    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Item {
            id: editorHost
            objectName: "mixedValueEditor"
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Caption {
                objectName: "mixedValueState"
                visible: field.mixed || field.unset || field.errorText.length > 0
                text: field.errorText.length > 0 ? field.errorText
                     : field.mixed ? field.mixedText : field.unset ? qsTr("No value") : ""
                color: field.errorText.length > 0 ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                wrapMode: Text.Wrap
                Tk.Flex.grow: 1
            }
            Tk.Button {
                objectName: "mixedValueReset"
                visible: field.showReset && field.binding && field.binding.writable
                enabled: visible
                text: qsTr("Reset")
                small: true
                tooltip: qsTr("Reset %1 to its default value").arg(field.label)
                onClicked: field.reset()
            }
        }
    }
}
