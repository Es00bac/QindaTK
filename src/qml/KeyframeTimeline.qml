// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Exact-time key selection and edits over a host-provided visible boundary
// grid. AutomationModel owns key identity, validation, history and evaluation.
T.Control {
    id: root
    property Tk.AutomationModel model: null
    property var parameterIds: model ? model.parameterIds : []
    property var timeBoundaries: [] // strictly increasing signed decimal tick strings
    property string playheadTicks: "0"
    property string selectedParameterId: ""
    property string selectedPointId: ""
    property string statusText: ""
    property string tooltip: qsTr("Keyframe timeline")
    property bool editingEnabled: true
    property string _serial: "0"

    signal selectionRequested(string parameterId, string pointId)
    signal editRejected(string reason)
    signal editCommitted(string transactionId)

    objectName: "keyframeTimeline"
    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.controlLg * 4
    activeFocusOnTab: enabled
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Pane
    Accessible.name: root.tooltip
    Accessible.description: root.statusText.length ? root.statusText
        : qsTr("%1 animated properties · %2 exact time boundaries").arg(parameterIds.length).arg(timeBoundaries.length)

    function decimalCompare(a, b) {
        let left = String(a), right = String(b)
        const ln = left.charAt(0) === "-", rn = right.charAt(0) === "-"
        if (ln !== rn) return ln ? -1 : 1
        if (ln) { left = left.slice(1); right = right.slice(1) }
        left = left.replace(/^0+(?=\d)/, ""); right = right.replace(/^0+(?=\d)/, "")
        const result = left.length !== right.length ? (left.length < right.length ? -1 : 1)
            : left === right ? 0 : left < right ? -1 : 1
        return ln ? -result : result
    }
    function gridValid() {
        if (!timeBoundaries || timeBoundaries.length < 2) return false
        for (let i = 0; i < timeBoundaries.length; ++i) {
            if (typeof timeBoundaries[i] !== "string" || !/^-?[0-9]+$/.test(timeBoundaries[i])) return false
            if (i && decimalCompare(timeBoundaries[i - 1], timeBoundaries[i]) >= 0) return false
        }
        return true
    }
    function boundaryIndex(ticks) {
        for (let i = 0; i < timeBoundaries.length; ++i)
            if (String(timeBoundaries[i]) === String(ticks)) return i
        return -1
    }
    function pointIndex(parameterId, pointId) {
        if (!root.model) return -1
        const points = root.model.points(parameterId)
        for (let i = 0; i < points.length; ++i)
            if (String(points[i].id) === String(pointId)) return i
        return -1
    }
    function pointData(parameterId, pointId) {
        if (!root.model || pointId.length === 0) return ({})
        void root.model.revision
        return root.model.pointData(parameterId, pointId)
    }
    function fractionForTicks(ticks) {
        const index = boundaryIndex(ticks)
        return index < 0 ? -1 : index / (timeBoundaries.length - 1)
    }
    function choose(parameterId, pointId) {
        if (!root.model || !root.model.parameterIds.includes(parameterId) || pointIndex(parameterId, pointId) < 0) return false
        selectedParameterId = parameterId
        selectedPointId = pointId
        forceActiveFocus(Qt.TabFocusReason)
        selectionRequested(parameterId, pointId)
        return true
    }
    function applyEdit(request, input) {
        if (!root.enabled || !root.editingEnabled || !root.model) return false
        const result = root.model.applyEdit(request, input || ({}))
        if (!result || result.ok !== true) {
            root.statusText = result && result.error ? result.error : qsTr("Keyframe edit was rejected")
            editRejected(root.statusText)
            return false
        }
        root.statusText = qsTr("%1 · revision %2").arg(request.kind).arg(result.revision)
        if (result.transactionId) editCommitted(String(result.transactionId))
        return true
    }
    function moveSelectedKey(direction) {
        if (!gridValid() || selectedPointId.length === 0) return false
        const point = pointData(selectedParameterId, selectedPointId)
        const here = boundaryIndex(String(point.ticks))
        const target = here + direction
        if (here < 0 || target < 0 || target >= timeBoundaries.length) return false
        return applyEdit({kind: "setPoint", parameterId: selectedParameterId, anchorPointId: selectedPointId},
                         {ticks: String(timeBoundaries[target])})
    }
    function addKey(parameterId, ticks, value) {
        if (boundaryIndex(ticks) < 0) return false
        const parameter = root.model ? root.model.parameterData(parameterId) : ({})
        const initial = value === undefined ? Number(parameter.default) : Number(value)
        return applyEdit({kind: "addPoint", parameterId: parameterId},
                         {ticks: String(ticks), value: initial, interpolation: String(parameter.interpolation || "linear")})
    }
    function addAtPlayhead() {
        if (!root.model || !root.parameterIds.includes(root.selectedParameterId)) return false
        const parameter = root.model.parameterData(root.selectedParameterId)
        return addKey(root.selectedParameterId, root.playheadTicks, Number(parameter.default))
    }
    function deleteSelectedKey() {
        if (selectedPointId.length === 0) return false
        return applyEdit({kind: "removePoints", parameterId: selectedParameterId,
                          pointIds: [selectedPointId]}, {})
    }
    function setSelectedInterpolation(interpolation) {
        if (selectedPointId.length === 0) return false
        return applyEdit({kind: "setInterpolation", parameterId: selectedParameterId,
                          pointIds: [selectedPointId]}, {interpolation: interpolation})
    }

    background: Rectangle {
        color: Tk.Theme.color.panel
        border.width: Tk.Theme.size.border
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.controlBorder
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.sm
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            width: parent.width
            Tk.Overline { text: qsTr("KEYFRAMES") }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Mono { text: root.playheadTicks + " ticks"; color: Tk.Theme.color.textMuted }
            Tk.Button {
                objectName: "keyframeAddAtPlayhead"
                text: qsTr("Add key")
                iconName: "key-round"
                enabled: root.model && root.editingEnabled && root.gridValid()
                tooltip: qsTr("Add a key at the exact host playhead time")
                onClicked: root.addAtPlayhead()
            }
        }
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Segmented {
                objectName: "keyframeParameterPicker"
                model: root.parameterIds.map(function(id) {
                    const descriptor = root.model ? root.model.parameterData(id) : ({})
                    return {text: String(descriptor.name || id), value: String(id), tooltip: String(id)}
                })
                currentIndex: Math.max(0, root.parameterIds.indexOf(root.selectedParameterId))
                tooltip: qsTr("Animated property")
                onActivated: function(index) { root.selectedParameterId = String(currentValue) }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button { text: qsTr("Step"); small: true; tooltip: qsTr("Use held value until the next key"); enabled: root.selectedPointId.length > 0; onClicked: root.setSelectedInterpolation("step") }
            Tk.Button { text: qsTr("Linear"); small: true; tooltip: qsTr("Use deterministic linear interpolation"); enabled: root.selectedPointId.length > 0; onClicked: root.setSelectedInterpolation("linear") }
            Tk.Button { text: qsTr("Cubic"); small: true; tooltip: qsTr("Use the WP-043 cubic curve evaluator"); enabled: root.selectedPointId.length > 0; onClicked: root.setSelectedInterpolation("cubic") }
            Tk.IconButton { objectName: "keyframeDelete"; iconName: "trash-2"; enabled: root.selectedPointId.length > 0; tooltip: qsTr("Delete selected key"); onClicked: root.deleteSelectedKey() }
        }
        Tk.Flex {
            objectName: "keyframeTracks"
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Repeater {
                model: root.parameterIds
                delegate: Tk.Flex {
                    id: lane
                    required property var modelData
                    readonly property string parameterId: String(modelData)
                    readonly property var keyRows: {
                        if (!root.model) return []
                        void root.model.revision
                        return root.model.points(parameterId)
                    }
                    objectName: "keyframeLane_" + parameterId
                    width: parent.width
                    height: Tk.Theme.size.controlLg
                    Tk.Flex.basis: Tk.Theme.size.controlLg
                    direction: Tk.Flex.Row
                    gap: Tk.Theme.space.sm
                    Tk.Label {
                        width: Tk.Theme.size.fieldWidth
                        text: String(root.model ? root.model.parameterData(lane.parameterId).name || lane.parameterId : lane.parameterId)
                        color: Tk.Theme.color.textMuted
                        elide: Text.ElideRight
                    }
                    Item {
                        id: keyAxis
                        objectName: "keyframeAxis_" + lane.parameterId
                        width: parent.width
                        height: Tk.Theme.size.controlLg
                        implicitHeight: Tk.Theme.size.controlLg
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        Rectangle { anchors.fill: parent; color: Tk.Theme.color.inputBg; radius: Tk.Theme.radius.sm }
                        Repeater {
                            model: lane.keyRows
                            delegate: T.AbstractButton {
                                id: keyButton
                                required property var modelData
                                readonly property string keyId: String(modelData.id)
                                readonly property real fraction: root.fractionForTicks(String(modelData.ticks))
                                objectName: "keyframeKey_" + keyId
                                visible: fraction >= 0
                                x: Math.max(0, Math.min(keyAxis.width - width, fraction * keyAxis.width - width / 2))
                                width: Tk.Theme.size.control
                                height: Tk.Theme.size.control
                                text: "◆"
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: qsTr("Key %1 at %2 ticks, value %3").arg(keyId).arg(String(modelData.ticks)).arg(String(modelData.value))
                                Accessible.selected: root.selectedPointId === keyId && root.selectedParameterId === lane.parameterId
                                Accessible.description: qsTr("%1 interpolation. Left and Right move by exact host grid boundaries.").arg(String(modelData.interpolation))
                                focusPolicy: Qt.TabFocus
                                hoverEnabled: true
                                onClicked: root.choose(lane.parameterId, keyId)
                                background: Rectangle {
                                    radius: Tk.Theme.radius.full
                                    color: keyButton.Accessible.selected ? Tk.Theme.color.accent : keyButton.hovered ? Tk.Theme.color.controlHoverBg : Tk.Theme.color.panelAlt
                                    border.width: Tk.Theme.size.border
                                    border.color: keyButton.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.controlBorder
                                }
                                contentItem: Tk.Label { text: keyButton.text; color: keyButton.Accessible.selected ? Tk.Theme.color.accentText : Tk.Theme.color.text; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                Tk.ToolTip { text: keyButton.Accessible.name; visible: keyButton.hovered }
                            }
                        }
                    }
                }
            }
        }
        Tk.Caption {
            objectName: "keyframeStatus"
            visible: root.statusText.length > 0 || !root.gridValid()
            text: root.statusText.length ? root.statusText : qsTr("Waiting for at least two ordered exact-time boundaries.")
            color: root.statusText.length && root.statusText.indexOf(qsTr("rejected")) >= 0 ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            elide: Text.ElideRight
        }
    }
    Keys.onPressed: function(event) {
        if (!root.enabled || !root.editingEnabled) return
        if (event.key === Qt.Key_Left && root.moveSelectedKey(-1)) event.accepted = true
        else if (event.key === Qt.Key_Right && root.moveSelectedKey(1)) event.accepted = true
        else if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) && root.deleteSelectedKey()) event.accepted = true
    }
}
