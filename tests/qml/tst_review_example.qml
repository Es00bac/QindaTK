// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest

// Drives examples/document-review/Main.qml (WP-052): ReferenceManager,
// ReviewPane and EquationEditor edit one rich-text document. A caption added
// in front renumbers the cross-reference in one undo step, a tracked change
// is accepted and restored, a reply joins its thread, and the equation
// source is validated and applied by the document's parser.
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
        tryVerify(function() { return document.references.length === 6 })
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
}
