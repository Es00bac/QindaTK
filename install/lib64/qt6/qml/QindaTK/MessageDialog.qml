// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a Tk.Dialog for a question or a
// warning: a status icon, wrapping body `text`, and up to three buttons —
// `primaryText` (accepted), `secondaryText` (rejected), `tertiaryText`
// (tertiary), e.g. Save / Cancel / Discard. `variant` picks the icon and
// its colour: "info", "question", "warning", "danger"; `destructive`
// paints the primary in the danger variant.
Tk.Dialog {
    id: dialog

    property string text: ""
    property string variant: "info"
    property string iconName: variant === "warning" ? "alert-triangle"
                            : variant === "danger" ? "alert-circle"
                            : variant === "question" ? "circle-help" : "info"
    readonly property color iconColor: variant === "warning" ? Tk.Theme.color.warning
                                     : variant === "danger" ? Tk.Theme.color.danger
                                     : Tk.Theme.color.accentText

    dialogWidth: 460
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")

    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Start
        gap: Tk.Theme.space.md

        Tk.Icon {
            objectName: "messageIcon"
            name: dialog.iconName
            size: Tk.Theme.size.iconXl
            color: dialog.iconColor
            Tk.Flex.shrink: 0
        }
        Tk.Label {
            objectName: "messageText"
            text: dialog.text
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minWidth: 0
        }
    }
}
