// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 960
    height: 700
    Tk.ResourceResolver { id: resources }
    Tk.AssetProvider { id: assets; resolver: resources }
    Tk.RichTextDocument { id: document }

    Component.onCompleted: {
        resources.registerResource("figure/page", Qt.resolvedUrl(
            "../../docs/universal-toolkit/implementation/evidence/WP-050-page2.png").toString())
        page.invokeCommand("insert.object", {
            kind: "image", source: "asset:figure/page", widthPt: 330, heightPt: 260,
            alt: "Managed reference figure"
        })
    }

    Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Heading { text: "Managed rich-text image" }
        Tk.Caption { text: "The figure is loaded through AssetProvider from a stable asset: resource ID." }
        Tk.RichDocumentView {
            id: page
            objectName: "richAssetView"
            width: parent.width
            Tk.Flex.grow: 1
            document: document
            assetProvider: assets
            tooltip: "Document with managed reference figure"
        }
    }
}
