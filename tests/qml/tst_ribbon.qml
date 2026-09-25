// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Ribbon v2 (D-297): weights, rows groups, live action binding, squeeze
// levels with no hidden command, compact mode, item menus, trailing slot.
TestCase {
    id: root
    name: "Ribbon"
    when: windowShown
    visible: true
    width: 1280
    height: 400

    Component { id: spyComponent; SignalSpy { } }
    Component {
        id: actionComponent
        QtObject {
            property string text: "&Bold"
            property string iconName: "bold"
            property bool enabled: true
            property bool visible: true
            property bool checkable: true
            property bool checked: false
            property string shortcutText: "Ctrl+B"
            property int triggers: 0
            function trigger() { if (!enabled) return; triggers += 1; checked = !checked }
        }
    }
    Component { id: comboComponent; Tk.ComboBox { objectName: "hostCombo"; small: true; implicitWidth: 140; model: ["A", "B"] } }
    Component {
        id: ribbonComponent
        Tk.Ribbon {
            id: ribbon
            property QtObject bold: null
            width: 1280
            trailing: Tk.SearchField { objectName: "hostSearch"; implicitWidth: 200 }
            tabs: [{ id: "home", label: "Home", groups: [
                { id: "clipboard", label: "Clipboard", items: [
                    { id: "paste", label: "Paste", iconName: "clipboard", size: "large", split: true,
                      menu: [{ id: "pastePlain", label: "Paste as text" }, "|", { id: "pasteSpecial", label: "Paste special" }] },
                    { id: "cut", label: "Cut", iconName: "scissors" },
                    { id: "copy", label: "Copy", iconName: "copy" },
                    { id: "painter", label: "Format painter", iconName: "brush" } ] },
                { id: "font", label: "Font", rows: [
                    [{ id: "family", component: comboComponent }, { id: "grow", label: "Grow", iconName: "no-such-icon-xyz", size: "icon" }],
                    [{ id: "bold", action: ribbon.bold, size: "icon" }, { id: "italic", label: "Italic", iconName: "italic", size: "icon" }] ] },
                { id: "insert", label: "Insert", items: [
                    { id: "table", label: "Table", iconName: "table", size: "large" },
                    { id: "image", label: "Picture", iconName: "image", size: "large" },
                    { id: "link", label: "Link", iconName: "link" } ] }
            ] }]
        }
    }

    function makeRibbon(width) {
        const bold = createTemporaryObject(actionComponent, root)
        const ribbon = createTemporaryObject(ribbonComponent, root, { bold: bold, width: width || 1280 })
        verify(ribbon)
        waitForRendering(ribbon)
        return ribbon
    }
    function allItemsInside(ribbon) {
        const ids = ["paste", "cut", "copy", "painter", "family", "grow", "bold", "italic", "table", "image", "link"]
        for (let i = 0; i < ids.length; ++i) {
            const item = findChild(ribbon, "ribbonItem_" + ids[i])
            verify(item !== null, ids[i])
            verify(item.visible && item.width > 0, ids[i] + " visible")
            const right = item.mapToItem(ribbon, item.width, item.height)
            verify(right.x <= ribbon.width + 0.5, ids[i] + " inside horizontally")
            verify(right.y <= ribbon.height + 0.5, ids[i] + " inside vertically")
        }
    }

    function test_weights_and_rows() {
        const ribbon = makeRibbon()
        compare(ribbon.squeezeLevel, 0)
        const paste = findChild(ribbon, "ribbonAction_paste")
        compare(paste.size, "large")
        compare(paste.height, ribbon.bandHeight)
        const cut = findChild(ribbon, "ribbonItem_cut")
        const copy = findChild(ribbon, "ribbonItem_copy")
        const painter = findChild(ribbon, "ribbonItem_painter")
        // Two small items stack in one column; the third starts the next.
        compare(cut.x, copy.x)
        verify(copy.y > cut.y)
        verify(painter.x > cut.x)
        // Rows groups keep their rows: the combo row above the bold row.
        const family = findChild(ribbon, "ribbonItem_family")
        const bold = findChild(ribbon, "ribbonItem_bold")
        verify(bold.y > family.y)
        compare(family.x, bold.x)
        verify(findChild(ribbon, "hostCombo"))
        // D-013: an unknown icon falls back to the label, never a blank square.
        const grow = findChild(ribbon, "ribbonAction_grow")
        compare(grow.iconName, "")
        compare(grow.implicitWidth, grow.smallWidth)
    }

    function test_action_binding_is_live() {
        const ribbon = makeRibbon()
        const button = findChild(ribbon, "ribbonAction_bold")
        verify(button)
        compare(button.text, "&Bold")
        compare(button.Accessible.name, "Bold")
        compare(button.checked, false)
        const spy = createTemporaryObject(spyComponent, root, { target: ribbon, signalName: "itemRequested" })
        mouseClick(button)
        compare(ribbon.bold.triggers, 1)
        compare(spy.count, 1)
        compare(button.checked, true)
        ribbon.bold.checked = false
        compare(button.checked, false)
        ribbon.bold.enabled = false
        compare(button.enabled, false)
        verify(!ribbon.activateItem("font", "bold"))
        compare(ribbon.bold.triggers, 1)
        ribbon.bold.visible = false
        compare(findChild(ribbon, "ribbonItem_bold").visible, false)
    }

    function test_squeeze_never_hides_a_command() {
        const ribbon = makeRibbon(1280)
        compare(ribbon.squeezeLevel, 0)
        const full = ribbon.implicitHeight
        let previous = 0
        for (const width of [900, 700, 560, 520, 420, 300]) {
            ribbon.width = width
            waitForRendering(ribbon)
            verify(ribbon.squeezeLevel >= previous, "levels only rise as the width shrinks")
            previous = ribbon.squeezeLevel
            allItemsInside(ribbon)
        }
        compare(ribbon.squeezeLevel, 3)
        verify(ribbon.implicitHeight > full, "wrapped groups make the ribbon taller")
        ribbon.width = 1280
        tryCompare(ribbon, "squeezeLevel", 0)
    }

    function test_compact_mode() {
        const ribbon = makeRibbon()
        const expanded = ribbon.implicitHeight
        ribbon.mode = "compact"
        waitForRendering(ribbon)
        verify(ribbon.implicitHeight < expanded)
        const paste = findChild(ribbon, "ribbonAction_paste")
        verify(paste.size !== "large")
        verify(!findChild(ribbon, "ribbonGroupCaption").visible)
        const cut = findChild(ribbon, "ribbonItem_cut")
        const copy = findChild(ribbon, "ribbonItem_copy")
        compare(cut.y, copy.y)
        allItemsInside(ribbon)
    }

    function test_split_menu_and_trailing() {
        const ribbon = makeRibbon()
        const trailing = findChild(ribbon, "ribbonTrailing")
        verify(trailing)
        compare(ribbon.trailing.parent, trailing)
        const paste = findChild(ribbon, "ribbonAction_paste")
        const spy = createTemporaryObject(spyComponent, root, { target: ribbon, signalName: "itemRequested" })
        // The body activates Paste; the chevron half opens the menu only.
        mouseClick(paste, paste.width / 2, paste.height / 4)
        compare(spy.count, 1)
        compare(spy.signalArguments[0][2], "paste")
        const area = findChild(paste, "ribbonButtonMenuArea")
        verify(area && area.visible)
        mouseClick(area)
        const menu = findChild(ribbon, "ribbonItemMenu")
        tryVerify(function() { return menu.opened })
        compare(spy.count, 1)
        compare(menu.count, 2)
        menu.itemAt(1).triggered()
        compare(spy.count, 2)
        compare(spy.signalArguments[1][2], "pasteSpecial")
        menu.close()
    }

    function test_collapse_and_tab_double_click() {
        const ribbon = makeRibbon()
        const toggled = createTemporaryObject(spyComponent, root, { target: ribbon, signalName: "collapseToggled" })
        findChild(ribbon, "ribbonCollapseButton").clicked()
        compare(ribbon.collapsed, true)
        compare(findChild(ribbon, "ribbonGroupViewport").height, 0)
        // Clicking a tab while collapsed shows the band again.
        mouseClick(findChild(ribbon, "ribbonTab_home"))
        compare(ribbon.collapsed, false)
        mouseDoubleClickSequence(findChild(ribbon, "ribbonTab_home"))
        compare(ribbon.collapsed, true)
        verify(toggled.count >= 3)
    }
}
