// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// `effects` is the host's ordered appearance graph snapshot. User operations
// are requests; the host validates capability and writes accepted state back.
// `paints` lists the object's fixed paint entries (fill, pattern, stroke) in
// the order they are painted, before any effect; they are inspected here
// and edited in the fill and stroke panels.
Item {
    id: stack
    property var effects: [] // [{id, name, kind, enabled, supported, parameters, masked}]
    property var paints: []  // [{id, kind, name, present, detail}]
    property string tooltip: qsTr("Inspect ordered vector appearance effects")

    signal reorderRequested(string effectId, int newIndex)
    signal enabledChangeRequested(string effectId, bool enabled)
    signal editRequested(string effectId)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function rowIndex(effectId) {
        for (let i = 0; i < effects.length; ++i) if (effects[i].id === effectId) return i
        return -1
    }
    function move(effectId, delta) {
        if (!enabled) return
        const index = rowIndex(effectId)
        const target = index + delta
        if (index < 0 || target < 0 || target >= effects.length) return
        reorderRequested(effectId, target)
    }

    Tk.Flex {
        id: column
        width: stack.width > 0 ? stack.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.xs

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("APPEARANCE") ; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { text: qsTr("Top renders first") }
        }
        Repeater {
            model: stack.paints
            delegate: Tk.Flex {
                id: paintRow
                required property var modelData
                required property int index
                objectName: "appearancePaint_" + modelData.id
                width: column.width
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.sm
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("%1: %2").arg(modelData.name).arg(modelData.detail)
                Tk.Caption {
                    text: String(paintRow.index + 1)
                    color: Tk.Theme.color.textMuted
                }
                Tk.Label {
                    text: paintRow.modelData.name
                    color: paintRow.modelData.present ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
                    Tk.Flex.grow: 1
                    Tk.Flex.basis: 0
                }
                Tk.Caption {
                    objectName: "appearancePaintDetail_" + paintRow.modelData.id
                    text: paintRow.modelData.detail
                    color: Tk.Theme.color.textMuted
                    elide: Text.ElideRight
                }
            }
        }
        Tk.Caption {
            visible: stack.paints.length > 0
            text: qsTr("Effects then apply to the painted result:")
            color: Tk.Theme.color.textMuted
        }
        Repeater {
            model: stack.effects
            delegate: Tk.Box {
                id: row
                required property var modelData
                required property int index
                objectName: "appearanceEffect_" + modelData.id
                width: column.width
                padding: Tk.Theme.space.xs
                color: modelData.supported === false ? Tk.Theme.color.panelAlt : Tk.Theme.color.surface
                borderColor: Tk.Theme.color.divider

                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Tk.CheckBox {
                        objectName: "appearanceEnabled_" + row.modelData.id
                        checked: row.modelData.enabled !== false
                        tooltip: qsTr("Enable %1").arg(row.modelData.name)
                        Accessible.name: tooltip
                        onToggled: function(checked) { stack.enabledChangeRequested(row.modelData.id, checked) }
                    }
                    // AGENT-GUARD: grow on the ROW's axis belongs to this
                    // column; on the label it collapsed the name to 0 px tall.
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        align: Tk.Flex.Start
                        gap: 0
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                        Tk.Label {
                            objectName: "appearanceName_" + row.modelData.id
                            text: row.modelData.name
                            elide: Text.ElideRight
                        }
                        Tk.Caption {
                            text: row.modelData.supported === false
                                  ? (row.modelData.unsupportedReason || qsTr("Unsupported effect; retained in document"))
                                  : (row.modelData.kind || qsTr("Effect"))
                            color: row.modelData.supported === false ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
                        }
                    }
                    Tk.IconButton {
                        objectName: "appearanceMoveUp_" + row.modelData.id
                        iconName: "arrow-up"
                        small: true
                        tooltip: qsTr("Move %1 up").arg(row.modelData.name)
                        enabled: row.index > 0
                        onClicked: stack.move(row.modelData.id, -1)
                    }
                    Tk.IconButton {
                        objectName: "appearanceMoveDown_" + row.modelData.id
                        iconName: "arrow-down"
                        small: true
                        tooltip: qsTr("Move %1 down").arg(row.modelData.name)
                        enabled: row.index + 1 < stack.effects.length
                        onClicked: stack.move(row.modelData.id, 1)
                    }
                    Tk.IconButton {
                        objectName: "appearanceEdit_" + row.modelData.id
                        iconName: "sliders-horizontal"
                        small: true
                        tooltip: qsTr("Edit %1 parameters").arg(row.modelData.name)
                        enabled: row.modelData.supported !== false
                        onClicked: stack.editRequested(row.modelData.id)
                    }
                }
            }
        }
        Tk.Caption {
            visible: stack.effects.length === 0
            text: qsTr("No appearance effects.")
        }
    }
}
