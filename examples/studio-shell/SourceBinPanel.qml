// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Sloom's Project Source Bin: library tabs, an import tool bar, and one
// row per asset with a thumbnail and the track chips that drop it onto
// the timeline.
Tk.Flex {
    id: bin
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    readonly property var assets: [
        { "name": "walk-wide.mp4", "kind": "video", "tracks": "V1 V2 V3 V4", "selected": true },
        { "name": "turn-cu.mp4", "kind": "video", "tracks": "V1 V2 V3 V4", "selected": false },
        { "name": "score.wav", "kind": "audio", "tracks": "A1", "selected": false }
    ]

    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Icon { name: "folder"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
        Tk.Label { text: "Source Library"; Tk.Flex.grow: 1 }
        Tk.Badge { text: "3" }
    }
    Tk.Caption {
        text: "Mixed media, generated assets, captions and reusable timeline elements."
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }
    Tk.TabStrip {
        objectName: "binTabs"
        model: ["Library", "Design assets"]
        currentIndex: 0
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        Tk.Chip { text: "Import media"; iconName: "import"; small: true }
        Tk.Spacer {}
        Tk.IconButton { iconName: "search"; tooltip: "Search the library"; small: true }
        Tk.IconButton { iconName: "ellipsis"; tooltip: "Library options"; small: true }
    }
    Tk.Segmented {
        objectName: "binFilter"
        model: ["All", "Video", "Audio", "Captions"]
        small: true
        stretch: true
    }
    Tk.Scroll {
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 0
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Repeater {
                model: bin.assets
                Tk.Card {
                    required property var modelData
                    objectName: "asset_" + modelData.name
                    padding: Tk.Theme.space.xs
                    selected: modelData.selected
                    interactive: true
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        Tk.Box {
                            implicitWidth: 32; implicitHeight: 18
                            radius: Tk.Theme.radius.xs
                            color: modelData.kind === "audio" ? Tk.Theme.color.accentSubtle : Tk.Theme.mix(Tk.Theme.color.accent, Tk.Theme.color.bg, 0.6)
                            borderWidth: 1; borderColor: Tk.Theme.color.controlBorder
                            Tk.Flex.shrink: 0
                            Tk.Icon {
                                anchors.centerIn: parent
                                name: modelData.kind === "audio" ? "audio-lines" : "film"
                                size: Tk.Theme.size.iconSm
                                color: Tk.Theme.color.accent
                            }
                        }
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            gap: 1
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                            Tk.Label { text: modelData.name; font.weight: Font.DemiBold }
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                gap: Tk.Theme.space.xs
                                Repeater {
                                    model: modelData.tracks.split(" ")
                                    Tk.Badge { required property string modelData; text: modelData; variant: "accent" }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
