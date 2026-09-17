// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Structure"
    note: "Panel · Island · PropertyRow · KeyValue · Notice · Card · StatusBar · EmptyState · Ruler · ZoomControl"

    GalleryRow {
        label: "Panel / Island"
        Tk.Panel {
            title: "Layers"; iconName: "layers"; closable: true; floatable: true
            implicitWidth: 240; implicitHeight: 120
            actions: [ Tk.IconButton { iconName: "plus"; tooltip: "Add layer"; small: true } ]
            Tk.Flex {
                direction: Tk.Flex.Column; gap: 1
                Tk.ListRow { text: "Lettering"; iconName: "eye"; small: true }
                Tk.ListRow { text: "Line art"; iconName: "eye"; small: true; selected: true }
                Tk.ListRow { text: "Background"; iconName: "eye-off"; small: true }
            }
        }
        Tk.Panel {
            title: "Floating"; floating: true; active: true; collapsible: false
            implicitWidth: 200; implicitHeight: 80
            Tk.Caption { text: "accent border when active"; wrapMode: Text.WordWrap }
        }
        Tk.Island {
            Tk.IconButton { iconName: "zoom-out"; tooltip: "Zoom out" }
            Tk.Mono { text: "100%" }
            Tk.IconButton { iconName: "zoom-in"; tooltip: "Zoom in" }
            Tk.Divider { vertical: true; inset: 4 }
            Tk.IconButton { iconName: "maximize"; tooltip: "Fit" }
        }
    }
    GalleryRow {
        label: "PropertyGroup"
        Tk.Box {
            color: Tk.Theme.color.panelAlt; borderWidth: 1; radius: Tk.Theme.radius.xs; padding: Tk.Theme.space.sm
            implicitWidth: 320
            Tk.Flex {
                direction: Tk.Flex.Column; gap: Tk.Theme.space.xs
                Tk.PropertyGroup {
                    title: "Document"; count: "3 of 5"
                    Tk.PropertyRow { label: "Page size"; Tk.ComboBox { model: ["Comic Book (170 x 260)", "A4", "Letter"]; currentIndex: 0 } }
                    Tk.PropertyRow { label: "Width"; Tk.NumberField { value: 170; suffix: "mm" } }
                    Tk.PropertyRow { label: "Bleed"; hint: "Printed past the trim"; Tk.NumberField { value: 3.17; decimals: 2; suffix: "mm" } }
                    Tk.PropertyRow { label: "Grid"; Tk.Switch { text: "enabled"; checked: true } }
                    Tk.PropertyRow { label: "Notes"; alignTop: true; Tk.TextArea { rows: 2; placeholderText: "Reviewer notes" } }
                }
                Tk.PropertyGroup {
                    title: "Sequence info"; collapsible: true
                    Tk.KeyValue { key: "Canvas"; value: "16:9 · 1080p" }
                    Tk.KeyValue { key: "Timebase"; value: "30 fps" }
                    Tk.KeyValue { key: "Length"; value: "12.8s"; accent: true }
                    Tk.KeyValue { key: "Tracks"; value: "V:2 · A:1"; mono: false }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Column; gap: Tk.Theme.space.sm
            Tk.Flex.basis: 320
            Tk.Notice { text: "Rendering uses the native FFmpeg route." }
            Tk.Notice { text: "Preview ready — 2 clips · 30 fps"; variant: "success" }
            Tk.Notice { title: "Provider key missing"; text: "Add a Gemini key in Settings to run this node."; variant: "warning"; dismissible: true }
            Tk.Notice { text: "Export failed: disk full"; variant: "danger"; dismissible: true
                actions: [ Tk.Button { text: "Retry"; small: true; variant: "ghost" } ] }
        }
    }
    GalleryRow {
        label: "Card / EmptyState"
        Tk.Card {
            implicitWidth: 200
            Tk.Flex { direction: Tk.Flex.Column; gap: Tk.Theme.space.xs
                Tk.Overline { title: "Comic spread" }
                Tk.Label { text: "Two pages, 6 panels"; muted: true }
            }
        }
        Tk.Card {
            selected: true; implicitWidth: 200
            Tk.Flex { direction: Tk.Flex.Column; gap: Tk.Theme.space.xs
                Tk.Overline { title: "Selected card" }
                Tk.Label { text: "Accent border"; muted: true }
            }
        }
        Tk.Card {
            interactive: true; implicitWidth: 200
            Tk.Flex { direction: Tk.Flex.Column; gap: Tk.Theme.space.xs
                Tk.Overline { title: "Interactive" }
                Tk.Label { text: "Hover lifts the border"; muted: true }
            }
        }
        Tk.Box {
            color: Tk.Theme.color.panelAlt; borderWidth: 1; radius: Tk.Theme.radius.xs
            implicitWidth: 240; implicitHeight: 110
            Tk.EmptyState { title: "No clips"; text: "Drop media from the source bin."; iconName: "film"
                actions: [ Tk.Button { text: "Import"; small: true; iconName: "import" } ] }
        }
    }
    GalleryRow {
        label: "StatusBar"
        Tk.StatusBar {
            implicitWidth: 640
            Tk.StatusField { text: "00:00 / 00:12"; iconName: "clock" }
            Tk.StatusField { text: "2 clips" }
            Tk.StatusField { text: "V1 · V2 · A1"; muted: false }
            Tk.Spacer {}
            Tk.StatusField { text: "185%"; iconName: "zoom-in" }
            Tk.StatusField { text: "Ready"; iconName: "circle-check"; muted: false }
        }
    }
    GalleryRow {
        label: "Ruler / Zoom"
        Tk.Box {
            color: Tk.Theme.color.canvas; borderWidth: 1
            implicitWidth: 480; implicitHeight: Tk.Theme.size.row
            Tk.Ruler { pixelsPerUnit: 40; unit: "s"; majorEvery: 5; minorEvery: 1; cursorPosition: 170 }
        }
        Tk.Box {
            color: Tk.Theme.color.canvas; borderWidth: 1
            implicitWidth: Tk.Theme.size.row; implicitHeight: 80
            Tk.Ruler { orientation: Qt.Vertical; pixelsPerUnit: 8; unit: "mm"; majorEvery: 5; minorEvery: 1 }
        }
        Tk.ZoomControl { value: 1.85 }
        Tk.Island { Tk.ZoomControl { value: 0.5 } }
    }
}
