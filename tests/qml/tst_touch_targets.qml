// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "TouchTargets"
    when: windowShown
    visible: true
    width: 600
    height: 200

    Component { id: button; Tk.Button { text: "Apply" } }
    Component { id: iconButton; Tk.IconButton { iconName: "plus"; tooltip: "Add" } }
    Component { id: listRow; Tk.ListRow { text: "Asset" } }
    Component { id: segmented; Tk.Segmented { model: ["S", "M"] } }
    Component { id: gallery; Loader { source: "../../examples/gallery/Main.qml" } }

    function test_themed_controls_keep_a_touch_sized_target() {
        const formerMode = Tk.Density.modeName
        try {
            Tk.Density.modeName = "touch"
            const action = createTemporaryObject(button, root)
            const icon = createTemporaryObject(iconButton, root)
            const row = createTemporaryObject(listRow, root)
            const segments = createTemporaryObject(segmented, root)
            verify(action && icon && row && segments)
            const segment = findChild(segments, "segment_0")
            verify(segment !== null)
            tryCompare(action, "height", 48)
            tryCompare(icon, "height", 48)
            tryCompare(row, "height", 48)
            tryCompare(segments, "height", 48)
            tryCompare(segment, "height", 44)
            verify(segment.width >= 44)
        } finally {
            Tk.Density.modeName = formerMode
        }
    }

    function test_large_text_gallery_keeps_all_presets_reachable() {
        const formerMode = Tk.Density.modeName
        const formerTextScale = Tk.Density.textScale
        try {
            Tk.Density.modeName = "touch"
            Tk.Density.textScale = 3
            const loader = createTemporaryObject(gallery, root)
            verify(loader !== null)
            tryCompare(loader, "status", Loader.Ready)
            const scroll = findChild(loader.item, "presetScroll")
            const switcher = findChild(loader.item, "presetSwitch")
            verify(scroll && switcher)
            const last = findChild(switcher, "segment_4")
            verify(last !== null)
            tryVerify(function() { return scroll.scrollableX })
            scroll.contentX = scroll.contentWidth - scroll.viewportWidth
            tryVerify(function() {
                const position = last.mapToItem(scroll, 0, 0)
                return position.x >= 0 && position.x + last.width <= scroll.viewportWidth + 0.5
            })
        } finally {
            Tk.Density.modeName = formerMode
            Tk.Density.textScale = formerTextScale
        }
    }
}
