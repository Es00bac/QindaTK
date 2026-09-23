// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Requests width-reduced peaks and forwards only provider data to Waveform.
Item {
    id: root

    property var provider: null
    property string resourceId: ""
    property var requestOptions: ({})
    property int columns: 0
    property var document: null
    property string viewId: ""
    property bool active: true
    property color color: Tk.Theme.color.info
    property bool showCentreLine: true
    property string tooltip: ""

    property string _subscriptionId: ""
    property var _subscriptionProvider: null
    property bool _componentReady: false
    property var _assetSnapshot: ({state: "idle", data: ({}), error: ""})
    readonly property string subscriptionId: _subscriptionId
    readonly property string assetState: _assetSnapshot.state || "idle"
    readonly property var assetData: _assetSnapshot.data || ({})
    readonly property string assetError: _assetSnapshot.error || ""
    readonly property var peaks: root.assetState === "ready"
                                  ? (root.assetData.peaks || []) : []

    objectName: "assetWaveform"
    implicitWidth: Tk.Theme.size.sparkline
    implicitHeight: Tk.Theme.size.waveformHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: root.tooltip.length > 0 ? root.tooltip : qsTr("Audio asset waveform")
    Accessible.description: root.assetError.length > 0 ? root.assetError : root.assetState

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
        widthTimer.stop()
        releaseCurrent()
        root._assetSnapshot = ({state: "idle", data: ({}), error: ""})
        if (!root._componentReady || !root.active || !root.provider
                || root.resourceId.length === 0)
            return

        const options = Object.assign({}, root.requestOptions || ({}))
        if (root.columns > 0)
            options.columns = root.columns
        else if (!options.columns)
            options.columns = Math.max(1, Math.round(root.width || root.implicitWidth))
        const id = root.provider.requestAsset(root.resourceId, "waveform", options,
                                             root.document, root.viewId)
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
    onRequestOptionsChanged: refresh()
    onColumnsChanged: refresh()
    onWidthChanged: {
        if (root._componentReady)
            widthTimer.restart()
    }
    onDocumentChanged: refresh()
    onViewIdChanged: refresh()
    onActiveChanged: refresh()
    Component.onCompleted: {
        root._componentReady = true
        refresh()
    }
    Component.onDestruction: releaseCurrent()

    Timer {
        id: widthTimer
        interval: Tk.Theme.motion.fast
        repeat: false
        onTriggered: root.refresh()
    }

    Tk.Waveform {
        objectName: "assetWaveformDisplay"
        anchors.fill: parent
        peaks: root.peaks
        color: root.color
        showCentreLine: root.showCentreLine
        tooltip: root.tooltip.length > 0 ? root.tooltip : qsTr("Audio waveform")
    }

    Tk.Caption {
        objectName: "assetWaveformStatus"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        visible: root.assetState === "loading" || root.assetState === "failed"
                 || root.assetState === "stale" || root.assetState === "missing"
                 || root.assetState === "cancelled"
                 || (root.assetState === "ready" && root.peaks.length === 0)
        text: root.assetError.length > 0 ? root.assetError
              : root.assetState === "loading" ? qsTr("Loading waveform…")
              : root.assetState === "stale" ? qsTr("Waveform request is stale")
              : root.assetState === "missing" ? qsTr("Audio resource is missing")
              : root.assetState === "cancelled" ? qsTr("Waveform request was cancelled")
              : qsTr("No waveform data")
        color: root.assetState === "failed" || root.assetState === "missing"
               ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        elide: Text.ElideRight
    }

    Connections {
        target: root.provider
        ignoreUnknownSignals: true
        function onAssetChanged(subscriptionId) {
            if (subscriptionId === root._subscriptionId)
                root.readSnapshot()
        }
    }
}
