// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Buttons and chips"
    note: "Button · IconButton · Chip · Segmented · ToolBar · Badge · ProgressBar · Spinner"

    GalleryRow {
        label: "Button variants"
        Tk.Button { text: "Default" }
        Tk.Button { text: "Accent"; variant: "accent"; iconName: "play" }
        Tk.Button { text: "Ghost"; variant: "ghost" }
        Tk.Button { text: "Outline"; variant: "outline"; trailingIconName: "chevron-down" }
        Tk.Button { text: "Danger"; variant: "danger"; iconName: "trash-2" }
        Tk.Button { text: "Checked"; checkable: true; checked: true }
        Tk.Button { text: "Busy"; busy: true }
        Tk.Button { text: "Disabled"; enabled: false }
    }
    GalleryRow {
        label: "Button sizes"
        Tk.Button { text: "Small"; small: true; iconName: "plus" }
        Tk.Button { text: "Regular"; iconName: "plus" }
        Tk.Button { text: "Large"; large: true; iconName: "plus" }
        Tk.Button { text: "Large accent"; large: true; variant: "accent" }
    }
    GalleryRow {
        label: "IconButton"
        Tk.IconButton { iconName: "undo-2"; tooltip: "Undo" }
        Tk.IconButton { iconName: "redo-2"; tooltip: "Redo" }
        Tk.IconButton { iconName: "layers"; tooltip: "Checked"; checkable: true; checked: true }
        Tk.IconButton { iconName: "trash-2"; tooltip: "Danger"; danger: true }
        Tk.IconButton { iconName: "settings"; tooltip: "Filled"; ghost: false }
        Tk.IconButton { iconName: "x"; tooltip: "Small"; small: true }
        Tk.IconButton { iconName: "lock"; tooltip: "Disabled"; enabled: false }
    }
    GalleryRow {
        label: "Chip"
        Tk.Chip { text: "Inputs & Data"; iconName: "database"; chevron: true }
        Tk.Chip { text: "Render"; iconName: "sparkles"; active: true }
        Tk.Chip { text: "Tinted"; iconName: "wand-sparkles"; emphasis: "tinted" }
        Tk.Chip { text: "Solid"; iconName: "download"; emphasis: "solid" }
        Tk.Chip { text: "Small"; iconName: "tag"; small: true }
        Tk.Chip { text: "Disabled"; iconName: "lock"; enabled: false }
    }
    GalleryRow {
        label: "Island actions"
        Tk.Island {
            Tk.Chip { text: "Functions"; iconName: "sigma"; rounded: true; emphasis: "tinted" }
            Tk.Chip { text: "Preview"; iconName: "eye"; rounded: true }
            Tk.Chip { text: "Export"; iconName: "upload"; rounded: true; emphasis: "solid" }
        }
    }
    GalleryRow {
        label: "Segmented"
        Tk.Segmented { model: ["Library", "Design assets"]; currentIndex: 0 }
        Tk.Segmented { model: [{ "text": "Edit stage", "iconName": "pencil" }, { "text": "Rendered", "iconName": "monitor" }]; currentIndex: 1 }
        Tk.Segmented { model: ["S", "M", "L"]; small: true; currentIndex: 1 }
        Tk.Segmented { model: ["Off", "On"]; enabled: false }
    }
    GalleryRow {
        label: "ToolBar"
        Tk.ToolBar {
            implicitWidth: 560
            Tk.Chip { text: "Rulers"; iconName: "ruler"; small: true; active: true }
            Tk.Chip { text: "Guides"; iconName: "crosshair"; small: true }
            Tk.Chip { text: "Grid"; iconName: "grid-3x3"; small: true }
            Tk.ToolSeparator {}
            Tk.IconButton { iconName: "zoom-out"; tooltip: "Zoom out" }
            Tk.Mono { text: "185%" }
            Tk.IconButton { iconName: "zoom-in"; tooltip: "Zoom in" }
            Tk.Spacer {}
            Tk.Button { text: "Inspector"; variant: "ghost"; iconName: "sliders-horizontal"; small: true }
        }
        Tk.ToolBar {
            compact: true
            implicitWidth: 260
            Tk.Caption { text: "compact 28px" }
            Tk.Spacer {}
            Tk.IconButton { iconName: "ellipsis"; tooltip: "More"; small: true }
        }
    }
    GalleryRow {
        label: "Badge"
        Tk.Badge { text: "12" }
        Tk.Badge { text: "NEW"; variant: "accent" }
        Tk.Badge { text: "3 errors"; variant: "danger" }
        Tk.Badge { text: "ready"; variant: "success" }
        Tk.Badge { text: "queued"; variant: "warning" }
        Tk.Badge { text: "info"; variant: "info" }
        Tk.Badge { dot: true; variant: "success" }
        Tk.Badge { dot: true; variant: "danger" }
    }
    GalleryRow {
        label: "Progress / Spinner"
        Tk.ProgressBar { value: 0.42; implicitWidth: 160 }
        Tk.ProgressBar { value: 0.8; thin: true; implicitWidth: 160 }
        Tk.ProgressBar { indeterminate: true; implicitWidth: 160 }
        Tk.Spinner {}
        Tk.Spinner { size: Tk.Theme.size.iconXl; color: Tk.Theme.color.warning }
    }
}
