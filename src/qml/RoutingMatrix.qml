// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (WP-062, docs/controls.md, docs/audio-mixer.md): ports and
// connections are immutable snapshots by stable ID. A selection proposes one
// connection; only the owner validates cycles, layouts and availability and
// reports acceptance or the reason for a refusal. Bound (`mixer`): sources,
// destinations and connections come from Tk.AudioMixerModel and each request
// is AudioMixerModel.setOutput, answered at once. A connection to a port that
// is no longer offered (its device went away) stays shown as unavailable; it
// is never displayed as another port or as disconnected.
// Selection is a stable input ID (`selectedInputId`, `inputSelected`): bound
// rows come from mixer.routingInputs, which moves and updates rows in place,
// so an accepted route (or a reorder) keeps the current row, its delegate and
// a focused destination editor; host arrays re-find the row by ID. Keys:
// Up/Down, Home/End and Page Up/Down move the selection; Return or Enter
// focuses its destination, whose Up/Down propose the neighbouring route;
// Escape goes back to the rows.
T.Control {
    id: matrix
    objectName: "routingMatrix"
    property var mixer: null // Tk.AudioMixerModel
    property var inputPorts: [] // [{id,label,layout,available}]
    property var outputPorts: [] // [{id,label,layout,available,reason}]
    property var connections: [] // [{inputId,outputId}]
    property string selectedInputId: ""
    property string tooltip: qsTr("Audio routing")
    property string _serial: "0"
    property string _pendingRequestId: ""
    property string _routeState: "idle"
    property string _routeMessage: ""
    readonly property string routeState: _routeState
    readonly property string routeMessage: _routeMessage
    readonly property alias inputList: routes
    readonly property var inputModel: mixer ? mixer.routingInputs : inputPorts
    readonly property int inputCount: mixer ? mixer.routingInputs.count : (inputPorts ? inputPorts.length : 0)
    readonly property var routeOptions: [{id: "", label: qsTr("Disconnected"), available: true}]
            .concat(outputPorts || [])

    signal routeRequested(string requestId, string inputPortId, string outputPortId)
    signal routeAcknowledged(string requestId, string state, string message)
    signal inputSelected(string inputId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: Tk.Theme.space.sm
    Accessible.role: Accessible.List
    Accessible.name: tooltip
    Accessible.description: qsTr("%1 input routes to %2 output ports. %3")
            .arg(inputCount).arg(outputPorts ? outputPorts.length : 0).arg(_routeMessage)

    Binding { target: matrix; property: "inputPorts"; when: matrix.mixer !== null; value: matrix.mixer ? matrix.mixer.routingSources : [] }
    Binding { target: matrix; property: "outputPorts"; when: matrix.mixer !== null; value: matrix.mixer ? matrix.mixer.routingDestinations : [] }
    Binding { target: matrix; property: "connections"; when: matrix.mixer !== null; value: matrix.mixer ? matrix.mixer.connections : [] }

    function inputIdAt(row) {
        if (mixer) return mixer.routingInputs.channelAt(row)
        return inputPorts && row >= 0 && row < inputPorts.length ? String(inputPorts[row].id) : ""
    }
    function rowOf(inputId) {
        if (mixer) return mixer.routingInputs.indexOf(String(inputId))
        for (let i = 0; inputPorts && i < inputPorts.length; ++i)
            if (String(inputPorts[i].id) === String(inputId)) return i
        return -1
    }
    function selectInput(inputId) {
        const row = rowOf(inputId)
        if (row < 0) return false
        routes.currentIndex = row
        if (selectedInputId !== String(inputId)) {
            selectedInputId = String(inputId)
            inputSelected(selectedInputId)
        }
        return true
    }
    function stepInput(delta) {
        if (inputCount === 0) return
        const row = rowOf(selectedInputId)
        selectInput(inputIdAt(row < 0 ? 0 : Math.max(0, Math.min(inputCount - 1, row + delta))))
    }
    // AGENT-GUARD: after the rows change the current row is re-found by ID,
    // never kept by index (a host array replaced, a row inserted above).
    function followSelection() {
        const row = rowOf(selectedInputId)
        if (row >= 0 && routes.currentIndex !== row) routes.currentIndex = row
    }
    // Focuses the destination editor of an input (keyboard route editing).
    function editInput(inputId) {
        if (!selectInput(inputId)) return false
        routes.positionViewAtIndex(routes.currentIndex, ListView.Contain)
        const item = routes.itemAtIndex(routes.currentIndex)
        if (!item) return false
        item.edit()
        return true
    }
    function find(list, id) {
        for (let i = 0; list && i < list.length; ++i)
            if (String(list[i].id) === String(id)) return list[i]
        return null
    }
    function outputIdFor(inputId) {
        for (let i = 0; i < connections.length; ++i)
            if (String(connections[i].inputId) === String(inputId)) return String(connections[i].outputId || "")
        return ""
    }
    // The destinations offered to one input: never itself, and its current
    // destination even when that port is not offered any more.
    function optionsFor(inputId) {
        const options = routeOptions.filter(function(option) { return String(option.id) !== String(inputId) })
        const current = outputIdFor(inputId)
        if (current.length && !find(options, current))
            options.push({id: current, label: qsTr("%1 (unavailable)").arg(current), available: false})
        return options
    }
    function indexIn(options, id) {
        for (let i = 0; i < options.length; ++i)
            if (String(options[i].id) === String(id)) return i
        return 0
    }
    function outputIndexFor(inputId) { return indexIn(optionsFor(inputId), outputIdFor(inputId)) }
    function refuse(requestId, message) {
        _pendingRequestId = ""
        _routeState = "rejected"
        _routeMessage = message
        routeAcknowledged(requestId, "rejected", message)
        return requestId
    }
    function requestRoute(inputId, outputId) {
        const input = mixer ? find(mixer.routingSources, inputId) : find(inputPorts, inputId)
        if (!input) return ""
        const target = String(outputId || "")
        const output = target.length ? find(outputPorts, target) : null
        _serial = String(Number(_serial) + 1)
        const requestId = "route:" + String(inputId) + ":" + _serial
        if (input.available === false)
            return refuse(requestId, qsTr("%1 is not available").arg(input.label || input.id))
        if (target.length && !output)
            return refuse(requestId, qsTr("%1 is not a destination").arg(target))
        if (output && output.available === false)
            return refuse(requestId, String(output.reason || qsTr("%1 is not available").arg(output.label || output.id)))
        _pendingRequestId = requestId
        _routeState = "pending"
        _routeMessage = qsTr("Waiting for host routing validation")
        routeRequested(requestId, String(inputId), target)
        if (mixer) {
            const result = mixer.setOutput(String(inputId), target)
            acknowledgeRoute(requestId, result.ok ? "accepted" : "rejected",
                             result.ok ? qsTr("%1 → %2").arg(input.label).arg(output ? output.label : qsTr("disconnected"))
                                       : String(result.error))
        }
        return requestId
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
        anchors.margins: matrix.padding
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            width: parent.width
            Tk.Overline { title: qsTr("INPUT → DESTINATION") }
            Tk.Spacer { }
            Tk.Caption { text: qsTr("%1 inputs").arg(matrix.inputCount); color: Tk.Theme.color.textMuted }
        }
        Tk.Caption {
            objectName: "routingMatrixStatus"
            visible: matrix._routeMessage.length > 0
            width: parent.width
            wrapMode: Text.Wrap
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
            model: matrix.inputModel
            reuseItems: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false // the keys below move the selection by ID
            activeFocusOnTab: true
            currentIndex: -1
            onCountChanged: Qt.callLater(matrix.followSelection)
            onModelChanged: Qt.callLater(matrix.followSelection)
            Keys.onPressed: (event) => {
                const page = Math.max(1, Math.floor(routes.height / (Tk.Theme.size.control + Tk.Theme.space.xs)))
                if (event.key === Qt.Key_Up) matrix.stepInput(-1)
                else if (event.key === Qt.Key_Down) matrix.stepInput(1)
                else if (event.key === Qt.Key_PageUp) matrix.stepInput(-page)
                else if (event.key === Qt.Key_PageDown) matrix.stepInput(page)
                else if (event.key === Qt.Key_Home) matrix.selectInput(matrix.inputIdAt(0))
                else if (event.key === Qt.Key_End) matrix.selectInput(matrix.inputIdAt(matrix.inputCount - 1))
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                    matrix.editInput(matrix.selectedInputId.length ? matrix.selectedInputId : matrix.inputIdAt(0))
                else return
                if (routes.currentIndex >= 0) routes.positionViewAtIndex(routes.currentIndex, ListView.Contain)
                event.accepted = true
            }
            highlightMoveDuration: Tk.Theme.motion.instant
            highlight: Rectangle { color: Tk.Theme.color.selection; radius: Tk.Theme.radius.sm }
            Accessible.role: Accessible.List
            Accessible.name: matrix.Accessible.name
            // AGENT-GUARD: the row is a FocusScope. When the current row moves
            // (a reorder), ListView re-focuses it; in a plain Item that took
            // focus from an open destination editor. Inside the scope the
            // editor keeps its focus until Escape or another row is current.
            delegate: FocusScope {
                id: row
                required property int index
                required property var model
                onFocusChanged: if (!focus) destination.focus = false
                ListView.onReused: destination.focus = false
                // Bound rows are roles of mixer.routingInputs; host rows are array entries.
                readonly property var inputPort: matrix.mixer
                    ? ({id: String(row.model.portId || ""), label: String(row.model.label || ""),
                        layout: String(row.model.layout || ""), available: row.model.available !== false})
                    : (row.model.modelData || ({}))
                function edit() { destination.forceActiveFocus() }
                readonly property var options: matrix.optionsFor(row.inputPort.id)
                objectName: "routingMatrixRow"
                width: routes.width
                height: Tk.Theme.size.control + Tk.Theme.space.xs
                MouseArea {
                    anchors.fill: parent
                    onClicked: { matrix.selectInput(row.inputPort.id); routes.forceActiveFocus() }
                }
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
                    Tk.Caption {
                        text: String(row.inputPort.layout || "")
                        color: Tk.Theme.color.textMuted
                        verticalAlignment: Text.AlignVCenter
                    }
                    Tk.ComboBox {
                        id: destination
                        objectName: "routingDestination"
                        width: parent.width - Tk.Theme.size.fieldWidth - Tk.Theme.space.sm
                        Tk.Flex.grow: 1
                        Tk.Flex.shrink: 1
                        model: row.options
                        textRole: "label"
                        enabled: row.inputPort.available !== false
                        tooltip: qsTr("Destination for %1").arg(row.inputPort.label || row.inputPort.id)
                        onActivated: function(index) {
                            matrix.requestRoute(row.inputPort.id, row.options[index].id)
                            currentIndex = matrix.outputIndexFor(row.inputPort.id)
                        }
                        onActiveFocusChanged: if (activeFocus) matrix.selectInput(row.inputPort.id)
                        Keys.onEscapePressed: (event) => {
                            if (destination.popup.visible) { event.accepted = false; return }
                            destination.focus = false
                            routes.forceActiveFocus()
                            event.accepted = true
                        }
                    }
                }
                // AGENT-GUARD: the combo box shows the owner's connection, not
                // the last pick: a refused route snaps back.
                Binding { target: destination; property: "currentIndex"; value: matrix.indexIn(row.options, matrix.outputIdFor(row.inputPort.id)) }
                Accessible.role: Accessible.ListItem
                Accessible.name: String(inputPort.label || inputPort.id || "")
                Accessible.description: qsTr("%1, to %2").arg(String(inputPort.layout || ""))
                        .arg(row.options.length ? row.options[matrix.indexIn(row.options, matrix.outputIdFor(inputPort.id))].label : "")
            }
        }
        Tk.EmptyState {
            objectName: "routingMatrixEmpty"
            visible: matrix.inputCount === 0
            width: parent.width
            height: Tk.Theme.size.panelMinHeight
            title: qsTr("NO INPUT PORTS")
            text: qsTr("The host has not supplied any routing inputs.")
        }
    }
    Tk.ToolTip { visible: matrix.hovered && matrix.tooltip.length > 0; text: matrix.tooltip }
}
