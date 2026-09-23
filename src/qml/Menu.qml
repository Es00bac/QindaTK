// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: a real T.Menu — popup(), cascading submenus, keyboard
// navigation and mnemonics all come from the template. Visuals: the
// popover surface with a 1px popover border, 6px radius, 22px items. Use
// Tk.MenuItem / Tk.MenuSeparator as children; `title` names it in a
// Tk.MenuBar or as a submenu entry.
T.Menu {
    id: menu

    property real minWidth: Tk.Theme.size.menuMinWidth

    readonly property real _widestItem: {
        let widest = 0
        for (let i = 0; i < menu.count; ++i) {
            const entry = menu.itemAt(i)
            if (entry && entry.visible) widest = Math.max(widest, entry.implicitWidth)
        }
        return widest
    }
    readonly property real _horizontalRoom: {
        let room = workArea.availableGeometry.width
        const window = menu.parent ? menu.parent.Window.window : null
        if (window) room = Math.min(room, window.width)
        return Math.max(0, room - 2 * menu.margins)
    }
    readonly property real _naturalWidth: Math.max(
        implicitBackgroundWidth + leftInset + rightInset,
        _widestItem + leftPadding + rightPadding, menu.minWidth)
    // AGENT-GUARD: use item natural widths before Qt creates Popup.Window;
    // Flex's growing label otherwise leaves a 160px menu with elided text.
    implicitWidth: _horizontalRoom > 0 ? Math.min(_naturalWidth, _horizontalRoom)
                                      : _naturalWidth
    readonly property real naturalHeight: Math.max(
        implicitBackgroundHeight + topInset + bottomInset,
        contentHeight + topPadding + bottomPadding)
    // AGENT-GUARD: Popup.Window resizes from implicitHeight after a model
    // change. Bound it before Qt creates or resizes the native popup window.
    readonly property real verticalRoom: Math.max(0, workArea.availableGeometry.height - 2 * margins)
    readonly property Tk.ScreenWorkArea workArea: Tk.ScreenWorkArea {
        objectName: "menuWorkArea"
        anchor: menu.parent
        anchorPoint: Qt.point(menu.x, menu.y)
        trackTransforms: menu.visible
    }
    onAboutToShow: workArea.refresh()
    implicitHeight: verticalRoom > 0 ? Math.min(naturalHeight, verticalRoom) : naturalHeight
    margins: Tk.Theme.space.xs
    overlap: 1
    padding: Tk.Theme.space.xs
    cascade: true

    delegate: Tk.MenuItem { }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: menu.contentModel
        interactive: contentHeight > height
        clip: true
        currentIndex: menu.currentIndex
        boundsBehavior: Flickable.StopAtBounds
        onCurrentIndexChanged: {
            if (currentIndex >= 0)
                positionViewAtIndex(currentIndex, ListView.Contain)
        }

        T.ScrollBar.vertical: Tk.ScrollBar { }
    }

    background: Rectangle {
        implicitWidth: menu.minWidth
        implicitHeight: Tk.Theme.size.menuItem
        color: Tk.Theme.color.popoverBg
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.popoverBorder
        radius: Tk.Theme.radius.md
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Tk.Theme.motion.fast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Tk.Theme.motion.fast }
    }

    T.Overlay.modal: Rectangle { color: Tk.Theme.alpha(Tk.Theme.color.overlay, Tk.Theme.opacity.hidden) }
    T.Overlay.modeless: Rectangle { color: Tk.Theme.alpha(Tk.Theme.color.overlay, Tk.Theme.opacity.hidden) }
}
