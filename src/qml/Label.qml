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
    readonly property color _resolvedTextColor: disabled ? Tk.Theme.color.textDisabled
        : accent ? Tk.Theme.color.accentText
        : muted ? Tk.Theme.color.textMuted
        : Tk.Theme.color.text

    // The TextEdit paints the glyphs in selectable mode. Keep Text's own
    // color role and all Text geometry, but suppress its duplicate glyphs.
    color: selectable
        ? Qt.rgba(_resolvedTextColor.r, _resolvedTextColor.g, _resolvedTextColor.b, 0)
        : _resolvedTextColor
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
        text: label.wrapMode === Text.NoWrap ? metrics.elidedText : label.text
        color: label._resolvedTextColor
        font: label.font
        horizontalAlignment: label.horizontalAlignment
        verticalAlignment: label.verticalAlignment
        wrapMode: label.wrapMode
        textFormat: label.textFormat
        renderType: label.renderType
        selectionColor: Tk.Theme.color.selection
        selectedTextColor: label._resolvedTextColor
        readOnly: true
        selectByMouse: true
        persistentSelection: true
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

    // Match Text's right-elision at constrained widths; selection and copy
    // then operate on the displayed string, including its ellipsis.
    TextMetrics {
        id: metrics
        font: label.font
        text: label.text
        elide: label.elide
        elideWidth: label.width
    }
}
