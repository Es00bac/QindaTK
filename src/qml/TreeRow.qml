// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A ListRow with a depth indent and a disclosure chevron. Clicking the
// chevron (or double-clicking the row) toggles `expanded` and emits
// `expansionToggled`; the row itself never owns the children it discloses.
// AGENT-NOTE: the signal is not `toggled(bool)` as docs/controls.md first
// said: AbstractButton already owns a `toggled()` signal and QML refuses
// the override.
Tk.ListRow {
    id: tree

    property int depth: 0
    property bool expandable: false
    property bool expanded: false
    // Inline rename: startEditing() (or a double-click when `editable`)
    // overlays a field on the label; Enter emits renamed(text), Escape
    // cancels. The row never changes its own `text`: the owner does after
    // it has renamed the real thing.
    property bool editable: false
    readonly property bool editing: editor.visible

    signal expansionToggled(bool expanded)
    signal renamed(string text)

    indent: tree.depth * Tk.Theme.space.lg

    function toggle() {
        if (!tree.expandable) {
            return
        }
        tree.expanded = !tree.expanded
        tree.expansionToggled(tree.expanded)
    }

    function startEditing() {
        if (!tree.editable) {
            return
        }
        editor.text = tree.text
        editor.visible = true
        editor.forceActiveFocus(Qt.TabFocusReason)
        editor.selectAll()
    }
    function cancelEditing() {
        editor.visible = false
    }

    onDoubleClicked: tree.editable ? tree.startEditing() : tree.toggle()

    Tk.TextField {
        id: editor
        objectName: "treeEditor"
        visible: false
        small: true
        z: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: tree.leftPadding + Tk.Theme.size.iconSm + tree.spacing
        anchors.rightMargin: tree.rightPadding
        onAccepted: {
            const value = editor.text.trim()
            editor.visible = false
            if (value.length > 0 && value !== tree.text) {
                tree.renamed(value)
            }
        }
        Keys.onEscapePressed: function(event) {
            editor.visible = false
            event.accepted = true
        }
        onActiveFocusChanged: if (!activeFocus && visible) editor.visible = false
    }

    // AGENT-NOTE: parented into the ListRow's leading slot so callers that
    // add their own leading items keep the chevron first.
    Item {
        parent: tree.leadingItem
        objectName: "treeChevron"
        implicitWidth: Tk.Theme.size.iconSm
        implicitHeight: Tk.Theme.size.iconSm
        Tk.Flex.shrink: 0

        Tk.Icon {
            anchors.centerIn: parent
            visible: tree.expandable
            name: tree.expanded ? "chevron-down" : "chevron-right"
            size: Tk.Theme.size.iconSm
            color: Tk.Theme.color.textMuted
        }
        TapHandler {
            enabled: tree.expandable
            onTapped: tree.toggle()
        }
    }
}
