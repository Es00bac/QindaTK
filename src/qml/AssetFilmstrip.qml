// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The outgoing head and incoming tail frame. Frame extraction is supplied by
// an injected AssetBackend; the default file backend reports Unsupported.
Item {
    id: root

    property var provider: null
    property string resourceId: ""
    property string headTimeTicks: "0"
    // Leave empty when duration is unknown; the tail subscription stays idle.
    property string tailTimeTicks: ""
    property string rateNumerator: "1"
    property string rateDenominator: "1"
    property var document: null
    property string viewId: ""
    property bool active: true
    property bool crop: true
    property var frameSize: ({width: Tk.Theme.size.thumbnail,
                              height: Tk.Theme.size.thumbnailHeight})
    property string tooltip: ""

    readonly property string assetState: {
        if (head.assetState === "failed" || head.assetState === "stale"
                || head.assetState === "missing" || head.assetState === "cancelled")
            return head.assetState
        if (tail.active && (tail.assetState === "failed" || tail.assetState === "stale"
                            || tail.assetState === "missing" || tail.assetState === "cancelled"))
            return tail.assetState
        if (head.assetState === "loading" || (tail.active && tail.assetState === "loading"))
            return "loading"
        if (head.assetState === "ready" && (!tail.active || tail.assetState === "ready"))
            return "ready"
        return "idle"
    }
    readonly property string assetError: head.assetError.length > 0 ? head.assetError : tail.assetError

    objectName: "assetFilmstrip"
    implicitWidth: Tk.Theme.size.thumbnail * 2 + Tk.Theme.space.xs
    implicitHeight: Tk.Theme.size.thumbnailHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: root.tooltip.length > 0 ? root.tooltip : qsTr("Video head and tail frames")
    Accessible.description: root.assetError.length > 0 ? root.assetError : root.assetState

    Tk.Flex {
        objectName: "assetFilmstripFrames"
        anchors.fill: parent
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs

        Tk.AssetThumbnail {
            id: head
            objectName: "assetFilmstripHead"
            provider: root.provider
            resourceId: root.resourceId
            requestKind: "frame"
            requestOptions: ({timeTicks: root.headTimeTicks,
                              rateNumerator: root.rateNumerator,
                              rateDenominator: root.rateDenominator,
                              size: root.frameSize})
            document: root.document
            viewId: root.viewId
            active: root.active
            crop: root.crop
            placeholderIcon: "film"
            caption: qsTr("Head")
            tooltip: qsTr("Video frame at %1 ticks").arg(root.headTimeTicks)
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }

        Tk.AssetThumbnail {
            id: tail
            objectName: "assetFilmstripTail"
            provider: root.provider
            resourceId: root.resourceId
            requestKind: "frame"
            requestOptions: ({timeTicks: root.tailTimeTicks,
                              rateNumerator: root.rateNumerator,
                              rateDenominator: root.rateDenominator,
                              size: root.frameSize})
            document: root.document
            viewId: root.viewId
            active: root.active && root.tailTimeTicks.length > 0
            crop: root.crop
            placeholderIcon: "film"
            caption: qsTr("Tail")
            tooltip: root.tailTimeTicks.length > 0
                     ? qsTr("Video frame at %1 ticks").arg(root.tailTimeTicks)
                     : qsTr("Set tailTimeTicks to request the final frame")
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }
    }
}
