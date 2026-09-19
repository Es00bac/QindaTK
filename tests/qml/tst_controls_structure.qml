// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Structure and navigation controls: each instantiates, reports the
// height docs/controls.md fixes, and answers one interaction.
TestCase {
    id: root
    name: "ControlsStructure"
    when: windowShown
    // AGENT-GUARD: a TestCase is invisible by default; Flex skips hidden
    // children and mouse events never reach them, so every layout or
    // pointer assertion below silently fails without this line.
    visible: true
    width: 640
    height: 480

    Component { id: tabStripComponent; Tk.TabStrip { model: ["One", "Two", "Three"]; width: 300 } }
    Component { id: statusBarComponent; Tk.StatusBar { width: 300; Tk.StatusField { text: "Saved" } } }
    Component { id: statusFieldComponent; Tk.StatusField { text: "3 assets"; iconName: "layers" } }
    Component { id: propertyRowComponent; Tk.PropertyRow { label: "Width"; width: 240; Tk.Label { text: "170" } } }
    Component { id: propertyRowHintComponent; Tk.PropertyRow { label: "Fit"; hint: "How the clip fills the frame."; width: 240; Tk.Label { text: "Contain" } } }
    Component { id: propertyGroupComponent; Tk.PropertyGroup { title: "Document"; width: 240; Tk.PropertyRow { label: "A"; Tk.Label { text: "1" } } Tk.PropertyRow { label: "B"; Tk.Label { text: "2" } } } }
    Component { id: keyValueComponent; Tk.KeyValue { key: "Canvas"; value: "16:9"; width: 240 } }
    Component { id: listRowComponent; Tk.ListRow { text: "walk-wide.mp4"; iconName: "film"; width: 240 } }
    Component { id: listRowTwoLineComponent; Tk.ListRow { text: "turn-cu.mp4"; secondaryText: "V1 · 8.0s"; width: 240 } }
    Component { id: treeRowComponent; Tk.TreeRow { text: "clips"; expandable: true; depth: 1; width: 240 } }
    Component { id: menuComponent; Tk.Menu { Tk.MenuItem { text: "Undo"; shortcut: "Ctrl+Z" } Tk.MenuSeparator {} Tk.MenuItem { text: "Redo" } } }
    Component { id: menuBarComponent; Tk.MenuBar { width: 300; Tk.Menu { title: "File"; Tk.MenuItem { text: "New" } } } }
    Component { id: popoverComponent; Tk.Popover { Tk.Label { text: "Snapping" } } }
    Component { id: dialogComponent; Tk.Dialog { title: "Discard?"; Tk.Label { text: "Body" } } }
    Component {
        id: paletteComponent
        Tk.CommandPalette {
            commands: [
                { id: "file.new", label: "New project", section: "File" },
                { id: "edit.undo", label: "Undo", section: "Edit", keywords: "revert" },
                { id: "view.grid", label: "Toggle grid", section: "View" }
            ]
        }
    }
    Component { id: noticeComponent; Tk.Notice { text: "Render complete."; variant: "success"; dismissible: true; width: 240 } }
    Component { id: splitterComponent; Tk.Splitter { width: 300; height: 100; Item { implicitWidth: 100 } Item { } } }
    Component { id: rulerComponent; Tk.Ruler { width: 300; pixelsPerUnit: 10 } }
    Component {
        id: markedRulerComponent
        Tk.Ruler {
            width: 300; pixelsPerUnit: 10
            band: [20, 280]; activeBand: [60, 240]
            markers: [{ id: "first", position: 80, kind: "indent-first", draggable: true },
                      { id: "left", position: 60, kind: "indent-left", draggable: true },
                      { id: "right", position: 240, kind: "indent-right", draggable: true }]
        }
    }
    Component { id: zoomComponent; Tk.ZoomControl { value: 1.0 } }
    Component { id: emptyComponent; Tk.EmptyState { title: "No markers"; text: "Add one with M."; width: 240 } }
    Component { id: cardComponent; Tk.Card { interactive: true; width: 240; Tk.Label { text: "Card" } } }

    function test_tab_strip() {
        const strip = createTemporaryObject(tabStripComponent, root)
        compare(strip.implicitHeight, Tk.Theme.size.tab)
        compare(strip.count, 3)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: strip, signalName: "tabActivated" })
        waitForRendering(strip)
        const second = findChild(strip, "tab_1")
        verify(second !== null)
        mouseClick(second)
        compare(spy.count, 1)
        compare(strip.currentIndex, 1)
    }

    // A C++ QStringList/QVariantList property reaches QML as a sequence
    // object: it has length and indexing but Array.isArray() is false.
    // Qt.application.arguments is such a value in-process. The strip must
    // count it — an isArray() gate hid every tab (QindaCalc's sheet bar).
    function test_tab_strip_accepts_cpp_sequence_model() {
        const args = Qt.application.arguments
        verify(args.length > 0)
        compare(Array.isArray(args), false) // documents the hazard this guards
        const strip = createTemporaryObject(tabStripComponent, root)
        strip.model = args
        compare(strip.count, args.length)
        waitForRendering(strip)
        verify(findChild(strip, "tab_0") !== null)
        strip.model = ["Solo"]
        compare(strip.count, 1)
    }

    function test_status_bar_and_field() {
        const bar = createTemporaryObject(statusBarComponent, root)
        compare(bar.implicitHeight, Tk.Theme.size.statusBar)
        const field = createTemporaryObject(statusFieldComponent, root, { y: 100 })
        compare(field.implicitHeight, Tk.Theme.size.statusBar)
        waitForRendering(field)
        verify(field.implicitWidth > 20)
    }

    function test_property_row_and_group() {
        const row = createTemporaryObject(propertyRowComponent, root)
        waitForRendering(row)
        verify(row.implicitHeight >= Tk.Theme.size.row)
        verify(findChild(row, "propertyLabel") !== null)
        const hinted = createTemporaryObject(propertyRowHintComponent, root, { y: 60 })
        waitForRendering(hinted)
        verify(hinted.implicitHeight > row.implicitHeight)
        const group = createTemporaryObject(propertyGroupComponent, root, { y: 160 })
        waitForRendering(group)
        const body = findChild(group, "groupBody")
        verify(body.visible)
        group.collapsed = true
        verify(!body.visible)
        const header = findChild(group, "groupHeader")
        compare(header.collapsed, true)
    }

    function test_key_value() {
        const kv = createTemporaryObject(keyValueComponent, root)
        compare(kv.implicitHeight, Tk.Theme.size.row)
        compare(findChild(kv, "valueLabel").text, "16:9")
    }

    function test_list_rows() {
        const row = createTemporaryObject(listRowComponent, root)
        compare(row.implicitHeight, Tk.Theme.size.row)
        const two = createTemporaryObject(listRowTwoLineComponent, root, { y: 100 })
        compare(two.implicitHeight, Tk.Theme.size.rowLg)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: row, signalName: "clicked" })
        waitForRendering(row)
        mouseClick(row)
        compare(spy.count, 1)
    }

    function test_tree_row_toggles() {
        const tree = createTemporaryObject(treeRowComponent, root)
        compare(tree.implicitHeight, Tk.Theme.size.row)
        compare(tree.indent, Tk.Theme.space.lg)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: tree, signalName: "expansionToggled" })
        tree.toggle()
        compare(spy.count, 1)
        compare(tree.expanded, true)
        waitForRendering(tree)
        mouseDoubleClickSequence(tree)
        compare(tree.expanded, false)
    }

    function test_menu_and_bar() {
        const menu = createTemporaryObject(menuComponent, root)
        compare(menu.count, 3)
        menu.popup(10, 10)
        tryVerify(function() { return menu.opened })
        compare(menu.itemAt(0).height, Tk.Theme.size.menuItem)
        menu.close()
        tryVerify(function() { return !menu.visible })
        const bar = createTemporaryObject(menuBarComponent, root)
        waitForRendering(bar)
        compare(bar.implicitHeight, Tk.Theme.size.statusBar)
        compare(bar.count, 1)
    }

    function test_popover_open_close() {
        const anchor = createTemporaryObject(cardComponent, root)
        anchor.x = 20
        anchor.y = 20
        const pop = createTemporaryObject(popoverComponent, root, { anchorItem: anchor, placement: "bottom" })
        pop.open()
        tryVerify(function() { return pop.opened })
        verify(pop.y >= anchor.y + anchor.height)
        pop.close()
        tryVerify(function() { return !pop.visible })
    }

    function test_dialog_accepts() {
        const dialog = createTemporaryObject(dialogComponent, root)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: dialog, signalName: "accepted" })
        dialog.open()
        tryVerify(function() { return dialog.opened })
        const primary = findChild(dialog.footer, "dialogPrimary")
        verify(primary !== null)
        mouseClick(primary)
        compare(spy.count, 1)
        tryVerify(function() { return !dialog.visible })
    }

    function test_command_palette_filters_and_activates() {
        const palette = createTemporaryObject(paletteComponent, root)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: palette, signalName: "activated" })
        palette.open()
        tryVerify(function() { return palette.opened })
        compare(palette.resultCount, 3)
        palette.filterText = "rev"
        compare(palette.resultCount, 1)
        compare(palette.rows[1].id, "edit.undo")
        keyClick(Qt.Key_Return)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "edit.undo")
        tryVerify(function() { return !palette.visible })
    }

    function test_notice_dismisses() {
        const notice = createTemporaryObject(noticeComponent, root)
        waitForRendering(notice)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: notice, signalName: "dismissed" })
        const dismiss = findChild(notice, "noticeDismiss")
        verify(dismiss !== null)
        mouseClick(dismiss)
        compare(spy.count, 1)
    }

    function test_splitter_ruler_empty_card() {
        const splitter = createTemporaryObject(splitterComponent, root)
        compare(splitter.handleSize, Tk.Theme.size.seam)
        const ruler = createTemporaryObject(rulerComponent, root, { y: 120 })
        compare(ruler.implicitHeight, Tk.Theme.size.row)
        const empty = createTemporaryObject(emptyComponent, root, { y: 160 })
        waitForRendering(empty)
        verify(empty.implicitHeight > Tk.Theme.size.row)
        const card = createTemporaryObject(cardComponent, root, { y: 320 })
        waitForRendering(card)
        compare(card.padding, Tk.Theme.space.md)
        verify(card.implicitHeight > Tk.Theme.space.md * 2)
    }

    function test_ruler_markers_drag_and_tabs() {
        const ruler = createTemporaryObject(markedRulerComponent, root, { y: 120 })
        waitForRendering(ruler)
        const moved = createTemporaryObject(signalSpyComponent, root, { target: ruler, signalName: "markerMoved" })
        const added = createTemporaryObject(signalSpyComponent, root, { target: ruler, signalName: "tabAdded" })
        verify(findChild(ruler, "rulerMarker_left") !== null)
        verify(findChild(ruler, "rulerBand").visible)
        // Drag the left indent on the bottom edge 30 px to the right.
        mousePress(ruler, 60, ruler.height - 4)
        mouseMove(ruler, 90, ruler.height - 4)
        mouseRelease(ruler, 90, ruler.height - 4)
        verify(moved.count >= 1)
        compare(moved.signalArguments[moved.count - 1][0], "left")
        compare(moved.signalArguments[moved.count - 1][1], 90)
        // A double-click on the empty bottom half adds a tab there.
        mouseDoubleClickSequence(ruler, 150, ruler.height - 4)
        compare(added.count, 1)
        compare(added.signalArguments[0][0], 150)
    }

    function test_zoom_control_steps() {
        const zoom = createTemporaryObject(zoomComponent, root)
        compare(zoom.implicitHeight, Tk.Theme.size.control)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: zoom, signalName: "valueModified" })
        waitForRendering(zoom)
        mouseClick(findChild(zoom, "zoomIn"))
        compare(spy.count, 1)
        compare(zoom.value, 1.5)
        mouseClick(findChild(zoom, "zoomOut"))
        compare(zoom.value, 1.0)
        zoom.value = 3
        zoom.reset()
        compare(zoom.value, 1.0)
    }

    Component { id: signalSpyComponent; SignalSpy { } }
}
