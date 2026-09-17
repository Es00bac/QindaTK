// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a Tk.Dialog that asks for one line
// of text (a name, a title). `label` captions the field, `text` is the
// value (preset it before open()), Enter accepts, Escape rejects; with
// `acceptEmpty: false` (the default) an empty or blank value greys OK and
// blocks Enter. The field takes focus and selects its text on open.
// objectName "promptField" on the field.
Tk.Dialog {
    id: dialog

    property string label: ""
    property alias text: field.text
    property alias placeholderText: field.placeholderText
    property bool acceptEmpty: false
    readonly property bool acceptable: acceptEmpty || field.text.trim().length > 0

    dialogWidth: 380
    primaryText: qsTr("OK")
    secondaryText: qsTr("Cancel")
    primaryEnabled: acceptable

    onOpened: {
        field.forceActiveFocus(Qt.TabFocusReason)
        field.selectAll()
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.Caption {
            objectName: "promptLabel"
            visible: dialog.label.length > 0
            text: dialog.label
        }
        // AGENT-NOTE: no onAccepted here. Return in the field bubbles up to
        // the dialog body's Keys handler (gated by primaryEnabled); a second
        // accept() from the field made Enter fire `accepted` twice.
        Tk.TextField {
            id: field
            objectName: "promptField"
            Tk.Flex.grow: 1
        }
    }
}
