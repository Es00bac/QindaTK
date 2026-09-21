// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Regression rows for the 2026-09-21 audit findings this wave fixes:
//  1. Tk.Box / Tk.Scroll counted invisible children in their implicit size.
//  2. Tk.Segmented's `enabled_` property was a decoy; only `enabled` works.
//  7. Tk.KeyCap / Tk.Filmstrip were built on the QtQuick Row positioner,
//     which AGENTS.md rule 4 forbids.
//  8. Tk.DataTable / Tk.Card / Tk.StatusBar set Accessible.role with no
//     accessible-name path.
// Finding 3 (vertical Tk.Slider) has its rows further down.
TestCase {
    id: root
    name: "AuditFixes"
    when: windowShown
    visible: true
    width: 480
    height: 240

    Component {
        id: boxComponent
        Tk.Box {
            property alias shownRect: shownRect
            property alias hiddenRect: hiddenRect
            Rectangle { id: shownRect; implicitWidth: 40; implicitHeight: 10 }
            Rectangle { id: hiddenRect; implicitWidth: 185; implicitHeight: 15; visible: false }
        }
    }

    Component {
        id: hiddenOnlyBoxComponent
        Tk.Box {
            Rectangle { implicitWidth: 185; implicitHeight: 15; visible: false }
        }
    }

    Component {
        id: scrollComponent
        Tk.Scroll {
            property alias shownRect: shownRect
            property alias hiddenRect: hiddenRect
            Rectangle { id: shownRect; implicitWidth: 40; implicitHeight: 10 }
            Rectangle { id: hiddenRect; implicitWidth: 185; implicitHeight: 15; visible: false }
        }
    }

    Component {
        id: hiddenOnlyScrollComponent
        Tk.Scroll {
            Rectangle { implicitWidth: 185; implicitHeight: 15; visible: false }
        }
    }

    // docs/layout.md: "Invisible children take no slot." A hidden child must
    // not reserve its size in the box's implicit size (finding 1).
    function test_boxIgnoresInvisibleChildren() {
        const box = createTemporaryObject(boxComponent, root)
        verify(box)
        compare(box.implicitWidth, 40)
        compare(box.implicitHeight, 10)
        box.hiddenRect.visible = true
        tryCompare(box, "implicitWidth", 185)
        tryCompare(box, "implicitHeight", 15)
        box.hiddenRect.visible = false
        tryCompare(box, "implicitWidth", 40)
        tryCompare(box, "implicitHeight", 10)
    }

    function test_boxWithOnlyAHiddenChildIsEmpty() {
        const box = createTemporaryObject(hiddenOnlyBoxComponent, root)
        verify(box)
        compare(box.implicitWidth, 0)
        compare(box.implicitHeight, 0)
    }

    function test_scrollIgnoresInvisibleChildren() {
        const scroll = createTemporaryObject(scrollComponent, root)
        verify(scroll)
        compare(scroll.implicitWidth, 40)
        compare(scroll.implicitHeight, 10)
        scroll.hiddenRect.visible = true
        tryCompare(scroll, "implicitWidth", 185)
        tryCompare(scroll, "implicitHeight", 15)
    }

    function test_scrollWithOnlyAHiddenChildIsEmpty() {
        const scroll = createTemporaryObject(hiddenOnlyScrollComponent, root)
        verify(scroll)
        compare(scroll.implicitWidth, 0)
        compare(scroll.implicitHeight, 0)
    }

    Component {
        id: segmentedComponent
        Tk.Segmented { model: ["A", "B"] }
    }

    // Finding 2: the advertised `enabled_` did nothing and is gone; the
    // built-in `enabled` dims and disables the control.
    function test_segmentedHonoursBuiltinEnabled() {
        const seg = createTemporaryObject(segmentedComponent, root)
        verify(seg)
        verify(!("enabled_" in seg))
        verify(seg.enabled)
        compare(seg.opacity, 1.0)
        seg.enabled = false
        verify(!seg.enabled)
        compare(seg.opacity, Tk.Theme.opacity.disabled)
    }

    Component {
        id: keyCapComponent
        Tk.KeyCap { sequence: "Ctrl+K" }
    }

    // Finding 7: the caps row is the toolkit's own layout, not a QtQuick
    // positioner — keyCapRow is the structural contract the dump tool reads.
    function test_keyCapIsLaidOutByTheToolkitFlex() {
        const cap = createTemporaryObject(keyCapComponent, root)
        verify(cap)
        const row = findChild(cap, "keyCapRow")
        verify(row)
        // Flex lays out on the next polish; geometry is read after a frame.
        waitForRendering(cap)
        compare(cap.implicitWidth, row.implicitWidth)
        const caps = []
        for (let i = 0; i < row.children.length; ++i) {
            if (row.children[i].objectName === "keyCapKey") {
                caps.push(row.children[i])
            }
        }
        compare(caps.length, 2)
        compare(caps[0].x, 0)
        compare(caps[0].y, 0)
        fuzzyCompare(caps[1].x - (caps[0].x + caps[0].width), Tk.Theme.space.xs, 0.01)
    }

    Component {
        id: filmstripComponent
        Tk.Filmstrip {
            width: 400
            implicitHeight: 40
            frames: [{caption: "in"}, {caption: "b"}, {caption: "c"}, {caption: "out"}]
        }
    }

    // Finding 7: same re-layout for the strip; the geometry the Row produced
    // (equal widths, toolkit gap, full height) must be preserved.
    function test_filmstripIsLaidOutByTheToolkitFlex() {
        const strip = createTemporaryObject(filmstripComponent, root)
        verify(strip)
        const row = findChild(strip, "filmstripRow")
        verify(row)
        waitForRendering(strip)
        compare(strip.shown, 4)
        const cells = []
        for (let i = 0; i < row.children.length; ++i) {
            const name = row.children[i].objectName
            if (name !== undefined && name.indexOf("filmstripFrame") === 0) {
                cells.push(row.children[i])
            }
        }
        compare(cells.length, 4)
        compare(cells[0].x, 0)
        fuzzyCompare(cells[0].width, (400 - Tk.Theme.space.xs * 3) / 4, 0.01)
        fuzzyCompare(cells[1].x - (cells[0].x + cells[0].width), Tk.Theme.space.xs, 0.01)
        compare(cells[0].height, strip.height)
    }

    Component {
        id: tableComponent
        Tk.DataTable { }
    }

    Component {
        id: cardComponent
        Tk.Card { }
    }

    Component {
        id: statusBarComponent
        Tk.StatusBar { }
    }

    // Finding 8: a role with no name announces a control nobody can
    // identify; each of these now has a name property with a sane default.
    function test_dataTableHasAnAccessibleNamePath() {
        const table = createTemporaryObject(tableComponent, root)
        verify(table)
        compare(table.Accessible.role, Accessible.Table)
        compare(table.Accessible.name, "Table")
        table.tooltip = "Processes"
        compare(table.Accessible.name, "Processes")
    }

    function test_cardHasAnAccessibleNamePath() {
        const card = createTemporaryObject(cardComponent, root)
        verify(card)
        compare(card.Accessible.role, Accessible.Grouping)
        compare(card.Accessible.name, "Card")
        card.tooltip = "Scene bin"
        compare(card.Accessible.name, "Scene bin")
        card.interactive = true
        compare(card.Accessible.role, Accessible.Button)
    }

    function test_statusBarHasAnAccessibleNamePath() {
        const bar = createTemporaryObject(statusBarComponent, root)
        verify(bar)
        compare(bar.Accessible.role, Accessible.StatusBar)
        compare(bar.Accessible.name, "Status bar")
        bar.tooltip = "Render status"
        compare(bar.Accessible.name, "Render status")
    }

    Component {
        id: vSliderComponent
        Tk.Slider {
            orientation: Qt.Vertical
            width: 20
            height: 160
            from: 0
            to: 100
            value: 50
        }
    }

    Component {
        id: hSliderComponent
        Tk.Slider { width: 160; height: 20; from: 0; to: 100; value: 50 }
    }

    // Finding 3: a vertical slider must put its handle where its value says.
    // Qt's convention: value grows upward, so visualPosition is 0 at the top.
    function test_verticalSliderPutsItsHandleWhereItsValueSays() {
        const slider = createTemporaryObject(vSliderComponent, root)
        verify(slider)
        const handle = slider.handle
        verify(handle)
        const track = findChild(slider, "sliderTrack")
        const fill = findChild(slider, "sliderFill")
        verify(track && fill)
        // The track is the thin, full-height column — not the old 20x2 stub.
        compare(track.width, slider.trackThickness)
        compare(track.height, slider.availableHeight)
        compare(handle.x, slider.leftPadding + (slider.availableWidth - handle.width) / 2)
        fuzzyCompare(handle.y, slider.topPadding + 0.5 * (slider.availableHeight - handle.height), 0.01)
        fuzzyCompare(fill.height, track.height * 0.5, 0.01)
        slider.value = 100
        fuzzyCompare(handle.y, slider.topPadding, 0.01)
        fuzzyCompare(fill.height, track.height, 0.01)
        slider.value = 0
        fuzzyCompare(handle.y, slider.topPadding + slider.availableHeight - handle.height, 0.01)
        fuzzyCompare(fill.height, 0, 0.01)
    }

    // The keyboard direction that goes with it: Up raises, Down lowers.
    function test_verticalSliderKeyboardMovesWithItsOrientation() {
        const slider = createTemporaryObject(vSliderComponent, root)
        verify(slider)
        slider.stepSize = 1
        slider.value = 50
        slider.forceActiveFocus()
        verify(slider.activeFocus)
        keyClick(Qt.Key_Up)
        compare(slider.value, 51)
        keyClick(Qt.Key_Down)
        compare(slider.value, 50)
    }

    // The horizontal path must be byte-for-byte the layout it always was.
    function test_horizontalSliderIsUnchanged() {
        const slider = createTemporaryObject(hSliderComponent, root)
        verify(slider)
        const handle = slider.handle
        const track = findChild(slider, "sliderTrack")
        const fill = findChild(slider, "sliderFill")
        verify(handle && track && fill)
        compare(track.width, slider.availableWidth)
        compare(track.height, slider.trackThickness)
        fuzzyCompare(handle.x, slider.leftPadding + 0.5 * (slider.availableWidth - handle.width), 0.01)
        compare(handle.y, slider.topPadding + (slider.availableHeight - handle.height) / 2)
        fuzzyCompare(fill.width, track.width * 0.5, 0.01)
        compare(fill.height, track.height)
    }

    // showValue on a vertical slider reserves a readout row below the track
    // instead of a column on the right.
    function test_verticalSliderReadoutSitsBelow() {
        const slider = createTemporaryObject(vSliderComponent, root, {showValue: true})
        verify(slider)
        compare(slider.rightPadding, 0)
        verify(slider.bottomPadding > 0)
        compare(slider.availableHeight, slider.height - slider.topPadding - slider.bottomPadding)
    }
}
