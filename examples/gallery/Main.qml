// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The QindaTK gallery: every control in docs/controls.md in every documented
// state, with live preset and density switching. Run:
//   qtk-preview examples/gallery/Main.qml
//   qtk-preview examples/gallery/Main.qml --density comfortable --grab gallery.png
Rectangle {
    id: root
    width: 1280
    height: 800
    color: Tk.Theme.color.bg

    readonly property var presets: [
        { "text": "Sloom dark", "value": "sloom-dark" },
        { "text": "Sloom light", "value": "sloom-light" },
        { "text": "Graphite", "value": "graphite" },
        { "text": "High contrast dark", "value": "high-contrast-dark" },
        { "text": "High contrast light", "value": "high-contrast-light" }
    ]
    readonly property var densities: [
        { "text": "Compact", "value": "compact" },
        { "text": "Comfortable", "value": "comfortable" },
        { "text": "Touch", "value": "touch" }
    ]

    function indexOfValue(list, value) {
        for (let i = 0; i < list.length; ++i) {
            if (list[i].value === value) return i
        }
        return 0
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column

        // ---- header ----
        Tk.Box {
            id: headerBox
            objectName: "galleryHeader"
            color: Tk.Theme.color.surface
            borderBottom: 1
            borderColor: Tk.Theme.color.divider
            implicitHeight: Math.max(Tk.Theme.size.toolbar + Tk.Theme.space.sm,
                                     headerRow.implicitHeight + Tk.Theme.size.border)
            Tk.Flex.shrink: 0

            Tk.Flex {
                id: headerRow
                direction: Tk.Flex.Row
                wrap: Tk.Flex.Wrap
                align: Tk.Flex.Center
                alignContent: Tk.Flex.Center
                gap: Tk.Theme.space.md
                rowGap: Tk.Theme.space.xs
                paddingLeft: Tk.Theme.space.md
                paddingRight: Tk.Theme.space.md
                paddingTop: Tk.Theme.space.xs
                paddingBottom: Tk.Theme.space.xs

                Tk.Overline { title: "QindaTK gallery"; wide: true; color: Tk.Theme.color.text }
                Tk.Divider { vertical: true; inset: 6 }
                Tk.Caption { text: "Preset" }
                Tk.Segmented {
                    objectName: "presetSwitch"
                    model: root.presets
                    currentIndex: root.indexOfValue(root.presets, Tk.Theme.preset)
                    onActivated: function(index) { Tk.Theme.applyPreset(root.presets[index].value) }
                }
                Tk.Caption { text: "Density" }
                Tk.Segmented {
                    objectName: "densitySwitch"
                    model: root.densities
                    currentIndex: root.indexOfValue(root.densities, Tk.Density.modeName)
                    onActivated: function(index) { Tk.Density.modeName = root.densities[index].value }
                }
                Tk.Spacer { visible: Tk.Density.textScale < 1.5 }
                Tk.SearchField {
                    id: search
                    objectName: "sectionFilter"
                    placeholderText: "Filter sections"
                    implicitWidth: 220
                }
                Tk.Caption { text: Tk.Theme.name + " · " + Tk.Density.modeName }
            }
        }

        // ---- sections ----
        Tk.Scroll {
            objectName: "galleryScroll"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: 0
            padding: Tk.Theme.space.md

            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md

                TextSection { filter: search.text }
                LayoutSection { filter: search.text }
                ButtonsSection { filter: search.text }
                InputsSection { filter: search.text }
                SelectionSection { filter: search.text }
                StructureSection { filter: search.text }
                PopupsSection { filter: search.text }
                OfficeSection { filter: search.text }
                TelemetrySection { filter: search.text }
                DockingSection { filter: search.text }
                ThemeSection { filter: search.text }
            }
        }
    }
}
