// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A titled block of PropertyRows: a SectionHeader (collapsible), the rows
// in a column with xs gaps, and a closing divider. Collapsing hides the
// rows and keeps the header, as the original's inspector sections do.
Item {
    id: group

    default property alias rows: body.data
    property string title: ""
    property string count: ""
    property bool collapsible: true
    property bool collapsed: false
    property bool divider: true
    property real spacing: Tk.Theme.space.xs

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    Accessible.role: Accessible.Grouping
    Accessible.name: group.title

    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: group.spacing

        Tk.SectionHeader {
            id: header
            objectName: "groupHeader"
            title: group.title
            count: group.count
            collapsible: group.collapsible
            // AGENT-NOTE: the header toggles its own `collapsed` by
            // assignment (which would break an inline binding), so the
            // two states are kept in step both ways explicitly.
            onCollapsedChanged: if (group.collapsed !== header.collapsed) group.collapsed = header.collapsed
        }
        Binding {
            target: header
            property: "collapsed"
            value: group.collapsed
        }

        Tk.Flex {
            id: body
            objectName: "groupBody"
            visible: !group.collapsed
            direction: Tk.Flex.Column
            gap: group.spacing
        }

        Tk.Divider {
            visible: group.divider
        }
    }
}
