// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the monitor presents a host-acknowledged frame identity and
// timestamp, and asks an injected provider for that exact frame. The host owns
// decoding, frame/resource lifetime, color transform, TimeMap conversion,
// transport state and seek acceptance. Requests never advance the playhead.
T.Control {
    id: monitor
    objectName: "sourceProgramMonitor"

    property string role: "source" // source or program
    property var frameProvider: null
    property var transport: null
    property var document: null
    property string viewId: ""
    property string resourceId: ""
    property string frameId: "" // exact stable host frame ID; often a decimal string
    property string frameTimeTicks: "0" // exact timestamp for the currently acknowledged frame
    property string rateNumerator: "1"
    property string rateDenominator: "1"
    property string previousFrameId: ""
    property string previousFrameTimeTicks: ""
    property string nextFrameId: ""
    property string nextFrameTimeTicks: ""
    property var streamMetadata: ({})
    property string displayTransform: ""
    property bool overlaysVisible: true
    property string tooltip: qsTr("Video monitor")
    property string accessibleName: qsTr("Video monitor")

    property string _requestSerial: "0"
    property string _pendingRequestId: ""
    property string _requestStatus: ""
    readonly property string pendingRequestId: _pendingRequestId
    readonly property string frameState: frame.assetState
    readonly property string frameError: frame.assetError
    readonly property string playbackText: !transport ? qsTr("No transport")
        : transport.playing ? qsTr("Playing") : qsTr("Stopped")
    readonly property string frameDescription: qsTr("Frame %1 at %2 ticks")
        .arg(frameId.length ? frameId : qsTr("unavailable"))
        .arg(frameTimeTicks)

    signal seekRequested(string requestId, string timeTicks, string frameId)
    signal seekAcknowledged(string requestId, bool accepted, string frameId,
                            string timeTicks, string message)
    signal transportCommandRequested(string action, string requestId)

    implicitWidth: Tk.Theme.size.thumbnail * 5
    implicitHeight: Tk.Theme.size.panelMinHeight * 3
    padding: Tk.Theme.space.sm
    spacing: Tk.Theme.space.xs
    hoverEnabled: true
    Accessible.role: Accessible.Pane
    Accessible.name: monitor.accessibleName
    Accessible.description: [monitor.frameDescription, monitor.playbackText,
                             monitor.frameState, monitor._requestStatus]
                            .filter(function(value) { return value.length > 0 }).join(". ")

    function requestSeek(timeTicks, requestedFrameId) {
        if (!monitor.enabled || String(timeTicks).length === 0) return ""
        const serial = String(Number(monitor._requestSerial) + 1)
        monitor._requestSerial = serial
        const requestId = (monitor.viewId.length ? monitor.viewId : monitor.role) + ":seek:" + serial
        monitor._pendingRequestId = requestId
        monitor._requestStatus = qsTr("Seek pending")
        monitor.seekRequested(requestId, String(timeTicks), String(requestedFrameId || ""))
        return requestId
    }
    function acknowledgeSeek(requestId, accepted, frameId, timeTicks, message) {
        if (!requestId || requestId !== monitor._pendingRequestId) return false
        monitor._pendingRequestId = ""
        monitor._requestStatus = accepted ? qsTr("Seek accepted")
                                          : (message || qsTr("Seek rejected"))
        monitor.seekAcknowledged(String(requestId), !!accepted, String(frameId || ""),
                                 String(timeTicks || ""), String(message || ""))
        return true
    }
    function stepFrame(direction) {
        const targetId = direction < 0 ? monitor.previousFrameId : monitor.nextFrameId
        const targetTicks = direction < 0 ? monitor.previousFrameTimeTicks : monitor.nextFrameTimeTicks
        if (!targetId.length || !targetTicks.length) return ""
        return monitor.requestSeek(targetTicks, targetId)
    }
    function play() {
        if (!monitor.transport || !monitor.transport.canPlay) return ""
        const requestId = String(monitor.transport.requestPlay())
        monitor.transportCommandRequested("play", requestId)
        return requestId
    }
    function stop() {
        if (!monitor.transport) return ""
        const requestId = String(monitor.transport.requestStop())
        monitor.transportCommandRequested("stop", requestId)
        return requestId
    }

    background: Rectangle {
        color: Tk.Theme.color.panel
        border.color: monitor.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.controlBorder
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.Flex {
            width: parent.width
            Tk.Overline {
                title: monitor.role === "program" ? qsTr("PROGRAM") : qsTr("SOURCE")
                color: monitor.role === "program" ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
            }
            Tk.Spacer { }
            Tk.Caption { text: monitor.playbackText; color: monitor.transport && monitor.transport.playing
                                  ? Tk.Theme.color.success : Tk.Theme.color.textMuted }
        }

        Item {
            objectName: "monitorFrameViewport"
            width: parent.width
            implicitHeight: Tk.Theme.size.thumbnailHeight * 3
            Tk.Flex.grow: 1
            clip: true
            Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas }
            Tk.AssetThumbnail {
                id: frame
                objectName: "monitorFrame"
                anchors.fill: parent
                provider: monitor.frameProvider
                resourceId: monitor.resourceId
                requestKind: "frame"
                requestOptions: ({
                    frameId: monitor.frameId,
                    timeTicks: monitor.frameTimeTicks,
                    rateNumerator: monitor.rateNumerator,
                    rateDenominator: monitor.rateDenominator,
                    displayTransform: monitor.displayTransform,
                    size: ({width: Math.max(1, Math.round(width)), height: Math.max(1, Math.round(height))})
                })
                document: monitor.document
                viewId: monitor.viewId
                active: monitor.enabled && monitor.frameProvider !== null && monitor.resourceId.length > 0
                crop: true
                caption: ""
                tooltip: monitor.frameDescription
            }
            Tk.Caption {
                objectName: "monitorFrameOverlay"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: Tk.Theme.space.sm
                visible: monitor.overlaysVisible && monitor.frameState !== "ready"
                text: monitor.frameState === "failed" || monitor.frameState === "missing"
                      ? monitor.frameError : qsTr("Frame %1 · %2 ticks · %3")
                           .arg(monitor.frameId || "—").arg(monitor.frameTimeTicks).arg(monitor.frameState)
                color: monitor.frameError.length > 0 ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onDoubleClicked: monitor.overlaysVisible = !monitor.overlaysVisible
            }
        }

        Tk.Flex {
            width: parent.width
            Tk.IconButton {
                objectName: "monitorPreviousFrame"
                iconName: "step-back"
                tooltip: qsTr("Seek previous host frame")
                enabled: monitor.previousFrameId.length > 0 && monitor.transport && monitor.transport.canSeek
                onClicked: monitor.stepFrame(-1)
            }
            Tk.IconButton {
                objectName: "monitorPlay"
                iconName: "play"
                tooltip: monitor.transport && !monitor.transport.canPlay
                         ? (monitor.transport.playUnavailableReason || qsTr("Playback unavailable"))
                         : qsTr("Play")
                enabled: monitor.transport && !monitor.transport.playing && monitor.transport.canPlay
                onClicked: monitor.play()
            }
            Tk.IconButton {
                objectName: "monitorStop"
                iconName: "square"
                tooltip: qsTr("Stop playback")
                enabled: monitor.transport && monitor.transport.playing
                onClicked: monitor.stop()
            }
            Tk.IconButton {
                objectName: "monitorNextFrame"
                iconName: "step-forward"
                tooltip: qsTr("Seek next host frame")
                enabled: monitor.nextFrameId.length > 0 && monitor.transport && monitor.transport.canSeek
                onClicked: monitor.stepFrame(1)
            }
            Tk.Spacer { }
            Tk.Mono {
                objectName: "monitorFrameIdentity"
                text: monitor.frameId.length ? qsTr("%1 · %2").arg(monitor.frameId).arg(monitor.frameTimeTicks)
                                             : qsTr("No acknowledged frame")
                Accessible.role: Accessible.StaticText
                Accessible.name: monitor.frameDescription
            }
        }
        Tk.Caption {
            objectName: "monitorRequestStatus"
            visible: monitor._requestStatus.length > 0 || monitor.frameError.length > 0
            text: monitor.frameError.length > 0 ? monitor.frameError : monitor._requestStatus
            color: monitor.frameError.length > 0 ? Tk.Theme.color.warning : Tk.Theme.color.info
        }
    }
    Tk.ToolTip { visible: monitor.hovered && monitor.tooltip.length > 0; text: monitor.tooltip }
}
