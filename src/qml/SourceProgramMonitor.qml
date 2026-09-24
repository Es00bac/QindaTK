// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: the monitor presents a host-acknowledged frame identity and
// timestamp. Host mode: it asks an injected provider for that exact frame and
// the host owns decoding, frame/resource lifetime, color transform, TimeMap
// conversion, transport state and seek acceptance. Player mode (`player` set,
// docs/media-playback.md): a Tk.MediaPlayer's engine acknowledges every seek
// and the identity shown is always the frame on screen (player.presented*),
// never the requested one. Requests never advance the playhead.
T.Control {
    id: monitor
    objectName: "sourceProgramMonitor"

    property string role: "source" // source or program
    property Tk.MediaPlayer player: null
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
    readonly property bool playerMode: monitor.player !== null
    readonly property var activeTransport: monitor.transport ? monitor.transport
                                           : (monitor.player ? monitor.player.transport : null)
    readonly property string displayedFrameId: monitor.playerMode ? monitor.player.presentedFrame : monitor.frameId
    readonly property string displayedTicks: monitor.playerMode ? monitor.player.presentedTicks : monitor.frameTimeTicks
    readonly property string pendingRequestId: _pendingRequestId
    readonly property string frameState: monitor.playerMode ? monitor._playerFrameState() : frame.assetState
    readonly property string frameError: monitor.playerMode ? monitor._playerError() : frame.assetError
    readonly property string playbackText: !activeTransport ? qsTr("No transport")
        : activeTransport.playing ? qsTr("Playing") : qsTr("Stopped")
    readonly property string frameDescription: qsTr("Frame %1 at %2 ticks")
        .arg(displayedFrameId.length ? displayedFrameId : qsTr("unavailable"))
        .arg(displayedTicks.length ? displayedTicks : "—")

    signal seekRequested(string requestId, string timeTicks, string frameId)
    signal seekAcknowledged(string requestId, bool accepted, string frameId,
                            string timeTicks, string message)
    signal transportCommandRequested(string action, string requestId)

    implicitWidth: Tk.Theme.size.thumbnail * 5
    implicitHeight: Tk.Theme.size.panelMinHeight * 3
    padding: Tk.Theme.space.sm
    spacing: Tk.Theme.space.xs
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Pane
    Accessible.name: monitor.accessibleName
    Accessible.description: [monitor.frameDescription, monitor.playbackText,
                             monitor.frameState, monitor._requestStatus]
                            .filter(function(value) { return value.length > 0 }).join(". ")

    function _playerFrameState() {
        if (!monitor.player) return "idle"
        if (monitor.player.state !== "ready" && monitor.player.state !== "missing") return monitor.player.state
        if (monitor.player.frameError.length > 0) return "failed"
        if (monitor.player.missingResourceIds.length > 0) return "missing"
        return monitor.player.pending ? "loading" : "ready"
    }
    function _playerError() {
        if (!monitor.player) return ""
        if (monitor.player.frameError.length > 0)
            return qsTr("Frame %1 unavailable: %2").arg(monitor.player.presentedFrame).arg(monitor.player.frameError)
        if (monitor.player.missingResourceIds.length > 0)
            return qsTr("Missing media: %1").arg(monitor.player.missingResourceIds.join(", "))
        if (monitor.player.state === "missing" || monitor.player.state === "failed") return monitor.player.lastError
        return ""
    }
    function _track(requestId, timeTicks, requestedFrameId) {
        if (!requestId || requestId.length === 0) {
            monitor._requestStatus = monitor.activeTransport && monitor.activeTransport.lastError
                ? monitor.activeTransport.lastError : qsTr("Seek rejected")
            return ""
        }
        monitor._pendingRequestId = requestId
        monitor._requestStatus = qsTr("Seek pending")
        monitor.seekRequested(requestId, String(timeTicks), String(requestedFrameId || ""))
        return requestId
    }
    function requestSeek(timeTicks, requestedFrameId) {
        if (!monitor.enabled) return ""
        if (monitor.playerMode) {
            const target = String(requestedFrameId || "")
            const id = target.length > 0 ? monitor.player.seekFrame(target)
                                         : monitor.player.seekTicks(String(timeTicks))
            return monitor._track(id, timeTicks, target.length > 0 ? target : monitor.player.frameAtTicks(String(timeTicks)))
        }
        if (String(timeTicks).length === 0) return ""
        const serial = String(Number(monitor._requestSerial) + 1)
        monitor._requestSerial = serial
        const requestId = (monitor.viewId.length ? monitor.viewId : monitor.role) + ":seek:" + serial
        return monitor._track(requestId, timeTicks, requestedFrameId)
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
        if (monitor.playerMode) {
            if (!monitor.player.targetFrame.length) return ""
            const target = String(Math.max(0, Math.min(Number(monitor.player.frameCount) - 1,
                                                       Number(monitor.player.targetFrame) + direction)))
            return monitor.requestSeek(monitor.player.frameStartTicks(target), target)
        }
        const targetId = direction < 0 ? monitor.previousFrameId : monitor.nextFrameId
        const targetTicks = direction < 0 ? monitor.previousFrameTimeTicks : monitor.nextFrameTimeTicks
        if (!targetId.length || !targetTicks.length) return ""
        return monitor.requestSeek(targetTicks, targetId)
    }
    function seekEdge(last) {
        if (!monitor.playerMode || Number(monitor.player.frameCount) < 1) return ""
        const target = last ? String(Number(monitor.player.frameCount) - 1) : "0"
        return monitor.requestSeek(monitor.player.frameStartTicks(target), target)
    }
    function play() {
        if (!monitor.activeTransport || !monitor.activeTransport.canPlay) return ""
        const requestId = String(monitor.activeTransport.requestPlay())
        monitor.transportCommandRequested("play", requestId)
        return requestId
    }
    function stop() {
        if (!monitor.activeTransport) return ""
        const requestId = String(monitor.activeTransport.requestStop())
        monitor.transportCommandRequested("stop", requestId)
        return requestId
    }

    // The engine's acknowledgment closes a player-mode seek.
    Connections {
        target: monitor.playerMode ? monitor.player.transport : null
        ignoreUnknownSignals: true
        function onCommandFinished(requestId, accepted, error) {
            monitor.acknowledgeSeek(requestId, accepted, monitor.player.targetFrame,
                                    monitor.player.frameStartTicks(monitor.player.targetFrame), error)
        }
    }

    Keys.onPressed: function(event) {
        if (!monitor.playerMode) return
        if (event.key === Qt.Key_Space) {
            if (monitor.activeTransport && monitor.activeTransport.playing) monitor.stop()
            else monitor.play()
        } else if (event.key === Qt.Key_Left) {
            monitor.stepFrame(-1)
        } else if (event.key === Qt.Key_Right) {
            monitor.stepFrame(1)
        } else if (event.key === Qt.Key_Home) {
            monitor.seekEdge(false)
        } else if (event.key === Qt.Key_End) {
            monitor.seekEdge(true)
        } else {
            return
        }
        event.accepted = true
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
            Tk.Caption { text: monitor.playbackText; color: monitor.activeTransport && monitor.activeTransport.playing
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
                visible: !monitor.playerMode
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
                active: !monitor.playerMode && monitor.enabled && monitor.frameProvider !== null
                        && monitor.resourceId.length > 0
                crop: true
                caption: ""
                tooltip: monitor.frameDescription
            }
            Tk.MediaFrameView {
                objectName: "monitorMediaFrame"
                anchors.fill: parent
                visible: monitor.playerMode
                player: monitor.player
            }
            Tk.Caption {
                objectName: "monitorFrameOverlay"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: Tk.Theme.space.sm
                visible: monitor.overlaysVisible && monitor.frameState !== "ready"
                         && !(monitor.playerMode && monitor.activeTransport && monitor.activeTransport.playing
                              && monitor.frameState === "loading")
                text: monitor.frameError.length > 0 ? monitor.frameError
                      : qsTr("Frame %1 · %2 ticks · %3").arg(monitor.displayedFrameId || "—")
                           .arg(monitor.displayedTicks || "—").arg(monitor.frameState)
                color: monitor.frameError.length > 0 ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onClicked: monitor.forceActiveFocus()
                onDoubleClicked: monitor.overlaysVisible = !monitor.overlaysVisible
            }
        }

        Tk.Flex {
            width: parent.width
            Tk.IconButton {
                objectName: "monitorPreviousFrame"
                iconName: "step-back"
                tooltip: qsTr("Seek previous frame")
                enabled: !!monitor.activeTransport && monitor.activeTransport.canSeek
                         && (monitor.playerMode ? Number(monitor.player.targetFrame) > 0
                                                : monitor.previousFrameId.length > 0)
                onClicked: monitor.stepFrame(-1)
            }
            Tk.IconButton {
                objectName: "monitorPlay"
                iconName: "play"
                tooltip: monitor.activeTransport && !monitor.activeTransport.canPlay
                         ? (monitor.activeTransport.playUnavailableReason || qsTr("Playback unavailable"))
                         : qsTr("Play")
                enabled: !!monitor.activeTransport && !monitor.activeTransport.playing && monitor.activeTransport.canPlay
                onClicked: monitor.play()
            }
            Tk.IconButton {
                objectName: "monitorStop"
                iconName: "square"
                tooltip: qsTr("Stop playback")
                enabled: !!monitor.activeTransport && monitor.activeTransport.playing
                onClicked: monitor.stop()
            }
            Tk.IconButton {
                objectName: "monitorNextFrame"
                iconName: "step-forward"
                tooltip: qsTr("Seek next frame")
                enabled: !!monitor.activeTransport && monitor.activeTransport.canSeek
                         && (monitor.playerMode ? monitor.player.targetFrame.length > 0
                                                  && Number(monitor.player.targetFrame) < Number(monitor.player.frameCount) - 1
                                                : monitor.nextFrameId.length > 0)
                onClicked: monitor.stepFrame(1)
            }
            Tk.Spacer { }
            Tk.Mono {
                objectName: "monitorFrameIdentity"
                text: monitor.displayedFrameId.length
                      ? (monitor.playerMode && monitor.player.presentedTimecode.length
                         ? qsTr("%1 · %2 · %3").arg(monitor.player.presentedTimecode)
                                               .arg(monitor.displayedFrameId).arg(monitor.displayedTicks)
                         : qsTr("%1 · %2").arg(monitor.displayedFrameId).arg(monitor.displayedTicks))
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
