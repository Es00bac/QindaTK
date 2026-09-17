// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: one inspector row. A muted caption label in a fixed
// column (Theme.size.labelWidth) and an editor that fills the rest; the
// row is at least Theme.size.row (22) tall so stacked rows align whatever
// the editor is. `hint` adds a caption line under the editor. The editor
// is the row's default content; a single editor is stretched (Box rules).
Item {
    id: row

    default property alias editor: editorHost.content
    readonly property Item editorItem: editorHost

    property string label: ""
    property real labelWidth: Tk.Theme.size.labelWidth
    property string hint: ""
    property bool alignTop: false
    property bool dense: false

    readonly property real minRowHeight: row.dense ? Tk.Theme.size.controlSm : Tk.Theme.size.row

    implicitWidth: row.labelWidth + Tk.Theme.space.sm + editorHost.implicitWidth
    implicitHeight: column.implicitHeight

    Accessible.role: Accessible.Grouping
    Accessible.name: row.label

    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.Flex {
            id: line
            direction: Tk.Flex.Row
            align: row.alignTop ? Tk.Flex.Start : Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Flex.minHeight: row.minRowHeight

            Tk.Caption {
                objectName: "propertyLabel"
                text: row.label
                Tk.Flex.basis: row.labelWidth
                Tk.Flex.shrink: 0
                // Keeps the label on the editor's first line when the row
                // is top-aligned for a tall editor.
                Tk.Flex.minHeight: row.alignTop ? row.minRowHeight : 0
                verticalAlignment: Text.AlignVCenter
            }
            Tk.Box {
                id: editorHost
                objectName: "propertyEditor"
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.minWidth: 0
            }
        }
        Tk.Caption {
            visible: row.hint.length > 0
            text: row.hint
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            Tk.Flex.minHeight: 0
        }
    }
}
