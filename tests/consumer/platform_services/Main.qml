// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 400
    height: 240

    Tk.ClipboardService { objectName: "installedClipboard" }
    Tk.DragDropService { objectName: "installedDragDrop" }
    Tk.LocaleService { objectName: "installedLocale" }
    Tk.GestureRouter { objectName: "installedGesture" }
    Tk.AccessibilityAdapter {
        objectName: "installedAccessibility"
        width: parent.width
        height: parent.height
        accessibleName: "Consumer virtual editor"
        Accessible.role: Accessible.Pane
    }
}
