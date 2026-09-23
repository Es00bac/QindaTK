// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// One visible timeline clip: body, label, peaks from the media seam, trim
// affordances, lock and selection state, or a summary of merged tiny clips.
//
// AGENT-CONTRACT: a ClipItem is presentation. EditTimeline supplies its
// geometry and exact tick strings from TimelineViewport rows and routes the
// pointer through TimelineViewport.hitTest, so a clip that scrolls out while
// it is dragged loses nothing: the gesture belongs to the lane, not to this
// delegate. The item only emits intents (select, nudge, activate); the model
// applies them. It releases its media request when pooled or destroyed.
Item {
    id: clipItem

    property string clipId: ""
    property string trackId: ""
    property string label: ""
    property string clipColor: ""
    property string kind: ""
    property string resourceId: ""
    property bool locked: false
    property bool selected: false
    property bool current: false
    property bool preview: false
    property bool ghost: false
    property bool invalid: false
    property bool summary: false
    property int summaryCount: 0
    property bool clippedStart: false
    property bool clippedEnd: false
    property string startTicks: "0"
    property string durationTicks: "0"
    property string visibleSourceStartTicks: ""
    property string visibleSourceEndTicks: ""
    // Accessible time summary supplied by the owner in its display mode.
    property string timeText: ""
    // Owner hit test: "clip", "trimStart", "trimEnd" or "".
    property string hoverPart: ""
    property var mediaProvider: null
    property string ticksPerSecondNumerator: "705600000"
    property string ticksPerSecondDenominator: "1"
    property real columnPixels: Tk.Theme.space.xs
    property string tooltip: clipItem.summary ? qsTr("%n clips", "", clipItem.summaryCount)
                           : clipItem.label.length > 0 ? clipItem.label : qsTr("Clip")
    readonly property var peaks: internal.peaks
    readonly property string mediaState: internal.state
    readonly property bool mediaEligible: !!clipItem.mediaProvider && !clipItem.summary && !clipItem.ghost
                                          && clipItem.resourceId.length > 0 && clipItem.kind !== "video"
                                          && clipItem.visibleSourceStartTicks.length > 0
                                          && clipItem.width >= clipItem.columnPixels
    readonly property int columns: Math.max(1, Math.min(4096, Math.round(clipItem.width
                                                                          / Math.max(1, clipItem.columnPixels))))
    readonly property string mediaKey: clipItem.mediaEligible
                                       ? [clipItem.resourceId, clipItem.visibleSourceStartTicks,
                                          clipItem.visibleSourceEndTicks, clipItem.columns].join("|") : ""
    readonly property bool documentColor: clipItem.clipColor.length > 0
    readonly property color fillColor: clipItem.summary ? Tk.Theme.color.border
                                     : clipItem.documentColor
                                       ? Tk.Theme.alpha(clipItem.clipColor, clipItem.selected ? Tk.Theme.opacity.island
                                                                                     : Tk.Theme.opacity.ghost)
                                       : clipItem.selected ? Tk.Theme.color.chipHoverBg : Tk.Theme.color.chipBg
    readonly property color edgeColor: clipItem.invalid ? Tk.Theme.color.danger
                                     : clipItem.selected ? Tk.Theme.color.accent
                                     : clipItem.locked ? Tk.Theme.color.border
                                     : Tk.Theme.color.chipBorder

    signal selectRequested(string clipId, int modifiers)
    signal nudgeRequested(string clipId, int steps)
    signal activated(string clipId)

    function releaseMedia() {
        if (internal.requestId.length > 0 && clipItem.mediaProvider)
            clipItem.mediaProvider.cancel(internal.requestId)
        internal.requestId = ""
    }
    function refreshMedia() {
        if (clipItem.mediaKey.length === 0) {
            releaseMedia()
            internal.state = clipItem.mediaEligible ? "idle" : "none"
            return
        }
        internal.state = "loading"
        internal.requestId = clipItem.mediaProvider.requestPeaks(
                    clipItem.clipId + ":peaks", clipItem.resourceId, clipItem.visibleSourceStartTicks,
                    clipItem.visibleSourceEndTicks, clipItem.ticksPerSecondNumerator,
                    clipItem.ticksPerSecondDenominator, clipItem.columns)
    }

    objectName: "clip_" + clipItem.clipId
    implicitWidth: Tk.Theme.size.thumbnail
    implicitHeight: Tk.Theme.size.rowLg + Tk.Theme.size.waveformHeight
    opacity: clipItem.ghost ? Tk.Theme.opacity.ghost : Tk.Theme.opacity.full
    clip: true

    Accessible.role: Accessible.ListItem
    Accessible.name: clipItem.tooltip
    Accessible.description: [clipItem.timeText, clipItem.locked ? qsTr("Locked") : "",
                             clipItem.invalid ? qsTr("This edit is not allowed here") : ""]
                            .filter(function(part) { return part.length > 0 }).join(". ")
    Accessible.selectable: !clipItem.summary && !clipItem.ghost
    Accessible.selected: clipItem.selected
    Accessible.focusable: !clipItem.summary && !clipItem.ghost
    Accessible.focused: clipItem.current
    Accessible.ignored: clipItem.ghost
    Accessible.onPressAction: if (!clipItem.summary && !clipItem.ghost) clipItem.selectRequested(clipItem.clipId, Qt.NoModifier)
    Accessible.onIncreaseAction: if (!clipItem.locked && !clipItem.summary && !clipItem.ghost) clipItem.nudgeRequested(clipItem.clipId, 1)
    Accessible.onDecreaseAction: if (!clipItem.locked && !clipItem.summary && !clipItem.ghost) clipItem.nudgeRequested(clipItem.clipId, -1)

    // AGENT-NOTE: requests are made directly; TimelineMediaProvider starts
    // backend work from a queued pump, so a burst of superseding requests
    // (initialization, scrolling) never reaches the backend. A deferred
    // Qt.callLater here would run after a recycled delegate was destroyed.
    onMediaKeyChanged: clipItem.refreshMedia()
    Component.onCompleted: clipItem.refreshMedia()
    Component.onDestruction: clipItem.releaseMedia()

    QtObject {
        id: internal
        property var peaks: []
        property string state: "none"
        property string requestId: ""
    }

    Connections {
        target: clipItem.mediaProvider
        ignoreUnknownSignals: true
        function onReady(requestId, ownerKey) {
            if (requestId !== internal.requestId) return
            internal.peaks = clipItem.mediaProvider.result(requestId).peaks
            internal.state = "ready"
        }
        function onFailed(requestId, ownerKey, error) {
            if (requestId !== internal.requestId) return
            internal.peaks = []
            internal.state = "failed"
        }
        function onResourceInvalidated(resourceId) {
            if (resourceId === clipItem.resourceId) clipItem.refreshMedia()
        }
    }

    Rectangle {
        objectName: "clipBody"
        anchors.fill: parent
        radius: Tk.Theme.radius.xs
        color: clipItem.fillColor
        border.width: clipItem.selected || clipItem.invalid ? Tk.Theme.size.focusRing : Tk.Theme.size.border
        border.color: clipItem.edgeColor
    }

    Tk.Waveform {
        objectName: "clipWaveform"
        visible: !clipItem.summary && clipItem.kind !== "video" && clipItem.height > Tk.Theme.size.row
        x: Tk.Theme.size.border
        y: clipLabel.visible ? clipLabel.y + clipLabel.height : Tk.Theme.size.border
        width: Math.max(0, clipItem.width - 2 * Tk.Theme.size.border)
        height: Math.max(0, clipItem.height - y - Tk.Theme.size.border)
        peaks: internal.peaks
        color: clipItem.documentColor ? Tk.Theme.onColor(clipItem.fillColor) : Tk.Theme.color.info
    }

    Tk.Icon {
        objectName: "clipKindIcon"
        visible: clipItem.kind === "video" && !clipItem.summary && clipItem.width > Tk.Theme.size.icon * 2
        name: "film"
        size: Tk.Theme.size.iconSm
        color: Tk.Theme.color.textMuted
        x: Tk.Theme.space.xs
        y: clipItem.height - height - Tk.Theme.space.xs
    }

    Tk.Label {
        id: clipLabel
        objectName: "clipLabel"
        visible: clipItem.width > Tk.Theme.size.icon * 2 && clipItem.height >= Tk.Theme.size.controlSm
        x: Tk.Theme.space.xs + (clipItem.clippedStart ? 0 : Tk.Theme.size.seam)
        y: Tk.Theme.size.border
        width: Math.max(0, clipItem.width - x - Tk.Theme.space.xs - (lockIcon.visible ? lockIcon.width : 0))
        text: clipItem.summary ? clipItem.tooltip : clipItem.label
        font.pixelSize: Tk.Theme.font.caption
        color: clipItem.locked ? Tk.Theme.color.textMuted
             : clipItem.documentColor ? Tk.Theme.onColor(clipItem.fillColor) : Tk.Theme.color.text
    }

    Tk.Icon {
        id: lockIcon
        objectName: "clipLockIcon"
        visible: clipItem.locked && !clipItem.summary && clipItem.width > Tk.Theme.size.icon * 3
        name: "lock"
        size: Tk.Theme.size.iconSm
        color: Tk.Theme.color.textMuted
        x: clipItem.width - width - Tk.Theme.space.xs
        y: Tk.Theme.size.border + Tk.Theme.space.xs
    }

    Rectangle {
        objectName: "clipTrimStartHandle"
        visible: !clipItem.locked && !clipItem.summary && !clipItem.ghost && !clipItem.clippedStart
                 && (clipItem.hoverPart === "trimStart" || clipItem.current)
        width: Tk.Theme.size.seam
        height: clipItem.height
        radius: Tk.Theme.radius.xs
        color: clipItem.hoverPart === "trimStart" ? Tk.Theme.color.accent : Tk.Theme.color.seamHover
    }

    Rectangle {
        objectName: "clipTrimEndHandle"
        visible: !clipItem.locked && !clipItem.summary && !clipItem.ghost && !clipItem.clippedEnd
                 && (clipItem.hoverPart === "trimEnd" || clipItem.current)
        x: clipItem.width - width
        width: Tk.Theme.size.seam
        height: clipItem.height
        radius: Tk.Theme.radius.xs
        color: clipItem.hoverPart === "trimEnd" ? Tk.Theme.color.accent : Tk.Theme.color.seamHover
    }

    Rectangle {
        objectName: "clipFocusRing"
        visible: clipItem.current
        anchors.fill: parent
        color: "transparent"
        radius: Tk.Theme.radius.xs
        border.width: Tk.Theme.size.focusRing
        border.color: Tk.Theme.color.focus
    }
}
