// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Sloom's sequencer timeline as the dock canvas: a chip tool row, a ruler
// in seconds, one Grid row per track (header | lane), clips as accent
// boxes with a striped thumbnail band, and a playhead in a Stack over the
// lanes. 90 px per second at 100% zoom.
Tk.Box {
    id: timeline

    property real pixelsPerSecond: 90 * zoom.value
    property real playheadSeconds: 0.0
    readonly property int headerWidth: 56
    readonly property real laneHeight: Tk.Theme.size.rowLg + Tk.Theme.space.sm

    readonly property var tracks: [
        { "name": "V1", "clips": [{ "name": "walk-wide.mp4", "start": 0.3, "length": 5.2 }], "audio": false },
        { "name": "V2", "clips": [{ "name": "turn-cu.mp4", "start": 4.1, "length": 8.0 }], "audio": false },
        { "name": "V3", "clips": [], "audio": false },
        { "name": "V4", "clips": [], "audio": false },
        { "name": "A1", "clips": [], "audio": true }
    ]

    color: Tk.Theme.color.canvas
    borderWidth: 1
    borderColor: Tk.Theme.color.divider
    padding: Tk.Theme.space.sm

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Icon { name: "film"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.accent }
            Tk.Label { text: "Sequencer Timeline"; font.weight: Font.DemiBold }
            Tk.Caption {
                text: "Visual and audio clips live on independent timed lanes; drag for rough placement, use the inspector for precise timing."
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
        }

        // ---- tool row ----
        // AGENT-NOTE: wraps so a narrow canvas gets two tool lines instead of
        // elided chips; every chip refuses to shrink (Tk.Flex.shrink: 0).
        Tk.Flex {
            objectName: "timelineTools"
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Chip { text: "Select"; iconName: "mouse-pointer-2"; small: true; active: true }
            Tk.Chip { text: "Cut"; iconName: "scissors"; small: true }
            Tk.Chip { text: "Slip"; iconName: "move-horizontal"; small: true }
            Tk.Chip { text: "Hand"; iconName: "hand"; small: true }
            Tk.Chip { text: "Snap"; iconName: "magnet"; small: true; emphasis: "tinted" }
            Tk.Chip { text: "Add Key"; iconName: "diamond"; small: true }
            Tk.IconButton { iconName: "chevron-left"; tooltip: "Previous key" }
            Tk.IconButton { iconName: "chevron-right"; tooltip: "Next key" }
            Tk.Spacer {}
            Tk.Icon { name: "zoom-in"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted; Tk.Flex.shrink: 0 }
            Tk.Slider { id: zoom; value: 1.0; from: 0.25; to: 3.0; stepSize: 0.05; implicitWidth: 100; Tk.Flex.shrink: 0 }
            Tk.Mono { text: Math.round(zoom.value * 100) + "%"; Tk.Flex.shrink: 0 }
            Tk.Chip { text: "Zoom to fit"; small: true; onClicked: zoom.value = 1.0 }
            Tk.Badge { text: "2 clips"; variant: "accent" }
        }

        // ---- ruler + lanes ----
        Tk.Stack {
            objectName: "timelineLanes"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0

            Tk.Grid {
                id: grid
                columns: timeline.headerWidth + " 1fr"
                columnGap: Tk.Theme.space.xs
                rowGap: 1
                alignItems: Tk.Grid.Stretch

                Tk.Box { color: "transparent"; implicitHeight: Tk.Theme.size.row }
                Tk.Box {
                    objectName: "timelineRulerBox"
                    color: Tk.Theme.color.panelAlt
                    borderBottom: 1
                    borderColor: Tk.Theme.color.divider
                    implicitHeight: Tk.Theme.size.row
                    Tk.Ruler {
                        objectName: "timelineRuler"
                        pixelsPerUnit: timeline.pixelsPerSecond
                        unit: "s"
                        majorEvery: 2
                        minorEvery: 0.5
                        cursorPosition: timeline.playheadSeconds * timeline.pixelsPerSecond
                    }
                }

                Repeater {
                    model: timeline.tracks
                    Tk.Box {
                        required property var modelData
                        objectName: "trackHeader_" + modelData.name
                        Tk.Grid.column: 0
                        color: Tk.Theme.color.panel
                        borderWidth: 1
                        borderColor: Tk.Theme.color.divider
                        radius: Tk.Theme.radius.xs
                        padding: Tk.Theme.space.xs
                        implicitHeight: timeline.laneHeight
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            justify: Tk.Flex.Center
                            gap: 1
                            Tk.Overline { title: modelData.name; color: modelData.audio ? Tk.Theme.color.warning : Tk.Theme.color.accent }
                            Tk.Caption { text: modelData.clips.length + (modelData.clips.length === 1 ? " clip" : " clips"); font.pixelSize: Tk.Theme.font.micro }
                        }
                    }
                }
                Repeater {
                    model: timeline.tracks
                    Tk.Box {
                        id: lane
                        required property var modelData
                        required property int index
                        objectName: "trackLane_" + modelData.name
                        Tk.Grid.column: 1
                        Tk.Grid.row: index + 1
                        color: Tk.Theme.color.panelAlt
                        borderBottom: 1
                        borderColor: Tk.Theme.color.divider
                        implicitHeight: timeline.laneHeight
                        fill: false
                        clipContent: true

                        Tk.Caption {
                            visible: lane.modelData.clips.length === 0
                            anchors.left: parent.left
                            anchors.leftMargin: Tk.Theme.space.sm
                            anchors.verticalCenter: parent.verticalCenter
                            text: lane.modelData.audio
                                  ? "Add audio clips or video-with-audio clips from the source bin into this lane."
                                  : "Add image, video, composition, or text items from the source bin into this video lane."
                            font.pixelSize: Tk.Theme.font.micro
                        }
                        Repeater {
                            model: lane.modelData.clips
                            Tk.Box {
                                required property var modelData
                                objectName: "clip_" + modelData.name
                                x: modelData.start * timeline.pixelsPerSecond
                                width: modelData.length * timeline.pixelsPerSecond
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.topMargin: 2
                                anchors.bottomMargin: 2
                                color: Tk.Theme.color.accentSubtle
                                borderWidth: 1
                                borderColor: Tk.Theme.color.accent
                                radius: Tk.Theme.radius.xs
                                clipContent: true
                                fill: false
                                Tk.Flex {
                                    anchors.fill: parent
                                    direction: Tk.Flex.Column
                                    Tk.Box {
                                        implicitHeight: Tk.Theme.size.row - Tk.Theme.space.sm
                                        paddingLeft: Tk.Theme.space.xs
                                        Tk.Mono { text: modelData.name; color: Tk.Theme.color.accentText }
                                    }
                                    Tk.Flex {
                                        direction: Tk.Flex.Row
                                        gap: 1
                                        Tk.Flex.grow: 1
                                        Tk.Flex.basis: 0
                                        Repeater {
                                            model: Math.max(1, Math.floor(modelData.length * timeline.pixelsPerSecond / 24))
                                            Rectangle {
                                                required property int index
                                                implicitWidth: 23
                                                color: index % 2 === 0
                                                       ? Tk.Theme.mix(Tk.Theme.color.accent, Tk.Theme.color.bg, 0.75)
                                                       : Tk.Theme.mix(Tk.Theme.color.danger, Tk.Theme.color.bg, 0.8)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- playhead across the lanes ----
            Item {
                objectName: "playhead"
                Tk.Stack.fill: false
                Tk.Stack.left: timeline.headerWidth + Tk.Theme.space.xs + timeline.playheadSeconds * timeline.pixelsPerSecond - 1
                Tk.Stack.top: 0
                Tk.Stack.bottom: 0
                implicitWidth: 3
                Rectangle { anchors.fill: parent; anchors.leftMargin: 1; anchors.rightMargin: 1; color: Tk.Theme.color.danger }
                Rectangle { width: 9; height: 9; x: -3; y: Tk.Theme.size.row - 9; rotation: 45; color: Tk.Theme.color.danger }
            }
        }
    }
}
