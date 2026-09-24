// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The inline rich-text editor with its format bar. Paginated editing with
// PageNavigator is in examples/rich-document.
Section {
    title: "Rich text"
    note: "RichTextEditor · FormatBar · RichDocumentView · PageNavigator"

    Tk.RichTextDocument {
        id: galleryNote
        Component.onCompleted: {
            const text = "Rich text keeps runs, links and scripts: עברית, العربية, 中文."
            const link = text.indexOf("links")
            loadJson(JSON.stringify({
                schema: "qindatk.richtext", version: 1,
                formats: [{}, { weight: 700 }, { href: "https://example.org/rich-text" }],
                blocks: [{ paragraph: { id: "3f0e7a52-9c1d-4b8e-a7f4-5d2c6b1e9a30", text: text, mark: 0,
                                        spans: [[9, 1], [link - 9, 0], [5, 2], [text.length - link - 5, 0]] } }]
            }))
        }
    }
    Item {
        implicitWidth: 720
        implicitHeight: noteColumn.implicitHeight
        Tk.Flex.shrink: 0
        Tk.Flex {
            id: noteColumn
            width: parent.width
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Tk.FormatBar { target: galleryEditor; compact: true }
            Tk.RichTextEditor {
                id: galleryEditor
                document: galleryNote
                rows: 3
                placeholderText: qsTr("Write a note")
                tooltip: qsTr("Gallery note")
            }
        }
    }
}
