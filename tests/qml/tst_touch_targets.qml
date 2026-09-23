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
    Component { id: slider; Tk.Slider { tooltip: "Value" } }
    Component { id: smallSlider; Tk.Slider { small: true; tooltip: "Value" } }
    Component { id: rangeSlider; Tk.RangeSlider { tooltip: "Range" } }
    Component { id: smallRangeSlider; Tk.RangeSlider { small: true; tooltip: "Range" } }
    Component { id: smallCheckbox; Tk.CheckBox { small: true; text: "Grid" } }
    Component { id: smallSwitch; Tk.Switch { small: true; text: "Snap" } }
    Component { id: smallRadio; Tk.RadioButton { small: true; text: "Mode" } }
    Component { id: gallery; Loader { source: "../../examples/gallery/Main.qml" } }

    function test_sliderTargetsFollowDensityWithoutChangingCompactRows() {
        const formerMode = Tk.Density.modeName
        const formerTextScale = Tk.Density.textScale
        try {
            const modes = ["compact", "comfortable", "touch"]
            const textScales = [1, 3]
            for (let i = 0; i < modes.length; ++i) {
                Tk.Density.modeName = modes[i]
                for (let j = 0; j < textScales.length; ++j) {
                    Tk.Density.textScale = textScales[j]
                    const regular = createTemporaryObject(slider, root)
                    const small = createTemporaryObject(smallSlider, root)
                    const range = createTemporaryObject(rangeSlider, root)
                    const rangeSmall = createTemporaryObject(smallRangeSlider, root)
                    verify(regular && small && range && rangeSmall)
                    const regularTarget = modes[i] === "touch" ? Tk.Theme.size.sliderTarget : 0
                    const smallTarget = modes[i] === "touch" ? Tk.Theme.size.sliderTargetSm : 0
                    compare(regular.height, Math.max(regular.rowHeight, regularTarget))
                    compare(small.height, Math.max(small.rowHeight, smallTarget))
                    compare(range.height, Math.max(range.rowHeight, regularTarget))
                    compare(rangeSmall.height, Math.max(rangeSmall.rowHeight, smallTarget))
                    compare(regular.handleSize, Tk.Theme.size.grip)
                    compare(small.handleSize, Tk.Theme.space.md)
                    compare(range.handleSize, Tk.Theme.size.grip)
                    compare(rangeSmall.handleSize, Tk.Theme.space.md)
                    if (modes[i] === "compact") {
                        compare(regular.height, 16)
                        compare(small.height, 14)
                        compare(range.height, 16)
                        compare(rangeSmall.height, 14)
                    }
                    regular.destroy()
                    small.destroy()
                    range.destroy()
                    rangeSmall.destroy()
                }
            }
        } finally {
            Tk.Density.modeName = formerMode
            Tk.Density.textScale = formerTextScale
        }
    }

    function test_smallToggleTargetsFollowThemeRole() {
        const formerMode = Tk.Density.modeName
        const formerTextScale = Tk.Density.textScale
        try {
            for (const mode of ["compact", "comfortable", "touch"]) {
                Tk.Density.modeName = mode
                for (const textScale of [1, 3]) {
                    Tk.Density.textScale = textScale
                    for (const component of [smallCheckbox, smallSwitch, smallRadio]) {
                        const control = createTemporaryObject(component, root)
                        verify(control !== null)
                        verify(control.height >= Tk.Theme.size.toggleTargetSm)
                        if (mode === "touch") verify(control.height >= 48)
                        if (mode === "compact" && textScale === 1)
                            compare(control.height, 16)
                        control.destroy()
                    }
                }
            }
        } finally {
            Tk.Density.modeName = formerMode
            Tk.Density.textScale = formerTextScale
        }
    }

    function test_sliderVisualMetricsStayAuthoredAtThreeTimesTextScale() {
        const formerMode = Tk.Density.modeName
        const formerTextScale = Tk.Density.textScale
        try {
            Tk.Density.modeName = "touch"
            Tk.Density.textScale = 3
            const regular = createTemporaryObject(slider, root)
            const small = createTemporaryObject(smallSlider, root)
            const range = createTemporaryObject(rangeSlider, root)
            const rangeSmall = createTemporaryObject(smallRangeSlider, root)
            verify(regular && small && range && rangeSmall)
            verify(regular.height >= 48)
            verify(small.height >= 48)
            verify(range.height >= 48)
            verify(rangeSmall.height >= 48)
            compare(regular.rowHeight, Tk.Theme.size.iconLg)
            compare(small.rowHeight, Tk.Theme.size.icon)
            compare(range.rowHeight, Tk.Theme.size.iconLg)
            compare(rangeSmall.rowHeight, Tk.Theme.size.icon)
        } finally {
            Tk.Density.modeName = formerMode
            Tk.Density.textScale = formerTextScale
        }
    }

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

    function test_narrow_large_text_gallery_keeps_header_and_content_reachable() {
        const formerMode = Tk.Density.modeName
        const formerTextScale = Tk.Density.textScale
        try {
            Tk.Density.modeName = "touch"
            Tk.Density.textScale = 3
            const loader = createTemporaryObject(gallery, root, { width: 480, height: 320 })
            verify(loader !== null)
            tryCompare(loader, "status", Loader.Ready)
            const header = findChild(loader.item, "galleryHeader")
            const headerScroll = findChild(loader.item, "galleryHeaderScroll")
            const content = findChild(loader.item, "galleryScroll")
            const densityScroll = findChild(loader.item, "densityScroll")
            const densitySwitch = findChild(loader.item, "densitySwitch")
            const search = findChild(loader.item, "sectionFilter")
            verify(header && headerScroll && content && densityScroll
                   && densitySwitch && search)
            tryVerify(function() { return headerScroll.scrollableY && densityScroll.scrollableX })
            verify(header.height <= loader.item.height / 2 + 0.5)
            verify(content.height >= loader.item.height / 2 - 0.5)
            densityScroll.contentX = densityScroll.contentWidth - densityScroll.viewportWidth
            const last = findChild(densitySwitch, "segment_2")
            verify(last !== null)
            tryVerify(function() {
                const p = last.mapToItem(densityScroll, 0, 0)
                return p.x >= 0 && p.x + last.width <= densityScroll.viewportWidth + 0.5
            })
            headerScroll.contentY = headerScroll.contentHeight - headerScroll.viewportHeight
            tryVerify(function() {
                const p = search.mapToItem(headerScroll, 0, 0)
                return p.y >= 0 && p.y + search.height <= headerScroll.viewportHeight + 0.5
            })
        } finally {
            Tk.Density.modeName = formerMode
            Tk.Density.textScale = formerTextScale
        }
    }
}
