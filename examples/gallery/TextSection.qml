// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Text"
    note: "Label · Caption · Overline · Mono · Heading"

    GalleryRow {
        label: "Label"
        Tk.Label { text: "Body text 12px" }
        Tk.Label { text: "Muted"; muted: true }
        Tk.Label { text: "Disabled"; disabled: true }
        Tk.Label { text: "Accent"; accent: true }
        Tk.Label { text: "Mono label"; mono: true }
    }
    GalleryRow {
        label: "Caption / Overline"
        Tk.Caption { text: "Caption 10px muted" }
        Tk.Overline { title: "Overline" }
        Tk.Overline { title: "Overline wide"; wide: true }
        Tk.Mono { text: "00:00:12:04" }
    }
    GalleryRow {
        label: "Heading"
        Tk.Heading { text: "Display heading"; level: 1 }
        Tk.Heading { text: "Title heading"; level: 2 }
        Tk.Heading { text: "Large heading"; level: 3 }
    }
    GalleryRow {
        label: "Wrapping"
        Tk.Label {
            text: "Labels elide when a layout narrows them; set wrapMode to wrap instead. " +
                  "This paragraph wraps at 420px and the row grows to fit it."
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
            Tk.Flex.basis: 420
            Tk.Flex.shrink: 1
        }
    }
}
