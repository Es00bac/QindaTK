// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the host owns the asset catalog, durable IDs, metadata,
// relinking and proxy jobs. MediaBin virtualizes thumbnails through AssetGrid
// and emits requests; it never mutates catalog records itself. With `catalog`
// (a Tk.MediaCatalog) bound, search, bin filtering, moving between bins and
// proxy creation/switching are applied to that catalog (docs/media-playback.md);
// relinking stays a host request because it needs a file choice.
T.Control {
    id: bin
    objectName: "mediaBin"
    property var model: null
    property var identityModel: null
    property Tk.SelectionModel selectionModel: null
    property Tk.MediaCatalog catalog: null
    property string idRole: "id"
    property var provider: null
    property var document: null
    property string viewId: ""
    property string selectedAssetId: ""
    property var selectedMetadata: ({})
    property string tooltip: qsTr("Browse source media")
    property string accessibleName: qsTr("Media bin")

    property var _catalogAsset: ({})
    readonly property var effectiveModel: bin.model ? bin.model : bin.catalog
    readonly property string effectiveIdRole: bin.catalog && bin.idRole === "id" ? "assetId" : bin.idRole
    readonly property var metadata: bin.catalog && bin.selectedAssetId.length ? bin._catalogAsset : bin.selectedMetadata
    readonly property var binChoices: bin._choices(qsTr("All media"))
    readonly property var moveChoices: bin._choices(qsTr("No bin"))

    signal searchRequested(string query)
    signal assetActivated(string assetId)
    signal relinkRequested(string assetId)
    signal proxyActionRequested(string assetId, string action)

    function refreshAsset() {
        bin._catalogAsset = bin.catalog && bin.selectedAssetId.length ? bin.catalog.asset(bin.selectedAssetId) : ({})
    }
    function _choices(rootName) {
        const choices = [({binId: "", name: rootName})]
        const list = bin.catalog ? bin.catalog.bins : []
        for (let i = 0; i < list.length; ++i) choices.push(list[i])
        return choices
    }
    function binIndexOf(binId) {
        for (let i = 0; i < bin.binChoices.length; ++i)
            if (bin.binChoices[i].binId === binId) return i
        return 0
    }
    onCatalogChanged: refreshAsset()
    onSelectedAssetIdChanged: refreshAsset()
    Connections {
        target: bin.catalog
        ignoreUnknownSignals: true
        function onAssetChanged(assetId) { if (assetId === bin.selectedAssetId) bin.refreshAsset() }
        function onBinsChanged() { bin.refreshAsset() }
    }

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Pane
    Accessible.name: accessibleName
    Accessible.description: selectedAssetId.length ? qsTr("Selected %1").arg(metadata.name || selectedAssetId)
                                                   : qsTr("No media selected")

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: bin.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Flex {
            width: parent.width
            Tk.Overline { title: qsTr("MEDIA BIN") }
            Tk.Spacer { }
            Tk.Caption {
                objectName: "mediaBinCount"
                text: bin.catalog ? qsTr("%1 of %2 assets").arg(bin.catalog.count).arg(bin.catalog.totalCount)
                      : bin.model ? qsTr("%1 assets").arg(bin.model.count || bin.model.rowCount()) : qsTr("No catalog")
            }
        }
        Tk.Flex {
            width: parent.width
            gap: Tk.Theme.space.xs
            Tk.SearchField {
                objectName: "mediaBinSearch"
                Tk.Flex.grow: 1
                tooltip: qsTr("Search media names, tags, and source metadata")
                onSearched: function(query) {
                    if (bin.catalog) bin.catalog.searchText = query
                    bin.searchRequested(query)
                }
            }
            Tk.ComboBox {
                objectName: "mediaBinFilter"
                visible: bin.catalog !== null
                model: bin.binChoices
                textRole: "name"
                small: true
                currentIndex: bin.catalog ? bin.binIndexOf(bin.catalog.binFilter) : 0
                tooltip: qsTr("Show one bin")
                onActivated: function(index) { if (bin.catalog) bin.catalog.binFilter = bin.binChoices[index].binId }
            }
        }
        Tk.AssetGrid {
            id: assetGrid
            objectName: "mediaBinAssets"
            width: parent.width
            Tk.Flex.grow: 1
            model: bin.effectiveModel
            identityModel: bin.identityModel
            selectionModel: bin.selectionModel
            idRole: bin.effectiveIdRole
            provider: bin.provider
            document: bin.document
            viewId: bin.viewId
            tooltip: bin.accessibleName
            onActivated: function(id) { bin.assetActivated(id) }
            onContextRequested: function(id) { bin.assetActivated(id) }
        }
        Item {
            objectName: "mediaBinMetadata"
            width: parent.width
            implicitHeight: Tk.Theme.size.row * 5 + Tk.Theme.space.sm * 2
            visible: bin.selectedAssetId.length > 0
            Rectangle { anchors.fill: parent; color: Tk.Theme.color.panel; radius: Tk.Theme.radius.sm }
            Tk.Flex {
                anchors.fill: parent
                anchors.margins: Tk.Theme.space.sm
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Label {
                    objectName: "mediaBinAssetName"
                    text: bin.metadata.name || bin.selectedAssetId
                    font.pixelSize: Tk.Theme.font.body
                    elide: Text.ElideRight
                }
                Tk.Caption {
                    objectName: "mediaBinAssetMetadata"
                    text: [bin.metadata.sourceTimecode || "", bin.metadata.duration || "",
                           bin.metadata.codec || "", bin.metadata.resolution || "", bin.metadata.audio || ""]
                          .filter(function(v) { return v.length > 0 }).join(" · ")
                    elide: Text.ElideRight
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                Tk.Caption {
                    objectName: "mediaBinAssetStatus"
                    text: [bin.metadata.online === false ? qsTr("Offline") : qsTr("Online"),
                           bin.metadata.proxyActive ? qsTr("Proxy in use")
                               : (bin.metadata.proxyStatus || qsTr("No proxy")),
                           (bin.metadata.tags || []).join(", ")]
                          .filter(function(v) { return v.length > 0 }).join(" · ")
                    color: bin.metadata.online === false ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
                }
                Tk.Flex {
                    width: parent.width
                    gap: Tk.Theme.space.xs
                    Tk.Button {
                        objectName: "mediaBinRelink"
                        text: qsTr("Relink")
                        tooltip: qsTr("Ask the host to relink this source")
                        onClicked: bin.relinkRequested(bin.selectedAssetId)
                    }
                    Tk.Button {
                        objectName: "mediaBinProxy"
                        text: qsTr("Create proxy")
                        enabled: bin.metadata.proxyStatus !== "creating" && bin.metadata.online !== false
                        tooltip: qsTr("Create a proxy for this source")
                        onClicked: {
                            if (bin.catalog) bin.catalog.createProxy(bin.selectedAssetId)
                            bin.proxyActionRequested(bin.selectedAssetId, "create")
                        }
                    }
                    Tk.Button {
                        objectName: "mediaBinUseProxy"
                        text: bin.metadata.proxyActive ? qsTr("Use original") : qsTr("Use proxy")
                        enabled: !!bin.metadata.proxyAvailable
                        tooltip: enabled ? qsTr("Switch previews between the proxy and the original")
                                         : qsTr("No proxy is available")
                        onClicked: {
                            const action = bin.metadata.proxyActive ? "original" : "use"
                            if (bin.catalog) bin.catalog.setProxyActive(bin.selectedAssetId, action === "use")
                            bin.proxyActionRequested(bin.selectedAssetId, action)
                        }
                    }
                    Tk.Spacer { }
                    Tk.ComboBox {
                        objectName: "mediaBinMoveTo"
                        visible: bin.catalog !== null
                        model: bin.moveChoices
                        textRole: "name"
                        small: true
                        currentIndex: bin.binIndexOf(bin.metadata.binId || "")
                        tooltip: qsTr("Move this asset to a bin")
                        onActivated: function(index) {
                            if (bin.catalog) bin.catalog.moveToBin([bin.selectedAssetId], bin.moveChoices[index].binId)
                        }
                    }
                }
            }
        }
    }
    Tk.ToolTip { visible: bin.hovered && bin.tooltip.length > 0; text: bin.tooltip }
}
