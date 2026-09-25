// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (VID-18 "authoring and posing modes are explicit"):
// RigEditor's authoring mode. It edits the rig itself (bones, hierarchy, rest
// head/tail, rotation limits) by stable bone ID and never touches poses or
// keys; the host commits each request through MeshRigDocument addBone,
// removeBone, renameBone, setBoneParent, setBoneRest and setBoneLimits, which
// refuse cycles, dangling references and limits that exclude stored poses.
Tk.Flex {
    id: panel
    objectName: "rigAuthoringPanel"
    property var bones: []
    property string selectedBoneId: ""
    property string tooltip: qsTr("Rig authoring: bone hierarchy, rest position and rotation limits")
    readonly property var selectedBone: boneById(selectedBoneId)
    readonly property var parentCandidates: [{ name: qsTr("No parent"), boneId: "" }].concat(
        (bones || []).filter(b => String(b.boneId) !== selectedBoneId && !isDescendant(String(b.boneId), selectedBoneId)))
    signal boneAddRequested(string name, string parentId, vector3d head, vector3d tail)
    signal boneRemoveRequested(string boneId)
    signal boneRenameRequested(string boneId, string name)
    signal boneParentRequested(string boneId, string parentId)
    signal boneRestRequested(string boneId, vector3d head, vector3d tail)
    signal boneLimitsRequested(string boneId, vector3d minimum, vector3d maximum)

    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function boneById(id) {
        for (const bone of (bones || [])) if (String(bone.boneId) === String(id)) return bone
        return null
    }
    // True when `id` descends from `ancestorId` through parentId links.
    function isDescendant(id, ancestorId) {
        let bone = boneById(id)
        for (let guard = 0; bone && guard < 256; ++guard) {
            if (String(bone.parentId) === String(ancestorId) && String(ancestorId).length) return true
            bone = boneById(bone.parentId)
        }
        return false
    }
    function axisOf(vector, axis) { return axis === 0 ? vector.x : (axis === 1 ? vector.y : vector.z) }
    function withAxis(vector, axis, value) {
        return Qt.vector3d(axis === 0 ? value : vector.x, axis === 1 ? value : vector.y, axis === 2 ? value : vector.z)
    }
    // A child continues the selected bone by its own length; a root starts at the origin.
    function requestChild() {
        const name = qsTr("Bone %1").arg((bones || []).length + 1)
        if (!selectedBone) {
            boneAddRequested(name, "", Qt.vector3d(0, 0, 0), Qt.vector3d(0, 1, 0))
            return true
        }
        const h = selectedBone.head, t = selectedBone.tail
        boneAddRequested(name, selectedBoneId, t, Qt.vector3d(2 * t.x - h.x, 2 * t.y - h.y, 2 * t.z - h.z))
        return true
    }
    function requestRest(end, axis, value) {
        if (!selectedBone) return false
        const head = end === "head" ? withAxis(selectedBone.head, axis, value) : selectedBone.head
        const tail = end === "tail" ? withAxis(selectedBone.tail, axis, value) : selectedBone.tail
        boneRestRequested(selectedBoneId, head, tail)
        return true
    }
    function requestLimit(side, axis, value) {
        if (!selectedBone) return false
        const minimum = side === "min" ? withAxis(selectedBone.minimum, axis, value) : selectedBone.minimum
        const maximum = side === "max" ? withAxis(selectedBone.maximum, axis, value) : selectedBone.maximum
        boneLimitsRequested(selectedBoneId, minimum, maximum)
        return true
    }
    function requestParent(parentId) {
        if (!selectedBone || String(parentId) === String(selectedBone.parentId)) return false
        boneParentRequested(selectedBoneId, String(parentId))
        return true
    }

    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.sm
        align: Tk.Flex.Center
        Tk.TextField {
            objectName: "rigBoneName"
            small: true
            Tk.Flex.flex: 1
            enabled: Boolean(panel.selectedBone)
            text: panel.selectedBone ? panel.selectedBone.name : ""
            tooltip: qsTr("Bone name")
            onEditingFinished: if (panel.selectedBone && text.length && text !== panel.selectedBone.name)
                                   panel.boneRenameRequested(panel.selectedBoneId, text)
        }
        Tk.ComboBox {
            objectName: "rigBoneParent"
            small: true
            Tk.Flex.flex: 1
            enabled: Boolean(panel.selectedBone)
            model: panel.parentCandidates
            textRole: "name"
            valueRole: "boneId"
            currentIndex: {
                const parentId = panel.selectedBone ? String(panel.selectedBone.parentId || "") : ""
                for (let i = 0; i < panel.parentCandidates.length; ++i)
                    if (String(panel.parentCandidates[i].boneId) === parentId) return i
                return 0
            }
            tooltip: qsTr("Parent bone; descendants are not offered, so the hierarchy cannot loop")
            onActivated: function(index) { panel.requestParent(panel.parentCandidates[index].boneId) }
        }
        Tk.Button {
            objectName: "rigAddBone"
            text: panel.selectedBone ? qsTr("Add child") : qsTr("Add bone")
            small: true
            tooltip: qsTr("Add a bone continuing the selected bone, or a root bone")
            onClicked: panel.requestChild()
        }
        Tk.Button {
            objectName: "rigRemoveBone"
            text: qsTr("Remove")
            small: true
            variant: "danger"
            enabled: Boolean(panel.selectedBone)
            tooltip: qsTr("Remove the bone; refused while children, weights, constraints or keys use it")
            onClicked: panel.boneRemoveRequested(panel.selectedBoneId)
        }
    }
    Repeater {
        model: [{ label: qsTr("Head"), key: "head" }, { label: qsTr("Tail"), key: "tail" },
                { label: qsTr("Min °"), key: "min" }, { label: qsTr("Max °"), key: "max" }]
        delegate: Tk.Flex {
            id: row
            required property var modelData
            readonly property bool limit: modelData.key === "min" || modelData.key === "max"
            readonly property var source: !panel.selectedBone ? Qt.vector3d(0, 0, 0)
                : modelData.key === "head" ? panel.selectedBone.head : modelData.key === "tail" ? panel.selectedBone.tail
                : modelData.key === "min" ? panel.selectedBone.minimum : panel.selectedBone.maximum
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            visible: Boolean(panel.selectedBone)
            Tk.Caption { text: row.modelData.label; Tk.Flex.basis: Tk.Theme.size.fieldWidth / 3; Tk.Flex.shrink: 0 }
            Repeater {
                model: ["X", "Y", "Z"]
                delegate: Tk.NumberField {
                    required property int index
                    required property string modelData
                    objectName: "rigAuthor_" + row.modelData.key + modelData
                    small: true
                    Tk.Flex.flex: 1
                    label: modelData
                    from: row.limit ? -180 : -100000
                    to: row.limit ? 180 : 100000
                    decimals: row.limit ? 0 : 2
                    stepSize: row.limit ? 1 : 0.1
                    value: panel.axisOf(row.source || Qt.vector3d(0, 0, 0), index)
                    tooltip: row.limit ? qsTr("%1 rotation limit %2 in degrees").arg(row.modelData.key === "min" ? qsTr("Minimum") : qsTr("Maximum")).arg(modelData)
                                       : qsTr("Bone %1 %2 in rest space").arg(row.modelData.key).arg(modelData)
                    onValueModified: function(value) {
                        if (row.limit) panel.requestLimit(row.modelData.key, index, value)
                        else panel.requestRest(row.modelData.key, index, value)
                    }
                }
            }
        }
    }
}
