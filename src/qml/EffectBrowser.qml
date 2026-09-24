// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Registry snapshots describe compatibility/capability; preview and insert
// are explicit host requests over effect and target IDs.
Item {
    id: root
    property var effects: [] // {effectId,label,category,description,compatible,reason,previewAssetId}
    property var categories: []
    property string categoryId: "all"
    property string searchText: ""
    property string selectedEffectId: ""
    property string targetLayerId: ""
    property string documentId: ""
    property string revision: "0"
    property string pendingRequestId: ""
    property string previewStatus: "idle"
    property string statusText: ""
    property string errorText: ""
    property string tooltip: qsTr("Browse effects for the selected layer")
    property string _serial: "0"
    readonly property var filteredEffects: filterEffects()
    readonly property var selectedEffect: findEffect(selectedEffectId)

    signal effectSelectionRequested(string effectId)
    signal effectPreviewRequested(string requestId, string effectId, string targetLayerId)
    signal effectInsertRequested(string requestId, string effectId, string targetLayerId,
                                 string expectedRevision)
    signal previewCancelRequested(string requestId, string pendingRequestId)

    objectName: "effectBrowser"
    implicitWidth: Tk.Theme.size.panelMinWidth * 1.5
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: errorText.length ? errorText
        : qsTr("%1 compatible of %2 effects · target %3").arg(compatibleCount).arg(effects.length).arg(targetLayerId || qsTr("none"))
    readonly property int compatibleCount: filteredEffects.filter(function(effect) { return effect.compatible !== false }).length

    function findEffect(id) {
        for (let i = 0; i < effects.length; ++i)
            if (String(effects[i].effectId) === String(id)) return effects[i]
        return null
    }
    function filterEffects() {
        const query = root.searchText.trim().toLowerCase()
        return (root.effects || []).filter(function(effect) {
            const categoryMatch = root.categoryId === "all" || String(effect.categoryId || effect.category || "") === root.categoryId
            const label = String(effect.label || effect.effectId || "")
            const searchable = (label + " " + String(effect.description || "") + " " + String(effect.category || "")).toLowerCase()
            return categoryMatch && (!query.length || searchable.indexOf(query) >= 0)
        })
    }
    function requestId(kind) {
        root._serial = String(Number(root._serial) + 1)
        return (documentId.length ? documentId : "effects") + ":" + kind + ":" + root._serial
    }
    function select(id) {
        const effect = findEffect(id)
        if (!enabled || !effect) return false
        selectedEffectId = String(effect.effectId)
        effectSelectionRequested(selectedEffectId)
        return true
    }
    function preview() {
        const effect = selectedEffect
        if (!enabled || !effect || effect.compatible === false || !targetLayerId.length || pendingRequestId.length) return false
        effectPreviewRequested(requestId("preview"), String(effect.effectId), targetLayerId)
        return true
    }
    function insert() {
        const effect = selectedEffect
        if (!enabled || !effect || effect.compatible === false || !targetLayerId.length || pendingRequestId.length) return false
        effectInsertRequested(requestId("insert"), String(effect.effectId), targetLayerId, revision)
        return true
    }
    function cancelPreview() {
        if (!enabled || pendingRequestId.length === 0) return false
        previewCancelRequested(requestId("cancel"), pendingRequestId)
        return true
    }

    Accessible.focusable: true
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.sm
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            width: parent.width
            Tk.Overline { text: qsTr("EFFECT BROWSER") }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption { text: qsTr("%1 compatible").arg(root.compatibleCount); color: Tk.Theme.color.textMuted }
        }
        Tk.Flex {
            width: parent.width
            Tk.TextField {
                objectName: "effectSearch"
                placeholderText: qsTr("Search effects")
                text: root.searchText
                clearable: true
                tooltip: qsTr("Filter effects by name, category, or description")
                onTextEdited: root.searchText = text
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.ComboBox {
                objectName: "effectCategory"
                model: root.categories
                textRole: "label"
                tooltip: qsTr("Effect category")
                onActivated: root.categoryId = String(currentValue)
            }
        }
        Tk.Caption {
            visible: root.errorText.length > 0
            text: root.errorText
            color: Tk.Theme.color.warning
            wrapMode: Text.WordWrap
        }
        Tk.Scroll {
            objectName: "effectResults"
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                width: parent.width
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Repeater {
                    model: root.filteredEffects
                    delegate: Tk.Card {
                        required property var modelData
                        required property int index
                        readonly property string effectId: String(modelData.effectId)
                        objectName: "effectResult_" + effectId
                        width: parent.width
                        padding: Tk.Theme.space.sm
                        selected: root.selectedEffectId === effectId
                        tooltip: String(modelData.description || modelData.label || effectId)
                        interactive: true
                        Accessible.role: Accessible.ListItem
                        Accessible.name: String(modelData.label || effectId)
                        Accessible.description: modelData.compatible === false
                            ? String(modelData.reason || qsTr("Incompatible with the selected target"))
                            : String(modelData.description || modelData.category || "")
                        onClicked: root.select(effectId)
                        implicitHeight: resultBody.implicitHeight + Tk.Theme.space.sm * 2
                        Tk.Flex {
                            id: resultBody
                            anchors.fill: parent
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            Tk.Flex {
                                width: parent.width
                                Tk.Label { text: String(modelData.label || effectId); color: Tk.Theme.color.text; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                                Tk.Badge {
                                    text: modelData.compatible === false ? qsTr("Unavailable") : String(modelData.category || qsTr("Effect"))
                                    variant: modelData.compatible === false ? "warning" : "default"
                                    tooltip: String(modelData.reason || modelData.category || "")
                                }
                            }
                            Tk.Caption {
                                text: modelData.compatible === false ? String(modelData.reason || qsTr("This effect is unavailable for the current target."))
                                    : String(modelData.description || "")
                                color: modelData.compatible === false ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
        Tk.Flex {
            width: parent.width
            Tk.Caption { text: root.statusText; color: Tk.Theme.color.textMuted; elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Button { objectName: "effectPreview"; text: qsTr("Preview"); iconName: "eye"; enabled: root.selectedEffect && root.selectedEffect.compatible !== false && root.targetLayerId.length > 0 && root.pendingRequestId.length === 0; tooltip: qsTr("Request a host preview job"); onClicked: root.preview() }
            Tk.Button { objectName: "effectCancelPreview"; text: qsTr("Cancel"); enabled: root.pendingRequestId.length > 0; tooltip: qsTr("Cancel the pending host preview"); onClicked: root.cancelPreview() }
            Tk.Button { objectName: "effectInsert"; text: qsTr("Add effect"); variant: "accent"; enabled: root.selectedEffect && root.selectedEffect.compatible !== false && root.targetLayerId.length > 0 && root.pendingRequestId.length === 0; tooltip: root.selectedEffect && root.selectedEffect.compatible === false ? String(root.selectedEffect.reason || qsTr("Incompatible effect")) : qsTr("Request insertion on the selected layer"); onClicked: root.insert() }
        }
    }
}
