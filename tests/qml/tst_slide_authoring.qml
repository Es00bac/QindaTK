// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-056 / OFF-19: Tk.SlideCanvas driven against a real Tk.SlideDeck. Every
// gesture lands in the deck as one undo step, and text edits go through the
// rich-text stack so formatting survives (docs/slides.md#canvas).
TestCase {
    name: "SlideAuthoring"
    when: windowShown
    visible: true
    width: 900
    height: 620

    Component { id: deckComponent; Tk.SlideDeck {} }
    Component { id: canvasComponent; Tk.SlideCanvas { width: 880; height: 600 } }

    function objectById(deck, slideId, id) {
        const objects = deck.objects(slideId)
        for (let i = 0; i < objects.length; ++i) if (objects[i].id === id) return objects[i]
        return null
    }
    function setup() {
        const deck = createTemporaryObject(deckComponent, this)
        const canvas = createTemporaryObject(canvasComponent, this, { deck: deck, slideId: deck.slideIds[0] })
        verify(canvas !== null)
        tryVerify(function() { return canvas.view.zoom > 0.3 && canvas.view.zoom < 1.5 })
        return { deck: deck, canvas: canvas, slide: deck.slideIds[0] }
    }

    function test_toolbar_edits_reach_the_deck() {
        const t = setup()
        compare(t.canvas.canvasDocument.objectCount, 2)
        const insert = findChild(t.canvas, "slideInsertShape")
        verify(insert !== null && insert.enabled)
        insert.click()
        const objects = t.deck.objects(t.slide)
        compare(objects.length, 3)
        const id = objects[2].id
        compare(t.canvas.selectedObjectIds, [id])
        compare(t.canvas.canvasDocument.objectCount, 3)

        const fill = findChild(t.canvas, "slideFill")
        verify(fill.enabled)
        fill.colorSelected("#ff0000")
        compare(objectById(t.deck, t.slide, id).fill, "#ff0000")

        t.canvas.view.forceActiveFocus()
        keyClick(Qt.Key_Delete)
        verify(objectById(t.deck, t.slide, id) === null)
        compare(t.canvas.selectedObjectIds.length, 0)
        verify(t.deck.undo())
        compare(objectById(t.deck, t.slide, id).fill, "#ff0000")
    }

    function test_dragging_the_selection_moves_it_in_one_step() {
        const t = setup()
        const id = t.deck.insertObject(t.slide, "shape", { x: 100, y: 300, width: 200, height: 100 }, "")
        t.canvas.select([id])
        const area = findChild(t.canvas, "slideMoveArea")
        verify(area !== null)
        tryVerify(function() { return area.visible && area.width > 20 })
        const x = area.width / 2, y = area.height / 2
        mousePress(area, x, y)
        mouseMove(area, x + 20, y + 5)
        mouseMove(area, x + 60, y + 15)
        mouseRelease(area, x + 60, y + 15)
        const moved = objectById(t.deck, t.slide, id).bounds
        verify(moved.x > 150, "x " + moved.x)
        compare(moved.width, 200)
        compare(t.deck.undoText, "Move selection")
        verify(t.deck.undo())
        compare(objectById(t.deck, t.slide, id).bounds.x, 100)

        // The bottom-right handle resizes through SpatialTransform.
        t.canvas.select([id])
        const handle = findChild(t.canvas, "handleBottomRight")
        verify(handle !== null)
        tryVerify(function() { return handle.visible })
        mousePress(handle, handle.width / 2, handle.height / 2)
        mouseMove(handle, handle.width / 2 + 30, handle.height / 2 + 30)
        mouseMove(handle, handle.width / 2 + 60, handle.height / 2 + 60)
        mouseRelease(handle, handle.width / 2 + 60, handle.height / 2 + 60)
        // A corner scales both axes from the opposite corner.
        const resized = objectById(t.deck, t.slide, id).bounds
        verify(resized.width > 250, "width " + resized.width)
        verify(resized.height > 130, "height " + resized.height)
        compare(resized.x, 100)
        compare(resized.y, 300)
    }

    function test_drag_move_snaps_to_the_centre_guide() {
        const t = setup()
        const id = t.deck.insertObject(t.slide, "shape", { x: 100, y: 300, width: 200, height: 100 }, "")
        t.canvas.select([id])
        const area = findChild(t.canvas, "slideMoveArea")
        tryVerify(function() { return area.visible && area.width > 20 })
        const view = t.canvas.view
        // Grab the shape at slide point (200, 350) and let go 3 slide px
        // left of the vertical centre guide (x = 480).
        const from = area.mapFromItem(view, view.mapFromDocument(Qt.point(200, 350)))
        const to = area.mapFromItem(view, view.mapFromDocument(Qt.point(477, 350)))
        mousePress(area, from.x, from.y)
        mouseMove(area, from.x + 20, from.y)
        mouseMove(area, to.x, to.y)
        mouseRelease(area, to.x, to.y)
        // The grab point lands exactly on the guide (pointer snapping, as in
        // every WP-042 canvas); integer mouse positions put it near x 200.
        fuzzyCompare(objectById(t.deck, t.slide, id).bounds.x, 100 + 480 - area.startDocument.x, 1e-6)
        verify(Math.abs(objectById(t.deck, t.slide, id).bounds.x - 380) < 1.5)
        verify(t.deck.undo())

        // With snapping off the same drag lands where the pointer is.
        mouseClick(findChild(t.canvas, "slideSnap"))
        verify(!t.canvas.snapModel.enabled)
        t.canvas.select([id])
        tryVerify(function() { return area.visible && area.width > 20 })
        const from2 = area.mapFromItem(view, view.mapFromDocument(Qt.point(200, 350)))
        const to2 = area.mapFromItem(view, view.mapFromDocument(Qt.point(477, 350)))
        mousePress(area, from2.x, from2.y)
        mouseMove(area, from2.x + 20, from2.y)
        mouseMove(area, to2.x, to2.y)
        mouseRelease(area, to2.x, to2.y)
        const free = objectById(t.deck, t.slide, id).bounds.x
        verify(Math.abs(free - 377) < 1.5, "x " + free)
        verify(Math.abs(free - (100 + 480 - area.startDocument.x)) > 1, "not snapped: " + free)
    }

    function test_text_edits_keep_rich_formatting() {
        const t = setup()
        const id = t.deck.insertObject(t.slide, "text", { x: 100, y: 380, width: 400, height: 80 }, "Hello")
        t.canvas.select([id])
        const edit = findChild(t.canvas, "slideEditText")
        verify(edit.enabled)
        edit.click()
        verify(t.canvas.textEditing)
        verify(t.deck.textEditing)
        const editor = findChild(t.canvas, "slideTextEditor")
        verify(editor !== null && editor.visible)
        editor.area.selectAll()
        verify(editor.invokeCommand("text.bold"))
        edit.click() // Done
        verify(!t.canvas.textEditing)
        verify(t.deck.objectTextJson(id).indexOf("\"weight\":700") >= 0, t.deck.objectTextJson(id))

        // A later canvas edit keeps the bold run (setObjectText would drop it).
        edit.click()
        verify(editor.area.goToParagraph(editor.document.paragraphIds()[0], 0))
        verify(editor.area.insertText("Well, "))
        edit.click()
        compare(objectById(t.deck, t.slide, id).text, "Well, Hello")
        verify(t.deck.objectTextJson(id).indexOf("\"weight\":700") >= 0, t.deck.objectTextJson(id))
        compare(t.deck.undoText, "Edit text")

        // Escape discards an edit.
        edit.click()
        verify(editor.area.insertText("zzz"))
        keyClick(Qt.Key_Escape)
        verify(!t.deck.textEditing)
        compare(objectById(t.deck, t.slide, id).text, "Well, Hello")
    }
}
