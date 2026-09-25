// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-056 authoring workspace on the persistent Tk.SlideDeck: sorter,
// canvas (WP-042 canvas stack, rich text), master editor, animation pane
// with preview, and a rehearsal presenter console. Colours and text below
// are demo deck data.
Rectangle {
    id: root
    width: 1520
    height: 960
    color: Tk.Theme.color.bg
    property string currentSlideId: deck.slideIds.length ? deck.slideIds[0] : ""
    property string selectedEffectId: ""
    property string selectedMasterId: "master-default"
    property string viewMode: "author"
    readonly property var currentSlide: deck.revision.length ? deck.slide(currentSlideId) : ({})

    function roleObject(slideId, role) {
        const objects = deck.objects(slideId)
        for (let i = 0; i < objects.length; ++i) if (objects[i].role === role) return objects[i].id
        return ""
    }

    Tk.SlideDeck {
        id: deck
        Component.onCompleted: {
            const first = deck.slideIds[0]
            deck.setMasterBackground("master-default", "#10202c", "#eef6fa")
            deck.setPlaceholderDefault("master-default", "title", "Signals in Motion")
            deck.setObjectText(root.roleObject(first, "subtitle"), "A design systems field guide")
            const accent = deck.insertObject(first, "shape", { x: 80, y: 392, width: 196, height: 14 }, "")
            deck.setObjectFill(accent, "#16c7d8")
            deck.setNotes(first, "Open with the field guide story.")
            const map = deck.insertSlide(first, "")
            deck.setObjectText(root.roleObject(map, "title"), "One system, many surfaces")
            deck.setObjectText(root.roleObject(map, "body"), "Tokens connect every panel, page and timeline.")
            const card = deck.insertObject(map, "shape", { x: 620, y: 300, width: 230, height: 132 }, "Shared language")
            deck.setObjectFill(card, "#2d3342")
            deck.transformObjects([{ id: card, bounds: { x: 620, y: 300, width: 230, height: 132 }, rotation: -6 }])
            deck.addEffect(card, "fly-in")
            deck.setTransition(map, "fade", 0.4)
            const paper = deck.createMaster("Paper")
            deck.setMasterBackground(paper, "#f6f1e7", "#2a2620")
            const craft = deck.insertSlide(map, "")
            deck.applyMaster(paper, [craft], "")
            deck.setObjectText(root.roleObject(craft, "title"), "Make details legible")
            deck.insertSlide(craft, "")
            root.selectedMasterId = "master-default"
        }
        onChanged: if (deck.indexOfSlide(root.currentSlideId) < 0 && deck.slideIds.length) root.currentSlideId = deck.slideIds[0]
    }
    Tk.SlideShow { id: rehearsal; deck: deck; liveClock: root.viewMode === "present" }
    Tk.SlidePreview { id: preview; deck: deck }

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Heading { text: qsTr("Tideglass · Presentation workshop"); level: 2; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.IconButton { objectName: "slideUndo"; iconName: "undo-2"; tooltip: qsTr("Undo"); enabled: deck.canUndo; onClicked: deck.undo() }
            Tk.IconButton { objectName: "slideRedo"; iconName: "redo-2"; tooltip: qsTr("Redo"); enabled: deck.canRedo; onClicked: deck.redo() }
            Tk.Button { objectName: "slideAuthorMode"; text: qsTr("Author"); iconName: "panels-top-left"; variant: root.viewMode === "author" ? "accent" : "outline"; tooltip: qsTr("Show slide authoring tools"); onClicked: root.viewMode = "author" }
            Tk.Button {
                objectName: "slidePresenterMode"
                text: qsTr("Rehearse")
                iconName: "presentation"
                variant: root.viewMode === "present" ? "accent" : "outline"
                tooltip: qsTr("Rehearse with the presenter console")
                onClicked: { root.viewMode = "present"; rehearsal.start(root.currentSlideId) }
            }
        }
        Tk.Caption {
            objectName: "slideHostStatus"
            text: deck.lastError.length ? deck.lastError
                : qsTr("%1 slides · revision %2 · every edit is one undo step").arg(deck.slideIds.length).arg(deck.revision)
            wrapMode: Text.WordWrap
        }
        Tk.Flex {
            visible: root.viewMode === "author"
            direction: Tk.Flex.Row
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Panel {
                objectName: "slideSorterPanel"
                title: qsTr("Slides · %1").arg(deck.slideIds.length)
                implicitWidth: Tk.Theme.size.panelMinWidth * 1.55
                Tk.SlideSorter {
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    documentId: deck.documentId
                    slides: deck.slides
                    currentSlideId: root.currentSlideId
                    selectedSlideIds: [root.currentSlideId]
                    onSelectionRequested: function(id, additive) { root.currentSlideId = id }
                    onReorderRequested: function(id, index) { deck.moveSlide(id, index) }
                    onDuplicateRequested: function(id) { root.currentSlideId = deck.duplicateSlide(id) }
                    onDeleteRequested: function(id) { deck.removeSlide(id) }
                    onHiddenChangeRequested: function(id, hidden) { deck.setSlideHidden(id, hidden) }
                    onInsertRequested: function(afterId, requestId) { root.currentSlideId = deck.insertSlide(afterId, "") }
                }
            }
            Tk.Panel {
                objectName: "slideCanvasPanel"
                title: qsTr("%1 · 16:9").arg(root.currentSlide.title || qsTr("Untitled slide"))
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.SlideCanvas {
                    objectName: "slideAuthoringCanvas"
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    deck: deck
                    slideId: root.currentSlideId
                }
            }
            Tk.Panel {
                objectName: "slideInspectorPanel"
                title: qsTr("Master and motion")
                implicitWidth: Tk.Theme.size.panelMinWidth * 2
                Tk.Scroll {
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    overflowY: Tk.Scroll.Auto
                    Tk.Flex {
                        direction: Tk.Flex.Column
                        gap: Tk.Theme.space.md
                        Tk.SlideMasterEditor {
                            objectName: "slideMasterEditor"
                            documentId: deck.documentId
                            masters: deck.masters
                            masterId: root.selectedMasterId
                            layoutId: root.currentSlide.layoutId || ""
                            selectedSlideId: root.currentSlideId
                            selectedSlideIds: [root.currentSlideId]
                            placeholderOverrides: deck.revision.length ? deck.placeholderOverrides(root.selectedMasterId) : []
                            onMasterSelectionRequested: function(id) { root.selectedMasterId = id }
                            onPlaceholderEditRequested: function(masterId, layoutId, role, value, requestId) { deck.setPlaceholderDefault(masterId, role, value) }
                            onApplyMasterRequested: function(masterId, ids, requestId) { deck.applyMaster(masterId, ids, "") }
                            onOverrideResetRequested: function(slideId, role) { deck.resetOverride(slideId, role) }
                        }
                        Tk.Divider {}
                        Tk.AnimationPane {
                            objectName: "slideAnimationPane"
                            documentId: deck.documentId
                            slideId: root.currentSlideId
                            effects: deck.revision.length ? deck.effects(root.currentSlideId) : []
                            selectedEffectId: root.selectedEffectId
                            targetChoices: deck.revision.length ? deck.targetChoices(root.currentSlideId) : []
                            errorText: deck.lastError
                            previewState: preview.state
                            onEffectSelectionRequested: function(id) { root.selectedEffectId = id }
                            onEffectMoveRequested: function(id, index) { deck.moveEffect(id, index) }
                            onEffectAddRequested: function(targetId, kind, requestId) { root.selectedEffectId = deck.addEffect(targetId, kind) }
                            onEffectDeleteRequested: function(id) { deck.removeEffect(id) }
                            onEffectEditRequested: function(id, patch, requestId) { deck.editEffect(id, patch) }
                            onPreviewRequested: function(id) { preview.start(id) }
                            onPreviewCancelRequested: function(id) { preview.cancel() }
                        }
                        Tk.SlideFrameView {
                            objectName: "slideEffectPreview"
                            deck: deck
                            slideId: preview.state === "idle" ? root.currentSlideId : preview.slideId
                            step: preview.step
                            ticks: preview.ticks
                            implicitHeight: Tk.Theme.size.control * 7
                            Accessible.role: Accessible.Graphic
                            Accessible.name: qsTr("Animation preview")
                        }
                    }
                }
            }
        }
        Tk.PresenterConsole {
            objectName: "slideRehearsalConsole"
            visible: root.viewMode === "present"
            show: rehearsal
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }
    }
}
