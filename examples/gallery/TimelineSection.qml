// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A small multitrack session in the reference TimelineModel. Peaks come from
// the labeled synthetic reference backend, not decoded media.
Section {
    id: section
    title: "Edit timeline"
    note: "EditTimeline · TrackHeader · ClipItem · TimeSelection · MarkerLane"

    property string playhead: "0"
    function ticks(seconds) { return String(Math.round(seconds * 705600000)) }
    function uuid(prefix, n) { return prefix + "-0000-4000-8000-" + String(n).padStart(12, "0") }

    Tk.TimelineModel { id: session }
    Tk.TimelineMediaProvider { id: media; referenceBackend: true }
    Component.onCompleted: {
        session.loadContent({
            tracks: [{ id: uuid("10000000", 1), name: "Dialogue", kind: "audio", routing: "→ Bus 1" },
                     { id: uuid("10000000", 2), name: "Music", kind: "audio", solo: true },
                     { id: uuid("10000000", 3), name: "Picture", kind: "video", locked: true }],
            clips: [{ id: uuid("20000000", 1), trackId: uuid("10000000", 1), start: ticks(0.5), duration: ticks(2.5), label: "Line 1", kind: "audio", resourceId: "dialogue" },
                    { id: uuid("20000000", 2), trackId: uuid("10000000", 1), start: ticks(3.5), duration: ticks(2), label: "Line 2", kind: "audio", resourceId: "dialogue-2" },
                    { id: uuid("20000000", 3), trackId: uuid("10000000", 2), start: ticks(0), duration: ticks(6), label: "Score", kind: "audio", resourceId: "score", color: "#a855f7" },
                    { id: uuid("20000000", 4), trackId: uuid("10000000", 3), start: ticks(1), duration: ticks(4), label: "Shot 12A", kind: "video", resourceId: "shot" }],
            markers: [{ id: uuid("30000000", 1), position: ticks(3.5), label: "Cut" },
                      { id: uuid("30000000", 2), position: ticks(8), duration: ticks(2), label: "Outro" }],
            editRange: { start: ticks(6.5), end: ticks(7.5) } })
    }

    Tk.EditTimeline {
        objectName: "galleryEditTimeline"
        implicitHeight: 250 // a Flex column sizes children by implicit height
        model: session
        mediaProvider: media
        playheadTicks: section.playhead
        tooltip: qsTr("Gallery timeline")
        Tk.Flex.shrink: 0
        onSeekRequested: function(ticks) { section.playhead = ticks }
        Component.onCompleted: viewport.pixelsPerSecond = 80
    }
}
