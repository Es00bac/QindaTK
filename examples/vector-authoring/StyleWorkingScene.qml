// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Style panels bound to the reference CanvasDocument. CanvasStyleModel
// publishes the selection's stroke and fill in the panels' descriptor shapes
// and applies only the fields a request changes, so untouched mixed values
// survive; CanvasAppearanceModel edits the key object's ordered effect stack;
// the path buttons combine the selection into one path. Every request is one
// undoable document edit, and the panels read the result back.
Rectangle {
    id: scene
    width: 1120
    height: 640
    color: Tk.Theme.color.bg
    property var selectedIds: []
    property string editingEffect: ""

    function combine(operation) {
        const id = document.combinePaths(scene.selectedIds, operation)
        if (id) scene.selectedIds = [String(id)]
    }
    function effectAmount(effects, effectId) {
        for (let i = 0; i < effects.length; ++i) {
            if (effects[i].id !== effectId) continue
            const parameters = effects[i].parameters
            return effects[i].kind === "gaussian-blur" ? parameters.radius : parameters.blur
        }
        return 0
    }
    function setEffectAmount(effectId, amount) {
        const effect = appearance.effect(effectId)
        if (!effect.parameters) return
        const parameters = Object.assign({}, effect.parameters)
        parameters[effect.kind === "gaussian-blur" ? "radius" : "blur"] = amount
        appearance.setEffectParameters(effectId, parameters)
    }

    Tk.CanvasDocument {
        id: document
        objectName: "styleWorkingDocument"
        pageRect: Qt.rect(0, 0, 420, 300)
        Component.onCompleted: {
            const a = addRect(52, 52, 110, 90, "#468fb8", "#e6a55a", 4)
            const b = addEllipse(120, 96, 100, 100, "#468fb8", "#e6a55a", 4)
            scene.selectedIds = [String(a), String(b)]
        }
    }
    Tk.CanvasStyleModel {
        id: styles
        objectName: "styleWorkingStyles"
        document: document
        selectedIds: scene.selectedIds
    }
    Tk.CanvasAppearanceModel {
        id: appearance
        objectName: "styleWorkingAppearance"
        document: document
        objectId: scene.selectedIds.length > 0 ? scene.selectedIds[0] : ""
    }
    Tk.ToolController { id: tools; document: document }
    Tk.CanvasViewState { id: pose }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.md

        Tk.CanvasView {
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
                    Tk.StrokeStyleEditor {
                        objectName: "styleWorkingStroke"
                        width: parent.width
                        style: styles.strokeStyle
                        capabilities: ({ units: ["document"] })
                        onStyleChangeRequested: function(next) { styles.applyStrokeStyle(next) }
                    }
                    Tk.FillStyleEditor {
                        objectName: "styleWorkingFill"
                        width: parent.width
                        fill: styles.fillStyle
                        patterns: styles.patterns
                        capabilities: ({ gradient: false })
                        onFillChangeRequested: function(next) { styles.applyFillStyle(next) }
                    }
                    Tk.PropertyRow {
                        label: qsTr("Opacity")
                        Tk.NumberField {
                            objectName: "styleWorkingOpacity"
                            value: styles.selectionStyle.opacity === undefined ? 1 : styles.selectionStyle.opacity
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
                        onReorderRequested: function(effectId, index) { appearance.moveEffect(effectId, index) }
                        onEnabledChangeRequested: function(effectId, on) { appearance.setEffectEnabled(effectId, on) }
                        onEditRequested: function(effectId) { scene.editingEffect = effectId }
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Row
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
                    }
                    Tk.PropertyRow {
                        visible: scene.editingEffect.length > 0
                        label: qsTr("Effect blur")
                        Tk.NumberField {
                            objectName: "styleWorkingEffectAmount"
                            value: scene.effectAmount(appearance.effects, scene.editingEffect)
                            from: 0
                            to: 250
                            stepSize: 0.5
                            decimals: 1
                            tooltip: qsTr("Blur of the effect chosen for editing")
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
