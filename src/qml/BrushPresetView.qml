// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.Box {
    id: control
    objectName: "brushPresetView"
    property var presets: [] // [{id,name,tags,previewColor,shape,available}]
    property string selectedPresetId: ""
    property string searchText: ""
    property string requestState: "idle"
    property string requestMessage: ""
    property string tooltip: qsTr("Brush presets")
    property string lastRequestId: ""
    property int _requestSerial: 0

    signal presetRequested(string requestId, string presetId, string action)

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: requestMessage

    function visiblePresets() {
        const query = searchText.trim().toLowerCase()
        return (presets || []).slice(0, 500).filter(function(preset) {
            if (!preset || !String(preset.id || "")) return false
            const content = (String(preset.name || "") + " " + (preset.tags || []).join(" ")).toLowerCase()
            return !query || content.indexOf(query) >= 0
        })
    }
    function requestPreset(presetId, action) {
        if (!visiblePresets().some(function(preset) { return String(preset.id) === presetId })) return false
        const requestId = "brush-preset:" + (++_requestSerial)
        lastRequestId = requestId; requestState = "pending"; requestMessage = ""
        presetRequested(requestId, presetId, action)
        return requestId
    }
    function acknowledge(requestId, state, message) {
        if (requestId !== lastRequestId || requestState !== "pending") return false
        requestState = state; requestMessage = message || ""
        return true
    }

    Tk.Flex {
        width: parent.width
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Overline { title: qsTr("Brush presets"); wide: true }
        Tk.SearchField {
            objectName: "brushPresetSearch"
            width: parent.width
            placeholderText: qsTr("Search presets and tags")
            text: control.searchText
            onTextEdited: control.searchText = text
        }
        ListView {
            id: presetList
            objectName: "brushPresetList"
            width: parent.width
            height: Math.min(contentHeight, Tk.Theme.size.control * 8)
            implicitHeight: height
            clip: true
            spacing: Tk.Theme.space.xs
            model: control.visiblePresets()
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Available brush presets")
            delegate: Tk.Flex {
                required property var modelData
                width: presetList.width
                padding: Tk.Theme.space.xs
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Rectangle {
                    objectName: "brushPresetPreview_" + String(modelData.id)
                    Tk.Flex.shrink: 0
                    Tk.Flex.basis: Tk.Theme.size.iconLg
                    implicitWidth: Tk.Theme.size.iconLg
                    implicitHeight: Tk.Theme.size.iconLg
                    width: Tk.Theme.size.iconLg
                    height: Tk.Theme.size.iconLg
                    radius: width / 2
                    color: String(modelData.previewColor || Tk.Theme.color.accentSubtle)
                    border.width: Tk.Theme.size.border
                    border.color: Tk.Theme.color.controlBorder
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Preview for %1").arg(String(modelData.name || modelData.id))
                }
                Tk.Flex {
                    direction: Tk.Flex.Column
                    Tk.Label { text: String(modelData.name || modelData.id); elide: Text.ElideRight }
                    Tk.Caption { text: (modelData.tags || []).slice(0, 5).join(" · ") || qsTr("No tags"); color: Tk.Theme.color.textMuted }
                }
                Tk.Spacer { Tk.Flex.grow: 1 }
                Tk.Button {
                    objectName: "brushPresetSelect_" + String(modelData.id)
                    text: control.selectedPresetId === String(modelData.id) ? qsTr("Selected") : qsTr("Select")
                    variant: control.selectedPresetId === String(modelData.id) ? "accent" : "outline"
                    tooltip: qsTr("Request preset selection")
                    enabled: modelData.available !== false
                    onClicked: control.requestPreset(String(modelData.id), "select")
                }
                Tk.Button {
                    text: qsTr("Copy")
                    tooltip: qsTr("Request a duplicate of this preset")
                    enabled: modelData.available !== false
                    onClicked: control.requestPreset(String(modelData.id), "duplicate")
                }
            }
        }
        Tk.Caption { objectName: "brushPresetStatus"; text: control.requestState + (control.requestMessage.length ? " · " + control.requestMessage : ""); color: control.requestState === "rejected" ? Tk.Theme.color.warning : Tk.Theme.color.textMuted; wrapMode: Text.Wrap }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.hovered }
}
