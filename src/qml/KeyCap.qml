// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A keyboard shortcut, drawn as keys rather than written as a string.
//
// AGENT-NOTE: "Ctrl+Shift+K" in running text is read as prose and skimmed
// past; the same shortcut as three caps is read as keys and remembered. Menus,
// command palettes and shortcut sheets all want this, and all three were
// hand-rolling it.
//
// AGENT-CONTRACT: `sequence` is a portable Qt key string ("Ctrl+Shift+K").
// The separator is normalised here rather than by callers, so a sheet and a
// menu cannot disagree about whether it is "+" or a space.
Row {
    id: root

    property string sequence: ""
    // Native-looking symbols where a platform expects them. Off by default:
    // this desktop writes Ctrl, not a caret.
    property bool symbolic: false

    readonly property var keys: {
        const text = root.sequence.trim()
        if (text.length === 0) {
            return []
        }
        return text.split("+").map(function(part) {
            const key = part.trim()
            if (!root.symbolic) {
                return key
            }
            return key === "Ctrl" ? "⌃" : key === "Shift" ? "⇧"
                 : key === "Alt" ? "⌥" : key === "Meta" ? "⌘" : key
        })
    }

    objectName: "keyCap"
    spacing: Tk.Theme.space.xs
    visible: root.keys.length > 0

    Accessible.role: Accessible.StaticText
    Accessible.name: root.sequence

    Repeater {
        model: root.keys
        delegate: Rectangle {
            required property string modelData
            objectName: "keyCapKey"
            height: Tk.Theme.size.row - Tk.Theme.space.xs * 2
            width: Math.max(height, label.implicitWidth + Tk.Theme.space.sm * 2)
            radius: Tk.Theme.radius.xs
            color: Tk.Theme.color.chipBg
            border.width: Tk.Theme.size.border
            border.color: Tk.Theme.color.chipBorder

            Tk.Caption {
                id: label
                objectName: "keyCapLabel"
                anchors.centerIn: parent
                text: modelData
                color: Tk.Theme.color.textMuted
            }
        }
    }
}
