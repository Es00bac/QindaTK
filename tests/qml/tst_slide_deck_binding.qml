// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-056: the slide controls driven by the persistent Tk.SlideDeck and the
// Tk.PresenterConsole bound to a Tk.SlideShow (docs/slides.md).
TestCase {
    name: "SlideDeckBinding"
    when: windowShown
    visible: true
    width: 900
    height: 620

    Component { id: deckComponent; Tk.SlideDeck {} }
    Component { id: showComponent; Tk.SlideShow { liveClock: false } }
    Component { id: sorterComponent; Tk.SlideSorter { width: 320; height: 560 } }
    Component { id: consoleComponent; Tk.PresenterConsole { width: 880; height: 580 } }
    Component { id: previewComponent; Tk.SlidePreview { liveClock: false } }
    Component {
        id: masterEditorComponent
        Tk.SlideMasterEditor {
            id: editor
            property Tk.SlideDeck deck: null
            width: 420
            masterId: "master-default"
            masters: deck ? deck.masters : []
            placeholderOverrides: deck && deck.revision.length ? deck.placeholderOverrides("master-default") : []
            onPlaceholderEditRequested: function(masterId, layoutId, role, value, requestId) { deck.setPlaceholderDefault(masterId, role, value) }
            onOverrideResetRequested: function(slideId, role) { deck.resetOverride(slideId, role) }
        }
    }
    Component {
        id: animationComponent
        Tk.AnimationPane {
            property Tk.SlideDeck deck: null
            property Tk.SlidePreview preview: null
            width: 520
            height: 420
            effects: deck && deck.revision.length ? deck.effects(slideId) : []
            targetChoices: deck && deck.revision.length ? deck.targetChoices(slideId) : []
            previewState: preview ? preview.state : "idle"
            onEffectSelectionRequested: function(id) { selectedEffectId = id }
            onEffectAddRequested: function(targetId, kind, requestId) { selectedEffectId = deck.addEffect(targetId, kind) }
            onEffectEditRequested: function(id, patch, requestId) { deck.editEffect(id, patch) }
            onPreviewRequested: function(id) { preview.start(id) }
            onPreviewCancelRequested: function(id) { preview.cancel() }
        }
    }

    function objectWithRole(deck, slideId, role) {
        const objects = deck.objects(slideId)
        for (let i = 0; i < objects.length; ++i) if (objects[i].role === role) return objects[i]
        return null
    }

    function test_master_editor_updates_inheriting_slides() {
        const deck = createTemporaryObject(deckComponent, this)
        const first = deck.slideIds[0]
        const second = deck.insertSlide(first, "")
        verify(deck.setObjectText(objectWithRole(deck, second, "title").id, "Agenda"))
        const editor = createTemporaryObject(masterEditorComponent, this, { deck: deck, selectedSlideId: first })
        const field = findChild(editor, "slidePlaceholderValue_title")
        verify(field !== null)
        compare(field.placeholderText, "Click to add title")
        field.text = "Quarterly review"
        field.textEdited()
        findChild(editor, "slidePlaceholderApply_title").click()
        compare(objectWithRole(deck, first, "title").text, "Quarterly review")
        compare(objectWithRole(deck, second, "title").text, "Agenda") // the deliberate override stays
        editor.selectedSlideId = second
        const reset = findChild(editor, "slidePlaceholderReset_title")
        verify(reset !== null && reset.enabled)
        reset.click()
        compare(objectWithRole(deck, second, "title").text, "Quarterly review")
    }

    function test_animation_pane_adds_any_kind_and_previews() {
        const deck = createTemporaryObject(deckComponent, this)
        const slide = deck.slideIds[0]
        const shape = deck.insertObject(slide, "shape", { x: 100, y: 100, width: 200, height: 100 }, "")
        const preview = createTemporaryObject(previewComponent, this, { deck: deck })
        const pane = createTemporaryObject(animationComponent, this, { deck: deck, preview: preview, slideId: slide })
        const target = findChild(pane, "animationTargetPicker")
        target.currentIndex = target.count - 1
        const kind = findChild(pane, "animationKindPicker")
        kind.currentIndex = pane.kindIndex("zoom-in")
        findChild(pane, "animationAddEffect").click()
        compare(deck.effects(slide).length, 1)
        compare(deck.effects(slide)[0].kind, "zoom-in")
        compare(deck.effects(slide)[0].targetId, shape)
        const effect = deck.effects(slide)[0].id
        compare(pane.selectedEffectId, effect)
        const perEffect = findChild(pane, "animationKind_" + effect)
        verify(perEffect !== null)
        perEffect.activated(pane.kindIndex("fly-in"))
        compare(deck.effects(slide)[0].kind, "fly-in")

        const button = findChild(pane, "animationPreview")
        verify(button.enabled)
        button.click()
        compare(preview.state, "running")
        compare(pane.previewState, "running")
        verify(preview.advance(String(705600000 / 10)))
        button.click() // Stop: the build shows complete
        compare(preview.state, "canceled")
        compare(preview.ticks, preview.stepEndTicks)
        button.click()
        verify(preview.advance("100000000000"))
        compare(pane.previewState, "finished")
    }

    function test_console_buttons_step_builds_and_draw_ink() {
        const deck = createTemporaryObject(deckComponent, this)
        const first = deck.slideIds[0]
        const shape = deck.insertObject(first, "shape", { x: 100, y: 100, width: 200, height: 100 }, "")
        const effect = deck.addEffect(shape, "fade-in")
        verify(deck.editEffect(effect, { trigger: "click" }))
        const second = deck.insertSlide(first, "")
        const show = createTemporaryObject(showComponent, this, { deck: deck })
        verify(show.start())
        const presenterConsole = createTemporaryObject(consoleComponent, this, { show: show })
        const next = findChild(presenterConsole, "presenterNext")
        const previous = findChild(presenterConsole, "presenterPrevious")
        verify(!previous.enabled)
        next.click() // the click build, not the next slide
        compare(show.currentSlideId, first)
        compare(show.buildStep, 1)
        verify(show.animating)
        verify(previous.enabled)
        next.click() // a click during a running build completes it
        compare(show.currentSlideId, first)
        verify(!show.animating)
        next.click()
        compare(show.currentSlideId, second)
        verify(next.enabled) // the last slide can still end the show
        previous.click() // back to the first slide, complete
        compare(show.currentSlideId, first)
        compare(show.buildStep, 1)
        previous.click()
        compare(show.buildStep, 0)

        // Ink: pick the pen, drag on the audience preview, clear it.
        presenterConsole.forceActiveFocus()
        keyClick(Qt.Key_P)
        compare(show.annotationTool, "pen")
        const ink = findChild(presenterConsole, "presenterInkInput")
        verify(ink !== null)
        mousePress(ink, ink.width * 0.3, ink.height * 0.5)
        mouseMove(ink, ink.width * 0.5, ink.height * 0.5)
        mouseMove(ink, ink.width * 0.7, ink.height * 0.5)
        mouseRelease(ink, ink.width * 0.7, ink.height * 0.5)
        compare(show.annotationCount, 1)
        const view = findChild(presenterConsole, "presenterConsoleView")
        view.annotationRequested(show.currentSlideId, "highlight")
        compare(show.annotationTool, "highlight")
        findChild(presenterConsole, "presenterClearInk").click()
        compare(show.annotationCount, 0)
        keyClick(Qt.Key_A)
        mousePress(ink, ink.width * 0.5, ink.height * 0.5)
        verify(show.pointerVisible)
        mouseRelease(ink, ink.width * 0.5, ink.height * 0.5)
        verify(!show.pointerVisible)
    }

    function test_sorter_reorders_the_deck_with_undo() {
        const deck = createTemporaryObject(deckComponent, this)
        verify(deck !== null)
        const first = deck.slideIds[0]
        const second = deck.insertSlide(first, "")
        verify(second.length > 0)
        const sorter = createTemporaryObject(sorterComponent, this, { currentSlideId: second })
        sorter.slides = Qt.binding(function() { return deck.slides })
        sorter.reorderRequested.connect(function(id, index) { deck.moveSlide(id, index) })
        const up = findChild(sorter, "slideMoveUp_" + second)
        verify(up !== null)
        verify(up.enabled)
        up.click()
        compare(deck.slideIds[0], second)
        compare(sorter.slides[0].id, second)
        verify(deck.undo())
        compare(deck.slideIds[1], second)
    }

    function test_console_shows_notes_and_navigates() {
        const deck = createTemporaryObject(deckComponent, this)
        const first = deck.slideIds[0]
        verify(deck.setNotes(first, "Remember the demo"))
        const second = deck.insertSlide(first, "")
        const show = createTemporaryObject(showComponent, this, { deck: deck })
        verify(show.start())
        const presenterConsole = createTemporaryObject(consoleComponent, this, { show: show })
        const view = findChild(presenterConsole, "presenterConsoleView")
        verify(view !== null)
        compare(view.speakerNotes, "Remember the demo")
        compare(view.slideCount, 2)
        const next = findChild(presenterConsole, "presenterNext")
        verify(next !== null && next.enabled)
        next.click()
        compare(show.currentSlideId, second)
        compare(view.speakerNotes, "")
        presenterConsole.forceActiveFocus()
        keyClick(Qt.Key_Left)
        compare(show.currentSlideId, first)
        keyClick(Qt.Key_B)
        verify(show.blackedOut)
        verify(view.blackedOut)
        const frame = findChild(presenterConsole, "presenterCurrentFrame")
        verify(frame !== null)
        compare(frame.show, show)
    }
}
