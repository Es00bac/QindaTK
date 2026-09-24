// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: host supplies stable device IDs/capabilities and both active
// and pending configurations. This panel emits candidate/apply requests only;
// only the host/engine may acknowledge or reject a reconfiguration. When the
// host reports engineState "lost"/"error", Apply re-requests the same stable
// device ID (Reconnect); the panel never falls back to another device.
T.Control {
    id: panel
    objectName: "deviceSettingsPanel"

    property var devices: []
    property var activeConfiguration: ({})
    property var pendingConfiguration: ({})
    property string engineState: ""   // host: running, stopped, lost, error; "" = not reported
    property string engineMessage: ""
    property string tooltip: qsTr("Audio device and I/O settings")
    property string accessibleName: qsTr("Device settings")

    property string _serial: "0"
    property string _applyRequestId: ""
    property string _applyState: "idle" // idle, pending, accepted, rejected, error
    property string _applyMessage: ""

    readonly property var pendingDevice: deviceForId(String(pendingConfiguration.deviceId || ""))
    readonly property string applyState: _applyState
    readonly property string applyMessage: _applyMessage
    readonly property bool configurationChanged: !sameConfiguration(activeConfiguration, pendingConfiguration)
    readonly property bool configurationAvailable: !!pendingDevice && pendingDevice.available !== false
            && optionAvailable(pendingDevice.sampleRates, pendingConfiguration.sampleRate)
            && optionAvailable(pendingDevice.bufferSizes, pendingConfiguration.bufferSize)
            && optionAvailable(pendingDevice.inputLayouts, pendingConfiguration.inputLayout)
            && optionAvailable(pendingDevice.outputLayouts, pendingConfiguration.outputLayout)
    readonly property bool engineFailed: engineState === "lost" || engineState === "error"
    readonly property bool recoveryAvailable: engineFailed && configurationAvailable
    readonly property string activeSummary: summary(activeConfiguration)
    readonly property string pendingSummary: summary(pendingConfiguration)

    signal configurationChangeRequested(var candidate)
    signal applyRequested(string requestId, var configuration)
    signal applyAcknowledged(string requestId, string state, var activeConfiguration, string message)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: Tk.Theme.space.md
    hoverEnabled: true
    Accessible.role: Accessible.Pane
    Accessible.name: panel.accessibleName
    Accessible.description: qsTr("Active: %1. Pending: %2. %3 %4")
            .arg(activeSummary).arg(pendingSummary).arg(engineFailed ? engineMessage : "").arg(_applyMessage)

    function deviceForId(id) {
        for (let i = 0; i < devices.length; ++i)
            if (String(devices[i].id) === String(id)) return devices[i]
        return null
    }
    function deviceIndex(id) {
        for (let i = 0; i < devices.length; ++i)
            if (String(devices[i].id) === String(id)) return i
        return -1
    }
    function optionIndex(options, value) {
        if (!options) return -1
        for (let i = 0; i < options.length; ++i)
            if (String(options[i]) === String(value)) return i
        return -1
    }
    function optionAvailable(options, value) { return optionIndex(options, value) >= 0 }
    function sameConfiguration(a, b) {
        if (!a || !b) return false
        return String(a.deviceId || "") === String(b.deviceId || "")
                && String(a.sampleRate || "") === String(b.sampleRate || "")
                && String(a.bufferSize || "") === String(b.bufferSize || "")
                && String(a.inputLayout || "") === String(b.inputLayout || "")
                && String(a.outputLayout || "") === String(b.outputLayout || "")
    }
    function summary(configuration) {
        if (!configuration || !configuration.deviceId) return qsTr("No device selected")
        const device = deviceForId(configuration.deviceId)
        const name = device ? device.name : configuration.deviceId
        return qsTr("%1 · %2 Hz · %3 frames · in %4 · out %5")
                .arg(name).arg(configuration.sampleRate || "—").arg(configuration.bufferSize || "—")
                .arg(configuration.inputLayout || "—").arg(configuration.outputLayout || "—")
    }
    function requestChange(field, value) {
        const candidate = Object.assign({}, panel.pendingConfiguration || {})
        candidate[field] = value
        panel._applyMessage = ""
        if (panel._applyState === "pending") {
            panel._applyState = "idle"
            panel._applyRequestId = ""
        }
        panel.configurationChangeRequested(candidate)
    }
    function requestApply() {
        if (!panel.enabled || !(configurationChanged || recoveryAvailable) || !configurationAvailable
                || _applyState === "pending") return ""
        _serial = String(Number(_serial) + 1)
        const requestId = "device-config:" + _serial
        _applyRequestId = requestId
        _applyState = "pending"
        _applyMessage = qsTr("Waiting for the audio host")
        applyRequested(requestId, Object.assign({}, pendingConfiguration))
        return requestId
    }
    function acknowledgeApply(requestId, state, configuration, message) {
        if (!requestId || requestId !== _applyRequestId || _applyState !== "pending") return false
        const outcome = String(state)
        if (["accepted", "rejected", "error"].indexOf(outcome) < 0) return false
        _applyRequestId = ""
        _applyState = outcome
        _applyMessage = String(message || (outcome === "accepted" ? qsTr("Configuration accepted")
                                                                     : qsTr("Configuration %1").arg(outcome)))
        applyAcknowledged(String(requestId), outcome, configuration || ({}), _applyMessage)
        return true
    }

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: panel.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Overline { title: qsTr("AUDIO DEVICE") }
        Tk.Caption { text: qsTr("Active configuration"); color: Tk.Theme.color.textMuted }
        Tk.Mono { objectName: "deviceSettingsActive"; text: panel.activeSummary; Accessible.role: Accessible.StaticText }
        Tk.Caption { text: qsTr("Pending configuration"); color: Tk.Theme.color.textMuted }
        Tk.Mono { objectName: "deviceSettingsPending"; text: panel.pendingSummary; Accessible.role: Accessible.StaticText }

        Tk.Flex {
            width: parent.width
            Tk.Label { text: qsTr("Device"); width: Tk.Theme.size.fieldWidth * 0.65; verticalAlignment: Text.AlignVCenter }
            Tk.ComboBox {
                id: deviceChoice
                objectName: "deviceSettingsDevice"
                width: parent.width - Tk.Theme.size.fieldWidth * 0.65
                Tk.Flex.grow: 1
                model: panel.devices
                textRole: "name"
                currentIndex: panel.deviceIndex(panel.pendingConfiguration.deviceId || "")
                tooltip: qsTr("Choose a stable host device ID")
                onActivated: function(index) { panel.requestChange("deviceId", String(panel.devices[index].id)) }
            }
        }
        Tk.Flex {
            width: parent.width
            Tk.Label { text: qsTr("Sample rate"); width: Tk.Theme.size.fieldWidth * 0.65; verticalAlignment: Text.AlignVCenter }
            Tk.ComboBox {
                objectName: "deviceSettingsSampleRate"
                width: parent.width - Tk.Theme.size.fieldWidth * 0.65
                Tk.Flex.grow: 1
                model: panel.pendingDevice ? panel.pendingDevice.sampleRates || [] : []
                currentIndex: panel.pendingDevice ? panel.optionIndex(panel.pendingDevice.sampleRates, panel.pendingConfiguration.sampleRate) : -1
                tooltip: qsTr("Rates supported by the selected host device")
                enabled: !!panel.pendingDevice && panel.pendingDevice.available !== false
                onActivated: function(index) { panel.requestChange("sampleRate", model[index]) }
            }
        }
        Tk.Flex {
            width: parent.width
            Tk.Label { text: qsTr("Buffer"); width: Tk.Theme.size.fieldWidth * 0.65; verticalAlignment: Text.AlignVCenter }
            Tk.ComboBox {
                objectName: "deviceSettingsBufferSize"
                width: parent.width - Tk.Theme.size.fieldWidth * 0.65
                Tk.Flex.grow: 1
                model: panel.pendingDevice ? panel.pendingDevice.bufferSizes || [] : []
                currentIndex: panel.pendingDevice ? panel.optionIndex(panel.pendingDevice.bufferSizes, panel.pendingConfiguration.bufferSize) : -1
                tooltip: qsTr("Buffer size in frames; actual latency is host reported")
                enabled: !!panel.pendingDevice && panel.pendingDevice.available !== false
                onActivated: function(index) { panel.requestChange("bufferSize", model[index]) }
            }
        }
        Tk.Flex {
            width: parent.width
            Tk.Label { text: qsTr("Input layout"); width: Tk.Theme.size.fieldWidth * 0.65; verticalAlignment: Text.AlignVCenter }
            Tk.ComboBox {
                objectName: "deviceSettingsInputLayout"
                width: parent.width - Tk.Theme.size.fieldWidth * 0.65
                Tk.Flex.grow: 1
                model: panel.pendingDevice ? panel.pendingDevice.inputLayouts || [] : []
                currentIndex: panel.pendingDevice ? panel.optionIndex(panel.pendingDevice.inputLayouts, panel.pendingConfiguration.inputLayout) : -1
                tooltip: qsTr("Host-supported input channel layout")
                enabled: !!panel.pendingDevice && panel.pendingDevice.available !== false
                onActivated: function(index) { panel.requestChange("inputLayout", model[index]) }
            }
        }
        Tk.Flex {
            width: parent.width
            Tk.Label { text: qsTr("Output layout"); width: Tk.Theme.size.fieldWidth * 0.65; verticalAlignment: Text.AlignVCenter }
            Tk.ComboBox {
                objectName: "deviceSettingsOutputLayout"
                width: parent.width - Tk.Theme.size.fieldWidth * 0.65
                Tk.Flex.grow: 1
                model: panel.pendingDevice ? panel.pendingDevice.outputLayouts || [] : []
                currentIndex: panel.pendingDevice ? panel.optionIndex(panel.pendingDevice.outputLayouts, panel.pendingConfiguration.outputLayout) : -1
                tooltip: qsTr("Host-supported output channel layout")
                enabled: !!panel.pendingDevice && panel.pendingDevice.available !== false
                onActivated: function(index) { panel.requestChange("outputLayout", model[index]) }
            }
        }
        Tk.Flex {
            width: parent.width
            Tk.Spacer { }
            Tk.Caption {
                objectName: "deviceSettingsStatus"
                text: panel.engineFailed && panel.engineMessage.length && panel._applyState !== "pending"
                      ? panel.engineMessage
                      : panel._applyMessage.length ? panel._applyMessage
                      : !panel.pendingDevice ? qsTr("Select an available device")
                      : panel.pendingDevice.available === false ? (panel.pendingDevice.unavailableReason || qsTr("Device unavailable"))
                      : !panel.configurationAvailable ? qsTr("Pending settings are not supported by this device")
                      : panel.configurationChanged ? qsTr("Pending · not active") : qsTr("Using active configuration")
                color: panel.engineFailed || panel._applyState === "error" || panel._applyState === "rejected"
                       || !panel.configurationAvailable ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            }
            Tk.Button {
                objectName: "deviceSettingsApply"
                text: panel._applyState === "pending" ? qsTr("Applying…")
                      : panel.recoveryAvailable && !panel.configurationChanged ? qsTr("Reconnect") : qsTr("Apply")
                enabled: (panel.configurationChanged || panel.recoveryAvailable) && panel.configurationAvailable
                         && panel._applyState !== "pending"
                tooltip: !enabled ? qsTr("No applicable pending configuration")
                         : panel.recoveryAvailable && !panel.configurationChanged
                           ? qsTr("Ask the host to reopen the same device by its stable ID")
                           : qsTr("Ask the host to apply the pending device configuration")
                onClicked: panel.requestApply()
            }
        }
    }
    Tk.ToolTip { visible: panel.hovered && panel.tooltip.length > 0; text: panel.tooltip }
}
