// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Style panels bound to the reference CanvasDocument. The canvas selection
// (a CanvasSelectionModel shared with the ToolController, so clicking shapes
// selects them) drives CanvasStyleModel, which publishes the selection's
// stroke and fill in the panels' descriptor shapes and applies exactly the
// fields a request names, so untouched mixed values survive;
// CanvasAppearanceModel shows the key object's paint order and edits its
// ordered effect stack; the gradient editor supplies the gradient fill; the
// path buttons combine the selection into one path. Every request is one
// undoable document edit, and the panels read the result back.
Rectangle {
    id: scene
    width: 1120
    height: 640
    color: Tk.Theme.color.bg
    readonly property var selectedIds: styles.selectedIds
    property string editingEffect: ""
    property bool gradientOpen: false
    readonly property bool hasSelection: (styles.selectionStyle.count || 0) > 0

    function combine(operation) {
        const id = document.combinePaths(scene.selectedIds, operation)
        if (id) selection.selectedIds = [id]
    }
    function effectAmount(effects, effectId) {
        for (let i = 0; i < effects.length; ++i) {
            if (effects[i].id !== effectId) continue
            return effects[i].parameters[scene.amountKey(effects[i].kind)]
        }
        return 0
    }
    function setEffectAmount(effectId, amount) {
        const effect = appearance.effect(effectId)
        if (!effect.parameters) return
        const parameters = {}
        parameters[scene.amountKey(effect.kind)] = amount
        appearance.setEffectParameters(effectId, parameters)
    }
    // The one parameter the inspector edits for each effect kind.
    function amountKey(kind) {
        return kind === "gaussian-blur" ? "radius" : kind === "color-adjust" ? "saturation" : "blur"
    }
    function applyGradient() {
        if (!styles.applyGradient(scene.selectedIds, gradients)) return
        scene.gradientOpen = false
    }

    Tk.CanvasDocument {
        id: document
        objectName: "styleWorkingDocument"
        pageRect: Qt.rect(0, 0, 420, 300)
        Component.onCompleted: {
            const a = addRect(52, 52, 110, 90, "#468fb8", "#e6a55a", 4)
            const b = addEllipse(120, 96, 100, 100, "#468fb8", "#e6a55a", 6)
            selection.selectedIds = [a, b]
        }
    }
    Tk.CanvasSelectionModel {
        id: selection
        objectName: "styleWorkingSelectionModel"
        document: document
    }
    Tk.GradientEditorModel { id: gradients; objectName: "styleWorkingGradients" }
    Tk.CanvasStyleModel {
        id: styles
        objectName: "styleWorkingStyles"
        document: document
        selectionModel: selection
        gradientEditor: gradients
    }
    Tk.CanvasAppearanceModel {
        id: appearance
        objectName: "styleWorkingAppearance"
        document: document
        selectionModel: selection
    }
    Tk.ToolController { id: tools; objectName: "styleWorkingTools"; document: document; selectionModel: selection }
    Tk.CanvasViewState { id: pose }
    // Owner-to-field syncing that survives the fields' own user edits (an
    // edit assigns a field's value, which would end a plain binding).
    Binding {
        target: opacityField
        property: "value"
        value: styles.selectionStyle.opacity === undefined ? 1 : styles.selectionStyle.opacity
    }
    Binding {
        target: effectAmountField
        property: "value"
        value: scene.effectAmount(appearance.effects, scene.editingEffect)
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.md

        Tk.CanvasView {
            id: canvas
            objectName: "styleWorkingCanvas"
            viewId: "style-working"
            document: document
            toolController: tools
            viewState: pose
            fitMode: "page"
            accessibleName: qsTr("Vector shapes with editable style")
            Tk.Flex.flex: 1
            height: parent.height
        }
        Item {
            implicitWidth: 390
            height: parent.height
            Tk.Scroll {
                anchors.fill: parent
                overflowY: Tk.Scroll.Auto
                accessibleName: qsTr("Style inspector")
                Tk.Flex {
                    width: parent.width
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    Tk.Caption {
                        objectName: "styleWorkingSelection"
                        width: parent.width
                        text: scene.hasSelection
                              ? qsTr("%n shape(s) selected", "", styles.selectionStyle.count)
                              : qsTr("No shape selected. Select shapes to edit their style.")
                        wrapMode: Text.WordWrap
                    }
                    Tk.StrokeStyleEditor {
                        objectName: "styleWorkingStroke"
                        width: parent.width
                        enabled: scene.hasSelection
                        style: styles.strokeStyle
                        capabilities: ({ units: styles.strokeUnits })
                        onStyleChangeRequested: function(next) { styles.applyStrokeStyle(next) }
                    }
                    Tk.FillStyleEditor {
                        objectName: "styleWorkingFill"
                        width: parent.width
                        enabled: scene.hasSelection
                        fill: styles.fillStyle
                        patterns: styles.patterns
                        onFillChangeRequested: function(next) { styles.applyFillStyle(next) }
                        onGradientEditRequested: scene.gradientOpen = true
                    }
                    Tk.GradientEditor {
                        objectName: "styleWorkingGradient"
                        width: parent.width
                        visible: scene.gradientOpen || styles.fillStyle.kind === "gradient"
                        enabled: scene.hasSelection
                        stops: gradients.stops
                        previewSamples: gradients.previewSamples
                        selectedStopId: gradients.selectedStopId
                        tooltip: qsTr("Gradient fill stops of the selected objects")
                        onSelectionRequested: function(id) { gradients.selectStop(String(id)) }
                        onAddStopRequested: function(position) {
                            if (gradients.addStop(position)) scene.applyGradient()
                        }
                        onMovePreviewRequested: function(id, position) {
                            if (!gradients.editing) gradients.beginGesture("Move gradient stop")
                            gradients.previewMove(String(id), position)
                        }
                        onMoveFinished: function() { if (gradients.commitGesture()) scene.applyGradient() }
                        onMoveCanceled: function() { gradients.cancelGesture() }
                        onDeleteStopRequested: function(id) {
                            if (gradients.deleteStop(String(id))) scene.applyGradient()
                        }
                    }
                    Tk.Button {
                        objectName: "styleWorkingApplyGradient"
                        visible: scene.gradientOpen
                        text: qsTr("Apply gradient")
                        tooltip: qsTr("Fill the selected shapes with this gradient")
                        enabled: scene.hasSelection
                        onClicked: scene.applyGradient()
                    }
                    Tk.PropertyRow {
                        label: qsTr("Opacity")
                        enabled: scene.hasSelection
                        Tk.NumberField {
                            id: opacityField
                            objectName: "styleWorkingOpacity"
                            from: 0
                            to: 1
                            stepSize: 0.05
                            decimals: 2
                            tooltip: qsTr("Opacity of the selected objects")
                            onValueModified: function(value) { styles.apply(scene.selectedIds, { opacity: value }) }
                        }
                    }
                    Tk.PropertyRow {
                        label: qsTr("Blend")
                        enabled: scene.hasSelection
                        Tk.ComboBox {
                            objectName: "styleWorkingBlend"
                            model: styles.blendModes
                            currentIndex: styles.blendModes.indexOf(styles.selectionStyle.blend || "normal")
                            tooltip: qsTr("How the selected objects mix with what is below them")
                            onActivated: function(index) {
                                styles.apply(scene.selectedIds, { blend: styles.blendModes[index] })
                            }
                        }
                    }
                    Tk.AppearanceStack {
                        objectName: "styleWorkingAppearanceStack"
                        width: parent.width
                        enabled: appearance.editable
                        effects: appearance.effects
                        paints: appearance.paints
                        onReorderRequested: function(effectId, index) { appearance.moveEffect(effectId, index) }
                        onEnabledChangeRequested: function(effectId, on) { appearance.setEffectEnabled(effectId, on) }
                        onEditRequested: function(effectId) { scene.editingEffect = effectId }
                    }
                    Tk.Flex {
                        width: parent.width
                        direction: Tk.Flex.Row
                        wrap: Tk.Flex.Wrap
                        gap: Tk.Theme.space.sm
                        Tk.Button {
                            objectName: "styleWorkingAddShadow"
                            text: qsTr("Add shadow")
                            tooltip: qsTr("Add a drop shadow to the key object")
                            enabled: appearance.editable
                            onClicked: appearance.addEffect("drop-shadow",
                                                            { offsetX: 6, offsetY: 6, blur: 4, color: "#66000000" })
                        }
                        Tk.Button {
                            objectName: "styleWorkingAddBlur"
                            text: qsTr("Add blur")
                            tooltip: qsTr("Add a Gaussian blur to the key object")
                            enabled: appearance.editable
                            onClicked: appearance.addEffect("gaussian-blur", { radius: 2 })
                        }
                        Tk.Button {
                            objectName: "styleWorkingAddAdjustment"
                            text: qsTr("Add adjustment")
                            tooltip: qsTr("Add a nondestructive colour adjustment (desaturated) to the key object")
                            enabled: appearance.editable
                            onClicked: appearance.addEffect("color-adjust", { saturation: 0 })
                        }
                    }
                    Tk.PropertyRow {
                        visible: scene.editingEffect.length > 0
                        label: qsTr("Effect amount")
                        Tk.NumberField {
                            id: effectAmountField
                            objectName: "styleWorkingEffectAmount"
                            from: 0
                            to: 250
                            stepSize: 0.5
                            decimals: 1
                            tooltip: qsTr("Blur, radius or saturation of the effect chosen for editing")
                            onValueModified: function(value) { scene.setEffectAmount(scene.editingEffect, value) }
                        }
                    }
                    Tk.Overline { title: qsTr("PATH OPERATIONS") }
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        wrap: Tk.Flex.Wrap
                        gap: Tk.Theme.space.sm
                        Repeater {
                            model: [
                                { id: "union", name: "Unite", label: qsTr("Unite"), tip: qsTr("Merge the selection into one shape") },
                                { id: "intersect", name: "Intersect", label: qsTr("Intersect"), tip: qsTr("Keep only the overlap") },
                                { id: "subtract", name: "Subtract", label: qsTr("Subtract"), tip: qsTr("Cut the front shapes out of the back one") },
                                { id: "exclude", name: "Exclude", label: qsTr("Exclude"), tip: qsTr("Keep what does not overlap") },
                                { id: "combine", name: "Combine", label: qsTr("Combine"), tip: qsTr("Join outlines into one compound path") }
                            ]
                            delegate: Tk.Button {
                                required property var modelData
                                objectName: "styleWorking" + modelData.name
                                text: modelData.label
                                tooltip: modelData.tip
                                enabled: scene.selectedIds.length > 1
                                onClicked: scene.combine(modelData.id)
                            }
                        }
                    }
                    Tk.Button {
                        objectName: "styleWorkingUndo"
                        text: qsTr("Undo")
                        tooltip: qsTr("Undo the last style, effect or path change")
                        enabled: document.canUndo
                        onClicked: document.undo()
                    }
                    Tk.Caption {
                        objectName: "styleWorkingError"
                        text: styles.lastError || appearance.lastError || document.lastError
                        visible: text.length > 0
                        color: Tk.Theme.color.warning
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
