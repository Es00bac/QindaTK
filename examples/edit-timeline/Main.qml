// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-048 fixture: 100 tracks and 10,000 clips in the reference TimelineModel.
// Peaks come from the labeled synthetic reference backend, not decoded media.
// Positions here stay below 2^53, so plain JS integers convert exactly; a
// host with longer sessions passes decimal strings end to end.
Item {
    id: page
    width: 1280
    height: 800

    property string playhead: "0"
    readonly property real second: 705600000

    function ticks(seconds) { return String(Math.round(seconds * page.second)) }
    function uuid(prefix, n) { return prefix + "-0000-4000-8000-" + String(n).padStart(12, "0") }
    function buildSession() {
        const palette = ["#3b82f6", "#22c55e", "#eab308", "#f97316", "#a855f7", "#14b8a6"]
        const tracks = [], clips = []
        for (let t = 0; t < 100; ++t) {
            const kind = t < 70 ? "audio" : t < 90 ? "video" : "midi"
            const name = kind === "audio" ? "Audio " + (t + 1) : kind === "video" ? "Video " + (t - 69) : "MIDI " + (t - 89)
            tracks.push({ id: page.uuid("10000000", t), name: name, kind: kind,
                          color: palette[t % palette.length], routing: kind === "audio" ? "→ Bus " + (1 + t % 4) : "",
                          muted: t === 3, solo: false, armed: t === 1, locked: t === 5 })
            for (let c = 0; c < 100; ++c) {
                const n = t * 100 + c
                const start = c * 2 + (t % 7) * 0.05
                const length = 0.6 + ((t * 7 + c * 3) % 13) / 10
                clips.push({ id: page.uuid("20000000", n), trackId: page.uuid("10000000", t),
                             start: page.ticks(start), duration: page.ticks(length),
                             sourceOffset: page.ticks((c % 5) * 0.25), sourceDuration: page.ticks(6),
                             resourceId: "demo:" + (t % 12), kind: kind,
                             label: (kind === "video" ? "Shot " : kind === "midi" ? "Phrase " : "Take ") + (c + 1),
                             color: c % 17 === 0 ? palette[(t + c) % palette.length] : "" })
            }
        }
        const markers = []
        for (let m = 0; m < 12; ++m)
            markers.push({ id: page.uuid("30000000", m), position: page.ticks(m * 15 + 1), label: "Cue " + (m + 1) })
        markers.push({ id: page.uuid("30000000", 100), position: page.ticks(30), duration: page.ticks(15),
                       label: "Chorus", color: "#a855f7" })
        markers.push({ id: page.uuid("30000000", 101), position: page.ticks(16), anchor: "musical", label: "Bar 9" })
        return session.loadContent({ timebase: { numerator: "705600000", denominator: "1" },
                                     tracks: tracks, clips: clips, markers: markers,
                                     editRange: { start: page.ticks(4), end: page.ticks(9) } })
    }

    Tk.TimelineModel {
        id: session
        Component.onCompleted: page.buildSession()
    }
    Tk.TimelineMediaProvider {
        id: media
        referenceBackend: true
    }

    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
        Tk.Flex {
            direction: Tk.Flex.Column

            Tk.ToolBar {
                objectName: "timelineToolbar"
                width: parent.width
                compact: true
                Tk.IconButton {
                    objectName: "timelineUndo"
                    iconName: "undo-2"
                    enabled: session.canUndo
                    tooltip: session.canUndo ? qsTr("Undo %1").arg(session.undoText) : qsTr("Nothing to undo")
                    onClicked: session.undo()
                }
                Tk.IconButton {
                    objectName: "timelineRedo"
                    iconName: "redo-2"
                    enabled: session.canRedo
                    tooltip: session.canRedo ? qsTr("Redo %1").arg(session.redoText) : qsTr("Nothing to redo")
                    onClicked: session.redo()
                }
                Tk.ToolSeparator { }
                Tk.IconButton {
                    objectName: "timelineSnap"
                    iconName: "magnet"
                    checked: timeline.snapEnabled
                    tooltip: qsTr("Snap to clip edges, markers, playhead and frames")
                    onClicked: timeline.snapEnabled = !timeline.snapEnabled
                }
                Tk.IconButton {
                    objectName: "timelineRipple"
                    iconName: "between-horizontal-start"
                    checked: timeline.rippleMode
                    tooltip: qsTr("Ripple: moves, trims and deletes shift later clips")
                    onClicked: timeline.rippleMode = !timeline.rippleMode
                }
                Tk.Segmented {
                    objectName: "timelineDisplayMode"
                    small: true
                    model: [{ text: qsTr("Timecode"), value: "timecode" }, { text: qsTr("Seconds"), value: "seconds" },
                            { text: qsTr("Frames"), value: "frames" }, { text: qsTr("Samples"), value: "samples" }]
                    tooltip: qsTr("Time display")
                    onActivated: function(index) { timeline.displayMode = currentValue }
                }
                Tk.ToolSeparator { }
                Tk.IconButton {
                    objectName: "timelineZoomOut"
                    iconName: "zoom-out"
                    tooltip: qsTr("Zoom out (Ctrl+Minus)")
                    onClicked: timeline.zoomBy(0.5)
                }
                Tk.IconButton {
                    objectName: "timelineZoomIn"
                    iconName: "zoom-in"
                    tooltip: qsTr("Zoom in (Ctrl+Plus)")
                    onClicked: timeline.zoomBy(2)
                }
                Tk.IconButton {
                    objectName: "timelineFit"
                    iconName: "move-horizontal"
                    tooltip: qsTr("Fit the whole session (Ctrl+0)")
                    onClicked: timeline.viewport.fitToContent()
                }
                Tk.Spacer { }
                Tk.Caption {
                    objectName: "timelineStatus"
                    text: timeline.statusText
                    color: Tk.Theme.color.danger
                }
            }

            Tk.EditTimeline {
                id: timeline
                objectName: "editTimelineExample"
                width: parent.width
                Tk.Flex.grow: 1
                model: session
                mediaProvider: media
                playheadTicks: page.playhead
                tooltip: qsTr("Session timeline")
                onSeekRequested: function(ticks) { page.playhead = ticks }
                Component.onCompleted: timeline.viewport.pixelsPerSecond = 90
            }

            Tk.StatusBar {
                width: parent.width
                Tk.StatusField {
                    objectName: "timelineCounts"
                    actionable: false
                    text: qsTr("%1 tracks · %2 clips · %3 markers").arg(session.trackCount)
                              .arg(session.clipCount).arg(session.markerCount)
                }
                Tk.StatusField {
                    objectName: "timelineDelegates"
                    actionable: false
                    text: qsTr("Live delegates: %1 clips, %2 track headers, %3 markers")
                              .arg(timeline.viewport.clips.count).arg(timeline.viewport.tracks.count)
                              .arg(timeline.viewport.markers.count)
                }
                Tk.Spacer { }
                Tk.StatusField {
                    objectName: "timelineRevision"
                    actionable: false
                    text: qsTr("Revision %1 · %n selected", "", timeline.selectedClipIds.length).arg(session.revision)
                }
            }
        }
    }
}
