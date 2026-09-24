// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: ports and current connections are immutable host snapshots.
// Selection emits a proposed connection by stable input/output IDs; only the
// host validates cycles/layout/availability and reports acceptance or error.
T.Control {
    id: matrix
    objectName: "routingMatrix"
    property var inputPorts: [] // [{id,label,layout,available}]
    property var outputPorts: [] // [{id,label,layout,available}]
    property var connections: [] // [{inputId,outputId}]
    property string tooltip: qsTr("Audio routing")
    property string _serial: "0"
    property string _pendingRequestId: ""
    property string _routeState: "idle"
    property string _routeMessage: ""
    readonly property string routeState: _routeState
    readonly property string routeMessage: _routeMessage
    readonly property alias inputList: routes
    readonly property var routeOptions: [{id: "", label: qsTr("Disconnected"), available: true}]
            .concat(outputPorts || [])

    signal routeRequested(string requestId, string inputPortId, string outputPortId)
    signal routeAcknowledged(string requestId, string state, string message)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.List
    Accessible.name: tooltip
    Accessible.description: qsTr("%1 input routes to %2 output ports. %3")
            .arg(inputPorts ? inputPorts.length : 0).arg(outputPorts ? outputPorts.length : 0).arg(_routeMessage)

    function outputIdFor(inputId) {
        for (let i = 0; i < connections.length; ++i)
            if (String(connections[i].inputId) === String(inputId)) return String(connections[i].outputId || "")
        return ""
    }
    function outputIndexFor(inputId) {
        const id = outputIdFor(inputId)
        for (let i = 0; i < routeOptions.length; ++i)
            if (String(routeOptions[i].id) === id) return i
        return 0
    }
    function requestRoute(inputId, outputId) {
        let input = null
        let output = null
        for (let i = 0; i < inputPorts.length; ++i)
            if (String(inputPorts[i].id) === String(inputId)) input = inputPorts[i]
        if (String(outputId).length > 0)
            for (let i = 0; i < outputPorts.length; ++i)
                if (String(outputPorts[i].id) === String(outputId)) output = outputPorts[i]
        if (!input || input.available === false || (String(outputId).length > 0 && (!output || output.available === false))) return ""
        _serial = String(Number(_serial) + 1)
        _pendingRequestId = "route:" + String(inputId) + ":" + _serial
        _routeState = "pending"
        _routeMessage = qsTr("Waiting for host routing validation")
        routeRequested(_pendingRequestId, String(inputId), String(outputId || ""))
        return _pendingRequestId
    }
    function acknowledgeRoute(requestId, state, message) {
        if (!requestId || requestId !== _pendingRequestId || _routeState !== "pending") return false
        const outcome = String(state)
        if (["accepted", "rejected", "error"].indexOf(outcome) < 0) return false
        _pendingRequestId = ""
        _routeState = outcome
        _routeMessage = String(message || (outcome === "accepted" ? qsTr("Route accepted")
                                                                      : qsTr("Route %1").arg(outcome)))
        routeAcknowledged(String(requestId), outcome, _routeMessage)
        return true
    }

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: matrix.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            width: parent.width
            Tk.Overline { title: qsTr("INPUT → DESTINATION") }
            Tk.Spacer { }
            Tk.Caption { text: qsTr("%1 inputs").arg(matrix.inputPorts ? matrix.inputPorts.length : 0); color: Tk.Theme.color.textMuted }
        }
        Tk.Caption {
            objectName: "routingMatrixStatus"
            visible: matrix._routeMessage.length > 0
            text: matrix._routeMessage
            color: matrix._routeState === "rejected" || matrix._routeState === "error"
                   ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        }
        ListView {
            id: routes
            objectName: "routingMatrixInputs"
            width: parent.width
            implicitHeight: Tk.Theme.size.panelMinHeight
            Tk.Flex.grow: 1
            clip: true
            model: matrix.inputPorts
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: matrix.Accessible.name
            delegate: Item {
                id: row
                required property int index
                required property var modelData
                property var inputPort: modelData || ({})
                objectName: "routingMatrixRow"
                width: routes.width
                height: Tk.Theme.size.control + Tk.Theme.space.xs
                Tk.Flex {
                    anchors.fill: parent
                    gap: Tk.Theme.space.sm
                    Tk.Label {
                        objectName: "routingInputLabel"
                        width: Tk.Theme.size.fieldWidth
                        text: String(row.inputPort.label || row.inputPort.id || "")
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        Accessible.name: text
                    }
                    Tk.ComboBox {
                        objectName: "routingDestination"
                        width: parent.width - Tk.Theme.size.fieldWidth - Tk.Theme.space.sm
                        Tk.Flex.grow: 1
                        model: matrix.routeOptions
                        textRole: "label"
                        currentIndex: matrix.outputIndexFor(row.inputPort.id)
                        enabled: row.inputPort.available !== false
                        tooltip: qsTr("Destination for input %1").arg(row.inputPort.label || row.inputPort.id)
                        onActivated: function(index) {
                            const destination = matrix.routeOptions[index]
                            matrix.requestRoute(row.inputPort.id, destination.id)
                        }
                    }
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: String(inputPort.label || inputPort.id || "")
                Accessible.description: String(inputPort.layout || "")
            }
        }
        Tk.EmptyState {
            objectName: "routingMatrixEmpty"
            visible: !matrix.inputPorts || matrix.inputPorts.length === 0
            width: parent.width
            height: Tk.Theme.size.panelMinHeight
            title: qsTr("NO INPUT PORTS")
            text: qsTr("The host has not supplied any routing inputs.")
        }
    }
    Tk.ToolTip { visible: matrix.hovered && matrix.tooltip.length > 0; text: matrix.tooltip }
}
