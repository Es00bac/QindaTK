// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 760
    height: 650
    color: Tk.Theme.color.bg

    Tk.FontCatalog { id: catalog }
    readonly property var preferred: catalog.resolveFamily("Newsreader")
    readonly property var selectedFace: preferred.installed ? preferred : catalog.families[0]
    readonly property string selectedStyle: selectedFace && selectedFace.styles ? selectedFace.styles[0] : ""

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg

        Tk.Overline { title: qsTr("TYPOGRAPHY"); wide: true; color: Tk.Theme.color.accent }
        Tk.Caption { text: qsTr("Installed font metadata and a specimen rendered in the selected face") }
        Tk.FontPicker {
            objectName: "previewFontPicker"
            width: Tk.Theme.size.fieldWidth * 3
            fontProvider: catalog
            family: selectedFace ? selectedFace.family : ""
            style: selectedStyle
            specimenText: "Aa Bb Cc 0123 fi ffi"
        }
        Tk.FontFeaturesEditor {
            objectName: "previewFontFeatures"
            width: Tk.Theme.size.fieldWidth * 3
            fontProvider: catalog
            fontFamily: selectedFace ? selectedFace.family : ""
            fontStyle: selectedStyle
            tooltip: qsTr("OpenType features and variable axes")
        }
    }
}
