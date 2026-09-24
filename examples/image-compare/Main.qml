// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 1100
    height: 760
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Heading { level: 2; text: qsTr("Before / after image comparison") }
        Tk.Notice { title: qsTr("Synchronized source pixels"); text: qsTr("Split, blink and encoded-sRGB difference share one zoom and pan transform. The artwork is a fixture; hosts provide images.") }
        Tk.CompareView {
            objectName: "compareFixture"
            width: parent.width
            Tk.Flex.grow: 1
            beforeSource: Qt.resolvedUrl("before.svg")
            afterSource: Qt.resolvedUrl("after.svg")
        }
    }
}
