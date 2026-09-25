// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtCore
import QindaTK as Tk

// A field notebook on the persistent Tk.NotebookDocument (docs/notebooks.md):
// a DocumentManager session owns it, so undo, save and recovery are the real
// services. The views are bound to the model; nothing here copies block data.
Rectangle {
    id: root
    width: 1520
    height: 960
    color: Tk.Theme.color.bg
    Accessible.role: Accessible.Window
    Accessible.name: qsTr("Notebook authoring example")

    property var session: null
    readonly property var notebook: session ? session.model : null
    property string statusText: qsTr("Opening the field notebook…")
    readonly property string savePath: String(StandardPaths.writableLocation(StandardPaths.TempLocation))
                                       .replace(/^file:\/\//, "") + "/field-notes.qtknb"
    readonly property string currentPageId: notebookView.currentPageId
    // The revision argument re-reads the links after every committed edit.
    readonly property int backlinkCount: backlinksFor(currentPageId, notebook ? notebook.revision : "")

    function backlinksFor(pageId, revision) {
        return notebook && pageId.length > 0 ? notebook.backlinks(pageId).length : 0
    }

    // Formatted text as {text, runs}; each part is [text] or [text, format].
    function rich(parts) {
        let text = ""
        const runs = []
        for (let i = 0; i < parts.length; ++i) {
            text += parts[i][0]
            runs.push(Object.assign({ length: parts[i][0].length }, parts[i][1] || {}))
        }
        return { text: text, runs: runs }
    }
    function block(id, type, data, parentId) {
        const record = { id: id, type: type, data: data }
        if (parentId) record.parentId = parentId
        return record
    }
    function page(id, sectionId, title, tags, blocks) {
        return { id: id, sectionId: sectionId, title: title, tags: tags, blocks: blocks }
    }
    function fixture(documentId) {
        const tide = { columns: ["Minute", "Level (m)", "Salinity"],
                       rows: [["0", "1.8", "18.2"], ["35", "1.6", "18.7"], ["70", "1.4", "19.1"], ["105", "1.1", "19.6"]],
                       rowIds: ["tide-0", "tide-35", "tide-70", "tide-105"] }
        return {
            format: "QindaTK.Notebook", version: 1, documentId: documentId, revision: "1", resetGeneration: "0",
            title: "Estuary field journal",
            sections: [{ id: "section-field", title: "Field journal" }, { id: "section-research", title: "Research" },
                       { id: "section-recipes", title: "Recipes" }],
            pages: [
                page("page-start", "section-field", "Start here", ["welcome", "guide"], [
                    block("start-heading", "heading", { text: "A notebook for the field", level: 1 }),
                    block("start-copy", "text", rich([["Keep "], ["observations", { bold: true }],
                        [", sources, sketches and measurements together. Every page and block keeps a "],
                        ["stable identity", { italic: true }], [" as the notebook changes."]])),
                    block("start-tip-1", "list", { text: "Ctrl+Enter splits a block; Backspace at its start joins it to the one above.", ordered: false }),
                    block("start-tip-2", "list", { text: "Tab nests a block under the previous one; Shift+Tab lifts it out.", ordered: false }),
                    block("start-embed", "embed", { targetId: "notebook:page-lab-log", title: "Today's survey log" })
                ]),
                page("page-lab-log", "section-field", "Tidal lab · day 4", ["estuary", "fieldwork", "important"], [
                    block("lab-heading", "heading", { text: "East inlet · morning tide", level: 2 }),
                    block("lab-intro", "text", rich([["The upper bank is still "], ["soft", { italic: true }],
                        [" after last night's rain. Three terns were feeding where the creek meets the sandbar."]])),
                    block("lab-detail", "text", rich([["Water cleared quickly after the first ebb; the "], ["salinity", { bold: true }],
                        [" climbed as the tide fell (see "], ["sample measurements", { href: "notebook:page-data" }], [")."]])),
                    block("lab-todo", "todo", { text: "Mark a second transect before the next survey.", checked: false }, "lab-detail"),
                    block("lab-table", "table", tide),
                    block("lab-chart", "chart", { sourceBlockId: "lab-table", kind: "line", xColumn: 0, yColumn: 2, title: "Salinity through the ebb" }),
                    block("lab-code", "code", { language: "javascript", text:
                        "const table = app.document.blocks.find(b => b.type === 'table');\n"
                        + "const salinity = table.rows.map(r => Number(r[2]));\n"
                        + "app.print('Samples: ' + salinity.length);\n"
                        + "return salinity.reduce((a, b) => a + b, 0) / salinity.length;" }),
                    block("lab-photo", "image", { resourceId: "field-photo", caption: "Sandbar at low water" })
                ]),
                page("page-sketches", "section-field", "Bank sketches", ["ink", "estuary"], [
                    block("sketch-text", "text", { text: "The sandbar curves toward the old pier. Add the north bank marks after the next walk." }),
                    block("sketch-photo", "image", { resourceId: "bank-sketch", caption: "North bank from the pier" }),
                    block("sketch-ink", "ink", { strokes: [{ id: "stroke-1" }, { id: "stroke-2" }] })
                ]),
                page("page-citations", "section-research", "Sources and quotations", ["references"], [
                    block("cite-quote", "quote", { text: "Estuaries are the nurseries of the coast." }),
                    block("cite-note", "text", rich([["Sources are attached by "], ["stable IDs", { code: true }],
                        [" so notes keep their target when sections are reorganized."]]))
                ]),
                page("page-data", "section-research", "Sample measurements", ["data", "estuary"], [
                    block("data-intro", "text", { text: "Water level readings for the morning survey; the salinity series is on the day 4 log." }),
                    block("data-table", "table", tide),
                    block("data-chart", "chart", { sourceBlockId: "data-table", kind: "bar", xColumn: 0, yColumn: 1, title: "Water level" })
                ]),
                page("page-recipe", "section-recipes", "Camp stove flatbread", ["food", "camp"], [
                    block("recipe-1", "list", { text: "Mix the flour and salt at the picnic table.", ordered: true }),
                    block("recipe-2", "list", { text: "Add water slowly until the dough holds together.", ordered: true }),
                    block("recipe-3", "list", { text: "Cook in the covered pan, two minutes a side.", ordered: true })
                ])
            ]
        }
    }
    function save() {
        if (!session) return
        // Typing still inside the idle window reaches the model first.
        blockEditor.flushDrafts()
        const started = session.uri.length > 0 ? session.requestSave() : session.requestSaveAs(savePath, true)
        statusText = started ? qsTr("Saving to %1…").arg(savePath) : session.lastError
    }
    function openLink(href) {
        const target = notebook.resolveLink(href)
        if (!target.valid) {
            statusText = qsTr("The link target %1 no longer exists").arg(href)
            return
        }
        notebookView.navigateTo(target.pageId)
        statusText = qsTr("Opened %1").arg(notebook.page(target.pageId).title)
    }

    Tk.DocumentManager { id: manager }
    Tk.ClipboardService { id: clipboard }
    Tk.ScriptRuntime { id: runtime }
    Tk.ResourceResolver { id: resolver }
    Connections {
        target: root.session
        function onSaveFinished(success, message, savedRevision) {
            root.statusText = success ? qsTr("Saved revision %1 to %2").arg(savedRevision).arg(root.savePath)
                                      : qsTr("Save failed: %1").arg(message)
        }
    }
    Connections {
        target: root.notebook
        function onBlockEvaluated(blockId, success) {
            root.statusText = success ? qsTr("Evaluated %1").arg(blockId) : qsTr("Evaluation of %1 failed").arg(blockId)
        }
    }

    Component.onCompleted: {
        session = manager.createDocument("qindatk.notebook.v1")
        if (!session) {
            statusText = manager.lastError
            return
        }
        session.autosaveDelayMs = 600000
        notebook.resetFromJson(JSON.stringify(fixture(notebook.documentId)))
        // The photo's file is not on this machine: the block keeps its
        // logical ID and shows the relink notice.
        resolver.registerResource("field-photo", savePath.replace(/[^/]*$/, "missing-sandbar.jpg"))
        // The sketch ships next to this file: it resolves and is drawn
        // from the resolver alone (no host AssetProvider).
        resolver.registerResource("bank-sketch", String(Qt.resolvedUrl("bank-sketch.png")))
        statusText = qsTr("New notebook from the field template · unsaved")
    }

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            objectName: "notebookAppBar"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Heading { text: qsTr("Field Notes"); level: 2 }
            Tk.Badge {
                objectName: "notebookDirty"
                text: root.session && root.session.dirty ? qsTr("Unsaved") : qsTr("Saved")
                variant: root.session && root.session.dirty ? "warning" : "success"
                tooltip: qsTr("Whether the notebook has changes that are not saved")
            }
            Tk.Caption {
                objectName: "notebookHostStatus"
                text: root.statusText
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Caption {
                objectName: "notebookBacklinks"
                text: qsTr("%n link(s) to this page", "", root.backlinkCount)
                color: Tk.Theme.color.textMuted
            }
            Tk.IconButton {
                objectName: "notebookUndo"
                iconName: "undo-2"
                tooltip: root.notebook && root.notebook.canUndo ? qsTr("Undo") : qsTr("Nothing to undo")
                enabled: !!root.notebook && root.notebook.canUndo
                onClicked: root.notebook.undo()
            }
            Tk.IconButton {
                objectName: "notebookRedo"
                iconName: "redo-2"
                tooltip: qsTr("Redo")
                enabled: !!root.notebook && root.notebook.canRedo
                onClicked: root.notebook.redo()
            }
            Tk.Button {
                objectName: "notebookSave"
                text: qsTr("Save")
                iconName: "save"
                variant: "accent"
                tooltip: qsTr("Save the notebook to %1").arg(root.savePath)
                enabled: !!root.session && !root.session.saving
                onClicked: root.save()
            }
        }
        Tk.NotebookView {
            id: notebookView
            objectName: "fieldNotebook"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            notebook: root.notebook
            notebookId: root.notebook ? root.notebook.documentId : ""
            currentPageId: "page-lab-log"
            onPageOpened: function(pageId) { root.statusText = qsTr("Opened %1").arg(root.notebook.page(pageId).title) }
            Tk.BlockEditor {
                id: blockEditor
                objectName: "notebookBlockEditor"
                anchors.fill: parent
                notebook: root.notebook
                clipboard: clipboard
                scriptRuntime: runtime
                resolver: resolver
                notebookId: notebookView.notebookId
                pageId: notebookView.currentPageId
                onBlockOpenRequested: function(blockId, targetId) { root.openLink(targetId) }
                onBlockActionRequested: function(blockId, action, requestId) {
                    root.statusText = action === "choose-media" ? qsTr("Choose a file to relink %1").arg(blockId)
                                                                : qsTr("%1 requested for %2").arg(action).arg(blockId)
                }
            }
        }
    }
}
