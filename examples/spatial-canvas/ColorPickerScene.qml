// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: scene
    width: 900
    height: 600
    color: Tk.Theme.color.bg
    Tk.ColorPickerModel {
        id: colorModel
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        align: Tk.Flex.Start
        gap: Tk.Theme.space.md

        Tk.Overline { title: qsTr("COLOR PICKER PRESENTATION"); objectName: "colorPickerSceneTitle" }
        Tk.Caption {
            text: qsTr("Typed document color. Picker previews convert through the active document profile.")
            wrapMode: Text.WordWrap
        }
        Item {
            objectName: "colorPickerSceneFrame"
            implicitWidth: Tk.Theme.size.panelMinWidth
            implicitHeight: implicitWidth + Tk.Theme.size.controlSm * 2 + Tk.Theme.space.sm * 2
            Tk.ColorPicker {
                objectName: "colorPickerSceneControl"
                anchors.fill: parent
                color: colorModel.previewColor
                tooltip: qsTr("Choose a display-referred sRGB preview colour")
                onPreviewRequested: function(next) { colorModel.previewSrgb(next) }
                onEditFinished: function(next) { colorModel.commitEdit() }
                onEditCanceled: colorModel.cancelEdit()
            }
        }
        Tk.Caption {
            objectName: "colorPickerSceneStatus"
            text: qsTr("Document RGB %1 · alpha %2 · profile %3")
                  .arg(Number(colorModel.typedValue.red).toFixed(3))
                  .arg(Number(colorModel.typedValue.alpha).toFixed(3))
                  .arg(String(colorModel.typedValue.profileId).slice(0, 24))
            mono: true
        }
    }
}
