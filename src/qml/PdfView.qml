// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The host supplies a bounded page window and PDF page AssetProvider. This
// view never parses PDF content or synthesizes text geometry.
Item {
    id: control
    property string documentId: ""
    property string resourceId: ""
    property bool resourceRegistered: true
    property var provider: null
    property var document: null
    property string viewId: ""
    property int pageCount: 0
    property int currentPage: 0 // zero-based
    property real zoom: 1.0
    property int maxVisiblePages: 7
    property var visiblePages: [] // bounded [{pageIndex,pageId,pageWidth,pageHeight,rotation,state,error}]
    property var capabilities: ({ canRender: true, canSearch: false, canSelectText: false, canRotate: false })
    property string query: ""
    property string searchStatus: "idle" // idle, running, complete, unavailable, failed
    property string searchError: ""
    property var searchResults: [] // [{id,pageIndex,snippet,bounds}]
    property var links: [] // [{id,label,target,pageIndex}]
    property string selectedSearchResultId: ""
    property string tooltip: qsTr("Browse PDF pages and search selectable text")
    readonly property var boundedPages: visiblePages.slice(0, Math.max(1, Math.min(9, maxVisiblePages)))
    readonly property var selectedResult: resultById(selectedSearchResultId)
    property int requestSerial: 1

    signal pageRequested(int pageIndex)
    signal zoomChangeRequested(real zoom)
    signal rotationRequested(int pageIndex, int deltaDegrees)
    signal searchRequested(string query, string requestId)
    signal searchResultRequested(string resultId, int pageIndex)
    signal textSelectionRequested(string resultId, int pageIndex, var bounds)
    signal linkActivatedRequested(string linkId, string target)

    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Math.max(content.implicitHeight, Tk.Theme.size.panelMinHeight * 7)
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: pageCount > 0 ? qsTr("Page %1 of %2 at %3 percent zoom")
                                               .arg(currentPage + 1).arg(pageCount).arg(Math.round(zoom * 100))
                                          : qsTr("PDF page renderer unavailable")

    function pageAt(index) {
        for (let i = 0; i < boundedPages.length; ++i)
            if (boundedPages[i].pageIndex === index) return boundedPages[i]
        return null
    }
    function resultById(id) {
        for (let i = 0; i < searchResults.length; ++i)
            if (searchResults[i].id === id) return searchResults[i]
        return null
    }
    function requestPage(index) {
        if (enabled && index >= 0 && index < pageCount) pageRequested(index)
    }
    function requestZoom(next) {
        if (enabled && isFinite(next)) zoomChangeRequested(Math.max(0.1, Math.min(8, next)))
    }
    function rotateCurrent() {
        if (enabled && capabilities.canRotate && pageCount > 0) rotationRequested(currentPage, 90)
    }
    function search() {
        if (!enabled || !capabilities.canSearch || query.trim().length === 0) return
        const requestId = documentId + ":search:" + requestSerial++
        searchRequested(query.trim(), requestId)
    }
    function showResult(result) {
        if (!enabled || !result || result.pageIndex < 0 || result.pageIndex >= pageCount) return
        selectedSearchResultId = result.id
        searchResultRequested(result.id, result.pageIndex)
        pageRequested(result.pageIndex)
    }
    function selectResultText(result) {
        if (enabled && capabilities.canSelectText && result && result.bounds)
            textSelectionRequested(result.id, result.pageIndex, result.bounds)
    }
    function activateLink(link) {
        if (enabled && link && capabilities.canFollowLinks)
            linkActivatedRequested(link.id, link.target)
    }

    Tk.Flex {
        id: content
        width: control.width > 0 ? control.width : implicitWidth
        height: control.height > 0 ? control.height : implicitHeight
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Button {
                objectName: "pdfPreviousPage"
                text: qsTr("Previous")
                iconName: "chevron-left"
                tooltip: qsTr("Show previous PDF page")
                enabled: control.currentPage > 0
                onClicked: control.requestPage(control.currentPage - 1)
            }
            Tk.Caption {
                objectName: "pdfPageCount"
                text: control.pageCount > 0 ? qsTr("%1 / %2").arg(control.currentPage + 1).arg(control.pageCount)
                                            : qsTr("No pages")
            }
            Tk.Button {
                objectName: "pdfNextPage"
                text: qsTr("Next")
                iconName: "chevron-right"
                tooltip: qsTr("Show next PDF page")
                enabled: control.currentPage + 1 < control.pageCount
                onClicked: control.requestPage(control.currentPage + 1)
            }
            Tk.Spacer { size: Tk.Theme.space.xs }
            Tk.Button {
                objectName: "pdfZoomOut"
                text: qsTr("−")
                tooltip: qsTr("Zoom out")
                enabled: control.zoom > 0.1
                onClicked: control.requestZoom(control.zoom / 1.2)
            }
            Tk.Caption { objectName: "pdfZoomValue"; text: qsTr("%1%").arg(Math.round(control.zoom * 100)); mono: true }
            Tk.Button {
                objectName: "pdfZoomIn"
                text: qsTr("+")
                tooltip: qsTr("Zoom in")
                enabled: control.zoom < 8
                onClicked: control.requestZoom(control.zoom * 1.2)
            }
            Tk.Button {
                objectName: "pdfRotate"
                text: qsTr("Rotate")
                iconName: "rotate-cw"
                tooltip: qsTr("Request clockwise rotation of the current page")
                enabled: control.capabilities.canRotate && control.pageCount > 0
                onClicked: control.rotateCurrent()
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.TextField {
                id: searchField
                objectName: "pdfSearchInput"
                text: control.query
                placeholderText: qsTr("Search PDF text")
                tooltip: qsTr("Search text through the PDF text provider")
                enabled: control.capabilities.canSearch
                onTextEdited: control.query = text
                onAccepted: control.search()
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Button {
                objectName: "pdfSearchSubmit"
                text: qsTr("Search")
                iconName: "search"
                tooltip: qsTr("Request text search")
                enabled: control.capabilities.canSearch && control.query.trim().length > 0
                onClicked: control.search()
            }
        }
        Tk.Caption {
            objectName: "pdfSearchStatus"
            visible: control.searchStatus !== "idle" || control.searchError.length > 0
            color: control.searchStatus === "failed" || control.searchStatus === "unavailable"
                   ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            text: control.searchError.length > 0 ? control.searchError
                 : qsTr("Search %1 · %2 results").arg(control.searchStatus).arg(control.searchResults.length)
            wrapMode: Text.WordWrap
        }
        Tk.Scroll {
            objectName: "pdfSearchResultsScroll"
            visible: control.searchResults.length > 0
            implicitHeight: Math.min(searchList.implicitHeight, Tk.Theme.size.menuItem * 4)
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                id: searchList
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Repeater {
                    model: control.searchResults
                    delegate: Tk.Flex {
                        required property var modelData
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.xs
                        Tk.Button {
                            objectName: "pdfSearchResult_" + modelData.id
                            text: qsTr("Page %1 · %2").arg(modelData.pageIndex + 1).arg(modelData.snippet)
                            tooltip: qsTr("Navigate to this search result")
                            variant: modelData.id === control.selectedSearchResultId ? "outline" : "ghost"
                            onClicked: control.showResult(modelData)
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                        }
                        Tk.Button {
                            objectName: "pdfSelectText_" + modelData.id
                            text: qsTr("Select text")
                            tooltip: qsTr("Request selection of matching PDF text")
                            enabled: control.capabilities.canSelectText && !!modelData.bounds
                            onClicked: control.selectResultText(modelData)
                        }
                    }
                }
            }
        }
        Tk.Flex {
            objectName: "pdfLinks"
            visible: control.links.length > 0
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Repeater {
                model: control.links.filter(function(link) { return link.pageIndex === control.currentPage })
                delegate: Tk.Button {
                    required property var modelData
                    objectName: "pdfLink_" + modelData.id
                    text: modelData.label
                    iconName: "external-link"
                    tooltip: qsTr("Follow PDF link to %1").arg(modelData.target)
                    enabled: control.capabilities.canFollowLinks
                    onClicked: control.activateLink(modelData)
                }
            }
        }
        Tk.Caption {
            objectName: "pdfRendererStatus"
            visible: !control.capabilities.canRender || control.resourceId.length === 0
            color: Tk.Theme.color.warning
            text: !control.capabilities.canRender ? qsTr("PDF rendering is unavailable for this document.")
                                                   : qsTr("No PDF resource is attached.")
            wrapMode: Text.WordWrap
        }
        Tk.Caption {
            objectName: "pdfVisiblePageLimit"
            visible: control.visiblePages.length > control.boundedPages.length
            color: Tk.Theme.color.textMuted
            text: qsTr("Showing a bounded window of %1 visible page requests.").arg(control.boundedPages.length)
        }
        Tk.Scroll {
            objectName: "pdfPageWindow"
            implicitHeight: Math.max(Tk.Theme.size.panelMinWidth * 2.4, Math.min(pageColumn.implicitHeight, Tk.Theme.size.panelMinWidth * 5))
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                id: pageColumn
                width: control.width > 0 ? control.width : implicitWidth
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.md
                Repeater {
                    model: control.boundedPages
                    delegate: Item {
                        required property var modelData
                        readonly property real sourceWidth: Math.max(1, Number(modelData.pageWidth) || 612)
                        readonly property real sourceHeight: Math.max(1, Number(modelData.pageHeight) || 792)
                        objectName: "pdfPage_" + modelData.pageId
                        implicitWidth: control.width
                        implicitHeight: Math.max(Tk.Theme.size.panelMinWidth, width * sourceHeight / sourceWidth)
                        Accessible.role: Accessible.Grouping
                        Accessible.name: qsTr("PDF page %1").arg(modelData.pageIndex + 1)

                        Tk.AssetThumbnail {
                            id: pageImage
                            objectName: "pdfPageRender_" + modelData.pageIndex
                            anchors.fill: parent
                            provider: control.provider
                            resourceId: control.resourceId
                            requestKind: "pdf-page"
                            requestOptions: ({ pageIndex: modelData.pageIndex,
                                               rotation: modelData.rotation || 0,
                                               size: { width: Math.max(1, Math.ceil(pageImage.width)),
                                                       height: Math.max(1, Math.ceil(pageImage.height)) } })
                            document: control.document
                            viewId: control.viewId
                            tooltip: qsTr("Rendered PDF page %1").arg(modelData.pageIndex + 1)
                            active: control.resourceRegistered && control.capabilities.canRender
                                    && modelData.pageIndex >= 0
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.width: Tk.Theme.size.border
                            border.color: modelData.pageIndex === control.currentPage
                                          ? Tk.Theme.color.accent : Tk.Theme.color.border
                            Accessible.ignored: true
                        }
                        Tk.Caption {
                            objectName: "pdfPageStatus_" + modelData.pageId
                            anchors.left: parent.left
                            anchors.bottom: parent.bottom
                            anchors.margins: Tk.Theme.space.xs
                            visible: (modelData.state && modelData.state !== "ready") || pageImage.assetState === "failed"
                            text: modelData.error || pageImage.assetError || modelData.state || pageImage.assetState
                            color: Tk.Theme.color.warning
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
