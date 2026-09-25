// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtCore
import QindaTK as Tk

// WP-072 on the real stack. On start it writes a generated clip (colour bars
// with a burned-in frame code, FFV1 in Matroska) to
// <temp>/qindatk-grading-scopes-live/harbor.mkv and decodes it through
// MediaPlayer. VideoFinishing attaches the ColorGradeEditSession (the program
// grade) and the CaptionTrackModel (burned-in cues) to the player, so the
// monitor shows graded, captioned pixels, and VideoScopesModel, bound to the
// same player, measures exactly the frame presented. Wheel drags and exposure
// edits are single undo steps. The file is rewritten in place on each start.
Rectangle {
    id: page
    width: 1280
    height: 800
    color: Tk.Theme.color.canvas

    readonly property string mediaPath: StandardPaths.writableLocation(StandardPaths.TempLocation)
                                        + "/qindatk-grading-scopes-live/harbor.mkv"
    property string signalJob: ""
    property string status: qsTr("Writing the test clip…")

    Tk.MediaCatalog {
        id: catalog
        onJobFinished: function(jobId, kind, assetId, ok, error) {
            if (jobId !== page.signalJob) return
            if (!ok) { page.status = qsTr("Test clip failed: %1").arg(error); return }
            player.resourceId = page.mediaPath
            page.status = qsTr("Decoding %1").arg(page.mediaPath)
        }
    }
    Tk.ColorGradeEditSession { id: session; clipId: "harbor" }
    Tk.CaptionTrackModel {
        id: captions
        sourceId: "harbor"
        trackId: "captions-en"
        language: "en-US"
        Component.onCompleted: {
            captions.setDisplayRange("0", "4000")
            captions.loadJson(JSON.stringify({version: 1, trackId: "captions-en", language: "en-US", cues: [
                {id: "open", inMs: "0", outMs: "2000", text: "Harbor bars, graded live", language: "en-US", styleId: "default", lane: 0},
                {id: "close", inMs: "2000", outMs: "4000", text: "Burned in after the grade", language: "en-US", styleId: "default", lane: 0}]}))
        }
    }
    Tk.VideoFinishing {
        id: finishing
        gradeSession: session
        captionTrack: captions
        burnInCaptions: true
    }
    Tk.MediaPlayer {
        id: player
        finishing: finishing
        maxPreviewWidth: 640
        maxPreviewHeight: 360
    }
    Tk.VideoScopesModel { id: scopes; player: player }

    Component.onCompleted: {
        // A starting look (warm highlights, cool shadows, a third of a stop
        // down) so the scopes open graded; each is one undo step.
        session.setWheelValue("gain", 0.22, -0.16)
        session.setWheelValue("lift", -0.12, 0.18)
        session.setExposure(-0.35)
        page.signalJob = catalog.generateTestSignal(page.mediaPath,
            ({width: 640, height: 360, frames: 100, channels: 0, popInterval: 50, popPhase: 25}))
        if (!page.signalJob.length) page.status = catalog.lastError
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Flex {
            objectName: "gradingHeader"
            width: parent.width
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Flex {
                direction: Tk.Flex.Column
                Tk.Flex.shrink: 1
                Tk.Heading { level: 2; text: qsTr("Live grading and scopes") }
                Tk.Caption {
                    text: qsTr("MediaPlayer decode · VideoFinishing grade + captions · VideoScopesModel on the presented frame")
                    color: Tk.Theme.color.textMuted
                    elide: Text.ElideRight
                }
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Badge {
                objectName: "gradingPlayerState"
                text: player.state
                variant: player.state === "ready" ? "success" : player.state === "failed" ? "danger" : "default"
                tooltip: qsTr("MediaPlayer state")
            }
            Tk.Switch {
                objectName: "gradingGradeEnabled"
                text: qsTr("Grade")
                checked: finishing.gradeEnabled
                tooltip: qsTr("Bypass or apply the program grade on the monitor and scopes")
                onToggled: finishing.gradeEnabled = checked
            }
            Tk.Switch {
                objectName: "gradingBurnCaptions"
                text: qsTr("Burn captions")
                checked: finishing.burnInCaptions
                tooltip: qsTr("Burn the caption cues into the program pixels after the grade")
                onToggled: finishing.burnInCaptions = checked
            }
            Tk.Button {
                objectName: "gradingUndo"
                text: qsTr("Undo")
                iconName: "undo-2"
                enabled: session.canUndo
                tooltip: qsTr("Undo the last grade gesture")
                onClicked: session.undo()
            }
            Tk.Button {
                objectName: "gradingRedo"
                text: qsTr("Redo")
                iconName: "redo-2"
                enabled: session.canRedo
                tooltip: qsTr("Redo the grade gesture")
                onClicked: session.redo()
            }
        }

        Tk.Grid {
            width: parent.width
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            columns: "minmax(0, 1fr) minmax(0, 1fr)"
            rows: "minmax(0, 1.15fr) minmax(0, 1fr)"
            gap: Tk.Theme.space.sm
            Tk.Panel {
                objectName: "gradingMonitorPanel"
                title: qsTr("Program monitor")
                iconName: "film"
                Tk.Grid.row: 0
                Tk.Grid.column: 0
                Tk.SourceProgramMonitor {
                    objectName: "gradingMonitor"
                    role: "program"
                    player: player
                    tooltip: qsTr("Decoded, graded and captioned frame")
                    accessibleName: qsTr("Graded program monitor")
                }
            }
            GradePanel {
                Tk.Grid.row: 1
                Tk.Grid.column: 0
                session: session
            }
            ScopesPanel {
                Tk.Grid.row: 0
                Tk.Grid.column: 1
                Tk.Grid.rowSpan: 2
                scopes: scopes
            }
        }

        Tk.Flex {
            objectName: "gradingStatus"
            width: parent.width
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Mono {
                objectName: "gradingFrameKey"
                text: player.presentedFrameKey.length ? player.presentedFrameKey : page.status
                color: Tk.Theme.color.textMuted
                elide: Text.ElideMiddle
                Tk.Flex.shrink: 1
            }
            Tk.Spacer { Tk.Flex.grow: 1 }
            Tk.Caption {
                objectName: "gradingFinishingSummary"
                text: qsTr("Finishing: %1").arg(finishing.summary)
                color: Tk.Theme.color.textMuted
            }
            Tk.Caption {
                objectName: "gradingAnalysisCount"
                text: qsTr("%n scope analysis(es)", "", scopes.analysisCount)
                color: Tk.Theme.color.textMuted
            }
        }
    }
}
