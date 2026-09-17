// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A Sloom monitor: a stage switch, a 16:9 frame (a gradient stands in for
// decoded video), the transport row with a mono timecode, and for the
// program monitor the render-status notice.
Tk.Flex {
    id: monitor
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs

    property bool program: false
    property string assetName: "walk-wide.mp4"
    property string timecode: "0:00 / 0:12"

    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Label { text: monitor.program ? "Program Monitor" : "Source Monitor"; font.weight: Font.DemiBold; Tk.Flex.grow: 1 }
        Tk.Segmented {
            small: true
            model: monitor.program ? ["Edit Stage", "Rendered Preview"] : ["Source", "Trim"]
            currentIndex: monitor.program ? 1 : 0
        }
    }
    Tk.Notice {
        visible: monitor.program
        variant: "success"
        text: "PREVIEW READY – Rendered editor sequence · 2 visual clips · 30 fps · Review: H.264 1080p · COMPLETED"
    }
    Tk.Caption {
        visible: !monitor.program
        text: "Preview the selected source asset before dropping it into the cut."
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }

    // ---- the frame: fills the remaining height, keeps 16:9 ----
    Tk.Box {
        objectName: monitor.program ? "programFrame" : "sourceFrame"
        Tk.Flex.grow: 1
        Tk.Flex.basis: 0
        Tk.Flex.minHeight: 60
        color: Tk.Theme.color.bg
        borderWidth: 1
        borderColor: Tk.Theme.color.divider
        fill: false

        Rectangle {
            id: frame
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height * 16 / 9)
            height: width * 9 / 16
            gradient: Gradient {
                GradientStop { position: 0.0; color: Tk.Theme.mix(Tk.Theme.color.bg, Tk.Theme.color.accent, 0.08) }
                GradientStop { position: 0.55; color: Tk.Theme.mix(Tk.Theme.color.bg, Tk.Theme.color.danger, 0.10) }
                GradientStop { position: 1.0; color: Tk.Theme.mix(Tk.Theme.color.bg, Tk.Theme.color.accent, 0.22) }
            }
            // Neon sign stand-ins.
            Rectangle { x: parent.width * 0.62; y: parent.height * 0.18; width: parent.width * 0.12; height: 3; color: Tk.Theme.color.accent; opacity: 0.8 }
            Rectangle { x: parent.width * 0.70; y: parent.height * 0.30; width: parent.width * 0.06; height: parent.height * 0.22; color: Tk.Theme.color.danger; opacity: 0.55 }
            Rectangle { x: parent.width * 0.12; y: parent.height * 0.36; width: parent.width * 0.16; height: 2; color: Tk.Theme.color.warning; opacity: 0.6 }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height * 0.28; color: Tk.Theme.alpha(Tk.Theme.color.accent, 0.10) }
            Tk.Overline {
                anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: Tk.Theme.space.sm
                title: monitor.program ? "composition-04982a" : monitor.assetName + " · video/mp4"
                color: Tk.Theme.alpha(Tk.Theme.color.text, 0.7)
            }
        }
    }

    // ---- transport ----
    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        Tk.IconButton { iconName: "skip-back"; tooltip: "Go to start"; small: true }
        Tk.IconButton { iconName: "play"; tooltip: "Play"; small: true }
        Tk.IconButton { iconName: "skip-forward"; tooltip: "Go to end"; small: true }
        Tk.Mono { text: monitor.timecode; Tk.Flex.grow: 1; Tk.Flex.shrink: 0 }
        Tk.Caption { visible: !monitor.program; text: "drop on" }
        Repeater {
            model: monitor.program ? [] : ["V1", "V2", "V3", "V4"]
            Tk.Chip { required property string modelData; text: modelData; small: true; tooltip: "Drop the asset on track " + modelData }
        }
        Tk.IconButton { visible: monitor.program; iconName: "volume-2"; tooltip: "Audio"; small: true }
        Tk.IconButton { visible: monitor.program; iconName: "maximize-2"; tooltip: "Full screen"; small: true }
    }
}
