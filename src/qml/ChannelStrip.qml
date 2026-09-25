// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (WP-062, docs/controls.md, docs/audio-mixer.md): one mixer
// channel: meter, fader, pan, mute/solo/arm and (detail) routing. Bound
// (`mixer` + `channelId`): the fader, pan and send levels are the mixer's
// shared AudioParameterBinding transactions, the meter is the engine's
// post-fader channel meter, and commands are AudioMixerModel operations whose
// answer is shown here; the strip keeps no routing, automation or engine
// state, so a recycled delegate cannot redirect an edit. Host mode (no mixer)
// keeps the request protocol: gainEditRequested/panEditRequested phases and
// channelCommandRequested by stable IDs, acknowledged by the host.
T.Control {
    id: strip
    objectName: "channelStrip"
    property var mixer: null // Tk.AudioMixerModel
    property string channelId: ""
    property string channelName: ""
    property string channelKind: "audio" // bound: "track", "bus" or "master"
    property string layout: "Stereo"
    property string gainParameterId: ""
    property real gainValue: 0
    property bool gainSilent: false
    property real gainMinimum: -60
    property real gainMaximum: 6
    property var gainMap: null
    property bool muted: false
    property bool soloed: false
    property bool armed: false
    property bool soloSafe: false
    property bool implicitlyMuted: false
    property real panValue: 0
    property real stereoWidth: 1
    property bool selected: false
    property bool detail: true
    property var meterModel: null // host: a shared Tk.AudioMeterModel, channel `meterChannel`
    property int meterChannel: 0
    property real peakDbfs: -120
    property real rmsDbfs: -120
    property string routeSummary: qsTr("No route details")
    property string output: ""
    property var sends: []
    property var groupNames: []
    property string commandState: "idle"
    property string commandMessage: ""
    property string tooltip: qsTr("Channel strip")
    property alias panningControl: pan
    property alias gainFader: fader
    property string _serial: "0"
    property string _gainGestureId: ""
    property string _lastGainRequestId: ""
    property string _lastCommandId: ""
    readonly property bool bound: mixer !== null && channelId.length > 0
    readonly property bool isMaster: channelKind === "master"
    readonly property var gainBinding: bound ? mixer.parameterBinding(channelId, "gain") : null
    readonly property var boundMeter: bound && mixer.engine ? mixer.meter(channelId) : null
    readonly property var meterLevels: boundMeter ? boundMeter.levels
        : meterModel && meterModel.levels && meterChannel >= 0 && meterChannel < meterModel.levels.length
          ? [meterModel.levels[meterChannel]]
          : [{channel: 0, name: "", peakDbfs: peakDbfs, rmsDbfs: rmsDbfs, displayPeakDbfs: peakDbfs,
              clipped: peakDbfs >= 0, silent: peakDbfs <= -120}]
    readonly property real shownPeak: meterLevels.length > 0
        ? Number(meterLevels[0].displayPeakDbfs !== undefined ? meterLevels[0].displayPeakDbfs : meterLevels[0].peakDbfs)
        : -120

    signal gainEditRequested(string channelId, string parameterId, string gestureId,
                             string phase, real value, bool silent)
    signal panEditRequested(string channelId, string parameterId, string gestureId,
                            string phase, real value)
    signal channelCommandRequested(string channelId, string requestId, string command, bool enabled)
    signal channelSelected(string channelId)

    implicitWidth: Tk.Theme.size.fieldWidth
    implicitHeight: body.implicitHeight + topPadding + bottomPadding
    padding: Tk.Theme.space.sm
    hoverEnabled: true
    Accessible.role: Accessible.Grouping
    Accessible.name: channelName.length ? channelName : tooltip
    Accessible.description: qsTr("%1, %2, gain %3, peak %4 dBFS%5")
            .arg(channelKind).arg(layout).arg(fader.valueText).arg(Number(shownPeak).toFixed(1))
            .arg(muted ? qsTr(", muted") : implicitlyMuted ? qsTr(", silent while another channel is soloed") : "")

    // Host mode: one gesture ID per fader gesture, every phase by stable IDs.
    function gainRequest(phase, value, isSilent) {
        if (phase === "begin" || !_gainGestureId) {
            _serial = String(Number(_serial) + 1)
            _gainGestureId = channelId + ":" + gainParameterId + ":" + _serial
        }
        const id = _gainGestureId
        _lastGainRequestId = id
        gainEditRequested(channelId, gainParameterId, id, phase, Number(value), !!isSilent)
        if (phase === "commit" || phase === "cancel") _gainGestureId = ""
        return id
    }
    function acknowledgeGainEdit(requestId, state, message) {
        if (!requestId || requestId !== _lastGainRequestId || ["accepted", "rejected", "error"].indexOf(state) < 0) return false
        commandState = String(state)
        commandMessage = String(message || (state === "accepted" ? qsTr("Gain accepted") : qsTr("Gain %1").arg(state)))
        return true
    }
    // mute, solo, arm, soloSafe (and hide, bound): a mixer operation when
    // bound, answered at once; a host request otherwise.
    function requestCommand(command, value) {
        _serial = String(Number(_serial) + 1)
        const id = channelId + ":" + command + ":" + _serial
        _lastCommandId = id
        if (!bound) {
            channelCommandRequested(channelId, id, command, !!value)
            return id
        }
        const on = !!value
        const result = command === "mute" ? mixer.setMuted(channelId, on)
                     : command === "solo" ? mixer.setSoloed(channelId, on)
                     : command === "arm" ? mixer.setArmed(channelId, on)
                     : command === "soloSafe" ? mixer.setSoloSafe(channelId, on)
                     : command === "hide" ? mixer.setHidden(channelId, on)
                     : ({ok: false, error: qsTr("Unknown command %1").arg(command)})
        commandState = result.ok ? "accepted" : "rejected"
        commandMessage = result.ok ? "" : String(result.error)
        return id
    }
    function acknowledgeCommand(requestId, state, message) {
        if (!requestId || requestId !== _lastCommandId || ["accepted", "rejected", "error"].indexOf(state) < 0) return false
        commandState = String(state)
        commandMessage = String(message || (state === "accepted" ? qsTr("Command accepted") : qsTr("Command %1").arg(state)))
        return true
    }
    Keys.onEscapePressed: function(event) { fader.cancelGesture(); pan.cancel(); event.accepted = true }

    background: Rectangle {
        color: Tk.Theme.color.panel
        opacity: strip.implicitlyMuted ? Tk.Theme.opacity.muted : Tk.Theme.opacity.full
        border.color: strip.selected ? Tk.Theme.color.accent : strip.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: strip.selected || strip.activeFocus ? Tk.Theme.size.focusRing : Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        id: body
        anchors.fill: parent
        anchors.margins: strip.padding
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        // AGENT-NOTE: only the fader grows and shrinks (down to its minimum);
        // every other part keeps its height, so a short strip never squeezes
        // the pan pad or the routing panel.
        Tk.Flex {
            width: parent.width
            gap: Tk.Theme.space.xs
            Tk.Flex.shrink: 0
            Tk.Label {
                objectName: "channelName"
                text: strip.channelName
                elide: Text.ElideRight
                font.pixelSize: Tk.Theme.font.body
                Tk.Flex.grow: 1
                Tk.Flex.shrink: 1
            }
            Tk.Caption { text: strip.channelKind.toUpperCase() + " · " + strip.layout; color: Tk.Theme.color.textMuted }
        }
        Tk.AudioMeter {
            objectName: "channelPeakMeter"
            width: parent.width
            Tk.Flex.shrink: 0
            meter: strip.boundMeter
            levels: strip.meterLevels
            showScale: strip.detail
            tooltip: qsTr("%1 post-fader level").arg(strip.channelName)
        }
        Tk.Fader {
            id: fader
            objectName: "channelGainFader"
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.shrink: 1
            Tk.Flex.minHeight: Tk.Theme.size.controlLg * 3
            binding: strip.gainBinding
            mapper: strip.bound ? null : strip.gainMap
            from: strip.gainMinimum
            to: strip.gainMaximum
            tooltip: qsTr("%1 gain").arg(strip.channelName)
            onGestureStarted: (value, silent) => { if (!strip.bound) strip.gainRequest("begin", value, silent) }
            onValuePreview: (value, silent) => { if (!strip.bound) strip.gainRequest("preview", value, silent) }
            onGestureCommitted: (value, silent) => { if (!strip.bound) strip.gainRequest("commit", value, silent) }
            onGestureCanceled: (value, silent) => { if (!strip.bound) strip.gainRequest("cancel", value, silent) }
        }
        Tk.PanningControl {
            id: pan
            objectName: "channelPanning"
            width: parent.width
            Tk.Flex.shrink: 0
            mixer: strip.bound ? strip.mixer : null
            channelId: strip.channelId
            speakerLayout: strip.layout
            pad: strip.detail
            panParameterId: strip.channelId + "/pan"
            widthParameterId: strip.channelId + "/width"
            panValue: strip.panValue
            stereoWidth: strip.stereoWidth
            tooltip: qsTr("%1 panning").arg(strip.channelName)
            onParameterEditRequested: (channelId, parameterId, gestureId, phase, value) =>
                strip.panEditRequested(channelId, parameterId, gestureId, phase, value)
        }
        Tk.Flex {
            width: parent.width
            gap: Tk.Theme.space.xs
            Tk.Flex.shrink: 0
            Tk.IconButton {
                objectName: "channelMute"
                iconName: "volume-x"
                checkable: false
                checked: strip.muted
                danger: strip.muted
                tooltip: strip.muted ? qsTr("Unmute %1").arg(strip.channelName) : qsTr("Mute %1").arg(strip.channelName)
                onClicked: strip.requestCommand("mute", !strip.muted)
            }
            Tk.IconButton {
                objectName: "channelSolo"
                visible: !strip.isMaster
                iconName: "target"
                checkable: false
                checked: strip.soloed
                tooltip: strip.soloed ? qsTr("Unsolo %1").arg(strip.channelName) : qsTr("Solo %1").arg(strip.channelName)
                onClicked: strip.requestCommand("solo", !strip.soloed)
            }
            Tk.IconButton {
                objectName: "channelArm"
                visible: strip.channelKind === "track"
                iconName: "circle-dot"
                checkable: false
                checked: strip.armed
                tooltip: strip.armed ? qsTr("Disarm %1").arg(strip.channelName) : qsTr("Arm %1 for recording").arg(strip.channelName)
                onClicked: strip.requestCommand("arm", !strip.armed)
            }
            Tk.IconButton {
                objectName: "channelSoloSafe"
                visible: strip.detail && strip.bound && !strip.isMaster
                iconName: "anchor"
                checkable: false
                checked: strip.soloSafe
                tooltip: strip.soloSafe ? qsTr("%1 is solo safe: click to let a solo mute it").arg(strip.channelName)
                                        : qsTr("Make %1 solo safe (never muted by another solo)").arg(strip.channelName)
                onClicked: strip.requestCommand("soloSafe", !strip.soloSafe)
            }
            Tk.Spacer { }
            Tk.IconButton {
                objectName: "channelHide"
                visible: strip.detail && strip.bound
                iconName: "eye-off"
                tooltip: qsTr("Hide %1 in the mixer").arg(strip.channelName)
                onClicked: strip.requestCommand("hide", true)
            }
        }
        Tk.Caption {
            objectName: "channelGroups"
            Tk.Flex.shrink: 0
            visible: strip.detail && strip.groupNames && strip.groupNames.length > 0
            width: parent.width
            elide: Text.ElideRight
            text: qsTr("Groups: %1").arg((strip.groupNames || []).join(", "))
            color: Tk.Theme.color.textMuted
        }
        Tk.ChannelRoutingPanel {
            objectName: "channelRouting"
            Tk.Flex.shrink: 0
            visible: strip.detail && strip.bound
            width: parent.width
            mixer: strip.bound ? strip.mixer : null
            channelId: strip.channelId
            output: strip.output
            sends: strip.sends
            tooltip: strip.channelName
        }
        Tk.Caption {
            objectName: "channelRouteSummary"
            Tk.Flex.shrink: 0
            visible: !strip.bound
            width: parent.width
            elide: Text.ElideRight
            text: strip.routeSummary
            color: Tk.Theme.color.textMuted
        }
        Tk.Caption {
            objectName: "channelCommandStatus"
            Tk.Flex.shrink: 0
            visible: strip.commandMessage.length > 0
            width: parent.width
            wrapMode: Text.Wrap
            text: strip.commandMessage
            color: strip.commandState === "rejected" || strip.commandState === "error"
                   ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        }
    }
    // Host mode: the fader shows the host's value whenever no gesture holds it,
    // and keeps the gesture's value (never the pre-binding default) while one does.
    Binding { target: fader; property: "value"; value: strip.gainValue; when: !strip.bound && !fader.gestureOpen; restoreMode: Binding.RestoreNone }
    Binding { target: fader; property: "silent"; value: strip.gainSilent; when: !strip.bound && !fader.gestureOpen; restoreMode: Binding.RestoreNone }
    Tk.ToolTip { visible: strip.hovered && strip.tooltip.length > 0; text: strip.tooltip }
    TapHandler {
        objectName: "channelSelectionArea"
        acceptedButtons: Qt.LeftButton
        onTapped: strip.channelSelected(strip.channelId)
    }
}
