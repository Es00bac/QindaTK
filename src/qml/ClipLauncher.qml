// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: scenes, slots, and playing state are host snapshots. Launch
// buttons report requests; only matching host acknowledgments update request
// status, and only a later host snapshot can change which slot is active.
T.Control {
    id: root

    property var slots: [] // {id, sceneId, label, state: empty|ready|queued|playing|recording, available, reason}
    property string quantization: "bar"
    property int columns: 4
    property string activeSlotId: ""
    property string tooltip: ""
    property string statusText: ""
    property string _pendingRequestId: ""
    property string _pendingSlotId: ""
    property string _requestState: "idle"
    property string _requestMessage: ""
    property int _serial: 0

    signal launchRequested(string requestId, string slotId, string quantization)
    signal stopRequested(string requestId, string slotId)
    signal slotSelected(string slotId)
    signal launchAcknowledged(string requestId, string state, string message)

    objectName: "clipLauncher"
    implicitWidth: Tk.Theme.size.sparkline * 2
    implicitHeight: Tk.Theme.size.waveformHeight * 3
    Accessible.role: Accessible.Grouping
    Accessible.name: root.tooltip.length ? root.tooltip : qsTr("Clip launcher")
    Accessible.description: root.statusText.length ? root.statusText : root.requestDescription

    readonly property string requestDescription: {
        if (root._requestState === "pending") return qsTr("Launch request pending for %1").arg(root._pendingSlotId)
        if (root._requestState === "accepted") return qsTr("Request accepted; waiting for host state snapshot")
        if (root._requestState === "rejected" || root._requestState === "error") return root._requestMessage
        return qsTr("Launch quantization: %1. Active slot: %2").arg(root.quantization).arg(root.activeSlotId || qsTr("none"))
    }

    function requestLaunch(slotId) {
        if (!root.enabled || root._requestState === "pending") return false
        let slot = null
        for (let i = 0; i < root.slots.length; ++i)
            if (String(root.slots[i].id) === String(slotId)) slot = root.slots[i]
        if (!slot || slot.available === false || slot.state === "empty") return false
        root._serial += 1
        root._pendingRequestId = "clip-launch-" + root._serial
        root._pendingSlotId = String(slot.id)
        root._requestState = "pending"
        root._requestMessage = qsTr("Waiting for host acknowledgment")
        root.slotSelected(root._pendingSlotId)
        root.launchRequested(root._pendingRequestId, root._pendingSlotId, root.quantization)
        return true
    }
    function requestStop(slotId) {
        if (!root.enabled || !slotId) return false
        root._serial += 1
        root.stopRequested("clip-stop-" + root._serial, String(slotId))
        return true
    }
    function acknowledgeLaunch(requestId, state, message) {
        if (String(requestId) !== root._pendingRequestId || root._requestState !== "pending") return false
        if (["accepted", "rejected", "error", "canceled"].indexOf(state) < 0) return false
        root._requestState = state
        root._requestMessage = String(message || "")
        root.launchAcknowledged(String(requestId), state, root._requestMessage)
        return true
    }
    function clearAcknowledgment() {
        if (root._requestState === "pending") return false
        root._requestState = "idle"
        root._requestMessage = ""
        root._pendingRequestId = ""
        root._pendingSlotId = ""
        return true
    }

    background: Rectangle {
        color: Tk.Theme.color.panelAlt
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        radius: Tk.Theme.radius.sm
    }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.xs
        gap: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("QUANTIZE"); color: Tk.Theme.color.textMuted }
            Tk.Label { text: root.quantization; color: Tk.Theme.color.accent }
            Tk.Button {
                objectName: "clipLauncherStopActive"
                small: true
                text: qsTr("Stop active")
                enabled: root.activeSlotId.length > 0
                tooltip: qsTr("Request stop for active slot %1").arg(root.activeSlotId)
                onClicked: root.requestStop(root.activeSlotId)
            }
            Tk.Spacer { Tk.Flex.flex: 1 }
            Tk.Caption { objectName: "clipLauncherStatus"; text: root.requestDescription; color: Tk.Theme.color.textMuted; elide: Text.ElideRight }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Repeater {
                model: root.slots
                delegate: Tk.Button {
                    required property var modelData
                    objectName: "clipLauncherSlot_" + modelData.id
                    Tk.Flex.basis: Math.max(Tk.Theme.size.control, root.width / Math.max(1, root.columns) - 2 * Tk.Theme.space.md)
                    Tk.Flex.minWidth: Tk.Theme.size.fieldWidth
                    text: modelData.label + "\n" + root.slotStatus(modelData)
                    enabled: root.enabled && modelData.available !== false && modelData.state !== "empty"
                    tooltip: modelData.reason || qsTr("Launch %1 · %2 quantization").arg(modelData.label).arg(root.quantization)
                    Accessible.name: modelData.label + ", " + root.slotStatus(modelData)
                    onClicked: root.requestLaunch(modelData.id)
                }
            }
        }
    }
    function slotStatus(slot) {
        if (slot.id === root._pendingSlotId && root._requestState === "pending") return qsTr("Pending…")
        if (slot.id === root.activeSlotId || slot.state === "playing") return qsTr("Playing")
        if (slot.state === "queued") return qsTr("Queued")
        if (slot.state === "recording") return qsTr("Recording")
        if (slot.state === "empty") return qsTr("Empty")
        return qsTr("Ready")
    }
    Tk.ToolTip { visible: hover.hovered && root.tooltip.length > 0; text: root.tooltip }
    HoverHandler { id: hover }
}
