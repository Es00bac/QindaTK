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
    Component { id: canvasComponent; Tk.SlideCanvas { width: 700; height: 460 } }

    function test_canvas_paints_the_audience_render_under_chrome() {
        const deck = createTemporaryObject(deckComponent, this)
        const slide = deck.slideIds[0]
        const canvas = createTemporaryObject(canvasComponent, this, { deck: deck, slideId: slide })
        canvas.objects = Qt.binding(function() { return deck.revision.length ? deck.objects(slide) : [] })
        verify(canvas.rendered)
        const render = findChild(canvas, "slideCanvasRender")
        verify(render !== null)
        verify(render.visible)
        compare(render.deck, deck)
        compare(render.step, -1)
        const title = canvas.objects.filter(function(o) { return o.role === "title" })[0]
        const chrome = findChild(findChild(canvas, "slideObject_" + title.id), "slideObjectChrome")
        verify(chrome !== null)
        compare(chrome.color.a, 0) // content comes from the painter, not the delegate
        tryVerify(function() { return render.paintCount > 0 })
        compare(render.displayedSlideId, slide)
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
