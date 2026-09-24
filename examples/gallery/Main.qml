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
            // AGENT-GUARD: at Touch 3x on a short window, all header rows
            // can exceed the viewport. Keep half the window for the gallery
            // and scroll the header rather than clipping its controls.
            implicitHeight: Math.min(
                Math.max(Tk.Theme.size.toolbar + Tk.Theme.space.sm,
                         headerRow.implicitHeight + Tk.Theme.size.border),
                Math.max(Tk.Theme.size.toolbar + Tk.Theme.space.sm, root.height / 2))
            Tk.Flex.shrink: 0

            Tk.Scroll {
                objectName: "galleryHeaderScroll"
                anchors.fill: parent
                overflowX: Tk.Scroll.Hidden
                overflowY: Tk.Scroll.Auto
                accessibleName: "Gallery controls"

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
                    Tk.Scroll {
                        id: presetScroll
                        objectName: "presetScroll"
                        implicitWidth: Math.min(presetSwitch.implicitWidth,
                                                Math.max(0, root.width - Tk.Theme.space.md * 2))
                        implicitHeight: presetSwitch.implicitHeight
                                        + (scrollableX ? Tk.Theme.size.scrollbar + Tk.Theme.space.xs : 0)
                        overflowX: Tk.Scroll.Auto
                        overflowY: Tk.Scroll.Hidden
                        accessibleName: "Theme presets"
                        Item {
                            implicitWidth: presetSwitch.implicitWidth
                            implicitHeight: presetScroll.implicitHeight
                            Tk.Segmented {
                                id: presetSwitch
                                objectName: "presetSwitch"
                                anchors.left: parent.left
                                anchors.top: parent.top
                                width: implicitWidth
                                height: implicitHeight
                                model: root.presets
                                currentIndex: root.indexOfValue(root.presets, Tk.Theme.preset)
                                onActivated: function(index) { Tk.Theme.applyPreset(root.presets[index].value) }
                            }
                        }
                    }
                    Tk.Caption { text: "Density" }
                    Tk.Scroll {
                        id: densityScroll
                        objectName: "densityScroll"
                        implicitWidth: Math.min(densitySwitch.implicitWidth,
                                                Math.max(0, root.width - Tk.Theme.space.md * 2))
                        implicitHeight: densitySwitch.implicitHeight
                                        + (scrollableX ? Tk.Theme.size.scrollbar + Tk.Theme.space.xs : 0)
                        overflowX: Tk.Scroll.Auto
                        overflowY: Tk.Scroll.Hidden
                        accessibleName: "Density modes"
                        Item {
                            implicitWidth: densitySwitch.implicitWidth
                            implicitHeight: densityScroll.implicitHeight
                            Tk.Segmented {
                                id: densitySwitch
                                objectName: "densitySwitch"
                                anchors.left: parent.left
                                anchors.top: parent.top
                                width: implicitWidth
                                height: implicitHeight
                                model: root.densities
                                currentIndex: root.indexOfValue(root.densities, Tk.Density.modeName)
                                onActivated: function(index) { Tk.Density.modeName = root.densities[index].value }
                            }
                        }
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
                DatabaseSection { filter: search.text }
                ProductivitySection { filter: search.text }
                RichTextSection { filter: search.text }
                TelemetrySection { filter: search.text }
                TimelineSection { filter: search.text }
                VideoSection { filter: search.text }
                MotionSection { filter: search.text }
                ChartReviewSection { filter: search.text }
                PlanningSection { filter: search.text }
                CanvasSection { filter: search.text }
                RasterSection { filter: search.text }
                QuerySection { filter: search.text }
                DiffSection { filter: search.text }
                HeatmapSection { filter: search.text }
                DistributionSection { filter: search.text }
                FinancialSection { filter: search.text }
                PolarSection { filter: search.text }
                UncertaintySection { filter: search.text }
                PartWholeSection { filter: search.text }
                DockingSection { filter: search.text }
                ThemeSection { filter: search.text }
            }
        }
    }
}
