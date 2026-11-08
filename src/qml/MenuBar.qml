// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// A 22px menu bar on the surface colour: small-font titles, the open one
// filled with the active control tint. Children are Tk.Menu { title }.
// AGENT-CONTRACT (D-315, docs/controls.md): when the bar is narrower than
// its menus, the trailing menus move — as themselves, with their items,
// mnemonics and submenus — into a "»" menu (`overflowMenu`, objectName
// "menuBarOverflow") that is the bar's last entry, and move back when it
// widens. The bar reports the natural width of ALL its menus as
// implicitWidth, so a layout never shrinks it because menus moved. While
// menus overflow, `count`, `menuAt()` and `menus` describe the bar as shown
// ("»" included); `overflowMenus` lists the moved menus in order. Hosts keep
// adding and removing menus with insertMenu/removeMenu: the bar re-flows
// after every change, and a moved menu its host destroys is dropped.
T.MenuBar {
    id: bar

    property bool overflowEnabled: true
    // AGENT-CONTRACT (D-361, docs/native-integration.md): every Tk.MenuBar is
    // published to the desktop's global menu; there is deliberately no switch
    // to turn that off. While the desktop confirms it shows this menu
    // (`desktopHosted`), the in-window bar collapses to zero height and its
    // Alt mnemonics stand down; in every other case (no global-menu panel,
    // offscreen, a non-QindaQt desktop, an app that exports through the
    // QindaQt AppShell itself) it stays the normal in-window bar.
    readonly property Tk.GlobalMenuExport globalMenu: Tk.GlobalMenuExport {
        objectName: "menuBarGlobalMenu"
        menuBar: bar
    }
    readonly property bool desktopHosted: globalMenu.hosted
    readonly property int overflowCount: _overflowList.length
    readonly property var overflowMenus: _overflowList
    readonly property Tk.Menu overflowMenu: Tk.Menu {
        objectName: "menuBarOverflow"
        title: "»"
    }
    property var _overflowList: []
    property var _widths: []
    property string _measuredKey: ""
    property real _fullWidth: 0
    property real _overflowEntryWidth: 0
    property bool _reflowing: false
    readonly property Timer _reflowRetry: Timer {
        interval: Tk.Theme.motion.slow
        onTriggered: bar._reflow()
    }

    Accessible.role: Accessible.MenuBar
    Accessible.name: "Menu bar"

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            Math.max(contentWidth, _fullWidth) + leftPadding + rightPadding)
    implicitHeight: desktopHosted ? 0
                                  : Math.max(implicitBackgroundHeight + topInset + bottomInset,
                                             contentHeight + topPadding + bottomPadding)
    padding: 0
    leftPadding: Tk.Theme.space.xs
    rightPadding: Tk.Theme.space.xs
    spacing: 1

    // The host's menus in the bar, in bar order ("»" excluded).
    function _barMenus() {
        const out = []
        for (let i = 0; i < bar.count; ++i) {
            const menu = bar.menuAt(i)
            if (menu && menu !== overflowMenu) out.push(menu)
        }
        return out
    }
    // The menus in "»", in order. A menu its host destroyed leaves an entry
    // whose subMenu is null (a guarded pointer); those are removed here.
    function _movedMenus() {
        const out = []
        for (let i = overflowMenu.count - 1; i >= 0; --i) {
            const item = overflowMenu.itemAt(i) as T.MenuItem
            if (!item || !item.subMenu) overflowMenu.removeItem(overflowMenu.itemAt(i))
        }
        for (let j = 0; j < overflowMenu.count; ++j) out.push((overflowMenu.itemAt(j) as T.MenuItem).subMenu)
        return out
    }
    function _overflowIndex() {
        for (let i = 0; i < bar.count; ++i) if (bar.menuAt(i) === overflowMenu) return i
        return -1
    }
    // Every moved menu back into the bar where "»" stood.
    function _restoreAll() {
        const at = _overflowIndex()
        if (at >= 0) bar.takeMenu(at)
        let index = at >= 0 ? at : bar.count
        while (overflowMenu.count > 0) {
            const menu = overflowMenu.takeMenu(0)
            if (menu) bar.insertMenu(index++, menu)
            else overflowMenu.removeItem(overflowMenu.itemAt(0))
        }
    }
    function _anyMenuOpen() {
        if (overflowMenu.visible) return true
        for (let i = 0; i < bar.count; ++i) if (bar.menuAt(i) && bar.menuAt(i).visible) return true
        return false
    }
    function _scheduleReflow() { if (!_reflowing) Qt.callLater(_reflow) }
    // AGENT-GUARD: the only place that moves menus, and it only uses
    // takeMenu/insertMenu/addMenu, which never destroy a menu (removeMenu
    // would). Widths are measured from the real MenuBarItems with every menu
    // in the bar, and re-measured only when the titles or the theme change,
    // so resizing a window moves menus only when the number that fits does.
    function _reflow() {
        if (_reflowing) return
        if (_anyMenuOpen()) { _reflowRetry.restart(); return }
        _reflowing = true
        const titlesKey = function(menus) {
            return menus.map(menu => String(menu.title)).join("\u001f") + "\u001e" + Tk.Theme.generation
        }
        if (titlesKey(_barMenus().concat(_movedMenus())) !== _measuredKey) {
            _restoreAll()
            const widths = []
            let total = 0
            for (let i = 0; i < bar.count; ++i) {
                widths.push(bar.itemAt(i).implicitWidth)
                total += (i > 0 ? spacing : 0) + widths[i]
            }
            bar.addMenu(overflowMenu)
            _overflowEntryWidth = bar.itemAt(bar.count - 1).implicitWidth
            bar.takeMenu(bar.count - 1)
            _widths = widths
            _fullWidth = total
            _measuredKey = titlesKey(_barMenus())
        }
        const all = _barMenus().concat(_movedMenus())
        const available = bar.width - bar.leftPadding - bar.rightPadding
        let fit = all.length
        if (overflowEnabled && available > 0 && _fullWidth > available) {
            let used = 0
            fit = 0
            while (fit < all.length) {
                const next = used + (fit > 0 ? spacing : 0) + _widths[fit]
                if (next + spacing + _overflowEntryWidth > available) break
                used = next
                ++fit
            }
        }
        const shown = _barMenus().length
        const settled = shown === fit && (fit === all.length ? _overflowIndex() < 0 : _overflowIndex() === bar.count - 1)
        if (!settled) {
            _restoreAll()
            const moved = []
            for (let n = bar.count - 1; n >= fit; --n) moved.unshift(bar.takeMenu(n))
            for (let m = 0; m < moved.length; ++m) overflowMenu.addMenu(moved[m])
            if (moved.length > 0) bar.addMenu(overflowMenu)
        }
        _overflowList = _movedMenus()
        _reflowing = false
    }

    onWidthChanged: _scheduleReflow()
    onCountChanged: _scheduleReflow()
    onOverflowEnabledChanged: _scheduleReflow()
    onLeftPaddingChanged: _scheduleReflow()
    onRightPaddingChanged: _scheduleReflow()
    Component.onCompleted: _scheduleReflow()
    Connections {
        target: Tk.Theme
        function onChanged() { bar._scheduleReflow() }
    }

    delegate: T.MenuBarItem {
        id: entry

        readonly property bool overflowEntry: entry.menu === bar.overflowMenu
        Accessible.role: Accessible.MenuItem
        Accessible.name: overflowEntry ? qsTr("More menus")
                                       : entry.text.replace(/&(?!&)/g, "").replace(/&&/g, "&")

        implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: Tk.Theme.size.statusBar
        leftPadding: Tk.Theme.space.sm
        rightPadding: Tk.Theme.space.sm
        topPadding: 0
        bottomPadding: 0
        hoverEnabled: true
        onTextChanged: bar._scheduleReflow()

        contentItem: Tk.Label {
            // The '&' mnemonic marker is for the keyboard (Alt+letter,
            // handled by the template), not for the eye.
            text: entry.overflowEntry ? entry.text : entry.Accessible.name
            font.pixelSize: Tk.Theme.font.small
            color: !entry.enabled ? Tk.Theme.color.textDisabled
                 : entry.highlighted || entry.hovered ? Tk.Theme.color.text : Tk.Theme.color.textMuted
            horizontalAlignment: Text.AlignHCenter
        }

        background: Rectangle {
            implicitWidth: 40
            implicitHeight: Tk.Theme.size.statusBar
            radius: Tk.Theme.radius.xs
            color: entry.highlighted ? Tk.Theme.color.controlActiveBg
                 : entry.hovered ? Tk.Theme.color.hover : "transparent"
        }
    }

    contentItem: Row {
        visible: !bar.desktopHosted
        spacing: bar.spacing
        Repeater {
            model: bar.contentModel
        }
    }

    background: Rectangle {
        visible: !bar.desktopHosted
        implicitHeight: Tk.Theme.size.statusBar
        color: Tk.Theme.color.surface
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Tk.Theme.color.divider
        }
    }
}
