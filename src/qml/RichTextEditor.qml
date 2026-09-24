// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#rich-text-and-paginated-documents): an
// inline rich-text field (notes, comments, captions, mail) over a
// Tk.RichTextDocument: one continuous flow wrapped to the field's width. It
// grows with its content from `rows` lines up to `maximumHeight`, then
// scrolls. The same document can also be open in a RichDocumentView; each
// view keeps its own layout and selection. Editing happens on the inner
// Tk.RichTextArea (objectName "richTextEditorArea").
T.Control {
    id: control

    property Tk.RichTextDocument document: null
    property Tk.AssetProvider assetProvider: null
    property bool readOnly: false
    property bool error: false
    property int rows: 3
    property real maximumHeight: Number.POSITIVE_INFINITY
    property string placeholderText: ""
    property string tooltip: ""
    readonly property alias area: area
    readonly property alias formatState: area.formatState
    readonly property real lineHeight: metrics.height

    signal linkActivated(string href)

    implicitWidth: Tk.Theme.size.fieldWidth * 2 + leftPadding + rightPadding
    implicitHeight: Math.min(maximumHeight,
                             Math.max(metrics.height * Math.max(1, rows), area.contentHeight - 2 * area.padding)
                             + topPadding + bottomPadding)
    padding: Tk.Theme.space.sm
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled
    onActiveFocusChanged: if (activeFocus) area.forceActiveFocus()

    function invokeCommand(commandId, args) { return area.invokeCommand(commandId, args === undefined ? {} : args) }

    // One line of the document's default font (points -> 96-dpi pixels).
    FontMetrics {
        id: metrics
        font.family: control.document ? control.document.defaultFontFamily : Tk.Theme.font.family
        font.pixelSize: control.document ? Math.round(control.document.defaultFontSize * 96 / 72) : Tk.Theme.font.body
    }

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered && !area.activeFocus
        delay: Tk.Theme.motion.tooltipDelay
    }

    background: Rectangle {
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.color.inputBg
        border.width: Tk.Theme.size.border
        border.color: control.error ? Tk.Theme.color.danger
                    : area.activeFocus ? Tk.Theme.color.inputFocusBorder
                    : control.hovered ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.inputBorder
        Behavior on border.color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        Rectangle {
            objectName: "richTextEditorFocusRing"
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: control.error ? Tk.Theme.alpha(Tk.Theme.color.danger, 0.4) : Tk.Theme.color.focus
            visible: area.activeFocus
        }
    }

    contentItem: Item {
        Tk.RichTextArea {
            id: area
            objectName: "richTextEditorArea"
            anchors.fill: parent
            focus: true
            paged: false
            padding: 0
            document: control.document
            assetProvider: control.assetProvider
            readOnly: control.readOnly
            inkColor: control.enabled ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
            pageColor: Tk.Theme.color.inputBg
            linkColor: Tk.Theme.color.accentText
            selectionColor: Tk.Theme.color.selection
            selectedTextColor: Tk.Theme.color.text
            caretColor: Tk.Theme.color.text
            borderColor: Tk.Theme.color.borderStrong
            objectFillColor: Tk.Theme.color.panelAlt
            objectTextColor: Tk.Theme.color.textMuted
            markerColor: Tk.Theme.color.textMuted
            insertionColor: Tk.Theme.color.success
            deletionColor: Tk.Theme.color.danger
            commentColor: Tk.Theme.color.accentSubtle
            caretWidth: Tk.Theme.size.border
            onLinkActivated: href => control.linkActivated(href)

            Accessible.role: Accessible.EditableText
            Accessible.name: control.tooltip.length > 0 ? control.tooltip : control.placeholderText
            Accessible.description: control.error ? qsTr("Error") : control.readOnly ? qsTr("Read only") : ""
            Accessible.multiLine: true
            Accessible.editable: !control.readOnly
            Accessible.focusable: true

            Repeater {
                model: area.visiblePages
                delegate: Item {
                    id: slice
                    required property int pageIndex
                    required property rect tileRect
                    required property rect sourceRect
                    x: tileRect.x
                    y: tileRect.y
                    width: tileRect.width
                    height: tileRect.height
                    Accessible.ignored: true
                    Tk.RichTextTile {
                        objectName: "richTextEditorTile"
                        anchors.fill: parent
                        area: area
                        paper: false
                        pageIndex: slice.pageIndex
                        sourceRect: slice.sourceRect
                    }
                }
            }
        }

        Tk.Label {
            objectName: "richTextEditorPlaceholder"
            visible: control.placeholderText.length > 0 && control.document !== null
                     && control.document.characterCount === 0 && area.preeditText.length === 0
            width: parent.width
            text: control.placeholderText
            color: Tk.Theme.color.textMuted
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }

        Tk.ScrollBar {
            objectName: "richTextEditorScrollBar"
            orientation: Qt.Vertical
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            size: area.height / Math.max(1, area.contentHeight)
            position: area.contentY / Math.max(1, area.contentHeight)
            onPositionChanged: if (pressed) area.contentY = position * area.contentHeight
        }
    }
}
