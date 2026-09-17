// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A 22px readout row: caption key on the left, value right-aligned in
// mono (the original's "Sequence Info" block). `accent` lights the value.
Item {
    id: kv

    property string key: ""
    property string value: ""
    property bool mono: true
    property bool accent: false

    implicitWidth: row.implicitWidth
    implicitHeight: Tk.Theme.size.row

    Accessible.role: Accessible.StaticText
    Accessible.name: kv.key + ": " + kv.value

    Tk.Flex {
        id: row
        anchors.fill: parent
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm

        Tk.Caption {
            objectName: "keyLabel"
            text: kv.key
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }
        Tk.Label {
            objectName: "valueLabel"
            text: kv.value
            mono: kv.mono
            accent: kv.accent
            font.pixelSize: Tk.Theme.font.small
            horizontalAlignment: Text.AlignRight
            Tk.Flex.shrink: 0
        }
    }
}
