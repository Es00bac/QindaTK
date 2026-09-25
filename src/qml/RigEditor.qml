// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: consumes MeshRigDocument.bones/vertices snapshots and emits
// stable bone/vertex/constraint IDs; the host commits pose, weight,
// constraint, key and frame edits. Sliders edit `sourcePose` (the static pose
// or the keyed value at the frame), never the constrained `evaluatedPose`, so
// the editor cannot bake constraint output into the rig (docs/mesh-rig.md).
// Slider drags are bracketed by editGestureStarted/editGestureFinished; the
// host maps them to MeshRigDocument.beginEditGesture/endEditGesture so one
// drag is one undo step (a per-tick step would evict older history).
T.Control {
    id: control
    objectName: "rigEditor"
    property var bones: []
    property var vertices: []
    property string selectedBoneId: ""
    property string selectedVertexId: ""
    property string mode: "pose" // "pose", "weights" or "author" (rig authoring)
    property int currentFrame: 0
    property real constraintInfluence: 1.0
    property string tooltip: qsTr("Rig editor: orthographic skeleton projection and pose controls")
    readonly property var selectedBone: boneById(selectedBoneId)
    readonly property var selectedVertex: vertexById(selectedVertexId)
    readonly property var constraintTargets: (bones || []).filter(b => String(b.boneId) !== selectedBoneId)
    signal boneSelected(string boneId)
    signal vertexSelected(string vertexId)
    signal bonePoseRequested(string boneId, vector3d eulerDegrees)
    signal vertexWeightsRequested(string vertexId, var weights)
    signal resetPoseRequested()
    signal constraintAddRequested(string boneId, string type, string targetId, real influence)
    signal constraintRemoveRequested(string constraintId)
    signal poseKeyRequested(string boneId, int frame, vector3d eulerDegrees)
    signal poseKeyRemoveRequested(string boneId, int frame)
    signal frameRequested(int frame)
    signal editGestureStarted()
    signal editGestureFinished()
    // Authoring mode (RigAuthoringPanel): rig structure, never poses or keys.
    signal boneAddRequested(string name, string parentId, vector3d head, vector3d tail)
    signal boneRemoveRequested(string boneId)
    signal boneRenameRequested(string boneId, string name)
    signal boneParentRequested(string boneId, string parentId)
    signal boneRestRequested(string boneId, vector3d head, vector3d tail)
    signal boneLimitsRequested(string boneId, vector3d minimum, vector3d maximum)

    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.waveformHeight * 6
    padding: Tk.Theme.space.md
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: qsTr("2D orthographic bone projection; posed geometry can be displayed in SceneView3D.")

    function boneById(id) {
        for (const bone of (bones || [])) if (String(bone.boneId) === String(id)) return bone
        return null
    }
    function vertexById(id) {
        for (const vertex of (vertices || [])) if (String(vertex.vertexId) === String(id)) return vertex
        return null
    }
    function selectBone(id) {
        if (boneById(id)) boneSelected(String(id))
    }
    function editablePose() {
        if (!selectedBone) return Qt.vector3d(0, 0, 0)
        return selectedBone.sourcePose || selectedBone.pose || Qt.vector3d(0, 0, 0)
    }
    function axisValue(axis) {
        const pose = editablePose()
        return axis === 0 ? pose.x : (axis === 1 ? pose.y : pose.z)
    }
    function axisLimit(axis, side) {
        if (!selectedBone) return side === "min" ? -180 : 180
        const limits = selectedBone[side === "min" ? "minimum" : "maximum"] || Qt.vector3d(side === "min" ? -180 : 180, side === "min" ? -180 : 180, side === "min" ? -180 : 180)
        return axis === 0 ? limits.x : (axis === 1 ? limits.y : limits.z)
    }
    function setPoseAxis(axis, value) {
        if (!selectedBone) return
        const pose = editablePose()
        let next = Qt.vector3d(pose.x, pose.y, pose.z)
        if (axis === 0) next.x = value
        else if (axis === 1) next.y = value
        else next.z = value
        bonePoseRequested(selectedBoneId, next)
    }
    function hasKeyAtFrame() {
        for (const key of ((selectedBone && selectedBone.keys) || []))
            if (Number(key.frame) === currentFrame) return true
        return false
    }
    function requestKey() {
        if (!selectedBone) return false
        poseKeyRequested(selectedBoneId, currentFrame, editablePose())
        return true
    }
    function requestKeyRemoval() {
        if (!hasKeyAtFrame()) return false
        poseKeyRemoveRequested(selectedBoneId, currentFrame)
        return true
    }
    function requestCopyRotation(targetId) {
        if (!selectedBone || !targetId || String(targetId) === selectedBoneId) return false
        constraintAddRequested(selectedBoneId, "copyRotation", String(targetId), constraintInfluence)
        return true
    }
    function requestBoneWeight(value) {
        if (!selectedVertex || !selectedBoneId) return
        const current = selectedVertex.weights || ({})
        const otherIds = Object.keys(current).filter(id => id !== selectedBoneId && Number(current[id]) > 0)
        const otherSum = otherIds.reduce((total, id) => total + Number(current[id]), 0)
        const effective = otherIds.length === 0 && value > 0 ? 1 : value
        const remainder = Math.max(0, 1 - effective)
        const next = ({})
        for (const id of otherIds) {
            const share = otherSum > 0 ? Number(current[id]) / otherSum : 1 / Math.max(1, otherIds.length)
            if (remainder * share > 0.0001) next[id] = remainder * share
        }
        if (effective > 0.0001) next[selectedBoneId] = effective
        vertexWeightsRequested(selectedVertexId, next)
    }

    background: Rectangle {
        color: Tk.Theme.color.panel
        border.color: control.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.md
    }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            Tk.Overline { text: qsTr("Rig") }
            Tk.Segmented {
                objectName: "rigEditMode"
                small: true
                tooltip: qsTr("Pose bones, edit vertex weights, or author the rig itself")
                model: [{ text: qsTr("Pose"), value: "pose" }, { text: qsTr("Weights"), value: "weights" },
                        { text: qsTr("Rig"), value: "author" }]
                currentIndex: control.mode === "weights" ? 1 : control.mode === "author" ? 2 : 0
                onActivated: function(index) { control.mode = ["pose", "weights", "author"][index] }
            }
            Tk.Spacer { size: Tk.Theme.space.sm }
            Tk.NumberField {
                objectName: "rigFrame"
                small: true
                label: qsTr("Frame")
                from: 0; to: 100000; stepSize: 1
                value: control.currentFrame
                tooltip: qsTr("Animation frame the rig is evaluated at")
                onValueModified: function(value) { control.frameRequested(Math.round(value)) }
            }
            Tk.Button {
                text: qsTr("Reset pose")
                small: true
                tooltip: qsTr("Reset all static bone rotations; animation keys are kept")
                enabled: control.bones && control.bones.length > 0
                onClicked: control.resetPoseRequested()
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Flex.flex: 1
            Item {
                objectName: "rigProjection"
                Tk.Flex.flex: 1
                implicitWidth: Tk.Theme.size.panelMinWidth
                implicitHeight: Tk.Theme.size.waveformHeight * 4
                Rectangle {
                    anchors.fill: parent
                    color: Tk.Theme.color.canvas
                    border.color: Tk.Theme.color.border
                    border.width: Tk.Theme.size.border
                }
                Canvas {
                    id: skeleton
                    anchors.fill: parent
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        const list = control.bones || []
                        let minX = -1, maxX = 1, minY = -1, maxY = 1
                        for (const b of list) {
                            minX = Math.min(minX, b.head.x, b.tail.x)
                            maxX = Math.max(maxX, b.head.x, b.tail.x)
                            minY = Math.min(minY, b.head.y, b.tail.y)
                            maxY = Math.max(maxY, b.head.y, b.tail.y)
                        }
                        const sx = (width - Tk.Theme.space.xl * 2) / Math.max(0.001, maxX - minX)
                        const sy = (height - Tk.Theme.space.xl * 2) / Math.max(0.001, maxY - minY)
                        const scale = Math.min(sx, sy)
                        const px = x => Tk.Theme.space.xl + (x - minX) * scale
                        const py = y => height - Tk.Theme.space.xl - (y - minY) * scale
                        ctx.lineWidth = Tk.Theme.size.border * 2
                        for (const b of list) {
                            const chosen = String(b.boneId) === control.selectedBoneId
                            ctx.strokeStyle = chosen ? Tk.Theme.color.accent : Tk.Theme.color.textMuted
                            ctx.fillStyle = chosen ? Tk.Theme.color.accent : Tk.Theme.color.controlBorder
                            ctx.beginPath(); ctx.moveTo(px(b.head.x), py(b.head.y)); ctx.lineTo(px(b.tail.x), py(b.tail.y)); ctx.stroke()
                            ctx.beginPath(); ctx.arc(px(b.head.x), py(b.head.y), Tk.Theme.space.sm, 0, Math.PI * 2); ctx.fill()
                            ctx.beginPath(); ctx.arc(px(b.tail.x), py(b.tail.y), Tk.Theme.space.sm, 0, Math.PI * 2); ctx.fill()
                        }
                    }
                }
                Accessible.ignored: true
            }
            Item {
                implicitWidth: Tk.Theme.size.panelMinWidth
                Tk.Flex {
                    anchors.fill: parent
                    direction: Tk.Flex.Column
                    Tk.Overline { text: qsTr("Bones · %1").arg(control.bones ? control.bones.length : 0) }
                    Tk.Scroll {
                        Tk.Flex.flex: 1
                        overflowY: Tk.Scroll.Auto
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            Repeater {
                                model: control.bones || []
                                delegate: Tk.Button {
                                    required property var modelData
                                    text: modelData.animated ? qsTr("%1 (keyed)").arg(modelData.name || modelData.boneId)
                                                  : (modelData.name || modelData.boneId)
                                    tooltip: qsTr("Select bone %1").arg(modelData.name || modelData.boneId)
                                    variant: String(modelData.boneId) === control.selectedBoneId ? "accent" : "default"
                                    onClicked: control.selectBone(modelData.boneId)
                                    Accessible.role: Accessible.ListItem
                                    Accessible.selected: String(modelData.boneId) === control.selectedBoneId
                                    objectName: "rigBone_" + modelData.boneId
                                }
                            }
                        }
                    }
                    Tk.Overline { visible: control.mode === "weights"; text: qsTr("Vertices") }
                    Tk.Scroll {
                        visible: control.mode === "weights"
                        Tk.Flex.flex: 1
                        overflowY: Tk.Scroll.Auto
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            Repeater {
                                model: control.vertices || []
                                delegate: Tk.Button {
                                    required property var modelData
                                    text: qsTr("Vertex %1").arg(modelData.vertexId)
                                    tooltip: qsTr("Select weighted vertex %1").arg(modelData.vertexId)
                                    variant: String(modelData.vertexId) === control.selectedVertexId ? "accent" : "default"
                                    onClicked: control.vertexSelected(String(modelData.vertexId))
                                    Accessible.role: Accessible.ListItem
                                    Accessible.selected: String(modelData.vertexId) === control.selectedVertexId
                                    objectName: "rigVertex_" + modelData.vertexId
                                }
                            }
                        }
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            visible: control.mode === "pose" && Boolean(control.selectedBone)
            Repeater {
                model: ["X", "Y", "Z"]
                delegate: Tk.Flex {
                    required property int index
                    required property string modelData
                    Tk.Flex.flex: 1
                    direction: Tk.Flex.Row
                    Tk.Caption { text: modelData }
                    Tk.Slider {
                        objectName: "rigPoseAxis_" + modelData
                        Tk.Flex.flex: 1
                        onPressedChanged: pressed ? control.editGestureStarted() : control.editGestureFinished()
                        from: control.axisLimit(index, "min")
                        to: control.axisLimit(index, "max")
                        stepSize: 1
                        value: control.axisValue(index)
                        tooltip: qsTr("Bone rotation %1 degrees").arg(modelData)
                        onValueModified: function(value) { control.setPoseAxis(index, value) }
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            align: Tk.Flex.Center
            visible: control.mode === "pose" && Boolean(control.selectedBone)
            Tk.Button {
                objectName: "rigKey"
                text: control.hasKeyAtFrame() ? qsTr("Update key") : qsTr("Key")
                small: true
                tooltip: qsTr("Key this bone's pose at frame %1").arg(control.currentFrame)
                onClicked: control.requestKey()
            }
            Tk.Button {
                objectName: "rigRemoveKey"
                text: qsTr("Remove key")
                small: true
                enabled: control.hasKeyAtFrame()
                tooltip: qsTr("Remove this bone's key at frame %1").arg(control.currentFrame)
                onClicked: control.requestKeyRemoval()
            }
            Tk.ComboBox {
                id: targetPicker
                objectName: "rigConstraintTarget"
                small: true
                Tk.Flex.flex: 1
                model: control.constraintTargets
                textRole: "name"
                valueRole: "boneId"
                tooltip: qsTr("Bone whose rotation the selected bone copies")
            }
            Tk.Button {
                objectName: "rigAddConstraint"
                text: qsTr("Copy rotation")
                small: true
                enabled: control.constraintTargets.length > 0
                tooltip: qsTr("Add a copy-rotation constraint from the chosen bone; loops are refused")
                onClicked: control.requestCopyRotation(targetPicker.currentValue)
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            visible: control.mode === "pose" && Boolean(control.selectedBone)
                     && (control.selectedBone.constraints || []).length > 0
            Repeater {
                model: control.selectedBone ? (control.selectedBone.constraints || []) : []
                delegate: Tk.Button {
                    required property var modelData
                    readonly property var target: control.boneById(modelData.targetId)
                    objectName: "rigConstraint_" + modelData.constraintId
                    small: true
                    trailingIconName: "x"
                    text: qsTr("Copy %1 × %2").arg(target ? target.name : modelData.targetId)
                                                 .arg(Number(modelData.influence).toFixed(2))
                    tooltip: qsTr("Remove this copy-rotation constraint")
                    onClicked: control.constraintRemoveRequested(String(modelData.constraintId))
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            visible: control.mode === "weights" && Boolean(control.selectedVertex) && Boolean(control.selectedBone)
            Tk.Label { text: qsTr("%1 weight").arg(control.selectedBone ? control.selectedBone.name : ""); muted: true }
            Tk.Slider {
                objectName: "rigSelectedWeight"
                Tk.Flex.flex: 1
                from: 0; to: 1; stepSize: 0.01
                showValue: true; decimals: 2
                value: control.selectedVertex && control.selectedBoneId
                    ? Number((control.selectedVertex.weights || ({}))[control.selectedBoneId] || 0) : 0
                tooltip: qsTr("Selected bone influence for vertex %1").arg(control.selectedVertexId)
                onPressedChanged: pressed ? control.editGestureStarted() : control.editGestureFinished()
                onValueModified: function(value) { control.requestBoneWeight(value) }
            }
        }
        Tk.RigAuthoringPanel {
            visible: control.mode === "author"
            bones: control.bones
            selectedBoneId: control.selectedBoneId
            onBoneAddRequested: function(name, parentId, head, tail) { control.boneAddRequested(name, parentId, head, tail) }
            onBoneRemoveRequested: function(id) { control.boneRemoveRequested(id) }
            onBoneRenameRequested: function(id, name) { control.boneRenameRequested(id, name) }
            onBoneParentRequested: function(id, parentId) { control.boneParentRequested(id, parentId) }
            onBoneRestRequested: function(id, head, tail) { control.boneRestRequested(id, head, tail) }
            onBoneLimitsRequested: function(id, minimum, maximum) { control.boneLimitsRequested(id, minimum, maximum) }
        }
        Tk.Caption {
            objectName: "rigEvaluatedPose"
            mono: true
            visible: Boolean(control.selectedBone) && control.mode !== "author"
            text: control.selectedBone && control.selectedBone.evaluatedPose
                ? qsTr("Evaluated %1°, %2°, %3° at frame %4")
                    .arg(Number(control.selectedBone.evaluatedPose.x).toFixed(1))
                    .arg(Number(control.selectedBone.evaluatedPose.y).toFixed(1))
                    .arg(Number(control.selectedBone.evaluatedPose.z).toFixed(1))
                    .arg(control.currentFrame)
                : ""
        }
        Tk.Caption {
            text: qsTr("Side view of bone rest coordinates; scene mesh display is supplied by the host.")
            wrapMode: Text.Wrap
        }
    }
    onBonesChanged: skeleton.requestPaint()
    onSelectedBoneIdChanged: skeleton.requestPaint()
    Keys.onPressed: function(event) {
        if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right) && selectedBone && mode === "pose") {
            const axis = (event.modifiers & Qt.ShiftModifier) ? 1 : 2
            const pose = editablePose()
            setPoseAxis(axis, pose[axis === 1 ? "y" : "z"] + (event.key === Qt.Key_Left ? -1 : 1))
            event.accepted = true
        } else if (event.key === Qt.Key_K && selectedBone) {
            requestKey()
            event.accepted = true
        }
    }
}
