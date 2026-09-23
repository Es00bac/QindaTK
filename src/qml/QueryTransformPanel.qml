// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Editor surface for the bounded detail projection and one typed join.
Item {
    id: panel
    property var builder: null
    property string tooltip: qsTr("Join, project and sort typed query fields")
    readonly property var baseFields: builder && builder.schema ? builder.schema.fields || [] : []
    readonly property var rightFields: builder && builder.joinSchema ? builder.joinSchema.fields || [] : []
    function indexOf(fields, id) {
        for (let i = 0; i < fields.length; ++i) if (fields[i].id === id) return i
        return -1
    }
    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: content.implicitHeight + Tk.Theme.space.sm * 2
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.surface
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.divider
        radius: Tk.Theme.radius.xs
    }
    Tk.Flex {
        id: content
        x: Tk.Theme.space.sm
        y: Tk.Theme.space.sm
        width: panel.width - Tk.Theme.space.sm * 2
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Label { text: qsTr("Transforms"); accent: true }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.CheckBox {
                objectName: "queryDetailMode"
                text: qsTr("Detail rows")
                tooltip: qsTr("Show selected source fields without aggregation")
                checked: panel.builder ? panel.builder.detailMode : false
                onToggled: { panel.builder.detailMode = checked; panel.builder.publish() }
            }
            Tk.CheckBox {
                objectName: "queryJoinEnabled"
                text: qsTr("Join source")
                tooltip: qsTr("Join a second typed grid by equal keys")
                enabled: panel.rightFields.length > 0
                checked: panel.builder ? panel.builder.joinEnabled : false
                onToggled: { panel.builder.joinEnabled = checked; panel.builder.publish() }
            }
        }
        Tk.Flex {
            visible: panel.builder && panel.builder.joinEnabled
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.Label { text: qsTr("Match") }
            Tk.ComboBox {
                objectName: "queryJoinLeftField"
                model: panel.baseFields
                textRole: "label"
                currentIndex: panel.indexOf(panel.baseFields, panel.builder.joinLeftField)
                tooltip: qsTr("Left source key")
                onActivated: function(i) {
                    panel.builder.joinLeftField = panel.baseFields[i].id
                    panel.builder.publish()
                }
            }
            Tk.Label { text: qsTr("with") }
            Tk.ComboBox {
                objectName: "queryJoinRightField"
                model: panel.rightFields
                textRole: "label"
                currentIndex: panel.indexOf(panel.rightFields, panel.builder.joinRightField)
                tooltip: qsTr("Joined source key")
                onActivated: function(i) {
                    panel.builder.joinRightField = panel.rightFields[i].id
                    panel.builder.publish()
                }
            }
            Tk.CheckBox {
                objectName: "queryJoinLeftOuter"
                text: qsTr("Keep unmatched left rows")
                tooltip: qsTr("Left join; missing right fields are null")
                checked: panel.builder.joinLeftOuter
                onToggled: { panel.builder.joinLeftOuter = checked; panel.builder.publish() }
            }
        }
        Tk.Label {
            visible: panel.builder && panel.builder.detailMode
            text: qsTr("Projected fields")
            accent: true
        }
        Tk.Flex {
            visible: panel.builder && panel.builder.detailMode
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            Repeater {
                model: panel.builder ? panel.builder.fields : []
                delegate: Tk.CheckBox {
                    required property var modelData
                    objectName: "queryProject_" + modelData.id
                    text: modelData.label
                    tooltip: qsTr("Include this typed field in detail results")
                    checked: panel.builder.projection.indexOf(modelData.id) >= 0
                    onToggled: panel.builder.setProjected(modelData.id, checked)
                }
            }
        }
        Tk.Flex {
            visible: panel.builder && panel.builder.detailMode
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.Label { text: qsTr("Sort") }
            Tk.ComboBox {
                objectName: "querySortField"
                model: [qsTr("Source ID")].concat(panel.builder ? panel.builder.fields.map(function(f) { return f.label }) : [])
                currentIndex: panel.builder && panel.builder.sortField
                              ? panel.builder.fieldIndex(panel.builder.sortField) + 1 : 0
                tooltip: qsTr("Typed sort field; source ID breaks ties")
                onActivated: function(i) {
                    panel.builder.sortField = i === 0 ? "" : panel.builder.fields[i - 1].id
                    panel.builder.publish()
                }
            }
            Tk.CheckBox {
                objectName: "querySortAscending"
                text: qsTr("Ascending")
                tooltip: qsTr("Sort low values first")
                checked: panel.builder ? panel.builder.sortAscending : true
                onToggled: { panel.builder.sortAscending = checked; panel.builder.publish() }
            }
            Tk.CheckBox {
                objectName: "querySortNullsLast"
                text: qsTr("Nulls last")
                tooltip: qsTr("Place missing values after known values")
                checked: panel.builder ? panel.builder.sortNullsLast : true
                onToggled: { panel.builder.sortNullsLast = checked; panel.builder.publish() }
            }
        }
    }
}
