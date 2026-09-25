// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import "review.js" as Fixture

// Document review workbench (WP-052). One Tk.RichTextDocument owns every
// edit: ReferenceManager, ReviewPane and EquationEditor only send requests,
// and each accepted request is one document undo step. Captions, the
// cross-reference, citations and the bibliography are generated fields that
// renumber inside the edit that moved them; the footnote's number is a field
// and its text is set at the foot of its page; the comment and the tracked
// replacement are marks in the text, so they follow edits and undo exactly;
// the equation is laid out and drawn on the page from its source.
// Run: qtk-preview examples/document-review/Main.qml --size 1280x900
Rectangle {
    id: scene
    width: 1280
    height: 900
    color: Tk.Theme.color.bg

    property string status: qsTr("Loading")
    property string selectedEquationId: ""
    property string commentDraft: ""
    property string referenceError: ""

    // Reports a request's outcome; a refusal shows the document's reason.
    function report(accepted, message) {
        status = accepted ? message : qsTr("Refused: %1").arg(document.lastError)
        return accepted
    }
    function find(list, id) {
        for (const entry of list) if (entry.id === id) return entry
        return null
    }
    // The report's fields, equation, comment and tracked change are made
    // with the document's own operations, then reopened as a clean history.
    function build() {
        if (!document.loadJson(Fixture.documentJson())) return report(false, "")
        const p = document.paragraphIds()
        const text = function(index) { return document.paragraphText(p[index]) }
        const wu = document.addSource({ author: "Wu, L.", title: "Field calibration of soil sensors",
                                        year: "2024", publisher: "Hydrology Press" })
        const okafor = document.addSource({ author: "Okafor, N.", title: "Seasonal energy budgets", year: "2021" })
        const figure = document.insertReference(p[Fixture.FigureCaption], 0, "caption", "sequence:Figure")
        document.insertReference(p[Fixture.Method], text(Fixture.Method).indexOf(" and extends"), "citation", wu)
        document.insertReference(p[Fixture.Method], text(Fixture.Method).length - 1, "cross-reference", figure)
        document.insertEquation(p[Fixture.Balance], text(Fixture.Balance).length - 1,
                                "E = \\sum_{i=1}^{n} q_i \\Delta t")
        document.insertReference(p[Fixture.Balance], text(Fixture.Balance).length, "caption", "sequence:Equation")
        document.insertReference(p[Fixture.Estimates], text(Fixture.Estimates).length - 1, "citation", okafor)
        document.insertReference(p[Fixture.Bibliography], 0, "bibliography", "")
        const work = text(Fixture.Method).indexOf("prior work")
        document.reviewAuthor = "A. Rivera"
        const thread = document.addComment(p[Fixture.Method], work, p[Fixture.Method], work + 10,
                                           "Could we cite the updated edition here?")
        document.reviewAuthor = "J. Kim"
        document.replyToComment(thread, "The 2024 edition is the one cited.")
        document.reviewAuthor = "M. Chen"
        document.trackChanges = true
        view.area.selectRange(p[Fixture.Estimates], 0, p[Fixture.Estimates], 7)
        view.area.insertText("Provisional")
        document.trackChanges = false
        // A footnote: its number is a field, its text sits at the page foot.
        document.insertNote(p[Fixture.Estimates], text(Fixture.Estimates).length - 1, "footnote",
                            "Readings between 02:00 and 04:00 were excluded after a logger fault.")
        document.reviewAuthor = qsTr("You")
        const opened = document.loadJson(document.toJson())
        selectedEquationId = document.equations.length ? document.equations[0].id : ""
        return report(opened, qsTr("Loaded the field report"))
    }

    Component.onCompleted: build()

    Tk.RichTextDocument { id: document }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("REFERENCES AND REVIEW"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Badge {
                objectName: "reviewReferenceCount"
                text: qsTr("%1 references").arg(document.references.length)
            }
            Tk.Badge {
                objectName: "reviewItemCount"
                text: qsTr("%1 review items").arg(document.reviewItems.length)
                variant: document.reviewItems.length > 0 ? "warning" : "default"
            }
            Tk.Switch {
                objectName: "reviewTrackChanges"
                text: qsTr("Track changes")
                tooltip: qsTr("Record insertions and deletions as tracked changes by %1").arg(document.reviewAuthor)
                checked: document.trackChanges
                onToggled: document.trackChanges = checked
            }
            Tk.IconButton {
                objectName: "reviewUndo"
                iconName: "undo-2"
                tooltip: document.canUndo ? qsTr("Undo %1").arg(document.undoText) : qsTr("Undo")
                enabled: document.canUndo
                onClicked: document.undo()
            }
            Tk.IconButton {
                objectName: "reviewRedo"
                iconName: "redo-2"
                tooltip: document.canRedo ? qsTr("Redo %1").arg(document.redoText) : qsTr("Redo")
                enabled: document.canRedo
                onClicked: document.redo()
            }
        }

        Tk.Flex {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.lg

            Tk.RichDocumentView {
                id: view
                objectName: "reviewDocument"
                document: document
                zoom: 0.75
                tooltip: qsTr("Field report pages")
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }

            Tk.Scroll {
                objectName: "reviewInspectorScroll"
                width: 430
                Tk.Flex.basis: 430
                Tk.Flex.shrink: 0
                overflowY: Tk.Scroll.Auto
                accessibleName: qsTr("Review inspector")

                // The Scroll sizes this column to its viewport; the right
                // padding keeps the overlay scroll bar off the controls.
                Tk.Flex {
                    direction: Tk.Flex.Column
                    align: Tk.Flex.Stretch
                    gap: Tk.Theme.space.md
                    padding: Tk.Theme.space.xs
                    paddingRight: Tk.Theme.size.scrollbar + Tk.Theme.space.sm

                    Tk.ReferenceManager {
                        id: referenceManager
                        objectName: "referenceManager"
                        references: document.references
                        targets: document.referenceTargets
                        supportedTypes: document.referenceTypes
                        errorText: scene.referenceError
                        onAddReferenceRequested: function(type, targetId) {
                            const id = view.area.insertReference(type, targetId)
                            scene.referenceError = id.length > 0 ? "" : document.lastError
                            if (scene.report(id.length > 0, qsTr("Inserted a %1 at the caret").arg(type)))
                                referenceManager.selectedReferenceId = id
                        }
                        onEditReferenceRequested: function(id, patch) {
                            const accepted = document.editReference(id, patch)
                            scene.referenceError = accepted ? "" : document.lastError
                            scene.report(accepted, qsTr("Reference updated"))
                        }
                        onRemoveReferenceRequested: function(id) {
                            if (scene.report(document.removeReference(id), qsTr("Reference removed")))
                                referenceManager.selectedReferenceId = ""
                        }
                        onNavigateRequested: function(id, targetId) {
                            const target = scene.find(document.referenceTargets, targetId)
                            const reference = scene.find(document.references, id)
                            const location = target && target.location ? target.location
                                           : reference ? reference.location : null
                            if (location) view.area.goToParagraph(location.paragraphId, location.offset)
                        }
                    }
                    Tk.Divider {}
                    Tk.ReviewPane {
                        id: reviewPane
                        objectName: "reviewPane"
                        items: document.reviewItems
                        // The document selects the item's anchored range (OFF-11 navigation).
                        onNavigateRequested: function(id, location) { view.area.goToReviewItem(id) }
                        onAcceptChangeRequested: function(id) {
                            scene.report(document.acceptChange(id), qsTr("Change accepted"))
                        }
                        onRejectChangeRequested: function(id) {
                            scene.report(document.rejectChange(id), qsTr("Change rejected"))
                        }
                        onReplyRequested: function(threadId, text) {
                            if (scene.report(document.replyToComment(threadId, text), qsTr("Reply added")))
                                reviewPane.replyDraft = ""
                        }
                        onResolveThreadRequested: function(threadId) {
                            scene.report(document.resolveComment(threadId), qsTr("Thread resolved"))
                        }
                    }
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.xs
                        Tk.TextField {
                            objectName: "reviewCommentDraft"
                            text: scene.commentDraft
                            placeholderText: qsTr("Comment on the selected text")
                            tooltip: qsTr("Text of a new comment on the page selection")
                            onTextEdited: scene.commentDraft = text
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                        }
                        Tk.Button {
                            objectName: "reviewAddComment"
                            text: qsTr("Comment")
                            iconName: "sticky-note"
                            tooltip: qsTr("Anchor a comment on the selected text")
                            enabled: scene.commentDraft.trim().length > 0 && view.area.hasSelection
                            onClicked: {
                                if (scene.report(view.area.addComment(scene.commentDraft).length > 0, qsTr("Comment added")))
                                    scene.commentDraft = ""
                            }
                        }
                    }
                    Tk.Divider {}
                    Tk.EquationEditor {
                        id: equationEditor
                        objectName: "reviewEquationEditor"
                        equation: {
                            document.revision // re-read after every edit
                            return scene.selectedEquationId.length ? document.equationSnapshot(scene.selectedEquationId) : null
                        }
                        symbols: document.equationSymbols
                        // Live validation: the document's parser names what it refuses.
                        onSourceDraftChanged: {
                            const check = document.validateEquation(sourceDraft)
                            errorText = check.valid ? "" : check.message
                        }
                        onSourceChangeRequested: function(id, source) {
                            const accepted = document.setEquationSource(id, source)
                            if (!accepted) equationEditor.errorText = document.lastError
                            scene.report(accepted, qsTr("Equation updated"))
                        }
                        onSymbolInsertRequested: function(id, symbolId, source, cursorPosition) {
                            equationEditor.sourceDraft = document.equationSourceWithSymbol(source, cursorPosition, symbolId)
                        }
                        onNavigateRequested: function(id, location) {
                            if (location) view.area.goToParagraph(location.paragraphId, location.offset)
                        }
                    }
                    Tk.Button {
                        objectName: "reviewInsertEquation"
                        text: qsTr("Insert equation")
                        iconName: "sigma"
                        tooltip: qsTr("Insert a fraction at the caret and edit it here")
                        onClicked: {
                            const id = view.area.insertEquation("\\frac{a}{b}")
                            if (scene.report(id.length > 0, qsTr("Equation inserted"))) scene.selectedEquationId = id
                        }
                    }
                }
            }
        }
        Tk.Notice {
            objectName: "reviewHostStatus"
            text: scene.status
            variant: scene.status.startsWith(qsTr("Refused")) ? "warning" : "info"
        }
    }
}
