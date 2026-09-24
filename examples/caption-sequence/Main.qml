// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 1120
    height: 700
    property string statusText: qsTr("Export a short PNG sequence and exact timing manifest for a downstream encoder.")
    property string previewDataUrl: ""
    readonly property var times: ["999", "1000", "1500", "2000", "2500"]

    Tk.CaptionTrackModel {
        id: track
        sourceId: "demo-clip"
        trackId: "captions-main"
        language: "en-US"
        Component.onCompleted: track.loadJson(JSON.stringify({version: 1, trackId: track.trackId, language: track.language, cues: [
            {id: "bridge-en", inMs: "1000", outMs: "2000", text: "Beyond the old stone bridge", language: "en-US", styleId: "default", lane: 0},
            {id: "river-fr", inMs: "1500", outMs: "2500", text: "La lumière revient sur la rivière", language: "fr-FR", styleId: "emphasis", lane: 1}]}))
    }
    Tk.CaptionFrameRenderer { id: renderer }
    Tk.CaptionSequenceExporter { id: exporter }
    function updatePreview() {
        const rendered = renderer.renderFixture(track, "1500")
        previewDataUrl = rendered.ok ? rendered.imageDataUrl : ""
    }
    function exportSample() {
        const path = "/tmp/qindatk-caption-sequence-" + Date.now()
        const result = exporter.exportFixtureSequence(track, root.times, path)
        root.statusText = result.ok
            ? qsTr("Wrote %1 ordered frames · %2").arg(result.frameCount).arg(result.manifestPath)
            : result.error
    }
    Connections {
        target: track
        function onChanged() { root.updatePreview() }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.md
        Tk.Flex {
            width: parent.width
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Heading { level: 2; text: qsTr("Caption sequence output") }
                Tk.Caption { text: qsTr("Ordered PNG frames · exact millisecond strings · host decoder seam"); color: Tk.Theme.color.textMuted }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Button {
                objectName: "exportCaptionSequence"
                text: qsTr("Export 5 frames")
                iconName: "download"
                tooltip: qsTr("Write a demonstration PNG sequence and timing manifest")
                onClicked: root.exportSample()
            }
        }
        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1fr) minmax(0, 1fr)"
            columnGap: Tk.Theme.space.md
            Tk.Panel {
                title: qsTr("Caption timing")
                iconName: "captions"
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    padding: Tk.Theme.space.md
                    gap: Tk.Theme.space.sm
                    Tk.Caption { text: qsTr("The cue intervals are [1000, 2000) and [1500, 2500) ms. A frame at the out boundary excludes that cue."); wrapMode: Text.Wrap }
                    Tk.Mono { text: root.times.join("  ·  "); color: Tk.Theme.color.accent }
                    Tk.Caption { text: qsTr("Downstream frame order follows the input list. The manifest preserves each decimal timestamp and active cue IDs."); wrapMode: Text.Wrap; color: Tk.Theme.color.textMuted }
                    Tk.Spacer { Tk.Flex.grow: 1 }
                    Tk.Caption { objectName: "sequenceExportStatus"; text: root.statusText; wrapMode: Text.Wrap; color: exporter.error.length ? Tk.Theme.color.warning : Tk.Theme.color.text }
                }
            }
            Tk.Panel {
                title: qsTr("Rendered sample frame")
                iconName: "film"
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    padding: Tk.Theme.space.md
                    gap: Tk.Theme.space.sm
                    Item {
                        width: parent.width
                        Tk.Flex.grow: 1
                        Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas; radius: Tk.Theme.radius.sm }
                        Image {
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            source: root.previewDataUrl
                            Accessible.role: Accessible.Graphic
                            Accessible.name: qsTr("Sample video frame with two overlapping burned captions")
                        }
                    }
                    Tk.Mono { text: qsTr("Frame index 2 · time %1 ms · active cue IDs: %2").arg("1500").arg(renderer.activeCueIds.join(", ")); color: Tk.Theme.color.textMuted }
                }
            }
        }
    }
    Component.onCompleted: {
        Qt.callLater(function() {
            root.updatePreview()
            root.exportSample()
        })
    }
}
