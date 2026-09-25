// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Internal part of Tk.BlockEditor: image, ink, embed, divider and preserved
// unsupported blocks. An image keeps its logical resource ID; availability
// comes from the editor's Tk.ResourceResolver, so a missing file shows a
// relink notice instead of losing the block.
Tk.Flex {
    id: root
    property var editor: null // the owning Tk.BlockEditor (var: a typed property would make a load cycle)
    property var block: ({})
    readonly property string blockId: block && block.id ? block.id : ""
    readonly property string blockType: block && block.type ? block.type : ""
    readonly property var blockData: block && block.data ? block.data : ({})
    readonly property string resourceId: String(blockData.resourceId || "")
    readonly property string availability: availabilityOf(resourceId, editor && editor.resolver ? editor.resolver.resources : [])
    readonly property var link: editor && editor.notebook && String(blockData.targetId || "").indexOf("notebook:") === 0
                                ? editor.notebook.resolveLink(blockData.targetId) : null
    readonly property bool known: ["image", "ink", "embed", "divider"].indexOf(blockType) >= 0

    function availabilityOf(id, resources) {
        if (id.length === 0) return "none"
        for (let i = 0; i < resources.length; ++i)
            if (resources[i].resourceId === id) return resources[i].availability
        return editor && editor.resolver ? "unregistered" : "unknown"
    }

    direction: Tk.Flex.Column
    align: Tk.Flex.Stretch
    gap: Tk.Theme.space.xs
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("%1 block").arg(blockType)
    Accessible.description: blockType === "image" && availability !== "ready" && availability !== "none"
                            ? qsTr("Media %1").arg(availability) : ""

    Tk.AssetThumbnail {
        objectName: "blockMediaPreview_" + root.blockId
        visible: root.blockType === "image" || root.blockType === "ink"
        provider: root.editor ? root.editor.provider : null
        resourceId: root.resourceId
        document: root.editor ? root.editor.document : null
        viewId: root.editor ? root.editor.viewId : ""
        requestKind: "thumbnail"
        tooltip: root.blockType === "ink" ? qsTr("Ink preview") : qsTr("Image block preview")
        active: visible && root.resourceId.length > 0
        implicitWidth: Tk.Theme.size.thumbnail * 2
        implicitHeight: Tk.Theme.size.thumbnailHeight
    }
    Tk.Notice {
        objectName: "blockMediaMissing_" + root.blockId
        visible: root.blockType === "image" && (root.availability === "missing" || root.availability === "unregistered"
                                               || root.availability === "modified" || root.blockData.state === "missing")
        variant: "warning"
        title: root.availability === "modified" ? qsTr("Media changed on disk") : qsTr("Media file is missing")
        text: qsTr("%1 is kept in the notebook; relink it to show the image.").arg(root.resourceId || qsTr("This image"))
    }
    Tk.TextField {
        objectName: "blockMediaCaption_" + root.blockId
        visible: root.blockType === "image"
        text: String(root.blockData.caption || "")
        placeholderText: qsTr("Caption")
        tooltip: qsTr("Image caption")
        readOnly: !root.editor || !root.editor.capabilities.canEdit || !!(root.block && root.block.locked)
        onEditingFinished: if (text !== String(root.blockData.caption || "")) root.editor.submitData(root.block, { caption: text })
    }
    Tk.Caption {
        visible: root.blockType === "ink"
        text: qsTr("%1 strokes").arg((root.blockData.strokes || []).length)
        color: Tk.Theme.color.textMuted
    }
    Tk.Button {
        objectName: "blockMediaRelink_" + root.blockId
        text: qsTr("Choose media")
        iconName: "image"
        tooltip: qsTr("Request a media link for this block")
        enabled: !!root.editor && !(root.block && root.block.locked)
        visible: root.blockType === "image"
        onClicked: root.editor.blockActionRequested(root.blockId, "choose-media", root.editor.requestId("media", root.blockId))
    }
    Tk.Notice {
        objectName: "blockEmbed_" + root.blockId
        visible: root.blockType === "embed"
        title: String(root.blockData.title || qsTr("Embedded content"))
        text: root.link && !root.link.valid ? qsTr("The linked page or block no longer exists.")
              : String(root.blockData.description || qsTr("Linked item · %1").arg(root.blockData.targetId || qsTr("no target")))
        variant: root.link ? (root.link.valid ? "info" : "warning") : root.blockData.state === "ready" ? "info" : "warning"
        Tk.Button {
            objectName: "blockEmbedOpen_" + root.blockId
            text: qsTr("Open")
            iconName: "external-link"
            tooltip: qsTr("Open linked content")
            visible: !!root.blockData.targetId && (!root.link || root.link.valid)
            onClicked: root.editor.openLink(root.blockId, root.blockData.targetId)
        }
    }
    Tk.Divider {
        visible: root.blockType === "divider"
    }
    Tk.Notice {
        objectName: "blockUnsupported_" + root.blockId
        visible: !root.known
        title: qsTr("Unsupported block type")
        text: qsTr("%1 is preserved unchanged but has no editor in this view.").arg(root.blockType)
        variant: "warning"
    }
}
