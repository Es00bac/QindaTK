// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Rich text and pages (WP-050): one Tk.RichTextDocument shown paged in a
// RichDocumentView and as a continuous draft in a RichTextEditor, with the
// FormatBar bound to whichever of the two has focus and a PageNavigator.
// showcase.qtkrich is a native qindatk.richtext/1 file: mixed scripts, lists,
// a link, an inline image, a 30-row table crossing a page break with a
// repeated header, a float that moves with its anchor, a page stamp and a
// page-break appendix with right-to-left paragraphs. Run:
//   qtk-preview examples/rich-document/Main.qml
//   qtk-preview examples/rich-document/Main.qml --dump --size 1280x800
Rectangle {
    id: root
    width: 1280
    height: 800
    color: Tk.Theme.color.bg

    property Item activeEditor: pages

    Tk.RichTextDocument {
        id: document
        Component.onCompleted: loadFromFile(Qt.resolvedUrl("showcase.qtkrich"))
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column

        Tk.FormatBar {
            objectName: "richFormatBar"
            target: root.activeEditor
            Tk.Flex.shrink: 0
        }

        Tk.Flex {
            direction: Tk.Flex.Row
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0

            Tk.PageNavigator {
                objectName: "richPageNavigator"
                view: pages
                Tk.Flex.shrink: 0
            }
            Tk.RichDocumentView {
                id: pages
                objectName: "richPages"
                document: document
                zoom: 0.75
                tooltip: qsTr("Field report")
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                onActiveFocusChanged: if (activeFocus) root.activeEditor = pages
            }
            Tk.Panel {
                objectName: "richDraftPanel"
                title: qsTr("Draft view: same document")
                implicitWidth: Tk.Theme.size.panelMinWidth * 1.5
                Tk.Flex.shrink: 0
                Tk.RichTextEditor {
                    id: draft
                    objectName: "richDraft"
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    document: document
                    maximumHeight: parent.height
                    tooltip: qsTr("Draft view")
                    onActiveFocusChanged: if (activeFocus) root.activeEditor = draft
                }
            }
        }

        Tk.StatusBar {
            objectName: "richStatus"
            Tk.Flex.shrink: 0
            Tk.Caption {
                text: qsTr("Page %1 of %2").arg(pages.currentPage + 1).arg(pages.pageCount)
            }
            Tk.Caption {
                text: document.missingFontFamilies().length > 0
                      ? qsTr("Missing fonts: %1").arg(document.missingFontFamilies().join(", "))
                      : qsTr("All fonts available")
            }
            Tk.Caption {
                text: qsTr("%1 paragraphs · revision %2").arg(document.paragraphCount).arg(document.revision)
            }
        }
    }
}
