// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: scene
    width: 900
    height: 600
    color: Tk.Theme.color.bg
    property color hostPreview: "#ff4b9a"
    property color committedColor: hostPreview

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        align: Tk.Flex.Start
        gap: Tk.Theme.space.md

        Tk.Overline { title: qsTr("COLOR PICKER PRESENTATION"); objectName: "colorPickerSceneTitle" }
        Tk.Caption {
            text: qsTr("Host-owned sRGB preview. Profile conversion and undo belong to the host.")
            wrapMode: Text.WordWrap
        }
        Item {
            objectName: "colorPickerSceneFrame"
            implicitWidth: Tk.Theme.size.panelMinWidth
            implicitHeight: implicitWidth + Tk.Theme.size.controlSm * 2 + Tk.Theme.space.sm * 2
            Tk.ColorPicker {
                objectName: "colorPickerSceneControl"
                anchors.fill: parent
                color: scene.hostPreview
                tooltip: qsTr("Choose a display-referred sRGB preview colour")
                onPreviewRequested: function(next) { scene.hostPreview = next }
                onEditFinished: function(next) { scene.committedColor = next }
                onEditCanceled: function(restored) { scene.hostPreview = restored }
            }
        }
        Tk.Caption {
            objectName: "colorPickerSceneStatus"
            text: qsTr("Host preview %1 · committed %2")
                  .arg(scene.hostPreview).arg(scene.committedColor)
            mono: true
        }
    }
}
