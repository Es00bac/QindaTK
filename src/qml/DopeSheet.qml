// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Exposure snapshots stay host-owned. Buttons report exact range operations;
// the host edits art references and publishes the next snapshot.
Item {
    id: root
    property var layers: [] // {layerId,label,exposures:[{exposureId,startTicks,endTicks,artworkId,held}]}
    property var frameBoundaries: [] // ordered decimal strings, frame starts
    property string selectedLayerId: ""
    property string selectedExposureId: ""
    property string playheadTicks: "0"
    property string pendingRequestId: ""
    property string statusText: ""
    property string errorText: ""
    property string viewId: ""
    property string tooltip: qsTr("Exposure dope sheet")
    property string _serial: "0"

    signal selectionRequested(string layerId, string exposureId)
    signal exposureEditRequested(string requestId, string action, string layerId,
                                 var exposureIds, string startTicks, string endTicks)

    readonly property int layerCount: layers ? layers.length : 0
    readonly property bool gridValid: validateGrid()
    readonly property var selectedExposure: findExposure(selectedLayerId, selectedExposureId)

    objectName: "dopeSheet"
    implicitWidth: Tk.Theme.size.panelMinWidth * 2.5
    implicitHeight: Tk.Theme.size.controlLg * 4
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: errorText.length ? errorText
        : qsTr("%1 layers · exact frame boundaries · exposure edits are host requests").arg(layerCount)

    function compare(a, b) {
        let left = String(a), right = String(b)
        const ln = left.charAt(0) === "-", rn = right.charAt(0) === "-"
        if (ln !== rn) return ln ? -1 : 1
        if (ln) { left = left.slice(1); right = right.slice(1) }
        left = left.replace(/^0+(?=\d)/, ""); right = right.replace(/^0+(?=\d)/, "")
        const result = left.length !== right.length ? (left.length < right.length ? -1 : 1)
            : left === right ? 0 : left < right ? -1 : 1
        return ln ? -result : result
    }
    function validateGrid() {
        if (!frameBoundaries || frameBoundaries.length < 2) return false
        for (let i = 0; i < frameBoundaries.length; ++i) {
            if (typeof frameBoundaries[i] !== "string" || !/^-?[0-9]+$/.test(frameBoundaries[i])) return false
            if (i > 0 && compare(frameBoundaries[i - 1], frameBoundaries[i]) >= 0) return false
        }
        return true
    }
    function boundaryIndex(ticks) {
        for (let i = 0; i < frameBoundaries.length; ++i)
            if (String(frameBoundaries[i]) === String(ticks)) return i
        return -1
    }
    function findExposure(layerId, exposureId) {
        for (let i = 0; i < layerCount; ++i) {
            const layer = layers[i]
            if (String(layer.layerId) !== String(layerId)) continue
            for (let j = 0; j < (layer.exposures || []).length; ++j)
                if (String(layer.exposures[j].exposureId) === String(exposureId)) return layer.exposures[j]
        }
        return null
    }
    function request(action, layerId, ids, startTicks, endTicks) {
        if (!enabled || !gridValid || pendingRequestId.length > 0 || !layerId || !layerId.length) return false
        root._serial = String(Number(root._serial) + 1)
        const requestId = (viewId.length ? viewId : "dope-sheet") + ":" + action + ":" + root._serial
        exposureEditRequested(requestId, action, String(layerId), ids || [], String(startTicks), String(endTicks))
        return true
    }
    function insertHold() {
        const index = boundaryIndex(playheadTicks)
        if (index < 0 || index + 1 >= frameBoundaries.length) return false
        return request("insertHold", selectedLayerId, [], playheadTicks,
                       String(frameBoundaries[index + 1]))
    }
    function deleteSelected() {
        if (!selectedExposure) return false
        return request("delete", selectedLayerId, [selectedExposureId],
                       String(selectedExposure.startTicks), String(selectedExposure.endTicks))
    }
    function retimeSelected(direction) {
        if (!selectedExposure) return false
        const start = boundaryIndex(selectedExposure.startTicks), end = boundaryIndex(selectedExposure.endTicks)
        const target = start + direction, targetEnd = target + (end - start)
        if (start < 0 || end <= start || target < 0 || targetEnd >= frameBoundaries.length) return false
        return request("retime", selectedLayerId, [selectedExposureId],
                       String(frameBoundaries[target]), String(frameBoundaries[targetEnd]))
    }
    function select(layerId, exposureId) {
        if (!findExposure(layerId, exposureId)) return false
        selectedLayerId = String(layerId)
        selectedExposureId = String(exposureId)
        selectionRequested(selectedLayerId, selectedExposureId)
        return true
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.sm
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            width: parent.width
            Tk.Overline { text: qsTr("DOPE SHEET") }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Mono { text: root.playheadTicks + " ticks"; color: Tk.Theme.color.textMuted }
            Tk.Button { objectName: "dopeSheetInsertHold"; text: qsTr("Hold"); iconName: "plus"; enabled: root.selectedLayerId.length > 0 && root.pendingRequestId.length === 0; tooltip: qsTr("Request a held exposure at the playhead"); onClicked: root.insertHold() }
            Tk.IconButton { objectName: "dopeSheetDelete"; iconName: "trash-2"; enabled: root.selectedExposure !== null && root.pendingRequestId.length === 0; tooltip: qsTr("Request deletion of the selected exposure"); onClicked: root.deleteSelected() }
        }
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Button { text: qsTr("← Retiming"); small: true; enabled: root.selectedExposure !== null && root.pendingRequestId.length === 0; tooltip: qsTr("Move selected exposure one exact frame earlier"); onClicked: root.retimeSelected(-1) }
            Tk.Button { text: qsTr("Retiming →"); small: true; enabled: root.selectedExposure !== null && root.pendingRequestId.length === 0; tooltip: qsTr("Move selected exposure one exact frame later"); onClicked: root.retimeSelected(1) }
            Tk.Caption { text: root.pendingRequestId.length ? qsTr("Pending %1").arg(root.pendingRequestId) : root.statusText; color: root.errorText.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted; elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
        }
        Tk.Scroll {
            objectName: "dopeSheetRows"
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                width: parent.width
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Repeater {
                    model: root.layers || []
                    delegate: Tk.Flex {
                        id: layerRow
                        required property var modelData
                        readonly property string layerId: String(modelData.layerId)
                        objectName: "dopeSheetLayer_" + layerId
                        width: parent.width
                        height: Tk.Theme.size.controlLg
                        Tk.Flex.basis: Tk.Theme.size.controlLg
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.sm
                        Tk.Label { width: Tk.Theme.size.fieldWidth; text: String(modelData.label || layerRow.layerId); color: Tk.Theme.color.textMuted; elide: Text.ElideRight }
                        Item {
                            id: exposureAxis
                            width: parent.width
                            height: Tk.Theme.size.controlLg
                            implicitHeight: Tk.Theme.size.controlLg
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                            Rectangle { anchors.fill: parent; color: Tk.Theme.color.inputBg; radius: Tk.Theme.radius.sm }
                            Repeater {
                                model: modelData.exposures || []
                                delegate: T.AbstractButton {
                                    id: exposureButton
                                    required property var modelData
                                    readonly property int startIndex: root.boundaryIndex(modelData.startTicks)
                                    readonly property int endIndex: root.boundaryIndex(modelData.endTicks)
                                    readonly property string exposureId: String(modelData.exposureId)
                                    objectName: "dopeSheetExposure_" + exposureId
                                    visible: root.gridValid && startIndex >= 0 && endIndex > startIndex
                                    x: startIndex < 0 ? 0 : startIndex / (root.frameBoundaries.length - 1) * exposureAxis.width
                                    width: startIndex < 0 || endIndex < 0 ? 0 : Math.max(Tk.Theme.size.control, (endIndex - startIndex) / (root.frameBoundaries.length - 1) * exposureAxis.width)
                                    height: parent.height
                                    text: String(modelData.label || "")
                                    Accessible.role: Accessible.ListItem
                                    Accessible.name: qsTr("Exposure %1, artwork %2, %3 to %4 ticks%5").arg(exposureId).arg(String(modelData.artworkId || ""))
                                        .arg(String(modelData.startTicks)).arg(String(modelData.endTicks))
                                        .arg(modelData.held ? qsTr(", held") : "")
                                    Accessible.selected: root.selectedLayerId === layerRow.layerId && root.selectedExposureId === exposureId
                                    focusPolicy: Qt.TabFocus
                                    hoverEnabled: true
                                    onClicked: root.select(layerRow.layerId, exposureId)
                                    background: Rectangle {
                                        radius: Tk.Theme.radius.sm
                                        color: exposureButton.Accessible.selected ? Tk.Theme.color.selection : modelData.held ? Tk.Theme.color.panelAlt : Tk.Theme.color.controlBg
                                        border.width: Tk.Theme.size.border
                                        border.color: exposureButton.activeFocus ? Tk.Theme.color.focus : exposureButton.Accessible.selected ? Tk.Theme.color.accent : Tk.Theme.color.controlBorder
                                    }
                                    contentItem: Tk.Label { text: modelData.label || (modelData.held ? qsTr("HOLD") : qsTr("%1 f").arg(endIndex - startIndex)); color: Tk.Theme.color.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                }
                            }
                        }
                    }
                }
            }
        }
        Tk.Caption {
            objectName: "dopeSheetStatus"
            text: root.errorText.length ? root.errorText : !root.gridValid ? qsTr("A host frame grid is required to edit exposures.") : ""
            visible: text.length > 0
            color: root.errorText.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        }
    }
}
