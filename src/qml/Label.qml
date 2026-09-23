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
        ? Tk.Theme.alpha(_resolvedTextColor, Tk.Theme.opacity.hidden)
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
    // remain the default layout contract. Instantiate the editor and metrics
    // only for an opted-in label; ordinary dense rows do not carry editors.
    Loader {
        anchors.fill: parent
        active: label.selectable
        sourceComponent: Component {
            TextEdit {
                objectName: "selectableText"
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
                activeFocusOnTab: true

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

                // Select/copy the displayed string when no-wrap text elides.
                TextMetrics {
                    id: metrics
                    font: label.font
                    text: label.text
                    elide: label.elide
                    elideWidth: label.width
                }
            }
        }
    }
}
