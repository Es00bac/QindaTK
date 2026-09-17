// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    id: section
    title: "Theme"
    note: Tk.Theme.name + " · " + Tk.Density.modeName + " · every colour role"

    readonly property var roles: [
        "bg", "surface", "panel", "panelAlt", "canvas", "border", "borderStrong", "divider",
        "text", "textMuted", "textDisabled", "accent", "accentContrast", "accentSubtle", "accentText",
        "hover", "pressed", "selection", "focus", "danger", "dangerContrast", "dangerSubtle",
        "warning", "success", "info", "controlBg", "controlBorder", "controlHoverBg", "controlHoverBorder",
        "controlActiveBg", "controlActiveBorder", "chipBg", "chipBorder", "chipHoverBg", "chipHoverBorder",
        "inputBg", "inputBorder", "inputFocusBorder", "headerBg", "headerText", "popoverBg", "popoverBorder",
        "tooltipBg", "tooltipText", "islandBg", "islandBorder", "scrollTrack", "scrollThumb",
        "seam", "seamHover", "dropZone", "shadow", "overlay"
    ]

    GalleryRow {
        label: "Type ramp"
        Repeater {
            model: ["micro", "caption", "small", "body", "medium", "large", "title", "display"]
            Tk.Label {
                required property string modelData
                text: modelData + " " + Tk.Theme.font[modelData]
                font.pixelSize: Tk.Theme.font[modelData]
            }
        }
    }
    GalleryRow {
        label: "Ladders"
        Tk.Mono { text: "space " + ["xs", "sm", "md", "lg", "xl", "xxl"].map(function(k) { return Tk.Theme.space[k] }).join("/") }
        Tk.Mono { text: "radius " + ["xs", "sm", "md", "lg"].map(function(k) { return Tk.Theme.radius[k] }).join("/") }
        Tk.Mono { text: "size control/row/chip/header " + [Tk.Theme.size.control, Tk.Theme.size.row, Tk.Theme.size.chip, Tk.Theme.size.header].join("/") }
        Tk.Mono { text: "motion " + [Tk.Theme.motion.fast, Tk.Theme.motion.base, Tk.Theme.motion.slow].join("/") + " ms" }
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.sm
        Repeater {
            model: section.roles
            Tk.Flex {
                required property string modelData
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Flex.basis: 104
                Tk.Flex.shrink: 0
                Tk.Box {
                    implicitHeight: 34
                    color: Tk.Theme.color[modelData]
                    borderWidth: 1
                    borderColor: Tk.Theme.color.borderStrong
                    radius: Tk.Theme.radius.sm
                }
                Tk.Caption { text: modelData; font.pixelSize: Tk.Theme.font.micro }
            }
        }
    }
}
