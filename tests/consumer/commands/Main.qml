// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 320
    height: 48

    property alias bindingEnabled: saveBinding.enabled
    property alias bindingReason: saveBinding.reason
    property alias bindingShortcut: saveBinding.shortcut
    property alias commandButtonEnabled: saveButton.enabled
    property alias commandButtonReason: saveButton.disabledReason
    property alias visualButton: saveButton.button

    Tk.CommandBinding {
        id: saveBinding
        registry: appCommandRegistry
        shortcutMap: appShortcutMap
        commandId: "document.save"
    }

    Tk.CommandButton {
        id: saveButton
        registry: appCommandRegistry
        shortcutMap: appShortcutMap
        commandId: "document.save"
    }
}
