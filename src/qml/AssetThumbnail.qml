// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A Thumbnail whose image lifetime is owned by AssetProvider. The adapter
// keeps the base tile's fixed geometry while the request loads or fails.
Tk.Thumbnail {
    id: root

    property var provider: null
    property string resourceId: ""
    property string requestKind: "thumbnail"
    property var requestOptions: ({})
    property var document: null
    property string viewId: ""
    // Set false when a virtualized delegate is pooled or has no current row.
    property bool active: true

    property string _subscriptionId: ""
    property var _subscriptionProvider: null
    property bool _componentReady: false
    property var _assetSnapshot: ({state: "idle", data: ({}), error: ""})
    readonly property string subscriptionId: _subscriptionId
    readonly property string assetState: _assetSnapshot.state || "idle"
    readonly property var assetData: _assetSnapshot.data || ({})
    readonly property string assetError: _assetSnapshot.error || ""

    objectName: "assetThumbnail"
    Accessible.description: root.assetError
    source: root.assetState === "ready" ? root.assetData.previewUrl || "" : ""
    placeholderIcon: root.assetState === "failed" || root.assetState === "stale"
                     || root.assetState === "missing" || root.assetState === "cancelled"
                     ? "alert-triangle" : "image"

    function releaseCurrent() {
        const oldId = root._subscriptionId
        root._subscriptionId = ""
        const oldProvider = root._subscriptionProvider
        root._subscriptionProvider = null
        if (oldId.length > 0 && oldProvider)
            oldProvider.release(oldId)
    }

    function readSnapshot() {
        if (!root.provider || root._subscriptionId.length === 0)
            return
        const snapshot = root.provider.asset(root._subscriptionId)
        if (snapshot && snapshot.subscriptionId === root._subscriptionId)
            root._assetSnapshot = snapshot
    }

    function refresh() {
        releaseCurrent()
        root._assetSnapshot = ({state: "idle", data: ({}), error: ""})
        if (!root._componentReady || !root.active || !root.provider
                || root.resourceId.length === 0)
            return

        const options = Object.assign({}, root.requestOptions || ({}))
        if (!options.size) {
            options.size = {
                width: Math.max(1, Math.round(root.implicitWidth)),
                height: Math.max(1, Math.round(root.implicitHeight))
            }
        }
        const id = root.provider.requestAsset(root.resourceId, root.requestKind,
                                             options, root.document, root.viewId)
        if (!id || id.length === 0) {
            root._assetSnapshot = ({state: "failed", data: ({}),
                                    error: root.provider.lastError || qsTr("Asset request could not start")})
            return
        }
        root._subscriptionId = id
        root._subscriptionProvider = root.provider
        readSnapshot()
    }

    onProviderChanged: refresh()
    onResourceIdChanged: refresh()
    onRequestKindChanged: refresh()
    onRequestOptionsChanged: refresh()
    onDocumentChanged: refresh()
    onViewIdChanged: refresh()
    onActiveChanged: refresh()
    Component.onCompleted: {
        root._componentReady = true
        refresh()
    }
    Component.onDestruction: releaseCurrent()

    Connections {
        target: root.provider
        ignoreUnknownSignals: true
        function onAssetChanged(subscriptionId) {
            if (subscriptionId === root._subscriptionId)
                root.readSnapshot()
        }
    }
}
