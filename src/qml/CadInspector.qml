// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk
import "CadViewportCommands.js" as Commands

// AGENT-NOTE (docs/cad.md): CadViewport's property strip. It edits the
// selection through the viewport's document, so every commit is one undo
// step and a rejected edit (unsatisfiable solve) restores the field from
// the document. It keeps no state of its own.
Rectangle {
    id: inspector
    required property var viewport
    readonly property var document: viewport.document
    readonly property var ids: viewport.selection
    readonly property string kind: ids.length === 1 ? Commands.entityKind(document, ids[0]) : ""
    readonly property var entity: {
        if (!document || document.revision < 0 || !kind) return null
        const source = kind === "circle" ? document.circles : kind === "arc" ? document.arcs : []
        for (let i = 0; i < source.length; ++i)
            if (String(source[i].entityId) === ids[0]) return source[i]
        return null
    }
    readonly property var relations: document && document.revision >= 0
        ? Commands.constraintsFor(document, ids) : []
    readonly property var kindNames: ({
        horizontal: qsTr("Horizontal"), vertical: qsTr("Vertical"), distance: qsTr("Distance"),
        angle: qsTr("Angle"), coincident: qsTr("Coincident"), tangent: qsTr("Tangent")
    })
    readonly property var kindKeys: ({
        horizontal: "H", vertical: "V", distance: "D", angle: "A", coincident: "C", tangent: "T"
    })

    // AGENT-GUARD: NumberField.commit and its clamp/rounding assign `value`,
    // which drops the binding. Rebinding on every entity snapshot keeps the
    // fields on the document after commits, rejected edits, undo and units.
    onEntityChanged: rebind()
    function rebind() {
        radiusField.value = Qt.binding(function() { return inspector.entity ? inspector.entity.radius : 0 })
        startField.value = Qt.binding(function() {
            return inspector.kind === "arc" && inspector.entity ? inspector.entity.startDegrees : 0
        })
        sweepField.value = Qt.binding(function() {
            return inspector.kind === "arc" && inspector.entity ? inspector.entity.sweepDegrees : 0
        })
    }
    function setRadius(value) {
        const ok = kind === "circle" ? document.setCircleRadius(ids[0], value)
            : document.setArc(ids[0], value, entity.startDegrees, entity.sweepDegrees)
        rebind()
        return ok
    }
    function setArcAngles(start, sweep) {
        const ok = document.setArc(ids[0], entity.radius, start, sweep)
        rebind()
        return ok
    }
    function relationText(relation) {
        const name = kindNames[relation.kind] || relation.kind
        if (relation.kind === "distance")
            return qsTr("%1 %2 %3").arg(name).arg(Number(relation.value).toFixed(3))
                .arg(document.units)
        if (relation.kind === "angle") return qsTr("%1 %2°").arg(name).arg(Number(relation.value).toFixed(3))
        return name
    }

    objectName: "cadInspector"
    implicitWidth: content.implicitWidth
    implicitHeight: content.implicitHeight
    width: implicitWidth
    height: implicitHeight
    radius: Tk.Theme.radius.md
    color: Tk.Theme.color.panel
    border.color: Tk.Theme.color.border
    border.width: Tk.Theme.size.border
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Sketch properties")

    Tk.Flex {
        id: content
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        padding: Tk.Theme.space.sm

        Tk.Flex {
            objectName: "cadConstraintBar"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            visible: inspector.viewport.constraintOptions.length > 0
            Tk.Label { text: qsTr("Constrain"); muted: true }
            Repeater {
                model: inspector.viewport.constraintOptions
                delegate: Tk.Button {
                    required property string modelData
                    objectName: "cadConstrain_" + modelData
                    // The canvas keeps focus so its H/V/D/A/C/T keys stay live.
                    focusPolicy: Qt.NoFocus
                    small: true
                    text: inspector.kindNames[modelData]
                    tooltip: qsTr("Add a %1 constraint (%2)").arg(text.toLowerCase())
                        .arg(inspector.kindKeys[modelData])
                    onClicked: inspector.viewport.applyConstraint(modelData)
                }
            }
        }

        Tk.Flex {
            objectName: "cadGeometryFields"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            visible: inspector.entity !== null
            Tk.Label { text: qsTr("Radius"); muted: true }
            Tk.NumberField {
                id: radiusField
                objectName: "cadRadiusEditor"
                small: true
                stepButtons: false
                scrub: false
                decimals: 3
                from: 0.001
                to: 1000000
                suffix: inspector.document ? inspector.document.units : ""
                value: inspector.entity ? inspector.entity.radius : 0
                tooltip: qsTr("Radius")
                onValueModified: function(next) { inspector.setRadius(next) }
            }
            Tk.Label { visible: inspector.kind === "arc"; text: qsTr("Start"); muted: true }
            Tk.NumberField {
                id: startField
                objectName: "cadArcStartEditor"
                visible: inspector.kind === "arc"
                small: true
                stepButtons: false
                scrub: false
                decimals: 2
                from: -360
                to: 360
                suffix: "°"
                value: inspector.kind === "arc" && inspector.entity ? inspector.entity.startDegrees : 0
                tooltip: qsTr("Arc start angle")
                onValueModified: function(next) {
                    inspector.setArcAngles(next, inspector.entity.sweepDegrees)
                }
            }
            Tk.Label { visible: inspector.kind === "arc"; text: qsTr("Sweep"); muted: true }
            Tk.NumberField {
                id: sweepField
                objectName: "cadArcSweepEditor"
                visible: inspector.kind === "arc"
                small: true
                stepButtons: false
                scrub: false
                decimals: 2
                from: 0.01
                to: 359.99
                suffix: "°"
                value: inspector.kind === "arc" && inspector.entity ? inspector.entity.sweepDegrees : 0
                tooltip: qsTr("Arc sweep angle")
                onValueModified: function(next) {
                    inspector.setArcAngles(inspector.entity.startDegrees, next)
                }
            }
        }

        Repeater {
            model: inspector.relations
            delegate: Tk.Flex {
                id: relationRow
                required property var modelData
                direction: Tk.Flex.Row
                align: Tk.Flex.Center
                gap: Tk.Theme.space.xs
                Tk.Label {
                    objectName: "cadConstraintLabel_" + relationRow.modelData.constraintId
                    text: inspector.relationText(relationRow.modelData)
                }
                Tk.IconButton {
                    objectName: "cadRemoveConstraint_" + relationRow.modelData.constraintId
                    iconName: "x"
                    small: true
                    tooltip: qsTr("Remove the %1 constraint")
                        .arg(String(inspector.kindNames[relationRow.modelData.kind]).toLowerCase())
                    onClicked: inspector.document.removeConstraint(relationRow.modelData.constraintId)
                }
            }
        }
    }
}
