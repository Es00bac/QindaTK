// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Host-owned semantic fixture: panel requests update these stable-ID arrays.
// Equation parsing/layout and comment-anchor reconciliation remain adapters.
Rectangle {
    id: root
    width: 1120
    height: 940
    color: Tk.Theme.color.bg
    property int revision: 12
    property string notice: qsTr("Select an item to inspect its stable identity.")
    property var targets: [
        { id: "heading-intro", label: "Introduction", type: "heading" },
        { id: "figure-3", label: "Figure 3 · Results", type: "caption" },
        { id: "source-wu", label: "Wu et al. · 2024", type: "bibliography" }
    ]
    property var references: [
        { id: "ref-104", label: "Prior work", type: "citation", targetId: "source-wu",
          status: "resolved", formattedText: "[4]", location: { blockId: "para-8", offset: 31 } },
        { id: "ref-109", label: "Figure result", type: "cross-reference", targetId: "figure-3",
          status: "broken", error: qsTr("Target was removed; choose a new target."), formattedText: "Figure ?",
          location: { blockId: "para-11", offset: 16 } }
    ]
    property var reviewItems: [
        { id: "change-52", kind: "change", author: "M. Chen", time: "14:32", summary: "Replace a term",
          body: "Changed ‘initial’ to ‘provisional’.", status: "open", location: { blockId: "para-12", start: 4, end: 12 },
          canAccept: true, canReject: true },
        { id: "thread-77", kind: "comment", author: "A. Rivera", time: "Yesterday", summary: "Check source wording",
          body: "Could we cite the updated edition here?", status: "open", threadId: "thread-77",
          replies: [], location: { blockId: "para-8", start: 28, end: 44 } },
        { id: "thread-81", kind: "comment", author: "J. Kim", time: "Sep 18", summary: "Orphaned after edit",
          body: "This note cannot be attached to the moved text.", status: "orphaned", threadId: "thread-81",
          error: qsTr("The original text range no longer exists."), location: null }
    ]
    property var equation: ({ id: "eq-21", source: "E = m c^2", linearText: "E equals m times c squared",
                               status: "ready", supported: true,
                               location: { blockId: "para-16", offset: 5 }, supportedSymbols: ["greek-alpha", "integral"] })
    property var symbols: [
        { id: "greek-alpha", label: "α", source: "\\alpha", tooltip: qsTr("Insert Greek alpha") },
        { id: "integral", label: "∫", source: "\\int", tooltip: qsTr("Insert integral") },
        { id: "matrix", label: "▦", source: "\\begin{matrix}\\end{matrix}", tooltip: qsTr("Matrix syntax is unsupported by this host") }
    ]

    function cloneArray(items) { return items.map(function(item) { return Object.assign({}, item) }) }
    function nextId(prefix) { return prefix + "-" + (revision + 1) }
    function addReference(type, targetId) {
        const target = referenceManager.targetById(targetId)
        if (!target) { referenceManager.errorText = qsTr("Target is no longer available."); return }
        const next = cloneArray(references)
        const id = nextId("ref")
        next.push({ id: id, label: target.label, type: type, targetId: targetId, status: "resolved",
                    formattedText: type === "citation" ? "[5]" : target.label,
                    location: { blockId: "para-new", offset: 0 } })
        references = next
        ++revision
        referenceManager.selectedReferenceId = id
        notice = qsTr("Added %1 reference %2; host revision %3.").arg(type).arg(id).arg(revision)
    }
    function editReference(id, patch) {
        const next = cloneArray(references)
        const index = next.findIndex(function(item) { return item.id === id })
        if (index < 0 || !referenceManager.targetById(patch.targetId)) return
        Object.assign(next[index], patch, { status: "resolved", error: "" })
        references = next
        ++revision
        notice = qsTr("Updated reference %1; host revision %2.").arg(id).arg(revision)
    }
    function removeReference(id) {
        references = references.filter(function(item) { return item.id !== id })
        ++revision
        referenceManager.selectedReferenceId = ""
        notice = qsTr("Removed reference %1; host revision %2.").arg(id).arg(revision)
    }
    function updateReview(id, patch) {
        const next = cloneArray(reviewItems)
        const index = next.findIndex(function(item) { return item.id === id })
        if (index < 0) return
        Object.assign(next[index], patch)
        reviewItems = next
        ++revision
    }

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Heading { text: qsTr("References and review"); level: 2; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Chip { text: qsTr("Revision %1").arg(root.revision); iconName: "history"; tooltip: qsTr("Host document revision") }
        }
        Tk.Caption { objectName: "fixtureNotice"; text: root.notice; wrapMode: Text.WordWrap }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Start
            gap: Tk.Theme.space.md
            Tk.Scroll {
                objectName: "documentReviewInspector"
                overflowY: Tk.Scroll.Auto
                Tk.Flex {
                    width: 340
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    Tk.Panel {
                        title: qsTr("References")
                        implicitWidth: 340
                        Tk.ReferenceManager {
                            id: referenceManager
                            objectName: "referenceManager"
                            anchors.fill: parent
                            anchors.margins: Tk.Theme.space.sm
                            references: root.references
                            targets: root.targets
                            selectedReferenceId: "ref-109"
                            supportedTypes: ["citation", "footnote", "caption", "cross-reference"]
                            onAddReferenceRequested: function(type, targetId) { root.addReference(type, targetId) }
                            onEditReferenceRequested: function(id, patch) { root.editReference(id, patch) }
                            onRemoveReferenceRequested: function(id) { root.removeReference(id) }
                            onNavigateRequested: function(id, targetId) {
                                root.notice = qsTr("Navigate request: %1 → %2").arg(id).arg(targetId)
                            }
                        }
                    }
                    Tk.Panel {
                        title: qsTr("Comments and changes")
                        implicitWidth: 340
                        Tk.ReviewPane {
                            id: reviewPane
                            objectName: "reviewPane"
                            anchors.fill: parent
                            anchors.margins: Tk.Theme.space.sm
                            items: root.reviewItems
                            selectedItemId: "change-52"
                            onNavigateRequested: function(id, location) {
                                root.notice = location ? qsTr("Navigate request for %1 at %2").arg(id).arg(location.blockId)
                                                        : qsTr("Review item %1 has no recoverable anchor.").arg(id)
                            }
                            onAcceptChangeRequested: function(id) {
                                root.updateReview(id, { status: "accepted" })
                                root.notice = qsTr("Accepted change %1; host revision %2.").arg(id).arg(root.revision)
                            }
                            onRejectChangeRequested: function(id) {
                                root.updateReview(id, { status: "rejected" })
                                root.notice = qsTr("Rejected change %1; host revision %2.").arg(id).arg(root.revision)
                            }
                            onReplyRequested: function(id, text) {
                                root.notice = qsTr("Reply requested for %1 (%2 characters). Host stores thread replies.").arg(id).arg(text.length)
                            }
                            onResolveThreadRequested: function(id) {
                                root.updateReview(id, { status: "resolved" })
                                root.notice = qsTr("Resolved thread %1; host revision %2.").arg(id).arg(root.revision)
                            }
                        }
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Equation source")
                implicitWidth: Tk.Theme.size.panelMinWidth * 2.6
                Tk.EquationEditor {
                    id: equationEditor
                    objectName: "equationEditor"
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    equation: root.equation
                    symbols: root.symbols
                    onSourceChangeRequested: function(id, source) {
                        if (source.indexOf("\\begin{matrix}") >= 0) {
                            equationEditor.errorText = qsTr("Matrix equations are unsupported by this document engine.")
                            return
                        }
                        root.equation = Object.assign({}, root.equation, { source: source, status: "ready" })
                        equationEditor.errorText = ""
                        ++root.revision
                        root.notice = qsTr("Equation source request accepted for %1; host revision %2.").arg(id).arg(root.revision)
                    }
                    onSymbolInsertRequested: function(id, symbolId, source, cursorPosition) {
                        if (symbolId === "matrix") {
                            equationEditor.errorText = qsTr("Matrix symbol is unavailable in the active parser.")
                            return
                        }
                        const symbol = root.symbols.find(function(value) { return value.id === symbolId })
                        const nextSource = source.slice(0, cursorPosition) + symbol.source + source.slice(cursorPosition)
                        equationEditor.sourceDraft = nextSource
                    }
                    onNavigateRequested: function(id, location) {
                        root.notice = qsTr("Navigate request for equation %1 at %2.").arg(id).arg(location.blockId)
                    }
                }
            }
            Tk.Panel {
                title: qsTr("Document excerpt")
                implicitWidth: Tk.Theme.size.panelMinWidth * 2.2
                Tk.Flex {
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.md
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.md
                    Tk.Overline { title: qsTr("FIELD REPORT · PAGE 4") }
                    Tk.Label {
                        text: qsTr("The revised method follows prior work [4] and is summarized in Figure 3.")
                        wrapMode: Text.WordWrap
                    }
                    Tk.Notice {
                        variant: "warning"
                        text: qsTr("Figure ? has a broken cross-reference. Select it in References to repair the target.")
                    }
                    Tk.Divider {}
                    Tk.Caption {
                        text: qsTr("E = m c²")
                        Accessible.name: root.equation.linearText
                        Accessible.role: Accessible.StaticText
                    }
                    Tk.Caption { text: qsTr("Tracked replacement: initial → provisional") }
                    Tk.Caption { text: qsTr("A comment is orphaned because its anchored range was removed."); color: Tk.Theme.color.warning; wrapMode: Text.WordWrap }
                    Item { implicitHeight: Tk.Theme.space.sm; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                    Tk.Caption { text: qsTr("Reference fields and review locations are resolved by the document host."); wrapMode: Text.WordWrap }
                }
            }
        }
    }
}
