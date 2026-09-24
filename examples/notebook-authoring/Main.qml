// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Local host-owned snapshots demonstrate notebook structure and block requests.
Rectangle {
    id: root
    width: 1520
    height: 960
    color: Tk.Theme.color.bg
    property string notebookId: "notebook-field-notes"
    property string currentPageId: "page-lab-log"
    property string selectedBlockId: "lab-intro"
    property string focusedBlockId: ""
    property string statusText: qsTr("Field Notes · local host model · unsaved demo changes")
    property var historyBack: []
    property var historyForward: []
    property var searchResultIds: []
    property string searchState: "idle"
    property var sections: [
        { id: "section-field", title: "Field journal", collapsed: false },
        { id: "section-research", title: "Research", collapsed: false },
        { id: "section-recipes", title: "Recipes", collapsed: false }
    ]
    property var pages: [
        { id: "page-start", sectionId: "section-field", title: "Start here", tags: ["welcome", "guide"], excerpt: "How to use these field notes", updatedText: "Updated today", status: "ready" },
        { id: "page-lab-log", sectionId: "section-field", title: "Tidal lab · day 4", tags: ["estuary", "fieldwork", "important"], excerpt: "A morning at the east inlet", updatedText: "Updated 8 minutes ago", status: "attention" },
        { id: "page-sketches", sectionId: "section-field", title: "Bank sketches", tags: ["ink", "estuary"], excerpt: "Shoreline observations", updatedText: "Updated yesterday", status: "ready" },
        { id: "page-citations", sectionId: "section-research", title: "Sources and quotations", tags: ["references"], excerpt: "Reports, papers and quotations", updatedText: "Updated Tuesday", status: "ready" },
        { id: "page-data", sectionId: "section-research", title: "Sample measurements", tags: ["data", "estuary"], excerpt: "Water level and salinity readings", updatedText: "Updated Monday", status: "ready" },
        { id: "page-recipe", sectionId: "section-recipes", title: "Camp stove flatbread", tags: ["food", "camp"], excerpt: "A quick meal after the walk", updatedText: "Updated last week", status: "ready" }
    ]
    property var blocksByPage: ({
        "page-start": [
            { id: "start-heading", type: "heading", data: ({ text: "A notebook for the field", level: 1 }) },
            { id: "start-copy", type: "text", data: ({ text: "Keep observations, sources, sketches and measurements together. Every page and block keeps a stable identity as the notebook changes." }) },
            { id: "start-embed", type: "embed", data: ({ targetId: "map-estuary", title: "Estuary map", description: "Linked map layer · last synced yesterday", state: "offline" }) }
        ],
        "page-lab-log": [
            { id: "lab-heading", type: "heading", data: ({ text: "East inlet · morning tide", level: 2 }) },
            { id: "lab-intro", type: "text", data: ({ text: "The upper bank is still soft after last night's rain. Three terns were feeding where the creek meets the sandbar." }) },
            { id: "lab-detail", type: "text", data: ({ text: "Water cleared quickly after the first ebb. The reed edge is narrower than it was in spring; mark a second transect before the next survey." }) },
            { id: "lab-code", type: "code", data: ({ language: "python", text: "salinity = [18.2, 18.7, 19.1]\nprint(sum(salinity) / len(salinity))" }) },
            { id: "lab-table", type: "table", data: ({ columns: ["Time", "Level", "Salinity"], rows: [["06:40", "1.8 m", "18.2"], ["07:15", "1.6 m", "18.7"], ["07:50", "1.4 m", "19.1"]] }) },
            { id: "lab-photo", type: "image", data: ({ resourceId: "field-photo-missing", caption: "Missing source image · relink from host", state: "missing" }), error: "The original media file is offline." },
            { id: "lab-map", type: "embed", data: ({ targetId: "map-estuary", title: "Survey map", description: "Interactive layer unavailable while offline", state: "offline" }) },
            { id: "lab-ink", type: "ink", data: ({ strokes: [{ id: "stroke-1" }, { id: "stroke-2" }], state: "preview unavailable" }) }
        ],
        "page-sketches": [
            { id: "sketch-text", type: "text", data: ({ text: "The sandbar curves toward the old pier. Add the north bank marks after the next walk." }) },
            { id: "sketch-ink", type: "ink", data: ({ strokes: [], state: "No strokes yet" }) }
        ],
        "page-citations": [
            { id: "cite-note", type: "text", data: ({ text: "Sources are attached by stable IDs so that notes keep their target when sections are reorganized." }) },
            { id: "cite-table", type: "table", data: ({ columns: ["Source", "Year"], rows: [["Coastal Survey", "2024"], ["Tide Atlas", "2023"]] }) }
        ],
        "page-data": [{ id: "data-code", type: "code", data: ({ language: "csv", text: "time,level,salinity\n06:40,1.8,18.2\n07:15,1.6,18.7" }) }],
        "page-recipe": [{ id: "recipe-text", type: "text", data: ({ text: "Mix the dry ingredients at the picnic table, add water slowly, then cook the flatbread in the covered pan." }) }]
    })

    function pageById(id) { return pages.find(function(page) { return page.id === id }) || null }
    function currentBlocks() { return blocksByPage[currentPageId] || [] }
    function replacePage(id, transform) {
        const next = pages.map(function(page) { return Object.assign({}, page, { tags: (page.tags || []).slice() }) })
        const index = next.findIndex(function(page) { return page.id === id })
        if (index >= 0) { next[index] = transform(next[index]); pages = next }
    }
    function openPage(id, recordHistory) {
        if (!pageById(id)) return
        if (recordHistory !== false && id !== currentPageId) { historyBack = historyBack.concat([currentPageId]); historyForward = [] }
        currentPageId = id
        focusedBlockId = ""
        const blocks = currentBlocks()
        selectedBlockId = blocks.length ? blocks[0].id : ""
        statusText = qsTr("Opened %1 · %2 blocks").arg(pageById(id).title).arg(blocks.length)
    }
    function moveBlock(id, targetIndex) {
        const next = currentBlocks().slice(), sourceIndex = next.findIndex(function(block) { return block.id === id })
        if (sourceIndex < 0 || targetIndex < 0 || targetIndex >= next.length) return
        const moved = next.splice(sourceIndex, 1)[0]; next.splice(targetIndex, 0, moved)
        blocksByPage = Object.assign({}, blocksByPage, { [currentPageId]: next })
        statusText = qsTr("Moved block %1 to position %2").arg(id).arg(targetIndex + 1)
    }
    function updateBlock(id, transform) {
        const next = currentBlocks().map(function(block) { return Object.assign({}, block, { data: Object.assign({}, block.data || ({})) }) })
        const index = next.findIndex(function(block) { return block.id === id })
        if (index >= 0) { next[index] = transform(next[index]); blocksByPage = Object.assign({}, blocksByPage, { [currentPageId]: next }) }
    }
    function createPage(afterId, sectionId, requestId) {
        const newPageId = "page-" + Date.now()
        const page = { id: newPageId, sectionId: sectionId, title: "New page", tags: [], excerpt: "", updatedText: "Just created", status: "ready" }
        const next = pages.slice(), afterIndex = next.findIndex(function(item) { return item.id === afterId })
        next.splice(afterIndex < 0 ? next.length : afterIndex + 1, 0, page)
        pages = next; blocksByPage = Object.assign({}, blocksByPage, { [newPageId]: [] }); openPage(newPageId)
        statusText = qsTr("Created page for request %1").arg(requestId)
    }

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Heading { text: qsTr("Field Notes"); level: 2; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { objectName: "notebookHostStatus"; text: root.statusText }
        }
        Tk.NotebookView {
            id: notebook
            objectName: "fieldNotebook"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            notebookId: root.notebookId
            notebookTitle: qsTr("Estuary field journal")
            sections: root.sections
            pages: root.pages
            currentPageId: root.currentPageId
            historyBackPageIds: root.historyBack
            historyForwardPageIds: root.historyForward
            searchResultPageIds: root.searchResultIds
            searchState: root.searchState
            onPageSelectedRequested: function(id) { root.openPage(id) }
            onHistoryNavigationRequested: function(id) {
                if (root.historyBack.length > 0 && id === root.historyBack[root.historyBack.length - 1]) {
                    root.historyBack = root.historyBack.slice(0, -1); root.historyForward = [root.currentPageId].concat(root.historyForward)
                } else if (root.historyForward.length > 0) {
                    root.historyForward = root.historyForward.slice(1); root.historyBack = root.historyBack.concat([root.currentPageId])
                }
                root.openPage(id, false)
            }
            onSectionCollapseRequested: function(id, collapsed) {
                root.sections = root.sections.map(function(item) { return item.id === id ? Object.assign({}, item, { collapsed: collapsed }) : item })
            }
            onSectionInsertRequested: function(title, requestId) {
                const id = "section-" + Date.now(); root.sections = root.sections.concat([{ id: id, title: title, collapsed: false }])
                root.statusText = qsTr("Created section %1 · %2").arg(title).arg(requestId)
            }
            onPageInsertRequested: function(sectionId, afterPageId, requestId) { root.createPage(afterPageId, sectionId, requestId) }
            onPageMoveRequested: function(pageId, sectionId, targetIndex, requestId) {
                root.replacePage(pageId, function(page) { page.sectionId = sectionId; return page })
                root.statusText = qsTr("Moved page %1 to section %2 · %3").arg(pageId).arg(sectionId).arg(requestId)
            }
            onPageOrderChangeRequested: function(pageId, targetIndex, requestId) {
                const next = root.pages.slice(), source = next.findIndex(function(page) { return page.id === pageId })
                const moved = next.splice(source, 1)[0]; next.splice(targetIndex, 0, moved); root.pages = next
                root.statusText = qsTr("Reordered page %1 · %2").arg(pageId).arg(requestId)
            }
            onPageDeleteRequested: function(id) {
                root.pages = root.pages.filter(function(page) { return page.id !== id })
                const nextBlocks = Object.assign({}, root.blocksByPage); delete nextBlocks[id]; root.blocksByPage = nextBlocks
                if (root.currentPageId === id) root.openPage(root.pages[Math.max(0, notebook.currentIndex - 1)].id)
                root.statusText = qsTr("Deleted page %1").arg(id)
            }
            onTagEditRequested: function(id, tags, requestId) { root.replacePage(id, function(page) { page.tags = tags; return page }); root.statusText = qsTr("Updated tags · %1").arg(requestId) }
            onSearchRequested: function(query, requestId) {
                const term = query.toLocaleLowerCase()
                root.searchResultIds = term.length ? root.pages.filter(function(page) {
                    const text = (page.title + " " + page.excerpt + " " + (page.tags || []).join(" ")).toLocaleLowerCase()
                    return text.indexOf(term) >= 0
                }).map(function(page) { return page.id }) : []
                root.searchState = "complete"
                root.statusText = qsTr("Metadata search %1 returned %2 pages").arg(requestId).arg(root.searchResultIds.length)
            }
            Tk.BlockEditor {
                objectName: "notebookBlockEditor"
                anchors.fill: parent
                notebookId: root.notebookId
                pageId: root.currentPageId
                blocks: root.currentBlocks()
                selectedBlockId: root.selectedBlockId
                focusedBlockId: root.focusedBlockId
                onBlockSelectionRequested: function(id) { root.selectedBlockId = id }
                onBlockFocusRequested: function(id) { root.focusedBlockId = id; root.selectedBlockId = id }
                onBlockNavigateRequested: function(id, direction) {
                    const index = root.currentBlocks().findIndex(function(item) { return item.id === id }), next = index + (direction === "next" ? 1 : -1)
                    if (next >= 0 && next < root.currentBlocks().length) root.focusedBlockId = root.currentBlocks()[next].id
                }
                onBlockInsertRequested: function(afterId, type, data, requestId) {
                    const id = "block-" + Date.now(), next = root.currentBlocks().slice(), index = next.findIndex(function(item) { return item.id === afterId })
                    next.splice(index < 0 ? next.length : index + 1, 0, { id: id, type: type, data: data })
                    root.blocksByPage = Object.assign({}, root.blocksByPage, { [root.currentPageId]: next }); root.selectedBlockId = id
                    root.statusText = qsTr("Inserted %1 block · %2").arg(type).arg(requestId)
                }
                onBlockMoveRequested: function(id, targetIndex, requestId) { root.moveBlock(id, targetIndex); root.statusText = qsTr("Block reordered · %1").arg(requestId) }
                onBlockContentEditRequested: function(id, data, requestId) {
                    root.updateBlock(id, function(block) { block.data = data; return block }); root.statusText = qsTr("Edited block %1 · %2").arg(id).arg(requestId)
                }
                onBlockSplitRequested: function(id, cursor, requestId) {
                    const next = root.currentBlocks().map(function(block) { return Object.assign({}, block, { data: Object.assign({}, block.data || ({})) }) })
                    const index = next.findIndex(function(block) { return block.id === id }), source = next[index], text = String(source.data.text || "")
                    const offset = Math.max(0, Math.min(text.length, cursor)), right = Object.assign({}, source, { id: "block-" + Date.now(), data: Object.assign({}, source.data, { text: text.slice(offset).trimStart() }) })
                    source.data.text = text.slice(0, offset).trimEnd(); next.splice(index + 1, 0, right)
                    root.blocksByPage = Object.assign({}, root.blocksByPage, { [root.currentPageId]: next }); root.selectedBlockId = right.id
                    root.statusText = qsTr("Split at character %1 · %2").arg(offset).arg(requestId)
                }
                onBlockMergeRequested: function(firstId, secondId, requestId) {
                    const next = root.currentBlocks().map(function(block) { return Object.assign({}, block, { data: Object.assign({}, block.data || ({})) }) })
                    const left = next.findIndex(function(block) { return block.id === firstId }), right = next.findIndex(function(block) { return block.id === secondId })
                    if (left >= 0 && right === left + 1) { next[left].data.text += " " + next[right].data.text; next.splice(right, 1); root.blocksByPage = Object.assign({}, root.blocksByPage, { [root.currentPageId]: next }); root.selectedBlockId = firstId }
                    root.statusText = qsTr("Merged blocks · %1").arg(requestId)
                }
                onBlockDeleteRequested: function(id) {
                    root.blocksByPage = Object.assign({}, root.blocksByPage, { [root.currentPageId]: root.currentBlocks().filter(function(block) { return block.id !== id }) })
                    root.selectedBlockId = root.currentBlocks().length ? root.currentBlocks()[0].id : ""
                }
                onBlockActionRequested: function(id, action, requestId) {
                    root.statusText = action === "choose-media" ? qsTr("Host media picker requested · %1").arg(requestId)
                                    : qsTr("Open structured table editor for %1 · %2").arg(id).arg(requestId)
                }
                onBlockOpenRequested: function(id, targetId) { root.statusText = qsTr("Open linked target %1 from %2").arg(targetId).arg(id) }
            }
        }
    }
}
