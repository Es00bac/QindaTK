// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (WP-062, docs/controls.md, docs/audio-mixer.md): the routing
// part of a channel strip, its output and its sends, over a
// Tk.AudioMixerModel. Every change is one mixer operation by stable channel or
// send ID; a refused change shows the mixer's explanation (a feedback loop,
// an unavailable port) and nothing moves. Send levels are
// AudioParameterBinding transactions through Tk.Knob. objectNames:
// "channelOutput", "channelSendRow", "channelSendLevel",
// "channelSendPreFader", "channelSendRemove", "channelAddSend",
// "channelRoutingStatus".
T.Control {
    id: panel
    objectName: "channelRoutingPanel"
    property var mixer: null // Tk.AudioMixerModel
    property string channelId: ""
    property string output: ""
    property var sends: [] // [{id, target, targetName, preFader, parameterId}] from the strip row
    property string tooltip: qsTr("Channel routing")
    property string status: ""
    property bool statusIsError: false
    readonly property bool isMaster: channelId === "master"
    // `revision` makes the list follow every mixer change.
    readonly property var destinations: mixer && channelId.length ? destinationsFor(mixer.revision) : []
    readonly property var sendOptions: {
        const options = [{id: "", label: qsTr("Add send…")}]
        if (!mixer || isMaster) return options
        const used = (sends || []).map(function(send) { return send.target })
        const buses = mixer.buses
        for (let i = 0; i < buses.length; ++i)
            if (buses[i].id !== channelId && used.indexOf(buses[i].id) < 0)
                options.push({id: buses[i].id, label: buses[i].name})
        return options
    }

    implicitWidth: Tk.Theme.size.fieldWidth
    implicitHeight: body.implicitHeight + topPadding + bottomPadding
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function destinationsFor(revision) { return mixer.destinations(channelId) }
    function outputIndex() {
        for (let i = 0; i < destinations.length; ++i)
            if (String(destinations[i].id) === output) return i
        return 0
    }
    function show(result, success) {
        statusIsError = !result.ok
        status = result.ok ? success : String(result.error)
        return !!result.ok
    }
    function route(destinationId) {
        if (!mixer) return false
        return show(mixer.setOutput(channelId, destinationId), qsTr("Routed to %1").arg(labelOf(destinationId)))
    }
    function labelOf(id) {
        for (let i = 0; i < destinations.length; ++i)
            if (String(destinations[i].id) === id) return destinations[i].label
        return id
    }
    function addSend(busId) {
        if (!mixer || !busId) return false
        return show(mixer.addSend(channelId, busId, false), qsTr("Sends to %1").arg(mixer.channelName(busId)))
    }
    function removeSend(sendId) {
        return mixer ? show(mixer.removeSend(sendId), qsTr("Send removed")) : false
    }
    function setPreFader(sendId, preFader) {
        return mixer ? show(mixer.setSendPreFader(sendId, preFader), preFader ? qsTr("Pre-fader send") : qsTr("Post-fader send"))
                     : false
    }

    contentItem: Tk.Flex {
        id: body
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Caption { text: qsTr("Output"); color: Tk.Theme.color.textMuted }
        Tk.ComboBox {
            id: outputCombo
            objectName: "channelOutput"
            width: parent.width
            small: true
            model: panel.destinations
            textRole: "label"
            tooltip: qsTr("Where %1 goes").arg(panel.tooltip)
            onActivated: (index) => {
                const target = panel.destinations[index]
                // A refused destination leaves the output where it was.
                if (!panel.route(String(target.id))) currentIndex = panel.outputIndex()
            }
        }
        Tk.Caption {
            visible: !panel.isMaster
            text: qsTr("Sends")
            color: Tk.Theme.color.textMuted
        }
        Repeater {
            model: panel.sends
            delegate: Tk.Flex {
                id: sendRow
                required property var modelData
                objectName: "channelSendRow"
                width: body.width
                gap: Tk.Theme.space.xs
                Tk.Caption {
                    text: String(sendRow.modelData.targetName)
                    elide: Text.ElideRight
                    Tk.Flex.grow: 1
                    Tk.Flex.shrink: 1
                }
                Tk.Knob {
                    objectName: "channelSendLevel"
                    numericEntry: false
                    showValue: false
                    binding: panel.mixer ? panel.mixer.parameterBinding(String(sendRow.modelData.id), "level") : null
                    tooltip: qsTr("Send level to %1").arg(sendRow.modelData.targetName)
                }
                Tk.IconButton {
                    objectName: "channelSendPreFader"
                    iconName: "sliders-horizontal"
                    small: true
                    checked: !!sendRow.modelData.preFader
                    tooltip: sendRow.modelData.preFader ? qsTr("Pre-fader: click to send after the fader")
                                                        : qsTr("Post-fader: click to send before the fader")
                    onClicked: panel.setPreFader(String(sendRow.modelData.id), !sendRow.modelData.preFader)
                }
                Tk.IconButton {
                    objectName: "channelSendRemove"
                    iconName: "x"
                    small: true
                    tooltip: qsTr("Remove the send to %1").arg(sendRow.modelData.targetName)
                    onClicked: panel.removeSend(String(sendRow.modelData.id))
                }
            }
        }
        Tk.ComboBox {
            objectName: "channelAddSend"
            visible: !panel.isMaster && panel.sendOptions.length > 1
            width: parent.width
            small: true
            model: panel.sendOptions
            textRole: "label"
            currentIndex: 0
            tooltip: qsTr("Add a send to a bus")
            onActivated: (index) => {
                if (index > 0) panel.addSend(String(panel.sendOptions[index].id))
                currentIndex = 0
            }
        }
        Tk.Caption {
            objectName: "channelRoutingStatus"
            visible: panel.status.length > 0
            width: parent.width
            wrapMode: Text.Wrap
            text: panel.status
            color: panel.statusIsError ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        }
    }
    // AGENT-GUARD: the combo box sets its own index when activated; this keeps
    // it on the mixer's output whenever the output (or the list) changes.
    Binding { target: outputCombo; property: "currentIndex"; value: panel.outputIndex() }
}
