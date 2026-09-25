// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-056: the slide controls on the persistent Tk.SlideDeck (undoable,
// saveable), a deterministic Tk.SlideShow, the Tk.PresenterConsole and native
// audience/presenter windows through Tk.PresentationWindows (docs/slides.md).
Rectangle {
    id: root
    width: 1440
    height: 900
    color: Tk.Theme.color.bg
    property string currentSlideId: deck.slideIds.length ? deck.slideIds[0] : ""
    property var selectedObjectIds: []
    property string selectedEffectId: ""
    property string selectedMasterId: "master-default"
    property string mode: "author"
    readonly property var currentSlide: deck.revision.length ? deck.slide(currentSlideId) : ({})

    Tk.SlideDeck {
        id: deck
        Component.onCompleted: {
            const first = deck.slideIds[0]
            deck.setPlaceholderDefault("master-default", "title", "Signals in Motion")
            deck.setNotes(first, "Welcome everyone. Mention the field guide.")
            const next = deck.insertSlide(first, "")
            const card = deck.insertObject(next, "shape", { x: 620, y: 250, width: 230, height: 132 }, "Shared language")
            deck.addEffect(card, "fly-in")
            deck.setTransition(next, "fade", 0.4)
            deck.setNotes(next, "Pause on the shared language card.")
        }
        onChanged: if (deck.indexOfSlide(root.currentSlideId) < 0 && deck.slideIds.length) root.currentSlideId = deck.slideIds[0]
    }
    Tk.SlideShow { id: slideShow; deck: deck }
    Tk.PresentationWindows {
        id: presentation
        show: slideShow
        presenterDelegate: Component { Tk.PresenterConsole {} }
    }

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Heading { text: qsTr("Slide deck · persistent model"); level: 2; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.IconButton { objectName: "slideUndo"; iconName: "undo-2"; tooltip: qsTr("Undo"); enabled: deck.canUndo; onClicked: deck.undo() }
            Tk.IconButton { objectName: "slideRedo"; iconName: "redo-2"; tooltip: qsTr("Redo"); enabled: deck.canRedo; onClicked: deck.redo() }
            Tk.Button { objectName: "slideAuthorMode"; text: qsTr("Author"); variant: root.mode === "author" ? "accent" : "outline"; tooltip: qsTr("Show slide authoring tools"); onClicked: root.mode = "author" }
            Tk.Button {
                objectName: "slidePresenterMode"
                text: qsTr("Rehearse")
                variant: root.mode === "present" ? "accent" : "outline"
                tooltip: qsTr("Run the show in this window with the presenter console")
                onClicked: { root.mode = "present"; slideShow.liveClock = true; slideShow.start(root.currentSlideId) }
            }
            Tk.Button {
                objectName: "slideOpenWindows"
                text: presentation.active ? qsTr("End show") : qsTr("Present")
                iconName: "monitor-play"
                tooltip: qsTr("Open separate native audience and presenter windows")
                onClicked: presentation.active ? presentation.close() : presentation.open()
            }
        }
        Tk.Caption {
            objectName: "slideHostStatus"
            text: deck.lastError.length ? deck.lastError
                : qsTr("%1 slides · revision %2 · audience %3").arg(deck.slideIds.length).arg(deck.revision).arg(presentation.audienceStatus)
            wrapMode: Text.WordWrap
        }
        Tk.Flex {
            visible: root.mode === "author"
            direction: Tk.Flex.Row
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Panel {
                title: qsTr("Slides")
                implicitWidth: Tk.Theme.size.panelMinWidth * 1.4
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
                title: qsTr("Canvas")
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.SlideCanvas {
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    deck: deck
                    documentId: deck.documentId
                    slideId: root.currentSlideId
                    masterId: root.currentSlide.masterId || ""
                    slideSize: deck.slideSize
                    objects: deck.revision.length ? deck.objects(root.currentSlideId) : []
                    selectedObjectIds: root.selectedObjectIds
                    masterChoices: deck.revision.length ? deck.masterChoices() : []
                    onSelectionRequested: function(id) { root.selectedObjectIds = [id] }
                    onObjectMoveRequested: function(id, bounds) { deck.moveObject(id, bounds) }
                    onObjectTextEditRequested: function(id, text) { deck.setObjectText(id, text) }
                    onObjectInsertRequested: function(kind, bounds, requestId) {
                        const id = deck.insertObject(root.currentSlideId, kind, bounds, kind === "text" ? qsTr("New text") : "")
                        if (id.length) root.selectedObjectIds = [id]
                    }
                    onMasterApplyRequested: function(masterId, slideId) { deck.applyMaster(masterId, [slideId], "") }
                }
            }
            Tk.Panel {
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
                        Tk.AnimationPane {
                            documentId: deck.documentId
                            slideId: root.currentSlideId
                            effects: deck.revision.length ? deck.effects(root.currentSlideId) : []
                            selectedEffectId: root.selectedEffectId
                            targetChoices: deck.revision.length ? deck.targetChoices(root.currentSlideId) : []
                            errorText: deck.lastError
                            onEffectSelectionRequested: function(id) { root.selectedEffectId = id }
                            onEffectMoveRequested: function(id, index) { deck.moveEffect(id, index) }
                            onEffectAddRequested: function(targetId, kind, requestId) { root.selectedEffectId = deck.addEffect(targetId, kind) }
                            onEffectDeleteRequested: function(id) { deck.removeEffect(id) }
                            onEffectEditRequested: function(id, patch, requestId) { deck.editEffect(id, patch) }
                        }
                        Tk.TextArea {
                            objectName: "slideNotesEditor"
                            text: deck.revision.length ? deck.notes(root.currentSlideId) : ""
                            placeholderText: qsTr("Speaker notes (presenter only)")
                            tooltip: qsTr("Speaker notes for the current slide")
                            implicitHeight: Tk.Theme.size.control * 4
                            onEditingFinished: deck.setNotes(root.currentSlideId, text)
                        }
                    }
                }
            }
        }
        Tk.PresenterConsole {
            visible: root.mode === "present"
            show: slideShow
            windows: presentation
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
        }
    }
}
