// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk
import "../../examples/gallery" as Gallery

TestCase {
    id: root
    name: "GallerySectionMatrix"
    when: windowShown
    visible: true
    width: 480
    height: 320

    Component {
        id: dockingComponent
        Gallery.DockingSection { width: 480 }
    }
    Component {
        id: telemetryComponent
        Gallery.TelemetrySection { width: 480 }
    }

    function test_docking_workspace_scrolls_and_contains_touch_actions() {
        const previousMode = Tk.Density.modeName
        const previousScale = Tk.Density.textScale
        const previousPreset = Tk.Theme.preset
        try {
            Tk.Density.modeName = "touch"
            Tk.Density.textScale = 3
            Tk.Theme.applyPreset("high-contrast-dark")

            const section = createTemporaryObject(dockingComponent, root)
            verify(section !== null)
            waitForRendering(section)

            const scroll = findChild(section, "galleryDockScroll")
            const host = findChild(section, "galleryDock")
            const left = findChild(host, "dockZone_left")
            const right = findChild(host, "dockZone_right")
            const tools = findChild(host, "dockFrame_tools")
            const place = findChild(host, "dockPlacement_tools")
            verify(scroll !== null)
            verify(host !== null)
            verify(left !== null)
            verify(right !== null)
            verify(tools !== null)
            verify(place !== null)
            verify(scroll.scrollableX)
            verify(left.x + left.width <= host.width + 1)
            verify(right.x + right.width <= host.width + 1)
            verify(place.x >= 0)
            verify(place.x + place.width <= tools.width + 1)
            verify(place.width >= Tk.Theme.size.seamTarget)
        } finally {
            Tk.Density.modeName = previousMode
            Tk.Density.textScale = previousScale
            Tk.Theme.applyPreset(previousPreset)
        }
    }

    function test_telemetry_table_stays_inside_gallery_column() {
        const previousMode = Tk.Density.modeName
        const previousScale = Tk.Density.textScale
        const previousPreset = Tk.Theme.preset
        try {
            Tk.Density.modeName = "touch"
            Tk.Density.textScale = 3
            Tk.Theme.applyPreset("high-contrast-light")

            const section = createTemporaryObject(telemetryComponent, root)
            verify(section !== null)
            waitForRendering(section)

            const table = findChild(section, "galleryTelemetryDataTable")
            verify(table !== null)
            verify(table.width <= section.width)
            verify(table.width > 0)
        } finally {
            Tk.Density.modeName = previousMode
            Tk.Density.textScale = previousScale
            Tk.Theme.applyPreset(previousPreset)
        }
    }
}
