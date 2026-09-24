// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (WP-087): the index owns stable asset IDs and graph state.
// Hosts resolve navigation, relink and rebuild requests; this view is read-only.
Item {
    id: root
    objectName: "assetDependencyView"
    property Tk.AssetDependencyIndex index: null
    property string selectedId: ""
    property string query: ""
    property string tooltip: qsTr("Asset dependencies")
    property int graphRevision: 0
    readonly property var visibleAssets: index ? index.assets.filter(row =>
        !query || String(row.id).toLowerCase().includes(query.toLowerCase())
               || String(row.label).toLowerCase().includes(query.toLowerCase())) : []
    readonly property var selectedAsset: {
        if (graphRevision < 0 || !index || !selectedId) return ({})
        return index.asset(selectedId)
    }
    readonly property var selectedDependencies: {
        if (graphRevision < 0 || !index || !selectedId) return []
        return index.dependencies(selectedId)
    }
    readonly property var selectedDependents: {
        if (graphRevision < 0 || !index || !selectedId) return []
        return index.dependents(selectedId)
    }
    readonly property var selectedImpact: {
        if (graphRevision < 0 || !index || !selectedId) return []
        return index.affectedBy(selectedId)
    }
    readonly property var selectedDiagnostics: index ? index.diagnostics.filter(row =>
        row.sourceId === selectedId || row.targetId === selectedId
        || (row.members && row.members.indexOf(selectedId) >= 0)) : []

    signal navigateRequested(string assetId)
    signal relinkRequested(string assetId)
    signal rebuildRequested(string assetId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Connections { target: root.index; function onChanged() { root.graphRevision += 1 } }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("ASSET DEPENDENCIES"); Tk.Flex.grow: 1 }
            Tk.Caption { text: root.index ? qsTr("%1 assets · %2 links")
                                              .arg(root.index.assetCount).arg(root.index.edgeCount) : "" }
        }
        Tk.SearchField {
            objectName: "dependencySearch"
            placeholderText: qsTr("Find an asset")
            tooltip: qsTr("Filter assets by name or ID")
            onSearched: function(text) { root.query = text }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
            ListView {
                id: assetList
                objectName: "dependencyAssetList"
                implicitWidth: Tk.Theme.size.panelMinWidth
                implicitHeight: Tk.Theme.size.panelMinHeight
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                clip: true
                model: root.visibleAssets
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                delegate: Tk.ListRow {
                    required property var modelData
                    objectName: "assetDependencyRow_" + modelData.id
                    width: assetList.width
                    text: modelData.label
                    secondaryText: modelData.id + (modelData.available ? "" : qsTr(" · Missing"))
                    selected: root.selectedId === modelData.id
                    tooltip: modelData.id
                    onClicked: root.selectedId = modelData.id
                    onDoubleClicked: root.navigateRequested(modelData.id)
                }
            }
            Tk.Scroll {
                objectName: "dependencyDetailScroll"
                implicitWidth: Tk.Theme.size.panelMinWidth
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.minWidth: 0
                overflowY: Tk.Scroll.Auto
                Tk.Flex {
                    direction: Tk.Flex.Column
                    align: Tk.Flex.Stretch
                    gap: Tk.Theme.space.sm
                    Tk.EmptyState {
                        visible: !root.selectedId
                        title: qsTr("Select an asset")
                        text: qsTr("Inspect inputs, dependents and impact.")
                    }
                    Tk.Heading {
                        objectName: "dependencySelectedTitle"
                        visible: !!root.selectedId
                        text: root.selectedAsset.label || root.selectedId
                        level: 3
                    }
                    Tk.Caption {
                        objectName: "dependencySelectedUri"
                        visible: !!root.selectedId
                        text: root.selectedAsset.uri || root.selectedId
                        wrapMode: Text.WrapAnywhere
                    }
                    Tk.Caption {
                        objectName: "dependencyStatus"
                        visible: !!root.selectedId
                        text: root.selectedAsset.available ? qsTr("Available") : qsTr("Missing")
                        color: root.selectedAsset.available ? Tk.Theme.color.success
                                                             : Tk.Theme.color.danger
                    }
                    Tk.Flex {
                        visible: !!root.selectedId
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.xs
                        Tk.Button {
                            objectName: "dependencyNavigate"
                            text: qsTr("Open")
                            tooltip: qsTr("Open the selected asset")
                            onClicked: root.navigateRequested(root.selectedId)
                        }
                        Tk.Button {
                            objectName: "dependencyRelink"
                            text: qsTr("Relink")
                            tooltip: qsTr("Choose a new location for this asset")
                            onClicked: root.relinkRequested(root.selectedId)
                        }
                        Tk.Button {
                            objectName: "dependencyRebuild"
                            text: qsTr("Rebuild")
                            tooltip: qsTr("Request a rebuild for this asset")
                            available: !!root.selectedAsset.available
                                       && root.selectedDiagnostics.length === 0
                            onClicked: root.rebuildRequested(root.selectedId)
                        }
                    }
                    Tk.Overline { visible: !!root.selectedId; title: qsTr("INPUTS") }
                    Repeater {
                        model: root.selectedDependencies
                        delegate: Tk.ListRow {
                            required property string modelData
                            objectName: "dependencyInput_" + modelData
                            text: modelData
                            tooltip: qsTr("Inspect input %1").arg(modelData)
                            onClicked: root.selectedId = modelData
                        }
                    }
                    Tk.Overline { visible: !!root.selectedId; title: qsTr("USED BY") }
                    Repeater {
                        model: root.selectedDependents
                        delegate: Tk.ListRow {
                            required property string modelData
                            text: modelData
                            tooltip: qsTr("Inspect dependent %1").arg(modelData)
                            onClicked: root.selectedId = modelData
                        }
                    }
                    Tk.Caption {
                        objectName: "dependencyImpact"
                        visible: !!root.selectedId
                        text: qsTr("%1 outputs affected: %2")
                              .arg(root.selectedImpact.length)
                              .arg(root.selectedImpact.join(", "))
                        wrapMode: Text.Wrap
                    }
                    Tk.Overline { visible: root.selectedDiagnostics.length > 0; title: qsTr("DIAGNOSTICS") }
                    Repeater {
                        model: root.selectedDiagnostics
                        delegate: Tk.Caption {
                            required property var modelData
                            objectName: "dependencyDiagnostic"
                            text: modelData.message
                            color: Tk.Theme.color.danger
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
