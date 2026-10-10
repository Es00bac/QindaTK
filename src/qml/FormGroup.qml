// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Labeled, collapsible form section. `validationItems` entries may provide
// {item, invalid, pending}; async pending is reported separately from errors.
// AGENT-CONTRACT (D-317, docs/controls.md): a user toggle (the header, or
// focusFirstInvalid expanding the group) is offered to the consumer first as
// toggled(collapsed). A consumer that binds `collapsed` updates the binding's
// source in onToggled and keeps its binding; only when `collapsed` is still
// unchanged after the signal does the group assign it itself (the behaviour
// before D-317), so an uncontrolled group still collapses on a click.
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
    // The user asked for `collapsed`; owner assignments never emit it.
    signal toggled(bool collapsed)

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: title
    Accessible.description: invalidCount > 0 ? qsTr("%1 invalid fields").arg(invalidCount)
                             : pendingCount > 0 ? qsTr("%1 fields validating").arg(pendingCount) : ""

    function requestCollapsed(next) {
        if (next === collapsed) return
        toggled(next)
        if (collapsed !== next) collapsed = next
    }
    function focusFirstInvalid() {
        for (let i = 0; i < validationItems.length; ++i) {
            const entry = validationItems[i]
            if (entry.invalid && entry.item && entry.item.forceActiveFocus) {
                requestCollapsed(false)
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
            id: header
            objectName: "formGroupHeader"
            title: group.title
            count: group.invalidCount > 0 ? String(group.invalidCount) : group.pendingCount > 0 ? qsTr("Checking") : ""
            collapsible: group.collapsible
            collapsed: group.collapsed
            // AGENT-GUARD: SectionHeader.toggle() assigns its own `collapsed`,
            // which breaks this binding; re-bind first, then route the request
            // through requestCollapsed so the header always mirrors the group.
            onCollapsedChanged: {
                if (collapsed === group.collapsed) return
                const next = collapsed
                collapsed = Qt.binding(function() { return group.collapsed })
                group.requestCollapsed(next)
            }
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
