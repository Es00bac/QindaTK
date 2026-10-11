// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// D-338: model-independent pivot assignment. Select a source field then a
// destination with the keyboard, or drag the field onto a well. The host
// handles assignment/removal and supplies a fresh wells snapshot.
T.Control {
    id: control
    property var fields: [] // [{id,label}]
    property var wells: [ {id:"filters",label:qsTr("Filters"),fieldIds:[]},
        {id:"columns",label:qsTr("Columns"),fieldIds:[]}, {id:"rows",label:qsTr("Rows"),fieldIds:[]},
        {id:"values",label:qsTr("Values"),fieldIds:[]} ]
    property string selectedFieldId: ""
    property string tooltip: qsTr("Field list and assignment areas")
    property string draggingFieldId: ""
    property point dragPoint: Qt.point(0, 0)
    signal assignmentRequested(string fieldId, string wellId, int index)
    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: contentItem.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    function labelFor(id) {
        const field = fields.find(entry => String(entry.id) === String(id))
        return field ? String(field.label || field.id) : String(id)
    }
    function assign(wellId) {
        if (!selectedFieldId) return false
        assignmentRequested(selectedFieldId, wellId, -1)
        return true
    }
    function finishDrag() {
        if (!draggingFieldId) return
        for (let i = 0; i < destinations.count; ++i) {
            const well = destinations.itemAt(i)
            const at = well.mapFromItem(control, dragPoint.x, dragPoint.y)
            if (at.x >= 0 && at.y >= 0 && at.x <= well.width && at.y <= well.height) {
                assignmentRequested(draggingFieldId, String(control.wells[i].id), -1)
                break
            }
        }
        draggingFieldId = ""
    }
    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Repeater {
                model: control.fields
                delegate: Tk.Button {
                    id: fieldButton
                    required property var modelData
                    objectName: "fieldWellSource_" + String(modelData.id)
                    text: String(modelData.label || modelData.id)
                    iconName: "grip-vertical"
                    checkable: true
                    checked: control.selectedFieldId === String(modelData.id)
                    tooltip: qsTr("Select or drag %1 to an area").arg(text)
                    onClicked: control.selectedFieldId = String(modelData.id)
                    DragHandler {
                        target: null
                        onActiveChanged: {
                            if (active) control.draggingFieldId = String(fieldButton.modelData.id)
                            else control.finishDrag()
                        }
                        onCentroidChanged: {
                            const point = control.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
                            control.dragPoint = Qt.point(point.x, point.y)
                        }
                    }
                }
            }
        }
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.sm
            Repeater {
                id: destinations
                model: control.wells
                delegate: Tk.Box {
                    id: wellBox
                    required property var modelData
                    objectName: "fieldWell_" + String(modelData.id)
                    implicitWidth: Math.min(control.width, Tk.Theme.size.panelMinWidth)
                    padding: Tk.Theme.space.sm
                    color: Tk.Theme.color.panel
                    borderColor: Tk.Theme.color.border
                    tooltip: String(modelData.label || modelData.id)
                    Tk.Flex {
                        width: parent.width
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.xs
                        Tk.Button {
                            objectName: "fieldWellAssign_" + String(wellBox.modelData.id)
                            width: parent.width
                            text: String(wellBox.modelData.label || wellBox.modelData.id)
                            tooltip: qsTr("Add selected field to %1").arg(text)
                            enabled: !!control.selectedFieldId
                            onClicked: control.assign(String(wellBox.modelData.id))
                        }
                        Repeater {
                            model: wellBox.modelData.fieldIds || []
                            delegate: Tk.Button {
                                required property var modelData
                                objectName: "fieldWellAssigned_" + String(wellBox.modelData.id) + "_" + String(modelData)
                                text: control.labelFor(modelData)
                                trailingIconName: "x"
                                tooltip: qsTr("Remove %1 from this area").arg(text)
                                onClicked: control.assignmentRequested(String(modelData), String(wellBox.modelData.id), -2)
                            }
                        }
                    }
                }
            }
        }
    }
    Tk.Label {
        objectName: "fieldWellDragPreview"
        visible: !!control.draggingFieldId
        x: control.dragPoint.x + Tk.Theme.space.sm
        y: control.dragPoint.y + Tk.Theme.space.sm
        text: control.labelFor(control.draggingFieldId)
        z: 2
    }
}
