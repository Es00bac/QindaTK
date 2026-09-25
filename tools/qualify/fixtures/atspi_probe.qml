// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-088 AT-SPI fixture (tools/qualify/atspi_probe.py). The probe finds the
// button and the check box by accessible name over the desktop AT-SPI bus,
// performs their Press/Toggle actions and waits for the names below to change.
import QtQuick
import QindaTK as Tk

Item {
    id: root
    objectName: "atspiProbe"
    width: 360
    height: 160
    property int presses: 0

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Button {
            objectName: "atspiButton"
            text: root.presses === 0 ? "Qualify press" : "Pressed " + root.presses
            tooltip: "Counts assistive presses"
            onClicked: root.presses += 1
        }

        Tk.CheckBox {
            objectName: "atspiCheck"
            tooltip: "Toggles on an assistive press"
            text: checked ? "Qualify toggle on" : "Qualify toggle off"
        }
    }
}
