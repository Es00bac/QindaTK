// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-071 on the real stack: the live counterpart of video-edit-surfaces.
// Generated test signals (colour bars rotated per shot, a frame code and a
// 1 kHz pop with a white flash each second) are cut into a nine-second
// sequence with linked picture and sound. TrimView, TransitionEditor and
// MulticamView edit it through SequenceEditor and MulticamModel; the program
// monitor renders the result, the timeline draws decoded peaks, every edit is
// one undo step, and Export queues a verified FFV1/PCM render of what the
// monitor shows. Media is rewritten into <temp>/qindatk-sequence-edit-live.
Item {
    id: page
    width: 1280
    height: 800

    LiveSequence { id: live }

    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column

            Tk.ToolBar {
                objectName: "liveToolbar"
                width: parent.width
                compact: true
                Tk.Overline { text: qsTr("SEQUENCE EDIT · LIVE") }
                Tk.ToolSeparator { }
                Tk.IconButton {
                    objectName: "liveUndo"
                    iconName: "undo-2"
                    enabled: live.timeline.canUndo
                    tooltip: live.timeline.canUndo ? qsTr("Undo %1").arg(live.timeline.undoText) : qsTr("Nothing to undo")
                    onClicked: live.timeline.undo()
                }
                Tk.IconButton {
                    objectName: "liveRedo"
                    iconName: "redo-2"
                    enabled: live.timeline.canRedo
                    tooltip: live.timeline.canRedo ? qsTr("Redo %1").arg(live.timeline.redoText) : qsTr("Nothing to redo")
                    onClicked: live.timeline.redo()
                }
                Tk.ToolSeparator { }
                Tk.Switch {
                    objectName: "liveLinked"
                    small: true
                    text: qsTr("Linked sound")
                    checked: live.editor.linked
                    tooltip: qsTr("Edits move each picture's linked sound with it")
                    onToggled: live.editor.linked = checked
                }
                Tk.Switch {
                    objectName: "liveFrameExact"
                    small: true
                    text: qsTr("Frame exact")
                    checked: live.editor.frameExact
                    tooltip: qsTr("Edit points land on program frame starts")
                    onToggled: live.editor.frameExact = checked
                }
                Tk.Switch {
                    objectName: "liveAudioFollows"
                    small: true
                    text: qsTr("Audio follows video")
                    checked: live.multicam.audioFollowsVideo
                    tooltip: qsTr("A multicam switch also cuts the linked sound")
                    onToggled: live.multicam.audioFollowsVideo = checked
                }
                Tk.Spacer { }
                Tk.Badge {
                    objectName: "liveRevision"
                    text: qsTr("REV %1 · %2 UNDO").arg(live.timeline.revision).arg(live.timeline.historyCount)
                    variant: live.timeline.canUndo ? "accent" : "default"
                    tooltip: qsTr("Timeline revision and undo steps")
                }
                Tk.Button {
                    objectName: "liveExport"
                    text: qsTr("Export")
                    iconName: "download"
                    small: true
                    variant: "accent"
                    enabled: live.ready && live.programPlayer.state === "ready"
                    tooltip: qsTr("Queue an FFV1/PCM Matroska render of the program from the original media")
                    onClicked: live.exportProgram()
                }
            }

            Tk.Box {
                width: parent.width
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                padding: Tk.Theme.space.sm
                Tk.Grid {
                    objectName: "liveEditGrid"
                    columns: "minmax(0, 1fr) minmax(0, 1.15fr) minmax(0, 0.95fr)"
                    rows: "minmax(0, 1.25fr) minmax(0, 1fr)"
                    columnGap: Tk.Theme.space.sm
                    rowGap: Tk.Theme.space.sm

                    Tk.Panel {
                        objectName: "liveProgramPanel"
                        title: qsTr("Program")
                        iconName: "monitor-play"
                        padding: 0
                        Tk.SourceProgramMonitor {
                            objectName: "liveProgramMonitor"
                            width: parent.width
                            height: parent.height
                            role: "program"
                            viewId: "sequence-edit-live-program"
                            player: live.programPlayer
                            tooltip: qsTr("The edited sequence as it renders and exports")
                        }
                    }
                    Tk.Panel {
                        objectName: "liveMulticamPanel"
                        title: qsTr("Multicam · V1 5–9 s")
                        iconName: "video"
                        padding: 0
                        Tk.MulticamView {
                            objectName: "liveMulticam"
                            width: parent.width
                            height: parent.height
                            multicam: live.multicam
                            angleColumns: 3
                            assetProvider: live.assets
                            viewId: "sequence-edit-live-multicam"
                        }
                    }
                    Tk.Panel {
                        objectName: "liveTransitionPanel"
                        title: qsTr("Transition into %1").arg(live.editor.transitionClipId.length
                                                             ? live.timeline.clipData(live.editor.transitionClipId).label || "—" : "—")
                        iconName: "blend"
                        padding: 0
                        Tk.TransitionEditor {
                            objectName: "liveTransition"
                            width: parent.width
                            height: parent.height
                            editor: live.editor
                        }
                    }
                    TrimPanel {
                        live: live
                        Tk.Grid.row: 1
                        Tk.Grid.column: 0
                        Tk.Grid.columnSpan: 3
                    }
                }
            }

            Tk.EditTimeline {
                id: timeline
                objectName: "liveTimeline"
                width: parent.width
                Tk.Flex.basis: Tk.Theme.size.rowLg * 6 // ruler, markers, V1, A1, scrollbar
                Tk.Flex.shrink: 0
                model: live.timeline
                mediaProvider: live.peaks
                playheadTicks: live.programPlayer.presentedTicks || "0"
                tooltip: qsTr("Sequence timeline: V1 picture, A1 linked sound")
                onSeekRequested: function(ticks) { live.programPlayer.seekTicks(ticks) }
                onClipActivated: function(clipId) {
                    const previous = live.timeline.adjacentClip(clipId, -1)
                    if (previous.length) live.editor.selectEditPoint(previous, clipId)
                    live.editor.transitionClipId = clipId
                }
                Component.onCompleted: timeline.viewport.pixelsPerSecond = 120
            }

            Tk.StatusBar {
                width: parent.width
                Tk.StatusField {
                    objectName: "liveStatus"
                    actionable: false
                    text: live.status
                }
                Tk.Spacer { }
                Tk.StatusField {
                    objectName: "livePeaks"
                    actionable: false
                    text: qsTr("Peaks: %1 decoded").arg(live.peaks.completedCount)
                }
                Tk.StatusField {
                    objectName: "liveQueue"
                    actionable: false
                    visible: live.queueOpen
                    text: {
                        const jobs = live.renderQueue.jobs
                        const last = jobs.length ? jobs[jobs.length - 1] : null
                        return last ? qsTr("Export %1 · %2 in queue").arg(last.state).arg(jobs.length) : ""
                    }
                }
            }
        }
    }
}
