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

    signal expansionToggled(bool expanded)

    indent: tree.depth * Tk.Theme.space.lg

    function toggle() {
        if (!tree.expandable) {
            return
        }
        tree.expanded = !tree.expanded
        tree.expansionToggled(tree.expanded)
    }

    onDoubleClicked: tree.toggle()

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
