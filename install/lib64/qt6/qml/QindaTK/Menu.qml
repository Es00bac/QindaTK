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

    property real minWidth: 160

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding, menu.minWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    margins: Tk.Theme.space.xs
    overlap: 1
    padding: Tk.Theme.space.xs
    cascade: true

    delegate: Tk.MenuItem { }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: menu.contentModel
        interactive: Window.window
                     ? contentHeight + menu.topPadding + menu.bottomPadding > Window.window.height
                     : false
        clip: true
        currentIndex: menu.currentIndex
        boundsBehavior: Flickable.StopAtBounds

        T.ScrollBar.vertical: Tk.ScrollBar { }
    }

    background: Rectangle {
        implicitWidth: menu.minWidth
        implicitHeight: Tk.Theme.size.menuItem
        color: Tk.Theme.color.popoverBg
        border.width: 1
        border.color: Tk.Theme.color.popoverBorder
        radius: Tk.Theme.radius.md
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Tk.Theme.motion.fast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Tk.Theme.motion.fast }
    }

    T.Overlay.modal: Rectangle { color: "transparent" }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
