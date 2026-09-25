// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The host evaluates effects and commits every accepted field edit.
// Preview: previewRequested/previewCancelRequested drive a host player
// (Tk.SlidePreview over a Tk.SlideDeck, docs/slides.md#preview) whose state
// the host binds back to previewState ("idle", "running", "finished",
// "canceled", "failed").
Item {
    id: control
    property string documentId: ""
    property string slideId: ""
    property var effects: [] // [{id,targetId,label,trigger,delay,duration,easing,status}]
    property string selectedEffectId: ""
    property var targetChoices: [] // [{id,label}]
    property var effectKinds: [{ id: "appear", label: qsTr("Appear") }, { id: "fade-in", label: qsTr("Fade in") },
                               { id: "fly-in", label: qsTr("Fly in") }, { id: "zoom-in", label: qsTr("Zoom in") },
                               { id: "fade-out", label: qsTr("Fade out") }]
    property var capabilities: ({ canEdit: true, canPreview: true })
    property string previewState: "idle"
    property string errorText: ""
    property string tooltip: qsTr("Order and edit slide animations")
    readonly property var selectedEffect: findEffect(selectedEffectId)

    signal effectSelectionRequested(string effectId)
    signal effectMoveRequested(string effectId, int targetIndex)
    signal effectAddRequested(string targetId, string kind, string requestId)
    signal effectDeleteRequested(string effectId)
    signal effectEditRequested(string effectId, var patch, string requestId)
    signal previewRequested(string effectId)
    signal previewCancelRequested(string effectId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 1.5
    implicitHeight: animationContent.implicitHeight
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: qsTr("%1 effects on slide %2").arg(effects.length).arg(slideId)

    function findEffect(id) {
        for (let i = 0; i < effects.length; ++i) if (effects[i].id === id) return effects[i]
        return null
    }
    function effectIndex(id) {
        for (let i = 0; i < effects.length; ++i) if (effects[i].id === id) return i
        return -1
    }
    function kindIndex(kind) {
        for (let i = 0; i < effectKinds.length; ++i) if (effectKinds[i].id === kind) return i
        return -1
    }
    function editField(effect, field, value) {
        if (!capabilities.canEdit || !effect) return
        const patch = {}
        patch[field] = value
        effectEditRequested(effect.id, patch, documentId + ":effect:" + Date.now())
    }

    Tk.Flex {
        id: animationContent
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.ComboBox {
                id: targetPicker
                objectName: "animationTargetPicker"
                model: control.targetChoices
                textRole: "label"
                tooltip: qsTr("Choose the target object for a new effect")
                enabled: control.capabilities.canEdit && control.targetChoices.length > 0
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.ComboBox {
                id: kindPicker
                objectName: "animationKindPicker"
                model: control.effectKinds
                textRole: "label"
                currentIndex: Math.max(0, control.kindIndex("fade-in"))
                tooltip: qsTr("Effect for the new animation")
                enabled: control.capabilities.canEdit && control.effectKinds.length > 0
            }
            Tk.Button {
                objectName: "animationAddEffect"
                text: qsTr("Add")
                iconName: "plus"
                tooltip: qsTr("Add the chosen effect to the selected target")
                enabled: targetPicker.currentIndex >= 0 && kindPicker.currentIndex >= 0 && control.capabilities.canEdit
                onClicked: control.effectAddRequested(control.targetChoices[targetPicker.currentIndex].id,
                    control.effectKinds[kindPicker.currentIndex].id, control.documentId + ":effect:new:" + Date.now())
            }
        }
        Tk.Caption {
            objectName: "animationError"
            visible: control.errorText.length > 0
            text: control.errorText
            color: Tk.Theme.color.warning
            wrapMode: Text.WordWrap
        }
        Tk.Scroll {
            objectName: "animationEffectList"
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Repeater {
                    model: control.effects
                    delegate: Tk.Box {
                        required property var modelData
                        required property int index
                        objectName: "animationEffect_" + modelData.id
                        color: control.selectedEffectId === modelData.id ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
                        borderColor: control.selectedEffectId === modelData.id ? Tk.Theme.color.accent : Tk.Theme.color.border
                        radius: Tk.Theme.radius.sm
                        padding: Tk.Theme.space.xs
                        implicitHeight: Tk.Theme.size.control * 3 + Tk.Theme.space.sm * 3
                        Accessible.role: Accessible.ListItem
                        Accessible.name: qsTr("%1 on %2, starts at %3 seconds, duration %4 seconds").arg(modelData.label || modelData.kind)
                            .arg(modelData.targetLabel || modelData.targetId).arg(Number(modelData.delay).toFixed(2)).arg(Number(modelData.duration).toFixed(2))
                        Accessible.selected: control.selectedEffectId === modelData.id
                        Tk.Flex {
                            anchors.fill: parent
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                gap: Tk.Theme.space.xs
                                Tk.Button {
                                    objectName: "animationSelect_" + modelData.id
                                    text: modelData.label || modelData.kind || qsTr("Effect")
                                    tooltip: qsTr("Select this effect")
                                    variant: control.selectedEffectId === modelData.id ? "accent" : "ghost"
                                    onClicked: control.effectSelectionRequested(modelData.id)
                                    Tk.Flex.grow: 1
                                    Tk.Flex.basis: 0
                                }
                                Tk.Caption { text: modelData.targetLabel || modelData.targetId; elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                                Tk.IconButton {
                                    objectName: "animationEarlier_" + modelData.id
                                    iconName: "arrow-up"
                                    tooltip: qsTr("Move effect earlier")
                                    enabled: control.capabilities.canEdit && index > 0
                                    onClicked: control.effectMoveRequested(modelData.id, index - 1)
                                }
                                Tk.IconButton {
                                    objectName: "animationLater_" + modelData.id
                                    iconName: "arrow-down"
                                    tooltip: qsTr("Move effect later")
                                    enabled: control.capabilities.canEdit && index < control.effects.length - 1
                                    onClicked: control.effectMoveRequested(modelData.id, index + 1)
                                }
                                Tk.IconButton {
                                    objectName: "animationDelete_" + modelData.id
                                    iconName: "trash-2"
                                    tooltip: qsTr("Delete effect")
                                    enabled: control.capabilities.canEdit
                                    onClicked: control.effectDeleteRequested(modelData.id)
                                }
                            }
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                gap: Tk.Theme.space.xs
                                Tk.NumberField {
                                    objectName: "animationDelay_" + modelData.id
                                    value: Number(modelData.delay || 0)
                                    label: qsTr("Start")
                                    from: 0
                                    to: 600
                                    stepSize: 0.1
                                    decimals: 2
                                    suffix: qsTr("s")
                                    tooltip: qsTr("Animation delay in seconds")
                                    enabled: control.capabilities.canEdit
                                    onValueModified: function(value) { control.editField(modelData, "delay", value) }
                                    Tk.Flex.grow: 1
                                    Tk.Flex.basis: 0
                                }
                                Tk.NumberField {
                                    objectName: "animationDuration_" + modelData.id
                                    value: Number(modelData.duration || 0.5)
                                    label: qsTr("Duration")
                                    from: 0.01
                                    to: 600
                                    stepSize: 0.1
                                    decimals: 2
                                    suffix: qsTr("s")
                                    tooltip: qsTr("Animation duration in seconds")
                                    enabled: control.capabilities.canEdit
                                    onValueModified: function(value) { control.editField(modelData, "duration", value) }
                                    Tk.Flex.grow: 1
                                    Tk.Flex.basis: 0
                                }
                            }
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                gap: Tk.Theme.space.xs
                                Tk.ComboBox {
                                    objectName: "animationKind_" + modelData.id
                                    model: control.effectKinds
                                    textRole: "label"
                                    currentIndex: control.kindIndex(modelData.kind)
                                    tooltip: qsTr("Effect")
                                    enabled: control.capabilities.canEdit
                                    onActivated: function(index) { control.editField(modelData, "kind", control.effectKinds[index].id) }
                                    Tk.Flex.grow: 1
                                    Tk.Flex.basis: 0
                                }
                                Tk.ComboBox {
                                    objectName: "animationTrigger_" + modelData.id
                                    model: [qsTr("With previous"), qsTr("After previous"), qsTr("On click")]
                                    currentIndex: modelData.trigger === "after" ? 1 : modelData.trigger === "click" ? 2 : 0
                                    tooltip: qsTr("Trigger for this effect")
                                    enabled: control.capabilities.canEdit
                                    onActivated: function(index) { control.editField(modelData, "trigger", ["with", "after", "click"][index]) }
                                    Tk.Flex.grow: 1
                                    Tk.Flex.basis: 0
                                }
                                Tk.ComboBox {
                                    objectName: "animationEasing_" + modelData.id
                                    model: [qsTr("Linear"), qsTr("Ease in"), qsTr("Ease out"), qsTr("Ease in/out")]
                                    currentIndex: ["linear", "in-cubic", "out-cubic", "in-out-cubic"].indexOf(modelData.easing || "linear")
                                    tooltip: qsTr("Easing used by the host animation evaluator")
                                    enabled: control.capabilities.canEdit
                                    onActivated: function(index) { control.editField(modelData, "easing", ["linear", "in-cubic", "out-cubic", "in-out-cubic"][index]) }
                                    Tk.Flex.grow: 1
                                    Tk.Flex.basis: 0
                                }
                            }
                        }
                    }
                }
                Tk.Caption { visible: control.effects.length === 0; text: qsTr("No effects yet. Add an effect to begin the sequence.") }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "animationPreview"
                text: control.previewState === "running" ? qsTr("Stop preview") : qsTr("Preview selected")
                iconName: control.previewState === "running" ? "square" : "play"
                tooltip: control.previewState === "running" ? qsTr("Stop the preview; the build shows complete")
                                                            : qsTr("Preview the build step of the selected animation")
                enabled: !!control.selectedEffect && control.capabilities.canPreview
                onClicked: control.previewState === "running" ? control.previewCancelRequested(control.selectedEffectId)
                                                                : control.previewRequested(control.selectedEffectId)
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Badge { visible: control.previewState.length > 0 && control.previewState !== "idle"; text: control.previewState; variant: control.previewState === "failed" ? "danger" : "info"; tooltip: qsTr("Animation preview state") }
        }
    }
}
