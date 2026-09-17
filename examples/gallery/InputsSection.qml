// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Inputs"
    note: "TextField · SearchField · TextArea · NumberField · Slider · ComboBox · ColorField"

    GalleryRow {
        label: "TextField"
        Tk.TextField { placeholderText: "Placeholder" }
        Tk.TextField { text: "With text"; clearable: true }
        Tk.TextField { text: "walk-wide"; iconName: "film"; clearable: true }
        Tk.TextField { text: "0xC0FFEE"; mono: true }
        Tk.TextField { text: "Invalid"; error: true }
        Tk.TextField { text: "Small"; small: true }
        Tk.TextField { text: "Disabled"; enabled: false }
    }
    GalleryRow {
        label: "SearchField"
        Tk.SearchField { placeholderText: "Search library" }
        Tk.SearchField { text: "comic"; implicitWidth: 200 }
    }
    GalleryRow {
        label: "TextArea"
        Tk.TextArea { placeholderText: "Notes for the reviewer"; rows: 3 }
        Tk.TextArea { text: "Monospace text area\nwith two lines"; mono: true; rows: 2 }
        Tk.TextArea { text: "Invalid"; error: true; rows: 2; implicitWidth: 160 }
    }
    GalleryRow {
        label: "NumberField"
        Tk.NumberField { label: "W"; value: 170; suffix: "mm" }
        Tk.NumberField { label: "H"; value: 260; suffix: "mm" }
        Tk.NumberField { value: 3.17; decimals: 2; suffix: "mm"; stepSize: 0.1 }
        Tk.NumberField { prefix: "$"; value: 9.94; decimals: 2 }
        Tk.NumberField { label: "%"; value: 100; from: 0; to: 100; small: true }
        Tk.NumberField { value: -1; error: true }
        Tk.NumberField { value: 12; enabled: false }
    }
    GalleryRow {
        label: "Slider"
        Tk.Slider { value: 0.65; implicitWidth: 140 }
        Tk.Slider { value: 72; from: 0; to: 100; showValue: true; suffix: "%"; implicitWidth: 180 }
        Tk.Slider { value: 0.3; small: true; implicitWidth: 120 }
        Tk.Slider { value: 0.5; enabled: false; implicitWidth: 120 }
    }
    GalleryRow {
        label: "ComboBox"
        Tk.ComboBox { model: ["Contain", "Cover", "Stretch"]; currentIndex: 0 }
        Tk.ComboBox { model: ["H.264", "H.265", "ProRes"]; placeholderText: "Codec"; currentIndex: -1 }
        Tk.ComboBox { model: ["Small", "combo"]; small: true; currentIndex: 0 }
        Tk.ComboBox { model: ["Disabled"]; enabled: false; currentIndex: 0 }
    }
    GalleryRow {
        label: "ColorField"
        Tk.ColorField { color: Tk.Theme.color.accent }
        Tk.ColorField { color: Tk.Theme.color.danger; small: true }
        Tk.ColorField { color: Tk.Theme.alpha(Tk.Theme.color.warning, 0.5) }
        Tk.ColorField { color: "#000000"; showHex: false }
        Tk.ColorField { color: Tk.Theme.color.success; error: true }
    }
}
