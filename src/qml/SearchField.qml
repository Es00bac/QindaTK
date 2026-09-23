// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a TextField with a search glyph, a
// clear button, Escape-to-clear, and `searched(text)` emitted `debounce`
// milliseconds after the last user edit (and immediately on Enter). Host-side
// text synchronization does not start a search.
Tk.TextField {
    id: field

    property int debounce: 150

    signal searched(string text)

    iconName: "search"
    clearable: true
    placeholderText: qsTr("Search")

    onTextEdited: debounceTimer.restart()
    onAccepted: {
        debounceTimer.stop()
        field.searched(field.text)
    }
    onCleared: {
        debounceTimer.stop()
        field.searched("")
    }

    Timer {
        id: debounceTimer
        interval: Math.max(0, field.debounce)
        repeat: false
        onTriggered: field.searched(field.text)
    }
}
