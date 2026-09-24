// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: control
    objectName: "trackingPanel"
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Motion tracking")
    Accessible.description: statusText.text

    // Direct snapshot shape from TrackingRotoModel.tracks.
    property var tracks: []
    property string selectedTrackId: ""
    property string startFrame: "0"
    property string endFrame: "0"
    property string currentFrame: "0"
    readonly property var track: tracks.find(function(item) { return String(item.trackId) === control.selectedTrackId }) || ({})
    readonly property var features: track.previewPoints || track.points || []
    property string tooltip: qsTr("Inspect tracking results and correct feature positions")

    signal trackSelected(string trackId)
    signal startRequested(string trackId, string startFrame, string endFrame)
    signal cancelRequested(string trackId)
    signal acceptRequested(string trackId)
    signal correctionRequested(string trackId, string frame, real x, real y)

    function rerun() {
        if (!track.trackId || track.state === "running") return false
        startRequested(String(track.trackId), String(startFrame), String(endFrame)); return true
    }
    function cancel() {
        if (!track.trackId || track.state !== "running") return false
        cancelRequested(String(track.trackId)); return true
    }
    function accept() {
        if (!track.trackId || track.state !== "ready") return false
        acceptRequested(String(track.trackId)); return true
    }
    function correctPoint(frame, x, y) {
        if (!track.trackId || track.state === "running") return false
        const editFrame = frame === undefined || frame === null || String(frame).length === 0
                          ? String(currentFrame) : String(frame)
        const px = Number(x), py = Number(y)
        if (!Number.isFinite(px) || !Number.isFinite(py)) return false
        correctionRequested(String(track.trackId), editFrame, px, py)
        return true
    }

    implicitWidth: Tk.Theme.size.controlLg * 13
    implicitHeight: Tk.Theme.size.controlLg * 9
    padding: Tk.Theme.space.md
    HoverHandler { id: hover }
    Tk.ToolTip { visible: hover.hovered && control.tooltip.length > 0; text: control.tooltip }
    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }

    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("Tracking"); wide: true }
            Tk.ComboBox {
                objectName: "trackingTrackSelect"
                model: control.tracks
                textRole: "label"
                currentIndex: Math.max(0, control.tracks.findIndex(function(item) { return String(item.trackId) === control.selectedTrackId }))
                tooltip: qsTr("Select a tracked feature")
                onActivated: function(index) { control.trackSelected(String(control.tracks[index].trackId)) }
            }
            Tk.Badge { text: String(control.track.state || qsTr("idle")).toUpperCase()
                       variant: control.track.state === "lost" ? "danger" : "info" }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button {
                objectName: "trackingRun"
                text: qsTr("Run")
                tooltip: qsTr("Run or rerun tracking")
                enabled: control.track.state !== "running"
                onClicked: control.rerun()
            }
            Tk.Button {
                objectName: "trackingCancel"
                text: qsTr("Cancel")
                variant: "outline"
                tooltip: qsTr("Cancel the active tracking request")
                enabled: control.track.state === "running"
                onClicked: control.cancel()
            }
        }
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("Reference %1").arg(String(control.track.referenceFrame || "—")) }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption { text: qsTr("%1 points").arg(control.features.length) }
        }
        ListView {
            id: featureList
            objectName: "trackingFeatureList"
            width: parent.width
            height: Math.min(Tk.Theme.size.control * 7,
                             Math.max(Tk.Theme.size.control, contentHeight))
            implicitHeight: height
            clip: true
            model: control.features.slice(0, 128)
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Tracked features")
            delegate: Tk.Flex {
                required property var modelData
                width: featureList.width
                height: Tk.Theme.size.control
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Rectangle {
                    width: Tk.Theme.size.iconSm
                    height: width
                    radius: Tk.Theme.radius.full
                    color: modelData.state === "lost" ? Tk.Theme.color.danger
                         : Tk.Theme.color.accent
                    Accessible.ignored: true
                }
                Tk.Label {
                    objectName: "trackingFeature_" + String(modelData.frame)
                    Tk.Flex.grow: 1
                    text: qsTr("Frame %1").arg(String(modelData.frame))
                    color: modelData.state === "lost" ? Tk.Theme.color.danger : Tk.Theme.color.text
                    Accessible.role: Accessible.ListItem
                    Accessible.name: qsTr("Tracking point at frame %1").arg(String(modelData.frame))
                    Accessible.description: String(modelData.state || "tracked")
                        + (modelData.state === "lost" ? qsTr("; tracking lost") : "")
                }
                Tk.Caption {
                    text: modelData.state === "lost" ? qsTr("LOST")
                        : Math.round(Math.max(0, Math.min(1, Number(modelData.confidence || 0))) * 100) + "%"
                    color: modelData.state === "lost" ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                }
                Tk.Button {
                    objectName: "trackingPointState_" + String(modelData.frame)
                    text: Math.round(Number(modelData.x)) + ", " + Math.round(Number(modelData.y))
                    variant: "ghost"
                    tooltip: qsTr("Source pixel position at frame %1").arg(String(modelData.frame))
                    enabled: false
                }
            }
        }
        Tk.Caption {
            id: statusText
            objectName: "trackingStatus"
            width: parent.width
            color: control.track.state === "lost" ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
            text: control.track.state === "lost"
                  ? qsTr("Tracking lost. Correct on the viewer or rerun analysis; no result was applied.")
                  : control.track.state === "running" ? qsTr("Analyzing tracking range…")
                    : String(control.track.error || qsTr("Results remain unapplied until accepted."))
        }
        Tk.Button { objectName: "trackingAccept"; text: qsTr("Accept results"); tooltip: qsTr("Apply this tracking result to the document"); enabled: control.track.state === "ready"; onClicked: control.accept() }
    }
}
