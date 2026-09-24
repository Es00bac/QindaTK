// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the host owns the asset catalog, durable IDs, metadata,
// relinking and proxy jobs. MediaBin virtualizes thumbnails through AssetGrid
// and emits requests; it never mutates catalog records itself.
T.Control {
    id: bin
    objectName: "mediaBin"
    property var model: null
    property var identityModel: null
    property Tk.SelectionModel selectionModel: null
    property string idRole: "id"
    property var provider: null
    property var document: null
    property string viewId: ""
    property string selectedAssetId: ""
    property var selectedMetadata: ({})
    property string tooltip: qsTr("Browse source media")
    property string accessibleName: qsTr("Media bin")

    signal searchRequested(string query)
    signal assetActivated(string assetId)
    signal relinkRequested(string assetId)
    signal proxyActionRequested(string assetId, string action)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.Pane
    Accessible.name: accessibleName
    Accessible.description: selectedAssetId.length ? qsTr("Selected %1").arg(selectedMetadata.name || selectedAssetId)
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
            Tk.Caption { text: bin.model ? qsTr("%1 assets").arg(bin.model.count || bin.model.rowCount()) : qsTr("No catalog") }
        }
        Tk.SearchField {
            objectName: "mediaBinSearch"
            width: parent.width
            tooltip: qsTr("Search media names, tags, and source metadata")
            onSearched: function(query) { bin.searchRequested(query) }
        }
        Tk.AssetGrid {
            id: assetGrid
            objectName: "mediaBinAssets"
            width: parent.width
            Tk.Flex.grow: 1
            model: bin.model
            identityModel: bin.identityModel
            selectionModel: bin.selectionModel
            idRole: bin.idRole
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
            implicitHeight: Tk.Theme.size.row * 4 + Tk.Theme.space.sm * 2
            visible: bin.selectedAssetId.length > 0
            Rectangle { anchors.fill: parent; color: Tk.Theme.color.panel; radius: Tk.Theme.radius.sm }
            Tk.Flex {
                anchors.fill: parent
                anchors.margins: Tk.Theme.space.sm
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Label {
                    objectName: "mediaBinAssetName"
                    text: bin.selectedMetadata.name || bin.selectedAssetId
                    font.pixelSize: Tk.Theme.font.body
                    elide: Text.ElideRight
                }
                Tk.Caption {
                    objectName: "mediaBinAssetMetadata"
                    text: [bin.selectedMetadata.sourceTimecode || "", bin.selectedMetadata.duration || "",
                           bin.selectedMetadata.codec || "", bin.selectedMetadata.resolution || ""]
                          .filter(function(v) { return v.length > 0 }).join(" · ")
                    elide: Text.ElideRight
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
                Tk.Caption {
                    objectName: "mediaBinAssetStatus"
                    text: [bin.selectedMetadata.online === false ? qsTr("Offline") : qsTr("Online"),
                           bin.selectedMetadata.proxyStatus || qsTr("No proxy"),
                           (bin.selectedMetadata.tags || []).join(", ")]
                          .filter(function(v) { return v.length > 0 }).join(" · ")
                    color: bin.selectedMetadata.online === false ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
                }
                Tk.Flex {
                    width: parent.width
                    Tk.Button {
                        objectName: "mediaBinRelink"
                        text: qsTr("Relink")
                        tooltip: qsTr("Ask the host to relink this source")
                        onClicked: bin.relinkRequested(bin.selectedAssetId)
                    }
                    Tk.Button {
                        objectName: "mediaBinProxy"
                        text: qsTr("Create proxy")
                        tooltip: qsTr("Ask the host to create a proxy for this source")
                        onClicked: bin.proxyActionRequested(bin.selectedAssetId, "create")
                    }
                    Tk.Button {
                        objectName: "mediaBinUseProxy"
                        text: qsTr("Use proxy")
                        enabled: !!bin.selectedMetadata.proxyAvailable
                        tooltip: enabled ? qsTr("Ask the host to switch this view to its proxy")
                                         : qsTr("No proxy is available")
                        onClicked: bin.proxyActionRequested(bin.selectedAssetId, "use")
                    }
                }
            }
        }
    }
    Tk.ToolTip { visible: bin.hovered && bin.tooltip.length > 0; text: bin.tooltip }
}
