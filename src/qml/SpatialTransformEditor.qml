// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/canvas.md#spatial-transforms, WP-041): the numeric
// inspector of a Tk.SpatialTransform. Position and size call setBounds,
// rotation calls rotateTo and pivot writes `pivot`, so a typed value is the
// same one-CanvasEdit operation (one undo entry, never snapped) as a handle
// drag or keyboard step. Unedited components come from the controller's
// exact selectionBounds, never from the rounded field text. While a handle
// gesture runs the fields follow previewBounds and are read-only.
Item {
    id: editor
    objectName: "spatialTransformEditor"

    // Named `controller`: Item.transform is a FINAL QtQuick property.
    property Tk.SpatialTransform controller: null
    property string tooltip: ""
    property bool small: false
    property int decimals: 2
    readonly property bool available: controller !== null
        && (controller.selectionBounds.width > 0 || controller.selectionBounds.height > 0)
    readonly property bool editable: available && !controller.active

    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip || qsTr("Selection transform")
    Accessible.description: available ? "" : qsTr("No selection")
    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight

    // Values are assigned, not bound: a NumberField commit replaces its
    // value binding, and a refused edit must snap the text back.
    function sync() {
        const t = editor.controller
        const bounds = t ? t.previewBounds : Qt.rect(0, 0, 0, 0)
        xField.value = bounds.x
        yField.value = bounds.y
        widthField.value = bounds.width
        heightField.value = bounds.height
        rotationField.value = t ? t.rotation : 0
        pivotXField.value = t ? t.pivot.x : 0
        pivotYField.value = t ? t.pivot.y : 0
    }
    function setBoundsComponent(index, value) {
        if (!editor.editable) { editor.sync(); return false }
        const b = editor.controller.selectionBounds
        const next = [b.x, b.y, b.width, b.height]
        next[index] = value
        const ok = editor.controller.setBounds(next[0], next[1], next[2], next[3])
        editor.sync()
        return ok
    }
    function setRotation(value) {
        const ok = editor.editable && editor.controller.rotateTo(value)
        editor.sync()
        return ok
    }
    function setPivotComponent(index, value) {
        if (editor.editable) {
            const p = editor.controller.pivot
            editor.controller.pivot = index === 0 ? Qt.point(value, p.y) : Qt.point(p.x, value)
        }
        editor.sync()
    }

    Connections {
        target: editor.controller
        function onChanged() { editor.sync() }
        function onPreviewChanged() { editor.sync() }
    }
    onControllerChanged: sync()
    Component.onCompleted: sync()

    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.PropertyRow {
            label: qsTr("Position")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.NumberField {
                    id: xField
                    objectName: "spatialX"
                    Tk.Flex.grow: 1
                    label: "X"
                    tooltip: qsTr("Selection left edge")
                    decimals: editor.decimals
                    small: editor.small
                    editable: editor.editable
                    onValueModified: function(value) { editor.setBoundsComponent(0, value) }
                }
                Tk.NumberField {
                    id: yField
                    objectName: "spatialY"
                    Tk.Flex.grow: 1
                    label: "Y"
                    tooltip: qsTr("Selection top edge")
                    decimals: editor.decimals
                    small: editor.small
                    editable: editor.editable
                    onValueModified: function(value) { editor.setBoundsComponent(1, value) }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Size")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.NumberField {
                    id: widthField
                    objectName: "spatialWidth"
                    Tk.Flex.grow: 1
                    label: "W"
                    tooltip: qsTr("Selection width")
                    decimals: editor.decimals
                    small: editor.small
                    editable: editor.editable
                    onValueModified: function(value) { editor.setBoundsComponent(2, value) }
                }
                Tk.NumberField {
                    id: heightField
                    objectName: "spatialHeight"
                    Tk.Flex.grow: 1
                    label: "H"
                    tooltip: qsTr("Selection height")
                    decimals: editor.decimals
                    small: editor.small
                    editable: editor.editable
                    onValueModified: function(value) { editor.setBoundsComponent(3, value) }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Rotation")
            Tk.NumberField {
                id: rotationField
                objectName: "spatialRotation"
                tooltip: qsTr("Rotation of the first selected object")
                suffix: "°"
                from: -180
                to: 180
                decimals: editor.decimals
                small: editor.small
                editable: editor.editable
                onValueModified: function(value) { editor.setRotation(value) }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Pivot")
            Tk.Flex {
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.xs
                Tk.NumberField {
                    id: pivotXField
                    objectName: "spatialPivotX"
                    Tk.Flex.grow: 1
                    label: "X"
                    tooltip: qsTr("Rotation pivot X")
                    decimals: editor.decimals
                    small: editor.small
                    editable: editor.editable
                    onValueModified: function(value) { editor.setPivotComponent(0, value) }
                }
                Tk.NumberField {
                    id: pivotYField
                    objectName: "spatialPivotY"
                    Tk.Flex.grow: 1
                    label: "Y"
                    tooltip: qsTr("Rotation pivot Y")
                    decimals: editor.decimals
                    small: editor.small
                    editable: editor.editable
                    onValueModified: function(value) { editor.setPivotComponent(1, value) }
                }
            }
        }
    }
}
