// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A real two-page PDF fixture plus host-owned search/annotation snapshots.
// Page parsing, render tiles, selection geometry and annotation storage are
// supplied by the PDF provider and document host.
Rectangle {
    id: root
    width: 1360
    height: 900
    color: Tk.Theme.color.bg
    readonly property string resourceId: "qindatk.example.pdf-review"
    property string statusText: qsTr("PDF annotations are review requests; accepted changes still require a writer backend.")
    property bool pdfResourceRegistered: false
    property string activeAnnotationTool: "highlight"
    property string selectedAnnotationId: "annot-22"
    property int currentPage: 0
    property real zoom: 0.78
    property string searchState: "idle"
    property string searchError: ""
    property string activeSearchRequestId: ""
    property var pendingTextRequests: []
    property var searchMatches: []
    property var pageRotations: [0, 90]
    property var selectedText: ({ pageIndex: 0, text: "reversible native PDF annotations",
                                  bounds: ({ x: 72, y: 690, width: 222, height: 16 }) })
    property var annotations: [
        { id: "annot-17", pageIndex: 0, kind: "highlight", text: "Important finding",
          author: "A. Rivera", status: "accepted", bounds: ({ x: 72, y: 660, width: 240, height: 16 }) },
        { id: "annot-22", pageIndex: 1, kind: "note", text: "Confirm this wording with reviewer.",
          author: "M. Chen", status: "pending", bounds: ({ x: 72, y: 500, width: 180, height: 20 }),
          canAccept: true, canReject: true }
    ]
    property var documentLinks: [
        { id: "link-3", pageIndex: 0, label: qsTr("Project glossary"), target: "https://example.invalid/glossary" }
    ]
    function visiblePageWindow() {
        const pages = []
        const first = Math.max(0, currentPage - 1)
        const last = Math.min(1, currentPage + 1)
        for (let index = first; index <= last; ++index)
            pages.push({ pageIndex: index, pageId: "pdf-page-" + (index + 1),
                         pageWidth: index === 0 ? 612 : 792, pageHeight: index === 0 ? 792 : 612,
                         rotation: pageRotations[index], state: "loading", error: "" })
        return pages
    }
    function updateAnnotation(id, patch) {
        const next = annotations.map(function(item) { return Object.assign({}, item) })
        const index = next.findIndex(function(item) { return item.id === id })
        if (index >= 0) { Object.assign(next[index], patch); annotations = next }
    }
    function releaseTextRequests() {
        for (let i = 0; i < pendingTextRequests.length; ++i)
            assets.release(pendingTextRequests[i].subscriptionId)
        pendingTextRequests = []
    }
    function consumeTextRequest(subscriptionId) {
        const requestIndex = pendingTextRequests.findIndex(function(item) {
            return item.subscriptionId === subscriptionId
        })
        if (requestIndex < 0) return
        const request = pendingTextRequests[requestIndex]
        const snapshot = assets.asset(subscriptionId)
        if (!snapshot || snapshot.state === "loading") return
        const nextPending = pendingTextRequests.slice()
        nextPending.splice(requestIndex, 1)
        pendingTextRequests = nextPending
        if (request.requestId === activeSearchRequestId) {
            if (snapshot.state === "ready") {
                const metadata = (snapshot.data || ({})).metadata || ({})
                const sourceText = String(metadata.text || "")
                const query = request.query.toLocaleLowerCase()
                const offset = sourceText.toLocaleLowerCase().indexOf(query)
                if (offset >= 0) {
                    const words = metadata.words || []
                    const firstTerm = query.split(/\s+/)[0]
                    let bounds = null
                    for (let i = 0; i < words.length; ++i) {
                        if (String(words[i].text || "").toLocaleLowerCase().indexOf(firstTerm) >= 0) {
                            bounds = words[i].rectPoints || null
                            break
                        }
                    }
                    const start = Math.max(0, offset - 28)
                    const end = Math.min(sourceText.length, offset + request.query.length + 44)
                    searchMatches = searchMatches.concat([{ id: request.requestId + ":page:" + request.pageIndex + ":" + offset,
                        pageIndex: request.pageIndex,
                        snippet: sourceText.slice(start, end).replace(/\s+/g, " ").trim(), bounds: bounds }])
                }
            } else {
                searchError = snapshot.error || qsTr("PDF text could not be read from page %1.").arg(request.pageIndex + 1)
            }
            if (!pendingTextRequests.some(function(item) { return item.requestId === activeSearchRequestId })) {
                searchState = searchError.length > 0 && searchMatches.length === 0 ? "failed" : "complete"
                statusText = qsTr("PDF text search returned %1 result(s).").arg(searchMatches.length)
            }
        }
        assets.release(subscriptionId)
    }
    function startTextSearch(query, requestId) {
        releaseTextRequests()
        activeSearchRequestId = requestId
        searchMatches = []
        searchError = ""
        searchState = "running"
        statusText = qsTr("Reading text from PDF pages…")
        if (!pdfResourceRegistered) {
            searchState = "failed"
            searchError = qsTr("PDF resource is not registered.")
            return
        }
        const createdIds = []
        for (let pageIndex = 0; pageIndex < 2; ++pageIndex) {
            const subscriptionId = assets.requestAsset(resourceId, "pdf-text", { pageIndex: pageIndex },
                                                       null, "pdf-review-search")
            if (!subscriptionId || subscriptionId.length === 0) {
                searchError = assets.lastError || qsTr("PDF text request could not start.")
                continue
            }
            pendingTextRequests = pendingTextRequests.concat([{ subscriptionId: subscriptionId,
                pageIndex: pageIndex, query: query, requestId: requestId }])
            createdIds.push(subscriptionId)
        }
        for (let i = 0; i < createdIds.length; ++i) consumeTextRequest(createdIds[i])
        if (pendingTextRequests.length === 0) {
            searchState = searchError.length > 0 ? "failed" : "complete"
            if (searchError.length > 0) statusText = searchError
        }
    }

    Tk.ResourceResolver {
        id: resolver
    }
    Tk.AssetProvider {
        id: assets
        resolver: resolver
    }
    Component.onCompleted: {
        root.pdfResourceRegistered = resolver.registerResource(root.resourceId, Qt.resolvedUrl("fixture.pdf"))
        if (!root.pdfResourceRegistered) root.statusText = resolver.lastError
    }
    Component.onDestruction: releaseTextRequests()

    Connections {
        target: assets
        function onAssetChanged(subscriptionId) { root.consumeTextRequest(subscriptionId) }
    }

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Heading { text: qsTr("PDF reader and annotations"); level: 2; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { text: root.statusText; wrapMode: Text.WordWrap }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.md
            Tk.Panel {
                objectName: "pdfViewHost"
                title: qsTr("Paper Lantern Guide · 2 pages")
                Tk.PdfView {
                    id: viewer
                    objectName: "pdfView"
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    documentId: root.resourceId
                    resourceId: root.resourceId
                    resourceRegistered: root.pdfResourceRegistered
                    provider: assets
                    viewId: "pdf-review-main"
                    pageCount: 2
                    currentPage: root.currentPage
                    zoom: root.zoom
                    maxVisiblePages: 3
                    visiblePages: root.visiblePageWindow()
                    searchStatus: root.searchState
                    searchError: root.searchError
                    searchResults: root.searchMatches
                    links: root.documentLinks
                    capabilities: ({ canRender: true, canSearch: true, canSelectText: true, canRotate: true, canFollowLinks: true })
                    onPageRequested: function(index) { root.currentPage = index }
                    onZoomChangeRequested: function(value) { root.zoom = value }
                    onRotationRequested: function(pageIndex, delta) {
                        const next = root.pageRotations.slice()
                        next[pageIndex] = (next[pageIndex] + delta) % 360
                        root.pageRotations = next
                        root.statusText = qsTr("Rotation request acknowledged for page %1.").arg(pageIndex + 1)
                    }
                    onSearchRequested: function(query, requestId) {
                        root.startTextSearch(query, requestId)
                    }
                    onSearchResultRequested: function(id, index) {
                        root.statusText = qsTr("Search result %1 is on page %2.").arg(id).arg(index + 1)
                    }
                    onLinkActivatedRequested: function(id, target) {
                        root.statusText = qsTr("Link %1 requests navigation to %2.").arg(id).arg(target)
                    }
                    onTextSelectionRequested: function(id, index, bounds) {
                        const result = root.searchMatches.find(function(item) { return item.id === id })
                        root.selectedText = { pageIndex: index, text: result ? result.snippet : "", bounds: bounds }
                        root.statusText = qsTr("Text selection requested for %1 on page %2.").arg(id).arg(index + 1)
                    }
                }
            }
            Tk.Panel {
                objectName: "pdfAnnotationHost"
                title: qsTr("Annotation review")
                implicitWidth: Tk.Theme.size.panelMinWidth * 2.5
                Tk.PdfAnnotationTools {
                    id: annotationTools
                    objectName: "pdfAnnotationTools"
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    documentId: root.resourceId
                    pageIndex: root.selectedText.pageIndex
                    selectedText: root.selectedText.text
                    selectedBounds: root.selectedText.bounds
                    annotations: root.annotations
                    selectedAnnotationId: root.selectedAnnotationId
                    activeTool: root.activeAnnotationTool
                    capabilities: ({ annotationKinds: ["highlight", "underline", "note", "redaction-proposal"],
                                    canAccept: true, canReject: true, canEditForms: false, canApplyRedaction: false })
                    onAnnotationSelectionRequested: function(id) { root.selectedAnnotationId = id }
                    onToolChangeRequested: function(kind) { root.activeAnnotationTool = kind }
                    onAnnotationCreateRequested: function(requestId, index, kind, bounds, text, note) {
                        const next = annotations.map(function(item) { return Object.assign({}, item) })
                        const id = "annot-" + (next.length + 20)
                        next.push({ id: id, pageIndex: index, kind: kind, text: note || text,
                                    author: "You", status: "pending", bounds: bounds,
                                    canAccept: true, canReject: true, requestId: requestId })
                        root.annotations = next
                        root.selectedAnnotationId = id
                        root.statusText = qsTr("Annotation request %1 awaits review as %2.").arg(requestId).arg(id)
                    }
                    onAnnotationAcceptRequested: function(id) {
                        root.updateAnnotation(id, { status: "accepted" })
                        root.statusText = qsTr("Accepted %1 in the host-owned review model.").arg(id)
                    }
                    onAnnotationRejectRequested: function(id) {
                        root.updateAnnotation(id, { status: "rejected" })
                        root.statusText = qsTr("Rejected %1 in the host-owned review model.").arg(id)
                    }
                    onAnnotationNavigateRequested: function(id, index) {
                        root.currentPage = index
                        root.statusText = qsTr("Navigate to annotation %1 on page %2.").arg(id).arg(index + 1)
                    }
                    onAnnotationDeleteRequested: function(id) {
                        root.annotations = root.annotations.filter(function(item) { return item.id !== id })
                        root.statusText = qsTr("Annotation removal accepted for %1.").arg(id)
                    }
                }
            }
        }
    }
}
