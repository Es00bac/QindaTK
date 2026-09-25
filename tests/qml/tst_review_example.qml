// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest

// Drives examples/document-review/Main.qml (WP-052): ReferenceManager,
// ReviewPane and EquationEditor edit one rich-text document. A caption added
// in front renumbers the cross-reference in one undo step, a tracked change
// is accepted and restored, a reply joins its thread, and the equation
// source is validated and applied by the document's parser. The second case
// drives the pane's navigation, a footnote added from the manager, and a
// visual equation edit that the source view and the document both follow.
TestCase {
    id: root
    name: "ReviewExample"
    when: windowShown
    visible: true
    width: 1280
    height: 900

    Loader {
        id: loader
        anchors.fill: parent
        source: Qt.resolvedUrl("../../examples/document-review/Main.qml")
    }

    // Each case starts from a freshly built example document.
    function init() {
        loader.active = false
        loader.active = true
        tryCompare(loader, "status", Loader.Ready)
        waitForRendering(loader.item) // clicks need the laid-out panels
    }

    function part(name) {
        const item = findChild(loader.item, name)
        verify(item !== null, name)
        return item
    }

    function entry(list, predicate) {
        for (let i = 0; i < list.length; ++i) if (predicate(list[i])) return list[i]
        return null
    }

    function test_panels_edit_one_document() {
        tryCompare(loader, "status", Loader.Ready)
        const view = part("reviewDocument")
        const document = view.document
        tryVerify(function() { return document.references.length === 7 })
        compare(document.reviewItems.length, 3)
        compare(document.equations.length, 1)
        compare(document.canUndo, false) // the fixture opens as a clean document
        // AGENT-NOTE: entries of a C++ list property read through to the
        // list by index, so keep the ID, not the entry, across edits.
        const xref = entry(document.references, function(r) { return r.type === "cross-reference" })
        const xrefId = xref.id
        compare(xref.formattedText, "Figure 1")
        compare(xref.status, "resolved")

        // A caption added at the top renumbers the reference in one step.
        const paragraphs = document.paragraphIds()
        verify(view.area.goToParagraph(paragraphs[0], 0))
        part("referenceManager").addReferenceRequested("caption", "sequence:Figure")
        compare(entry(document.references, function(r) { return r.id === xrefId }).formattedText, "Figure 2")
        compare(document.undoText, "Insert reference")
        verify(document.undo())
        compare(entry(document.references, function(r) { return r.id === xrefId }).formattedText, "Figure 1")

        // Accepting the tracked insertion from the pane, and undoing it.
        const pane = part("reviewPane")
        const insertion = entry(document.reviewItems, function(i) { return i.changeKind === "insertion" })
        compare(insertion.author, "M. Chen")
        pane.acceptChangeRequested(String(insertion.id))
        compare(document.reviewItems.length, 2)
        verify(document.undo())
        compare(document.reviewItems.length, 3)

        // A reply joins the thread.
        const thread = entry(document.reviewItems, function(i) { return i.kind === "comment" })
        const threadId = thread.threadId
        compare(thread.replies.length, 1)
        pane.replyRequested(threadId, "Noted")
        compare(entry(document.reviewItems, function(i) { return i.id === threadId }).replies.length, 2)

        // The equation editor shows the document's linear text and applies source.
        const editor = part("reviewEquationEditor")
        compare(editor.equation.linearText, "E = ∑_(i = 1)^n q_iΔt")
        editor.sourceChangeRequested(editor.equation.id, "\\frac{a}{b}")
        compare(document.equations[0].source, "\\frac{a}{b}")
        compare(editor.equation.linearText, "a/b")
        editor.sourceDraft = "\\begin{matrix}"
        verify(editor.errorText.length > 0)
        compare(editor.canCommit, false)
    }

    function sameRange(selection, location) {
        return selection.anchorParagraphId === location.paragraphId && selection.focusParagraphId === location.endParagraphId
            && Math.min(selection.anchorOffset, selection.focusOffset) === location.offset
            && Math.max(selection.anchorOffset, selection.focusOffset) === location.endOffset
    }

    function test_navigation_notes_and_visual_equation() {
        tryCompare(loader, "status", Loader.Ready)
        const view = part("reviewDocument")
        const document = view.document
        tryVerify(function() { return document.reviewItems.length === 3 })
        const pane = part("reviewPane")

        // Next steps through the anchored items in document order and the
        // page selects each one's range; Previous wraps back to the last.
        const order = document.reviewItems.map(function(i) { return String(i.id) })
        mouseClick(part("reviewNext"))
        compare(pane.selectedItemId, order[0])
        verify(sameRange(view.area.selectionRange(), document.reviewItemLocation(order[0])))
        mouseClick(part("reviewNext"))
        compare(pane.selectedItemId, order[1])
        verify(sameRange(view.area.selectionRange(), document.reviewItemLocation(order[1])))
        mouseClick(part("reviewPrevious"))
        mouseClick(part("reviewPrevious"))
        compare(pane.selectedItemId, order[order.length - 1])
        verify(sameRange(view.area.selectionRange(), document.reviewItemLocation(order[order.length - 1])))
        // The Go to button selects the chosen item's range.
        pane.selectedItemId = order[1]
        mouseClick(part("reviewNavigate"))
        verify(sameRange(view.area.selectionRange(), document.reviewItemLocation(order[1])))
        compare(view.area.goToAdjacentReviewItem(true, "comment").length > 0, true)

        // A footnote from the manager carries its text into the document.
        const manager = part("referenceManager")
        manager.draftType = "footnote"
        verify(part("referenceNoteText").visible)
        verify(!part("referenceTarget").visible)
        verify(!part("referenceAdd").enabled)
        manager.draftNoteText = "Measured at noon."
        mouseClick(part("referenceAdd"))
        const noteId = manager.selectedReferenceId
        const note = entry(document.references, function(r) { return r.id === noteId })
        verify(note !== null)
        compare(note.type, "footnote")
        compare(note.noteText, "Measured at noon.")
        // Notes number in document order: the fixture's footnote follows the new one or precedes it.
        const fixtureNote = entry(document.references, function(r) { return r.type === "footnote" && r.id !== noteId })
        compare(Number(note.formattedText) + Number(fixtureNote.formattedText), 3)

        // Typing in the visual view writes canonical source; Apply sends it.
        const editor = part("reviewEquationEditor")
        const visual = part("equationVisual")
        mouseClick(visual, visual.width - 2, visual.height / 2)
        verify(visual.activeFocus)
        compare(editor.activeView, "visual")
        visual.selectAll()
        keyClick(Qt.Key_Delete)
        keyClick(Qt.Key_X)
        keyClick(Qt.Key_AsciiCircum)
        keyClick(Qt.Key_2)
        compare(editor.sourceDraft, "x^2")
        compare(part("equationSource").text, "x^2")
        keyClick(Qt.Key_Right) // out of the superscript
        mouseClick(part("equationSymbol_frac")) // the palette inserts at the visual caret
        compare(editor.sourceDraft, "x^2\\frac{}{}")
        compare(visual.caretPath, "1.0:0") // in the empty numerator
        keyClick(Qt.Key_A)
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_B)
        compare(editor.sourceDraft, "x^2\\frac{a}{b}")
        compare(editor.errorText, "")
        const apply = part("equationApply")
        verify(apply.enabled)
        apply.clicked()
        compare(document.equations[0].source, "x^2\\frac{a}{b}")
        compare(document.equations[0].linearText, editor.equation.linearText)
        // A source edit re-derives the visual tree.
        editor.sourceDraft = "\\sqrt{y}"
        compare(visual.source, "\\sqrt{y}")
        compare(visual.linearText, "√(y)")
    }
}
