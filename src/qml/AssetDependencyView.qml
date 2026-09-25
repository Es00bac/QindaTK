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
    // Outgoing edges with where they were written: {targetId, kind, line, reference}.
    readonly property var selectedReferences: {
        if (graphRevision < 0 || !index || !selectedId) return []
        return index.references(selectedId)
    }
    // AGENT-NOTE: relink candidates come from a bounded walk of the project
    // tree on a worker (relinkCandidatesAsync), asked only when the selection
    // becomes a missing asset that something still uses, and after a scan;
    // never from a binding, which would re-walk on every index change.
    property var relinkCandidates: []
    property string relinkRequest: ""
    readonly property string relinkKey: graphRevision >= 0 && index && selectedId
        && selectedAsset.available === false && selectedDependents.length > 0 ? selectedId : ""
    onRelinkKeyChanged: requestRelinkCandidates()
    function requestRelinkCandidates() {
        relinkCandidates = []
        relinkRequest = relinkKey && index ? index.relinkCandidatesAsync(relinkKey) : ""
    }
    // Missing leaves or cycles anywhere in the selected asset's inputs.
    readonly property var rebuildBlockers: {
        if (graphRevision < 0 || !index || !selectedId) return []
        return index.rebuildBlockers(selectedId)
    }
    // What replacing the selected asset rebuilds: {valid: [ids], blocked: [{id, reason}]}.
    readonly property var rebuildPlan: {
        if (graphRevision < 0 || !index || !selectedId) return ({valid: [], blocked: []})
        return index.rebuildPlan(selectedId)
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
    // validIds: the affected outputs whose inputs are all present, inputs first.
    signal rebuildAffectedRequested(string assetId, var validIds)
    // plan comes from index.relinkPlan: [{sourceId, line, kind, reference,
    // replacement, note}]. The host applies the edits and rescans the sources.
    signal relinkChosen(string assetId, string replacementId, var plan)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Connections {
        target: root.index
        function onChanged() { root.graphRevision += 1 }
        function onProjectScanFinished() { if (root.relinkKey) root.requestRelinkCandidates() }
        function onRelinkCandidatesReady(requestId, assetId, candidates) {
            if (requestId !== root.relinkRequest) return
            root.relinkRequest = ""
            root.relinkCandidates = candidates
        }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("ASSET DEPENDENCIES"); Tk.Flex.grow: 1 }
            Tk.Caption {
                objectName: "dependencyCounts"
                text: !root.index ? ""
                      : root.index.scanning ? qsTr("Scanning project…")
                      : qsTr("%1 assets · %2 links").arg(root.index.assetCount)
                        .arg(root.index.edgeCount)
            }
            Tk.Button {
                objectName: "dependencyCancelScan"
                visible: !!root.index && root.index.scanning
                text: qsTr("Cancel")
                tooltip: qsTr("Stop scanning the project")
                onClicked: root.index.cancelProjectScan()
            }
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
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.minWidth: 0
                Tk.Segmented {
                    id: detailMode
                    objectName: "dependencyDetailMode"
                    small: true
                    model: [qsTr("Details"), qsTr("Graph")]
                    tooltip: qsTr("Show details or the dependency graph")
                }
                Tk.AssetDependencyGraph {
                    visible: detailMode.currentIndex === 1
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                    index: root.index
                    selectedId: root.selectedId
                    revision: root.graphRevision
                    onAssetSelected: function(assetId) { root.selectedId = assetId }
                }
                Tk.Scroll {
                    objectName: "dependencyDetailScroll"
                    implicitWidth: Tk.Theme.size.panelMinWidth
                    visible: detailMode.currentIndex === 0
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
                            wrap: Tk.Flex.Wrap   // narrow panes keep every action reachable
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
                                tooltip: root.rebuildBlockers.length === 0
                                         ? qsTr("Request a rebuild for this asset")
                                         : root.rebuildBlockers[0].message
                                available: root.rebuildBlockers.length === 0
                                onClicked: root.rebuildRequested(root.selectedId)
                            }
                            Tk.Button {
                                objectName: "dependencyRebuildAffected"
                                text: qsTr("Rebuild affected")
                                tooltip: qsTr("Rebuild the outputs that use this asset")
                                available: root.rebuildPlan.valid.length > 0
                                onClicked: root.rebuildAffectedRequested(root.selectedId,
                                                                         root.rebuildPlan.valid)
                            }
                        }
                        Tk.Caption {
                            objectName: "dependencyRebuildBlocked"
                            visible: !!root.selectedId && root.rebuildBlockers.length > 0
                            text: root.rebuildBlockers.length > 0
                                  ? qsTr("Rebuild blocked: %1").arg(root.rebuildBlockers[0].message) : ""
                            color: Tk.Theme.color.danger
                            wrapMode: Text.Wrap
                        }
                        Tk.Overline { visible: !!root.selectedId; title: qsTr("INPUTS") }
                        Repeater {
                            model: root.selectedReferences
                            delegate: Tk.ListRow {
                                required property var modelData
                                objectName: "dependencyInput_" + modelData.targetId
                                text: modelData.targetId
                                secondaryText: (modelData.line > 0 ? qsTr("line %1 · ").arg(modelData.line) : "")
                                               + modelData.kind
                                               + (modelData.available ? "" : qsTr(" · Missing"))
                                tooltip: qsTr("Inspect input %1").arg(modelData.targetId)
                                onClicked: root.selectedId = modelData.targetId
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
                        Tk.Caption {
                            objectName: "dependencyRebuildPlan"
                            visible: !!root.selectedId && root.selectedImpact.length > 0
                            text: qsTr("Replacing it rebuilds %1 · %2 blocked")
                                  .arg(root.rebuildPlan.valid.join(", ") || qsTr("nothing"))
                                  .arg(root.rebuildPlan.blocked.length)
                            wrapMode: Text.Wrap
                        }
                        Tk.Overline {
                            visible: root.relinkCandidates.length > 0
                            title: qsTr("RELINK TO")
                        }
                        Repeater {
                            model: root.relinkCandidates
                            delegate: Tk.ListRow {
                                required property string modelData
                                objectName: "dependencyCandidate_" + modelData
                                text: modelData
                                tooltip: qsTr("Relink %1 to %2").arg(root.selectedId).arg(modelData)
                                onClicked: root.relinkChosen(root.selectedId, modelData,
                                                             root.index.relinkPlan(root.selectedId, modelData))
                            }
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
}
