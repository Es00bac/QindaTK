// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Navigation and search results are host snapshots addressed by stable IDs.
Item {
    id: control
    property string notebookId: ""
    property string notebookTitle: ""
    property var sections: [] // [{id,title,collapsed}]
    property var pages: [] // [{id,sectionId,title,tags,excerpt,status,updatedText}]
    property string currentPageId: ""
    property string focusedSectionId: ""
    property string query: ""
    property var searchResultPageIds: [] // host search snapshot; empty means local metadata filter
    property string searchState: "idle"
    property string errorText: ""
    property var historyBackPageIds: []
    property var historyForwardPageIds: []
    property var capabilities: ({ canEditStructure: true, canSearch: true, canNavigate: true })
    property string tooltip: qsTr("Navigate notebook sections and pages")
    default property alias pageContent: pageContentSlot.data
    readonly property var currentPage: findPage(currentPageId)
    readonly property int currentIndex: pageIndex(currentPageId)
    readonly property var visiblePages: filteredPages()

    signal pageSelectedRequested(string pageId)
    signal sectionSelectionRequested(string sectionId)
    signal sectionCollapseRequested(string sectionId, bool collapsed)
    signal sectionInsertRequested(string title, string requestId)
    signal pageInsertRequested(string sectionId, string afterPageId, string requestId)
    signal pageMoveRequested(string pageId, string targetSectionId, int targetIndex, string requestId)
    signal pageDeleteRequested(string pageId)
    signal tagEditRequested(string pageId, var tags, string requestId)
    signal searchRequested(string query, string requestId)
    signal historyNavigationRequested(string pageId)
    signal pageOrderChangeRequested(string pageId, int targetIndex, string requestId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 5
    implicitHeight: Tk.Theme.size.panelMinHeight * 3
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: qsTr("%1 sections, %2 pages").arg(sections.length).arg(pages.length)

    function findPage(id) {
        for (let i = 0; i < pages.length; ++i) if (pages[i].id === id) return pages[i]
        return null
    }
    function pageIndex(id) {
        for (let i = 0; i < pages.length; ++i) if (pages[i].id === id) return i
        return -1
    }
    function findSection(id) {
        for (let i = 0; i < sections.length; ++i) if (sections[i].id === id) return sections[i]
        return null
    }
    function pagesInSection(sectionId) {
        return filteredPages().filter(function(page) { return page.sectionId === sectionId })
    }
    function filteredPages() {
        const queryText = query.trim().toLocaleLowerCase()
        return pages.filter(function(page) {
            if (queryText.length === 0) return true
            // Indexed content hits need not contain the query in their title,
            // tags or excerpt. A completed host search is authoritative,
            // including the empty-result case.
            if (searchState === "complete") return searchResultPageIds.indexOf(page.id) >= 0
            const metadata = (String(page.title || "") + " " + String(page.excerpt || "") + " " + (page.tags || []).join(" ")).toLocaleLowerCase()
            return metadata.indexOf(queryText) >= 0
        })
    }
    function navigateTo(id) {
        if (enabled && capabilities.canNavigate && findPage(id)) pageSelectedRequested(id)
    }
    function submitSearch() {
        if (enabled && capabilities.canSearch) searchRequested(query.trim(), notebookId + ":search:" + Date.now())
    }
    function moveCurrent(delta) {
        const target = currentIndex + delta
        if (capabilities.canEditStructure && target >= 0 && target < pages.length)
            pageOrderChangeRequested(currentPageId, target, notebookId + ":page-order:" + Date.now())
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Flex {
            objectName: "notebookToolbar"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Heading { text: control.notebookTitle || qsTr("Notebook"); level: 2; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Button {
                objectName: "notebookBack"
                text: qsTr("Back")
                iconName: "arrow-left"
                tooltip: qsTr("Return to the previous page")
                enabled: control.historyBackPageIds.length > 0
                onClicked: control.historyNavigationRequested(control.historyBackPageIds[control.historyBackPageIds.length - 1])
            }
            Tk.Button {
                objectName: "notebookForward"
                text: qsTr("Forward")
                iconName: "arrow-right"
                tooltip: qsTr("Go forward to the next page in history")
                enabled: control.historyForwardPageIds.length > 0
                onClicked: control.historyNavigationRequested(control.historyForwardPageIds[0])
            }
            Tk.TextField {
                id: searchField
                objectName: "notebookSearch"
                text: control.query
                placeholderText: qsTr("Search notebook")
                tooltip: qsTr("Search page titles, tags and request indexed content from the host")
                enabled: control.capabilities.canSearch
                onTextEdited: control.query = text
                onAccepted: control.submitSearch()
            }
            Tk.Button {
                objectName: "notebookSearchSubmit"
                text: qsTr("Search")
                iconName: "search"
                tooltip: qsTr("Search notebook")
                enabled: control.capabilities.canSearch
                onClicked: control.submitSearch()
            }
        }
        Tk.Caption {
            objectName: "notebookStatus"
            visible: control.errorText.length > 0 || control.searchState !== "idle"
            text: control.errorText.length > 0 ? control.errorText
                  : control.searchState === "running" ? qsTr("Searching notebook…")
                  : qsTr("Search %1 · %2 pages").arg(control.searchState).arg(control.visiblePages.length)
            color: control.errorText.length > 0 ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Panel {
                objectName: "notebookNavigationPanel"
                title: qsTr("Contents · %1 pages").arg(control.pages.length)
                implicitWidth: Tk.Theme.size.panelMinWidth * 1.7
                Tk.Flex.shrink: 0
                Tk.Flex {
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.xs
                        Tk.TextField {
                            id: newSectionTitle
                            objectName: "notebookSectionTitle"
                            placeholderText: qsTr("New section")
                            tooltip: qsTr("Name for a new notebook section")
                            enabled: control.capabilities.canEditStructure
                            Tk.Flex.grow: 1
                            Tk.Flex.basis: 0
                        }
                        Tk.IconButton {
                            objectName: "notebookInsertSection"
                            iconName: "folder-plus"
                            tooltip: qsTr("Create a notebook section")
                            enabled: control.capabilities.canEditStructure && newSectionTitle.text.trim().length > 0
                            onClicked: {
                                control.sectionInsertRequested(newSectionTitle.text.trim(), control.notebookId + ":section:" + Date.now())
                                newSectionTitle.text = ""
                            }
                        }
                    }
                    Tk.Scroll {
                        objectName: "notebookOutlineScroll"
                        overflowY: Tk.Scroll.Auto
                        Tk.Flex {
                            direction: Tk.Flex.Column
                            align: Tk.Flex.Stretch
                            gap: Tk.Theme.space.sm
                            Repeater {
                                model: control.sections
                                delegate: Tk.Flex {
                                    required property var modelData
                                    objectName: "notebookSection_" + modelData.id
                                    direction: Tk.Flex.Column
                                    gap: Tk.Theme.space.xs
                                    Tk.Button {
                                        objectName: "notebookSectionHeader_" + modelData.id
                                        text: qsTr("%1 · %2").arg(modelData.title).arg(control.pagesInSection(modelData.id).length)
                                        iconName: modelData.collapsed === true ? "chevron-right" : "chevron-down"
                                        variant: "ghost"
                                        tooltip: qsTr("Expand or collapse section %1").arg(modelData.title)
                                        onClicked: control.sectionCollapseRequested(modelData.id, modelData.collapsed !== true)
                                        Tk.Flex.shrink: 0
                                    }
                                    Repeater {
                                        model: modelData.collapsed ? [] : control.pagesInSection(modelData.id)
                                        delegate: Tk.Box {
                                            required property var modelData
                                            objectName: "notebookPageRow_" + modelData.id
                                            color: control.currentPageId === modelData.id ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
                                            borderColor: control.currentPageId === modelData.id ? Tk.Theme.color.accent : Tk.Theme.alpha(Tk.Theme.color.border, Tk.Theme.opacity.hidden)
                                            radius: Tk.Theme.radius.sm
                                            padding: Tk.Theme.space.xs
                                            implicitHeight: Tk.Theme.size.control * 2 + Tk.Theme.space.xs
                                            Accessible.role: Accessible.ListItem
                                            Accessible.name: qsTr("%1, section %2").arg(modelData.title).arg(modelData.sectionId)
                                            Accessible.selected: control.currentPageId === modelData.id
                                            Tk.Flex {
                                                anchors.fill: parent
                                                direction: Tk.Flex.Row
                                                align: Tk.Flex.Center
                                                gap: Tk.Theme.space.xs
                                                Tk.Button {
                                                    objectName: "notebookPageSelect_" + modelData.id
                                                    text: modelData.title
                                                    iconName: modelData.status === "attention" ? "alert-circle" : "file-text"
                                                    variant: control.currentPageId === modelData.id ? "accent" : "ghost"
                                                    tooltip: qsTr("Open page %1").arg(modelData.title)
                                                    onClicked: control.navigateTo(modelData.id)
                                                    Tk.Flex.grow: 1
                                                    Tk.Flex.basis: 0
                                                }
                                                Tk.IconButton {
                                                    objectName: "notebookPageAddAfter_" + modelData.id
                                                    iconName: "plus"
                                                    tooltip: qsTr("Insert a page after %1").arg(modelData.title)
                                                    enabled: control.capabilities.canEditStructure
                                                    onClicked: control.pageInsertRequested(modelData.sectionId, modelData.id, control.notebookId + ":page:" + Date.now())
                                                }
                                            }
                                            TapHandler { onTapped: control.navigateTo(modelData.id) }
                                        }
                                    }
                                    Tk.Button {
                                        objectName: "notebookPageAdd_" + modelData.id
                                        visible: modelData.collapsed === true || control.pagesInSection(modelData.id).length === 0
                                        text: qsTr("Add page")
                                        iconName: "plus"
                                        tooltip: qsTr("Insert a page in %1").arg(modelData.title)
                                        enabled: control.capabilities.canEditStructure
                                        onClicked: control.pageInsertRequested(modelData.id, "", control.notebookId + ":page:" + Date.now())
                                    }
                                }
                            }
                            Tk.Caption { visible: control.sections.length === 0; text: qsTr("No sections yet.") }
                        }
                    }
                }
            }
            Tk.Panel {
                objectName: "notebookPagePanel"
                title: control.currentPage ? control.currentPage.title : qsTr("Choose a page")
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex {
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    Tk.Flex {
                        objectName: "notebookPageHeader"
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.xs
                        Tk.Caption { text: control.currentPage ? control.currentPage.updatedText || qsTr("Recently updated") : qsTr("No page selected"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                        Tk.ComboBox {
                            id: moveSectionPicker
                            objectName: "notebookMoveSection"
                            model: control.sections
                            textRole: "title"
                            currentIndex: {
                                if (!control.currentPage) return -1
                                for (let i = 0; i < control.sections.length; ++i)
                                    if (control.sections[i].id === control.currentPage.sectionId) return i
                                return -1
                            }
                            tooltip: qsTr("Move current page to another section")
                            enabled: !!control.currentPage && control.capabilities.canEditStructure
                            onActivated: function(index) {
                                const target = control.sections[index]
                                control.pageMoveRequested(control.currentPageId, target.id, control.pagesInSection(target.id).length,
                                                          control.notebookId + ":page-move:" + Date.now())
                            }
                        }
                        Tk.IconButton {
                            objectName: "notebookPageMoveUp"
                            iconName: "arrow-up"
                            tooltip: qsTr("Move page earlier in notebook order")
                            enabled: control.currentIndex > 0 && control.capabilities.canEditStructure
                            onClicked: control.moveCurrent(-1)
                        }
                        Tk.IconButton {
                            objectName: "notebookPageMoveDown"
                            iconName: "arrow-down"
                            tooltip: qsTr("Move page later in notebook order")
                            enabled: control.currentIndex >= 0 && control.currentIndex < control.pages.length - 1 && control.capabilities.canEditStructure
                            onClicked: control.moveCurrent(1)
                        }
                        Tk.IconButton {
                            objectName: "notebookPageDelete"
                            iconName: "trash-2"
                            tooltip: qsTr("Delete current page")
                            enabled: !!control.currentPage && control.capabilities.canEditStructure && control.pages.length > 1
                            onClicked: control.pageDeleteRequested(control.currentPageId)
                        }
                    }
                    Tk.Flex {
                        objectName: "notebookPageTags"
                        visible: !!control.currentPage
                        direction: Tk.Flex.Row
                        wrap: Tk.Flex.Wrap
                        gap: Tk.Theme.space.xs
                        Repeater {
                            model: control.currentPage ? control.currentPage.tags || [] : []
                            delegate: Tk.Chip {
                                required property string modelData
                                text: modelData
                                tooltip: qsTr("Page tag %1").arg(modelData)
                            }
                        }
                        Tk.Caption { visible: !!control.currentPage && (control.currentPage.tags || []).length === 0; text: qsTr("No tags") }
                    }
                    Item {
                        id: pageContentSlot
                        objectName: "notebookPageContent"
                        visible: !!control.currentPage
                        Accessible.ignored: !visible
                        Tk.Flex.grow: 1
                        Tk.Flex.basis: 0
                    }
                    Tk.Notice {
                        objectName: "notebookNoPage"
                        visible: !control.currentPage
                        title: qsTr("Select a page")
                        text: qsTr("Choose a page from the notebook outline to see its content.")
                        variant: "info"
                    }
                }
            }
        }
    }
}
