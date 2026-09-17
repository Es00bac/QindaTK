// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A Sloom-like workspace on Tk.DockHost: source bin left, inspector plus a
// Layers/Channels/Paths tab group right, a program monitor in the centre
// zone above the canvas, a timeline at the bottom and a floating History
// panel. Drag headers to tear off and re-dock, drag seams, right-click
// tabs. Run:
//   qtk-preview examples/docking/Main.qml
//   qtk-preview examples/docking/Main.qml --dump | grep dockZone
Rectangle {
    id: root
    width: 1280
    height: 800
    color: Tk.Theme.color.bg

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: 0

        // ---- top bar with layout actions ----
        Tk.Box {
            color: Tk.Theme.color.surface
            borderBottom: 1
            borderColor: Tk.Theme.color.divider
            implicitHeight: Tk.Theme.size.toolbar
            Tk.Flex.shrink: 0

            Tk.Flex {
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                paddingLeft: Tk.Theme.space.md
                paddingRight: Tk.Theme.space.md

                Tk.Overline { title: "Docking"; wide: true }
                Tk.Spacer { size: Tk.Theme.space.md }
                Tk.IconButton {
                    iconName: "maximize-2"; tooltip: "Float the inspector"
                    onClicked: dock.floatPanel("inspector")
                }
                Tk.IconButton {
                    iconName: "eye-off"; tooltip: "Hide the layers group tab"
                    onClicked: dock.hidePanel("layers")
                }
                Tk.IconButton {
                    iconName: "eye"; tooltip: "Show every panel"
                    onClicked: {
                        for (const entry of dock.hiddenPanels) {
                            dock.showPanel(entry)
                        }
                    }
                }
                Tk.IconButton {
                    iconName: "rotate-ccw"; tooltip: "Reset the layout"
                    onClicked: dock.resetLayout()
                }
                Tk.Spacer {}
                Tk.Caption { text: "hidden: " + (dock.hiddenPanels.length > 0 ? dock.hiddenPanels.join(", ") : "none") }
            }
        }

        // ---- the dock host ----
        Tk.DockHost {
            id: dock
            objectName: "dock"
            workspace: "video"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
            // storageKey on the model would persist this arrangement:
            // model: Tk.DockModel { storageKey: "example/docking" }

            canvas: Tk.Box {
                objectName: "canvasBox"
                color: Tk.Theme.color.canvas
                borderWidth: 1
                borderColor: Tk.Theme.color.divider
                fill: false
                Tk.Island {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Tk.Theme.space.md
                    Tk.IconButton { iconName: "zoom-out"; tooltip: "Zoom out" }
                    Tk.Mono { text: "100%" }
                    Tk.IconButton { iconName: "zoom-in"; tooltip: "Zoom in" }
                    Tk.IconButton { iconName: "maximize"; tooltip: "Fit" }
                }
                Tk.Caption {
                    anchors.centerIn: parent
                    text: "canvas"
                }
            }

            Tk.DockPanel {
                panelId: "sourceBin"; title: "Project Source Bin"; iconName: "folder"
                zone: "left"; extent: 230; minWidth: 180
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.SectionHeader { title: "Source Library"; count: "3" }
                    Repeater {
                        model: ["walk-wide.mp4", "turn-cu.mp4", "score.wav"]
                        delegate: Tk.Box {
                            required property string modelData
                            color: Tk.Theme.color.panelAlt
                            borderWidth: 1
                            radius: Tk.Theme.radius.sm
                            padding: Tk.Theme.space.xs
                            interactive: true
                            Tk.Flex {
                                direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
                                Tk.Icon { name: modelData.endsWith(".wav") ? "audio-lines" : "film"; size: Tk.Theme.size.iconLg; color: Tk.Theme.color.accent }
                                Tk.Label { text: modelData; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                                Tk.Caption { text: "V1  V2" }
                            }
                        }
                    }
                }
            }

            Tk.DockPanel {
                panelId: "inspector"; title: "Inspector"; iconName: "sliders-horizontal"
                zone: "right"; order: 0; extent: 260; share: 1.2
                Tk.Scroll {
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.SectionHeader { title: "Selected visual clip" }
                        Tk.Grid {
                            columns: "auto 1fr"; columnGap: Tk.Theme.space.sm; rowGap: Tk.Theme.space.xs
                            alignItems: Tk.Grid.Center
                            Tk.Caption { text: "Track" }      Tk.Mono { text: "Video 2"; horizontalAlignment: Text.AlignRight }
                            Tk.Caption { text: "Start" }      Tk.Mono { text: "4.1s"; horizontalAlignment: Text.AlignRight }
                            Tk.Caption { text: "Duration" }   Tk.Mono { text: "8.0s"; horizontalAlignment: Text.AlignRight }
                            Tk.Caption { text: "Fit mode" }   Tk.Mono { text: "contain"; horizontalAlignment: Text.AlignRight }
                            Tk.Caption { text: "Opacity" }    Tk.Mono { text: "100%"; horizontalAlignment: Text.AlignRight }
                        }
                    }
                }
            }

            Tk.DockPanel {
                panelId: "layers"; title: "Layers"; iconName: "layers"
                zone: "right"; order: 1; group: "image-stack"; groupActive: true
                Tk.Flex {
                    direction: Tk.Flex.Column; gap: 1
                    Repeater {
                        model: ["Lettering", "Balloons", "Line art", "Colour", "Background"]
                        delegate: Tk.Box {
                            required property string modelData
                            required property int index
                            implicitHeight: Tk.Theme.size.row
                            color: index === 1 ? Tk.Theme.color.selection : "transparent"
                            radius: Tk.Theme.radius.xs
                            paddingLeft: Tk.Theme.space.xs
                            Tk.Flex {
                                direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
                                Tk.Icon { name: "eye"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
                                Tk.Label { text: modelData; Tk.Flex.grow: 1 }
                                Tk.Caption { text: "100%" }
                            }
                        }
                    }
                }
            }
            Tk.DockPanel {
                panelId: "channels"; title: "Channels"; iconName: "blend"
                zone: "right"; order: 2; group: "image-stack"; groupActive: false
                Tk.Label { text: "RGB · R · G · B"; muted: true }
            }
            Tk.DockPanel {
                panelId: "paths"; title: "Paths"; iconName: "spline"
                zone: "right"; order: 3; group: "image-stack"; groupActive: false
                Tk.Label { text: "No paths yet."; muted: true }
            }

            Tk.DockPanel {
                panelId: "monitor"; title: "Program Monitor"; iconName: "monitor"
                zone: "center"; extent: 220; minHeight: 120
                Tk.Box {
                    color: Tk.Theme.color.bg
                    borderWidth: 1
                    Tk.Caption { anchors.centerIn: parent; text: "PREVIEW READY · 1080p · 30 fps" }
                }
            }

            Tk.DockPanel {
                panelId: "timeline"; title: "Timeline"; iconName: "film"
                zone: "bottom"; extent: 200; minHeight: 120
                allowedZones: ["bottom", "top"]
                Tk.Flex {
                    direction: Tk.Flex.Column; gap: 1
                    Repeater {
                        model: ["V2", "V1", "A1"]
                        delegate: Tk.Flex {
                            required property string modelData
                            direction: Tk.Flex.Row; gap: Tk.Theme.space.xs
                            Tk.Box {
                                color: Tk.Theme.color.panelAlt; radius: Tk.Theme.radius.xs; implicitWidth: 44; implicitHeight: 28
                                Tk.Mono { anchors.centerIn: parent; text: modelData }
                            }
                            Tk.Box {
                                color: Tk.Theme.color.panelAlt; Tk.Flex.grow: 1; implicitHeight: 28
                                Tk.Box {
                                    visible: modelData !== "A1"
                                    x: modelData === "V1" ? 20 : 240; width: modelData === "V1" ? 200 : 120
                                    anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.margins: 3
                                    color: Tk.Theme.color.accentSubtle; borderWidth: 1; borderColor: Tk.Theme.color.accent; radius: Tk.Theme.radius.xs
                                }
                            }
                        }
                    }
                }
            }

            Tk.DockPanel {
                panelId: "history"; title: "History"; iconName: "history"
                mode: "floating"; zone: "right"
                floatingRect: { "x": 700, "y": 90, "width": 240, "height": 200 }
                Tk.Flex {
                    direction: Tk.Flex.Column; gap: Tk.Theme.space.xs
                    Tk.Label { text: "Import walk-wide.mp4" }
                    Tk.Label { text: "Add clip to V1" }
                    Tk.Label { text: "Trim clip"; accent: true }
                }
            }
        }

        // ---- status line ----
        Tk.Box {
            color: Tk.Theme.color.surface
            borderTop: 1
            borderColor: Tk.Theme.color.divider
            implicitHeight: Tk.Theme.size.statusBar
            Tk.Flex.shrink: 0
            Tk.Flex {
                direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.md
                paddingLeft: Tk.Theme.space.md; paddingRight: Tk.Theme.space.md
                Tk.Caption { text: "generation " + (dock.model ? dock.model.generation : 0) }
                Tk.Caption { text: "floating: " + dock.floatingIds.join(", ") }
                Tk.Spacer {}
                Tk.Caption { text: "drag a header to re-dock · drag seams to resize · right-click a tab" }
            }
        }
    }
}
