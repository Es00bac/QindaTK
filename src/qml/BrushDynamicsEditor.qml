// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.Box {
    id: control
    objectName: "brushDynamicsEditor"
    property string brushId: ""
    property var parameters: [] // [{id,label,value,minimum,maximum,step,unit}]
    property var workingParameters: []
    // Host-rendered sample stroke for the working values (CAN-16), e.g.
    // TiledRasterDocument.brushPreviewDataUrl(preset, color, width, height).
    property url previewSource: ""
    property bool editing: false
    property string requestState: "idle"
    property string requestMessage: ""
    property string validationError: ""
    property string tooltip: qsTr("Brush dynamics parameters")
    property string lastRequestId: ""
    property int _requestSerial: 0
    readonly property int maximumParameters: 16

    signal dynamicsEditRequested(string brushId, string requestId, string phase, var parameters)

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: validationError || requestMessage

    function normalise(source) {
        if (!Array.isArray(source) || source.length > maximumParameters) {
            validationError = qsTr("Supply at most %1 brush parameters.").arg(maximumParameters)
            return []
        }
        const ids = Object.create(null)
        const rows = []
        for (let i = 0; i < source.length; ++i) {
            const item = source[i] || {}
            const id = String(item.id || "")
            const minimum = Number(item.minimum), maximum = Number(item.maximum)
            const value = Number(item.value)
            if (!id || ids[id] || !Number.isFinite(minimum) || !Number.isFinite(maximum)
                    || !Number.isFinite(value) || minimum >= maximum
                    || Math.abs(minimum) > 1000000 || Math.abs(maximum) > 1000000) {
                validationError = qsTr("A brush parameter has an invalid ID or numeric range.")
                return []
            }
            ids[id] = true
            rows.push(Object.assign({}, item, {id: id, minimum: minimum, maximum: maximum,
                value: Math.max(minimum, Math.min(maximum, value)),
                step: Math.max(0.0001, Math.min(1000000, Math.abs(Number(item.step) || 1)))}))
        }
        validationError = ""
        return rows
    }
    function setParameterValue(parameterId, value) {
        const rows = normalise(workingParameters)
        const numeric = Number(value)
        if (!Number.isFinite(numeric)) return false
        const index = rows.findIndex(function(row) { return row.id === parameterId })
        if (index < 0) return false
        rows[index].value = Math.max(rows[index].minimum, Math.min(rows[index].maximum, numeric))
        workingParameters = rows
        return true
    }
    function request(phase) {
        if (!brushId || validationError || !["begin", "preview", "commit", "cancel"].includes(phase)) return false
        if ((phase === "preview" || phase === "commit" || phase === "cancel") && !editing) return false
        const requestId = brushId + ":dynamics:" + (++_requestSerial)
        lastRequestId = requestId; requestState = "pending"; requestMessage = ""
        dynamicsEditRequested(brushId, requestId, phase,
            JSON.parse(JSON.stringify(normalise(workingParameters))))
        if (phase === "begin") editing = true
        if (phase === "commit" || phase === "cancel") editing = false
        return requestId
    }
    function beginEdit() { return request("begin") }
    function previewEdit() { return request("preview") }
    function commitEdit() { return request("commit") }
    function cancelEdit() {
        if (!editing) return false
        workingParameters = normalise(parameters)
        return request("cancel")
    }
    function acknowledge(requestId, state, message) {
        if (requestId !== lastRequestId || requestState !== "pending") return false
        requestState = state; requestMessage = message || ""
        return true
    }
    onParametersChanged: if (!editing) workingParameters = normalise(parameters)
    Component.onCompleted: workingParameters = normalise(parameters)

    Tk.Flex {
        width: parent.width
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            Tk.Overline { title: qsTr("Brush dynamics"); wide: true }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Badge { text: control.brushId || qsTr("No brush"); variant: "info" }
        }
        ListView {
            id: parameterList
            objectName: "brushDynamicsParameters"
            width: parent.width
            height: Math.min(Tk.Theme.size.control * 5,
                             Math.max(Tk.Theme.size.control, control.workingParameters.length * Tk.Theme.size.control))
            implicitHeight: height
            clip: true
            model: control.workingParameters
            delegate: Tk.Flex {
                required property var modelData
                required property int index
                width: parameterList.width
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Tk.Label { text: String(modelData.label || modelData.id); width: Tk.Theme.size.fieldWidth; elide: Text.ElideRight }
                Tk.Slider {
                    objectName: "brushDynamicsSlider_" + String(modelData.id)
                    Tk.Flex.grow: 1
                    from: Number(modelData.minimum); to: Number(modelData.maximum)
                    stepSize: Number(modelData.step); value: Number(modelData.value)
                    showValue: true; decimals: 2; suffix: modelData.unit ? " " + modelData.unit : ""
                    tooltip: String(modelData.label || modelData.id)
                    onValueModified: function(nextValue) {
                        if (!control.editing) control.beginEdit()
                        if (control.setParameterValue(String(modelData.id), nextValue)) control.previewEdit()
                    }
                }
            }
        }
        Rectangle {
            objectName: "brushDynamicsPreviewFrame"
            visible: String(control.previewSource).length > 0
            width: parent.width
            height: Tk.Theme.size.control * 2
            color: Tk.Theme.color.canvas
            radius: Tk.Theme.radius.sm
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.controlBorder
            Image {
                objectName: "brushDynamicsPreview"
                anchors.fill: parent
                anchors.margins: Tk.Theme.size.border
                source: control.previewSource
                fillMode: Image.PreserveAspectFit
                cache: false
                Accessible.role: Accessible.Graphic
                Accessible.name: qsTr("Sample stroke preview for %1").arg(control.brushId)
            }
        }
        Tk.Caption { visible: control.validationError.length > 0; text: control.validationError; color: Tk.Theme.color.danger }
        Tk.Flex {
            width: parent.width
            Tk.Button { text: control.editing ? qsTr("Preview") : qsTr("Begin"); tooltip: qsTr("Request a host preview of bounded dynamics parameters"); enabled: !control.validationError.length; onClicked: control.editing ? control.previewEdit() : control.beginEdit() }
            Tk.Button { text: qsTr("Commit"); tooltip: qsTr("Request a host commit for this edit"); enabled: control.editing; variant: "accent"; onClicked: control.commitEdit() }
            Tk.Button { text: qsTr("Cancel"); tooltip: qsTr("Cancel the active host edit"); enabled: control.editing; onClicked: control.cancelEdit() }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption { objectName: "brushDynamicsStatus"; text: control.requestState + (control.requestMessage.length ? " · " + control.requestMessage : ""); color: control.requestState === "rejected" ? Tk.Theme.color.warning : Tk.Theme.color.textMuted }
        }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.hovered }
}
