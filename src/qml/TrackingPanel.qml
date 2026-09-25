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
    // Consecutive lost frames the host tracker searches through before stopping.
    property int occlusionFrames: 0
    // Job progress from TrackingRotoModel (analysisProgress, loadingFrames,
    // frameLoadProgress); shown while a job runs.
    property real analysisProgress: 0
    property bool loadingFrames: false
    property real frameLoadProgress: 0
    // Apply-target mapping: masks the selected track can drive
    // ([{maskId, label}], e.g. TrackingRotoModel.masks) and the frame whose
    // mask shape is carried along the tracked motion.
    property var targets: []
    property string targetMaskId: ""
    property string maskSourceFrame: startFrame
    property real offsetX: 0
    property real offsetY: 0
    readonly property bool running: track.state === "running"
    readonly property var target: targets.find(function(item) { return String(item.maskId) === control.targetMaskId }) || ({})
    readonly property var track: tracks.find(function(item) { return String(item.trackId) === control.selectedTrackId }) || ({})
    readonly property var features: (track.previewPoints || []).length
                                    ? track.previewPoints : (track.points || [])
    readonly property int lostCount: features.filter(function(item) { return item.state === "lost" }).length
    property string tooltip: qsTr("Inspect tracking results and correct feature positions")

    signal trackSelected(string trackId)
    signal startRequested(string trackId, string startFrame, string endFrame, int occlusionFrames)
    signal cancelRequested(string trackId)
    signal acceptRequested(string trackId)
    signal correctionRequested(string trackId, string frame, real x, real y)
    signal optionsChangeRequested(string trackId, int radius, int searchRadius, string motion)
    signal rebaseRequested(string trackId, string frame)
    signal offsetRequested(string trackId, real dx, real dy, string fromFrame, string toFrame)
    signal targetSelected(string maskId)
    signal applyToMaskRequested(string trackId, string maskId, string sourceFrame,
                                string fromFrame, string toFrame)

    function rerun() {
        if (!track.trackId || track.state === "running") return false
        startRequested(String(track.trackId), String(startFrame), String(endFrame),
                       Math.max(0, occlusionFrames))
        return true
    }
    function cancel() {
        if (!track.trackId || track.state !== "running") return false
        cancelRequested(String(track.trackId)); return true
    }
    function accept() {
        if (!track.trackId || track.state !== "ready" || !(track.previewPoints || []).length)
            return false
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
    // Radii are tracking-proxy pixels (source pixels up to 4 MP); motion is
    // "translation" or "similarity" (also solves rotation and scale).
    function setOptions(radius, searchRadius, motion) {
        if (!track.trackId || running) return false
        const r = Math.round(Number(radius)), s = Math.round(Number(searchRadius))
        const m = String(motion || track.motion || "translation")
        if (!(r >= 2 && r <= 16) || !(s >= 1 && s <= 64)
                || (m !== "translation" && m !== "similarity")) return false
        optionsChangeRequested(String(track.trackId), r, s, m)
        return true
    }
    function rebase() {
        if (!track.trackId || running) return false
        const point = (track.points || []).find(function(item) { return String(item.frame) === String(control.currentFrame) })
        if (!point || point.state === "lost") return false
        rebaseRequested(String(track.trackId), String(currentFrame))
        return true
    }
    function offset(dx, dy) {
        const x = Number(dx === undefined ? offsetX : dx), y = Number(dy === undefined ? offsetY : dy)
        if (!track.trackId || running || !Number.isFinite(x) || !Number.isFinite(y)
                || (x === 0 && y === 0) || !(track.points || []).length) return false
        offsetRequested(String(track.trackId), x, y, String(startFrame), String(endFrame))
        return true
    }
    function applyToMask() {
        if (!track.trackId || running || !target.maskId || !(track.points || []).length) return false
        applyToMaskRequested(String(track.trackId), String(target.maskId), String(maskSourceFrame),
                             String(startFrame), String(endFrame))
        return true
    }

    implicitWidth: Tk.Theme.size.controlLg * 16
    implicitHeight: Tk.Theme.size.controlLg * 15
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
                       variant: control.track.state === "lost" ? "danger"
                              : control.lostCount > 0 ? "warning" : "info" }
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
            objectName: "trackingRange"
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.NumberField {
                objectName: "trackingStartFrame"
                Tk.Flex.flex: 1
                label: qsTr("In"); from: 0; to: 1e7
                value: Number(control.startFrame)
                enabled: !control.running
                tooltip: qsTr("First source frame to analyze")
                onValueModified: function(value) { control.startFrame = String(Math.round(value)) }
            }
            Tk.NumberField {
                objectName: "trackingEndFrame"
                Tk.Flex.flex: 1
                label: qsTr("Out"); from: 0; to: 1e7
                value: Number(control.endFrame)
                enabled: !control.running
                tooltip: qsTr("Last source frame to analyze; below In tracks backwards")
                onValueModified: function(value) { control.endFrame = String(Math.round(value)) }
            }
            Tk.NumberField {
                objectName: "trackingOcclusion"
                Tk.Flex.flex: 1
                label: qsTr("Occl"); from: 0; to: 30
                value: control.occlusionFrames
                enabled: !control.running
                tooltip: qsTr("Lost frames searched through before tracking stops")
                onValueModified: function(value) { control.occlusionFrames = Math.round(value) }
            }
        }
        Tk.Flex {
            objectName: "trackingOptions"
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.NumberField {
                objectName: "trackingPatchRadius"
                Tk.Flex.flex: 1
                label: qsTr("Patch"); from: 2; to: 16
                value: Number(control.track.radius || 5)
                enabled: Boolean(control.track.trackId) && !control.running
                tooltip: qsTr("Patch radius in tracking pixels")
                onValueModified: function(value) { control.setOptions(value, control.track.searchRadius || 16, control.track.motion) }
            }
            Tk.NumberField {
                objectName: "trackingSearchRadius"
                Tk.Flex.flex: 1
                label: qsTr("Search"); from: 1; to: 64
                value: Number(control.track.searchRadius || 16)
                enabled: Boolean(control.track.trackId) && !control.running
                tooltip: qsTr("Per-frame search radius in tracking pixels")
                onValueModified: function(value) { control.setOptions(control.track.radius || 5, value, control.track.motion) }
            }
            Tk.ComboBox {
                objectName: "trackingMotion"
                Tk.Flex.flex: 1
                model: [{text: qsTr("Translation"), value: "translation"},
                        {text: qsTr("Rotate + scale"), value: "similarity"}]
                textRole: "text"
                currentIndex: control.track.motion === "similarity" ? 1 : 0
                enabled: Boolean(control.track.trackId) && !control.running
                tooltip: qsTr("Motion model the tracker solves")
                onActivated: function(index) {
                    control.setOptions(control.track.radius || 5, control.track.searchRadius || 16,
                                       index === 1 ? "similarity" : "translation")
                }
            }
        }
        Tk.Flex {
            objectName: "trackingProgressRow"
            width: parent.width
            visible: control.running || control.loadingFrames
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.ProgressBar {
                objectName: "trackingProgress"
                Tk.Flex.grow: 1
                from: 0; to: 1
                value: control.loadingFrames ? control.frameLoadProgress : control.analysisProgress
                tooltip: control.loadingFrames ? qsTr("Decoding source frames") : qsTr("Tracking analysis")
            }
            Tk.Caption {
                objectName: "trackingProgressText"
                text: (control.loadingFrames ? qsTr("Decoding %1%") : qsTr("Analyzing %1%"))
                      .arg(Math.round(100 * Math.max(0, Math.min(1, control.loadingFrames
                          ? control.frameLoadProgress : control.analysisProgress))))
            }
        }
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Caption {
                text: qsTr("Reference %1").arg(control.track.referenceFrame !== undefined
                                                  ? String(control.track.referenceFrame) : "—")
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption { text: qsTr("%1 points").arg(control.features.length) }
        }
        ListView {
            id: featureList
            objectName: "trackingFeatureList"
            width: parent.width
            height: Math.min(Tk.Theme.size.control * 5,
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
                  : control.lostCount > 0
                    ? qsTr("Lost on %1 frames and reacquired; lost frames hold the last trusted position.").arg(control.lostCount)
                  : (control.track.previewPoints || []).length
                    ? qsTr("Results remain unapplied until accepted.")
                  : (control.track.points || []).length
                    ? qsTr("%1 accepted tracking points.").arg(control.track.points.length)
                    : String(control.track.error || qsTr("Run tracking to generate points."))
        }
        Tk.Flex {
            objectName: "trackingEdits"
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Button { objectName: "trackingAccept"; text: qsTr("Accept results"); tooltip: qsTr("Apply this tracking result to the document"); enabled: control.track.state === "ready" && (control.track.previewPoints || []).length > 0; onClicked: control.accept() }
            Tk.Button {
                objectName: "trackingRebase"
                text: qsTr("Rebase")
                variant: "outline"
                tooltip: qsTr("Use the accepted point at the current frame as the new reference")
                enabled: !control.running && (control.track.points || []).some(function(item) {
                    return String(item.frame) === String(control.currentFrame) && item.state !== "lost" })
                onClicked: control.rebase()
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.NumberField {
                objectName: "trackingOffsetX"
                label: "X"; from: -1e4; to: 1e4; decimals: 1; small: true
                value: control.offsetX
                tooltip: qsTr("Horizontal offset in source pixels")
                onValueModified: function(value) { control.offsetX = value }
            }
            Tk.NumberField {
                objectName: "trackingOffsetY"
                label: "Y"; from: -1e4; to: 1e4; decimals: 1; small: true
                value: control.offsetY
                tooltip: qsTr("Vertical offset in source pixels")
                onValueModified: function(value) { control.offsetY = value }
            }
            Tk.Button {
                objectName: "trackingOffset"
                text: qsTr("Offset")
                variant: "outline"
                tooltip: qsTr("Shift accepted points in the In–Out range")
                enabled: !control.running && (control.offsetX !== 0 || control.offsetY !== 0)
                onClicked: control.offset()
            }
        }
        Tk.Flex {
            objectName: "trackingApplyRow"
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Caption { text: qsTr("Drive mask") }
            Tk.ComboBox {
                objectName: "trackingTargetSelect"
                Tk.Flex.grow: 1
                model: control.targets
                textRole: "label"
                placeholderText: qsTr("No mask")
                currentIndex: control.targets.findIndex(function(item) { return String(item.maskId) === control.targetMaskId })
                tooltip: qsTr("Mask moved by this track")
                onActivated: function(index) {
                    control.targetMaskId = String(control.targets[index].maskId)
                    control.targetSelected(control.targetMaskId)
                }
            }
            Tk.Caption { text: qsTr("from %1").arg(control.maskSourceFrame) }
            Tk.Button {
                objectName: "trackingApplyMask"
                text: qsTr("Apply to mask")
                tooltip: qsTr("Key the mask along accepted tracking in the In–Out range")
                enabled: !control.running && Boolean(control.target.maskId) && (control.track.points || []).length > 0
                onClicked: control.applyToMask()
            }
        }
    }
}
