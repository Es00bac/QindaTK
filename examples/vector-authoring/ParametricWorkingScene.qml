// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Live ShapePalette fixture. The palette requests a definition ID, the scene
// creates it with CanvasDocument.addParametricShape, and the fields edit the
// selected shape's parameters; each edit regenerates the path as one undo
// step. Dragging a PathEditor node makes the shape freeform (undo restores it).
Rectangle {
    id: scene
    width: 1100
    height: 640
    color: Tk.Theme.color.bg
    property string selectedId: ""
    property string selectedKind: ""
    property int createdCount: 0
    property string statusText: ""

    function refreshParameters() {
        const params = selectedId ? document.parametricParameters(selectedId) : ({})
        const info = selectedId ? document.objectInfo(selectedId) : ({})
        selectedKind = params.kind || (info.kind === "path" ? "freeform" : "")
        pointsField.value = params.points || params.sides || 5
        innerField.value = params.innerRadius || 0.45
        radiusField.value = params.radius === undefined ? 18 : params.radius
    }
    function insertShape(kind, params) {
        const slot = createdCount % 6
        const x = 30 + (slot % 3) * 165
        const y = 50 + Math.floor(slot / 3) * 175
        const color = kind === "star" ? "#c52d61" : kind === "polygon" ? "#2877c6" : "#3a9b69"
        const id = document.addParametricShape(kind, x, y, 120, 120,
                                                params, color, "#f5c869", 3)
        if (!id) {
            statusText = qsTr("Could not create %1").arg(kind)
            return
        }
        selectedId = String(id)
        createdCount++
        refreshParameters()
        statusText = qsTr("Created %1").arg(kind)
    }
    function updateShape(patch) {
        // Rejected parameters make no edit; the fields return to the model.
        if (!selectedId || !document.setParametricParameters(selectedId, patch)) {
            statusText = qsTr("Invalid shape parameters")
            refreshParameters()
            return
        }
        refreshParameters()
        statusText = qsTr("Updated %1").arg(selectedKind)
    }

    Tk.CanvasDocument {
        id: document
        objectName: "parametricWorkingDocument"
        pageRect: Qt.rect(0, 0, 540, 400)
        Component.onCompleted: {
            scene.insertShape("star", {points: 5, innerRadius: 0.45})
            const first = scene.selectedId
            scene.insertShape("polygon", {sides: 6})
            scene.insertShape("rounded-rectangle", {radius: 22})
            scene.selectedId = first
            scene.refreshParameters()
            scene.statusText = "" // setup is not a user action
        }
    }
    // Undo, redo and PathEditor node drags change the selection's definition.
    Connections {
        target: document
        function onChanged() { scene.refreshParameters() }
    }
    Tk.ToolController { id: tools; document: document }
    Tk.CanvasViewState { id: pose }

    // Row children stretch to the full height; the inspector keeps a fixed
    // 290px basis and the canvas takes the rest (docs/layout.md).
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Row
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.md
        Tk.CanvasView {
            id: canvas
            objectName: "parametricWorkingCanvas"
            viewId: "parametric-working"
            document: document
            toolController: tools
            viewState: pose
            fitMode: "page"
            accessibleName: qsTr("Editable polygon, star and rounded rectangle")
            Tk.Flex.flex: 1
            Tk.PathEditor {
                objectName: "parametricWorkingEditor"
                anchors.fill: parent
                document: document
                surface: canvas
                objectId: scene.selectedId
            }
        }
        Tk.Scroll {
            objectName: "parametricWorkingPanel"
            Tk.Flex.basis: 290
            Tk.Flex.shrink: 0
            overflowY: Tk.Scroll.Auto
            accessibleName: qsTr("Parametric vector controls")
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.md
                Tk.Label { text: qsTr("PARAMETRIC SHAPES") }
                Tk.ShapePalette {
                    objectName: "parametricWorkingPalette"
                    columns: 2
                    definitions: [
                        {id: "star", label: qsTr("Star"), category: qsTr("Symbols"), iconName: "star",
                         parameters: {points: 5, innerRadius: 0.45}},
                        {id: "polygon", label: qsTr("Polygon"), category: qsTr("Symbols"), iconName: "hexagon",
                         parameters: {sides: 6}},
                        {id: "rounded-rectangle", label: qsTr("Rounded"), category: qsTr("Basic"),
                         iconName: "square", parameters: {radius: 22}}
                    ]
                    onShapeRequested: function(kind, parameters) { scene.insertShape(kind, parameters) }
                }
                Tk.Label {
                    objectName: "parametricWorkingSelection"
                    text: qsTr("SELECTED: %1").arg(scene.selectedKind || qsTr("None"))
                }
                Tk.Caption { text: qsTr("Dragging a node makes the shape freeform.") }
                Tk.NumberField {
                    id: pointsField
                    objectName: "parametricPoints"
                    label: scene.selectedKind === "polygon" ? qsTr("Sides") : qsTr("Points")
                    tooltip: qsTr("Polygon sides or star tips")
                    from: 3; to: 64; stepSize: 1
                    visible: scene.selectedKind === "polygon" || scene.selectedKind === "star"
                    onValueModified: function(next) {
                        scene.updateShape(scene.selectedKind === "polygon" ? {sides: next} : {points: next})
                    }
                }
                Tk.NumberField {
                    id: innerField
                    objectName: "parametricInnerRadius"
                    label: qsTr("Inner")
                    tooltip: qsTr("Star inner radius as a fraction of its outer radius")
                    from: 0.05; to: 0.95; stepSize: 0.05; decimals: 2
                    visible: scene.selectedKind === "star"
                    onValueModified: function(next) { scene.updateShape({innerRadius: next}) }
                }
                Tk.NumberField {
                    id: radiusField
                    objectName: "parametricCornerRadius"
                    label: qsTr("Radius")
                    tooltip: qsTr("Rounded rectangle corner radius in document units")
                    from: 0; to: 60; stepSize: 1
                    visible: scene.selectedKind === "rounded-rectangle"
                    onValueModified: function(next) { scene.updateShape({radius: next}) }
                }
                Tk.Button {
                    objectName: "parametricWorkingUndo"
                    text: qsTr("Undo shape edit")
                    tooltip: qsTr("Undo the last shape creation, parameter or node change")
                    enabled: document.canUndo
                    onClicked: document.undo()
                }
                Tk.Caption {
                    objectName: "parametricWorkingStatus"
                    text: scene.statusText
                    visible: text.length > 0
                }
            }
        }
    }
}
