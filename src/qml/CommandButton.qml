// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Toolbar friendly button backed by one semantic command. The contained
// Button remains the sole pointer, keyboard and accessibility activation
// surface; all accepted activations call CommandBinding.trigger() once.
Item {
    id: control

    property Tk.CommandRegistry registry: null
    property Tk.ShortcutMap shortcutMap: null
    property string commandId: ""
    property bool iconOnly: false
    property bool small: false
    property string tooltip: ""
    readonly property alias button: button
    readonly property string disabledReason: binding.reason

    signal triggered()

    enabled: binding.enabled
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight

    Tk.CommandBinding {
        id: binding
        registry: control.registry
        shortcutMap: control.shortcutMap
        commandId: control.commandId
    }

    Tk.Button {
        id: button
        objectName: "commandButtonAction"
        anchors.fill: parent
        text: control.iconOnly ? "" : binding.label
        iconName: binding.iconName
        small: control.small
        tooltip: control.tooltip.length > 0 ? control.tooltip
                 : binding.reason.length > 0 ? binding.reason : binding.label
        enabled: binding.enabled
        checkable: binding.checkable
        checked: binding.checked
        Accessible.name: binding.label.length > 0 ? binding.label : control.commandId
        Accessible.description: binding.reason
        onClicked: {
            if (binding.trigger())
                control.triggered()
        }
    }
}
