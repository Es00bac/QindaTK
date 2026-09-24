// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#rich-text-and-paginated-documents): a
// paginated editing surface over a Tk.RichTextDocument. Only visible pages
// exist as delegates (area.visiblePages); each paints through
// Tk.RichTextTile from the area's own layout. Pages use theme surfaces; text
// without an explicit document colour uses Theme text. Editing, IME, caret
// and accessibility live on the inner Tk.RichTextArea (objectName
// "richDocumentArea"), which takes focus when the view does.
T.Control {
    id: control

    property Tk.RichTextDocument document: null
    property Tk.AssetProvider assetProvider: null
    property bool readOnly: false
    property string tooltip: ""
    property alias zoom: area.zoom
    readonly property alias area: area
    readonly property alias formatState: area.formatState
    readonly property int currentPage: area.currentPage
    readonly property int pageCount: area.pageCount
    readonly property bool paginationComplete: area.paginationComplete

    signal linkActivated(string href)

    implicitWidth: area.pageSize.width * area.zoom + 2 * area.padding + Tk.Theme.size.scrollbar
    implicitHeight: area.pageSize.height * area.zoom + 2 * area.padding
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    onActiveFocusChanged: if (activeFocus) area.forceActiveFocus()

    function goToPage(page) { return area.goToPage(page) }
    function invokeCommand(commandId, args) { return area.invokeCommand(commandId, args === undefined ? {} : args) }

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered && !area.activeFocus
        delay: Tk.Theme.motion.tooltipDelay
    }

    background: Rectangle {
        color: Tk.Theme.color.canvas
        border.width: Tk.Theme.size.border
        border.color: area.activeFocus ? Tk.Theme.color.inputFocusBorder : Tk.Theme.color.border
    }

    contentItem: Item {
        Tk.RichTextArea {
            id: area
            objectName: "richDocumentArea"
            anchors.fill: parent
            focus: true
            paged: true
            document: control.document
            assetProvider: control.assetProvider
            readOnly: control.readOnly
            padding: Tk.Theme.space.xl
            pageGap: Tk.Theme.space.lg
            inkColor: Tk.Theme.color.text
            pageColor: Tk.Theme.color.surface
            linkColor: Tk.Theme.color.accentText
            selectionColor: Tk.Theme.color.selection
            selectedTextColor: Tk.Theme.color.text
            caretColor: Tk.Theme.color.text
            borderColor: Tk.Theme.color.borderStrong
            objectFillColor: Tk.Theme.color.panelAlt
            objectTextColor: Tk.Theme.color.textMuted
            markerColor: Tk.Theme.color.textMuted
            frameGuideColor: Tk.Theme.color.accent
            oversetColor: Tk.Theme.color.danger
            caretWidth: Tk.Theme.size.border
            onLinkActivated: href => control.linkActivated(href)

            Accessible.role: Accessible.EditableText
            Accessible.name: control.tooltip.length > 0 ? control.tooltip : qsTr("Document")
            Accessible.description: area.caretContext.length > 0
                                    ? qsTr("Page %1 of %2, %3").arg(area.currentPage + 1).arg(area.pageCount).arg(area.caretContext)
                                    : qsTr("Page %1 of %2").arg(area.currentPage + 1).arg(area.pageCount)
            Accessible.multiLine: true
            Accessible.editable: !control.readOnly
            Accessible.focusable: true

            Repeater {
                model: area.visiblePages
                delegate: Item {
                    id: page
                    required property int pageIndex
                    required property string pageKey
                    required property string revision
                    required property rect tileRect
                    required property rect sourceRect
                    required property rect pageRect
                    objectName: "richDocumentPage_" + pageKey
                    x: pageRect.x
                    y: pageRect.y
                    width: pageRect.width
                    height: pageRect.height
                    Accessible.ignored: true

                    Rectangle {
                        x: Tk.Theme.space.xs
                        y: Tk.Theme.space.xs
                        width: parent.width
                        height: parent.height
                        color: Tk.Theme.color.shadow
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: Tk.Theme.color.surface
                        border.width: Tk.Theme.size.border
                        border.color: Tk.Theme.color.border
                    }
                    Tk.RichTextTile {
                        objectName: "richDocumentTile_" + page.pageKey
                        area: area
                        pageIndex: page.pageIndex
                        sourceRect: page.sourceRect
                        x: page.tileRect.x - page.pageRect.x
                        y: page.tileRect.y - page.pageRect.y
                        width: page.tileRect.width
                        height: page.tileRect.height
                    }
                }
            }
        }

        Tk.ScrollBar {
            id: verticalBar
            objectName: "richDocumentVerticalScrollBar"
            orientation: Qt.Vertical
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            size: area.height / Math.max(1, area.contentHeight)
            position: area.contentY / Math.max(1, area.contentHeight)
            onPositionChanged: if (pressed) area.contentY = position * area.contentHeight
        }
        Tk.ScrollBar {
            objectName: "richDocumentHorizontalScrollBar"
            orientation: Qt.Horizontal
            anchors.left: parent.left
            anchors.right: verticalBar.left
            anchors.bottom: parent.bottom
            size: area.width / Math.max(1, area.contentWidth)
            position: area.contentX / Math.max(1, area.contentWidth)
            onPositionChanged: if (pressed) area.contentX = position * area.contentWidth
        }
        Tk.Caption {
            objectName: "richDocumentPagination"
            visible: !area.paginationComplete
            anchors.right: verticalBar.left
            anchors.bottom: parent.bottom
            anchors.margins: Tk.Theme.space.sm
            text: qsTr("Laying out pages… %1 of about %2").arg(area.pageCount).arg(area.estimatedPageCount)
        }
    }
}
