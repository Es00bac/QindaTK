// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Body text in the theme's face and colour. Elides when a layout narrows
// it; set `wrapMode` to wrap instead. `muted`/`disabled`/`accent` switch
// the colour role so call sites never name a colour.
Text {
    id: label

    property bool muted: false
    property bool disabled: false
    property bool accent: false
    property bool mono: false
    property bool selectable: false

    color: disabled ? Tk.Theme.color.textDisabled
         : accent ? Tk.Theme.color.accentText
         : muted ? Tk.Theme.color.textMuted
         : Tk.Theme.color.text
    font.family: mono ? Tk.Theme.font.monoFamily : Tk.Theme.font.family
    font.pixelSize: Tk.Theme.font.body
    elide: Text.ElideRight
    verticalAlignment: Text.AlignVCenter
    renderType: Text.NativeRendering
    textFormat: Text.PlainText

    Accessible.role: Accessible.StaticText
    Accessible.name: text
    Accessible.ignored: selectable

    // Keep Text as the public base so its implicit size, wrapping and elision
    // remain the default layout contract. This read-only editor overlays the
    // same text only when the opt-in selection path is enabled.
    TextEdit {
        objectName: "selectableText"
        anchors.fill: parent
        visible: label.selectable
        text: label.text
        color: label.color
        font: label.font
        horizontalAlignment: label.horizontalAlignment
        verticalAlignment: label.verticalAlignment
        wrapMode: label.wrapMode
        textFormat: label.textFormat
        renderType: label.renderType
        selectionColor: Tk.Theme.color.selection
        selectedTextColor: label.color
        readOnly: true
        selectByMouse: true
        persistentSelection: true
        focus: label.selectable
        activeFocusOnTab: label.selectable

        Accessible.role: Accessible.StaticText
        Accessible.name: label.text

        Keys.onPressed: function(event) {
            if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_A) {
                selectAll()
                event.accepted = true
            } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C) {
                copy()
                event.accepted = true
            }
        }
    }
}
