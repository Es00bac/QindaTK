// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#conditionalformateditor): the rule list of
// Tk.ConditionalFormatEditor and Tk.DataValidationEditor. It shows `rules`
// (Workbook rule maps, in order) by id and reports choices; the editors call
// the Workbook. Rows are objectName "sheetRule_<id>"; `movable` adds
// priority buttons (conditional formats apply first-to-last).
Item {
    id: list

    property var rules: []
    property string currentId: ""
    property bool movable: false
    property string tooltip: qsTr("Rules")
    // Text for one rule map; the host words it (type, condition, ranges).
    property var summary: function(rule) { return rule.id }

    signal selected(string ruleId)
    signal moveRequested(string ruleId, int priority)
    signal removeRequested(string ruleId)

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.List
    Accessible.name: tooltip

    Tk.Flex {
        id: column
        width: list.width
        direction: Tk.Flex.Column
        gap: 0
        Tk.Caption {
            objectName: "sheetRuleListEmpty"
            visible: list.rules.length === 0
            text: qsTr("No rules yet")
            color: Tk.Theme.color.textMuted
        }
        Repeater {
            model: list.rules
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool current: modelData.id === list.currentId
                objectName: "sheetRule_" + modelData.id
                implicitHeight: Tk.Theme.size.row
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                color: current ? Tk.Theme.color.selection : hover.hovered ? Tk.Theme.color.hover : "transparent"
                Accessible.role: Accessible.ListItem
                Accessible.name: list.summary(modelData)
                Accessible.selected: current
                Accessible.onPressAction: list.selected(row.modelData.id)
                HoverHandler { id: hover }
                TapHandler { onTapped: list.selected(row.modelData.id) }
                Tk.Flex {
                    anchors.fill: parent
                    anchors.leftMargin: Tk.Theme.space.sm
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Tk.Label {
                        text: (list.movable ? (row.index + 1) + ". " : "") + list.summary(row.modelData)
                        elide: Text.ElideRight
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        Tk.Flex.minWidth: 0
                    }
                    Tk.IconButton {
                        objectName: "sheetRuleUp"
                        visible: list.movable
                        enabled: row.index > 0
                        small: true
                        iconName: "arrow-up"
                        tooltip: qsTr("Apply earlier")
                        onClicked: list.moveRequested(row.modelData.id, row.index - 1)
                    }
                    Tk.IconButton {
                        objectName: "sheetRuleDown"
                        visible: list.movable
                        enabled: row.index < list.rules.length - 1
                        small: true
                        iconName: "arrow-down"
                        tooltip: qsTr("Apply later")
                        onClicked: list.moveRequested(row.modelData.id, row.index + 1)
                    }
                    Tk.IconButton {
                        objectName: "sheetRuleRemove"
                        small: true
                        danger: true
                        iconName: "x"
                        tooltip: qsTr("Remove this rule")
                        onClicked: list.removeRequested(row.modelData.id)
                    }
                }
            }
        }
    }
}
