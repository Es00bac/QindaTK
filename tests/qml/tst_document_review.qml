// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "DocumentReview"
    when: windowShown
    visible: true
    width: 760
    height: 520

    Component {
        id: managerComponent
        Tk.ReferenceManager {
            width: 500
            references: [{ id: "ref-1", label: "Prior study", type: "citation", targetId: "bib-2", status: "resolved" }]
            targets: [{ id: "bib-2", label: "Author · 2025", type: "bibliography" }]
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_add_reference_emits_stable_target_request() {
        const manager = createTemporaryObject(managerComponent, this)
        verify(manager !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: manager, signalName: "addReferenceRequested" })
        manager.draftType = "citation"
        manager.draftTargetId = "bib-2"
        const add = findChild(manager, "referenceAdd")
        verify(add !== null && add.enabled)

        add.click()

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "citation")
        compare(requested.signalArguments[0][1], "bib-2")
        compare(manager.references.length, 1)
    }

    Component {
        id: paneComponent
        Tk.ReviewPane {
            width: 500
            height: 400
            items: [{ id: "c1", kind: "comment", status: "open", location: { paragraphId: "p", offset: 0, endParagraphId: "p", endOffset: 3 } },
                    { id: "c2", kind: "comment", status: "orphaned", location: null },
                    { id: "i1", kind: "change", status: "open", location: { paragraphId: "p", offset: 5, endParagraphId: "p", endOffset: 6 } }]
        }
    }

    function test_note_draft_sends_its_text() {
        const manager = createTemporaryObject(managerComponent, this, { supportedTypes: ["citation", "footnote"] })
        const requested = createTemporaryObject(spyComponent, this,
            { target: manager, signalName: "addReferenceRequested" })
        manager.draftType = "footnote"
        const noteText = findChild(manager, "referenceNoteText")
        verify(noteText.visible)
        verify(!findChild(manager, "referenceTarget").visible)
        verify(!manager.canAdd) // a note needs text, not a target
        manager.draftNoteText = "  See the appendix. "
        findChild(manager, "referenceAdd").click()
        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "footnote")
        compare(requested.signalArguments[0][1], "See the appendix.")
    }

    function test_next_and_previous_skip_orphans_and_wrap() {
        const pane = createTemporaryObject(paneComponent, this)
        const navigated = createTemporaryObject(spyComponent, this,
            { target: pane, signalName: "navigateRequested" })
        findChild(pane, "reviewNext").click()
        compare(pane.selectedItemId, "c1")
        findChild(pane, "reviewNext").click()
        compare(pane.selectedItemId, "i1") // the orphan has no range to go to
        findChild(pane, "reviewNext").click()
        compare(pane.selectedItemId, "c1") // wraps
        findChild(pane, "reviewPrevious").click()
        compare(pane.selectedItemId, "i1")
        compare(navigated.count, 4)
        compare(navigated.signalArguments[3][0], "i1")
        compare(navigated.signalArguments[3][1].offset, 5)
    }
}
