// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Labeled, collapsible form section. `validationItems` entries may provide
// {item, invalid, pending}; async pending is reported separately from errors.
Item {
    id: group
    default property alias fields: body.data
    property string title: ""
    property bool collapsed: false
    property bool collapsible: true
    property var validationItems: []
    property real spacing: Tk.Theme.space.xs
    readonly property int invalidCount: {
        let count = 0
        for (let i = 0; i < validationItems.length; ++i) if (validationItems[i].invalid) ++count
        return count
    }
    readonly property int pendingCount: {
        let count = 0
        for (let i = 0; i < validationItems.length; ++i) if (validationItems[i].pending) ++count
        return count
    }
    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: title
    Accessible.description: invalidCount > 0 ? qsTr("%1 invalid fields").arg(invalidCount)
                             : pendingCount > 0 ? qsTr("%1 fields validating").arg(pendingCount) : ""

    function focusFirstInvalid() {
        for (let i = 0; i < validationItems.length; ++i) {
            const entry = validationItems[i]
            if (entry.invalid && entry.item && entry.item.forceActiveFocus) {
                if (collapsed) collapsed = false
                entry.item.forceActiveFocus(Qt.TabFocusReason)
                return true
            }
        }
        return false
    }

    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: group.spacing
        Tk.SectionHeader {
            objectName: "formGroupHeader"
            title: group.title
            count: group.invalidCount > 0 ? String(group.invalidCount) : group.pendingCount > 0 ? qsTr("Checking") : ""
            collapsible: group.collapsible
            collapsed: group.collapsed
            onCollapsedChanged: group.collapsed = collapsed
        }
        Tk.Flex {
            id: body
            objectName: "formGroupBody"
            visible: !group.collapsed
            direction: Tk.Flex.Column
            gap: group.spacing
        }
    }
}
