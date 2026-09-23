// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md#rich-text-and-paginated-documents): page
// thumbnails and a heading outline for a RichDocumentView (`view`, or a paged
// Tk.RichTextArea). Rows come from Tk.RichTextPageModel (stable page keys:
// section identity + ordinal); a recycling ListView keeps thumbnail delegates
// bounded to the visible rows, and each thumbnail paints the page from the
// view's layout, so no page image is loaded for pages out of view.
// Navigating moves the view's caret to the page; the text selection of the
// view is otherwise untouched. Sections (page-break groups) can be moved.
T.Control {
    id: control

    property Item view: null
    readonly property Tk.RichTextArea area: control.view === null ? null
        : control.view.area !== undefined ? control.view.area : control.view
    property string mode: "pages"            // "pages" or "outline"
    property real thumbnailWidth: Tk.Theme.size.thumbnail * 2
    property string tooltip: qsTr("Pages")
    readonly property int currentPage: area ? area.currentPage : -1
    readonly property int pageCount: pageModel.count
    readonly property alias pageList: pages
    readonly property alias pageModel: pageModel

    signal pageActivated(int page)
    signal headingActivated(string paragraphId)

    implicitWidth: thumbnailWidth + 2 * Tk.Theme.space.lg + Tk.Theme.size.scrollbar
    implicitHeight: Tk.Theme.size.panelMinHeight * 3
    padding: Tk.Theme.space.sm
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Pane
    Accessible.name: control.tooltip

    function activatePage(page) {
        if (!control.area || page < 0 || page >= control.pageCount) return false
        pages.currentIndex = page
        control.area.goToPage(page)
        control.pageActivated(page)
        return true
    }
    // Moves the page's section one place earlier (-1) or later (+1).
    function moveSection(page, delta) {
        if (!control.area) return false
        const section = pageModel.sectionIndex(page)
        const target = delta < 0 ? section - 1 : section + 2
        if (section < 0 || target < 0 || target > pageModel.sectionCount()) return false
        const moved = control.area.moveSectionOfPage(page, target)
        if (moved) pages.currentIndex = control.area.currentPage
        return moved
    }

    Tk.RichTextPageModel {
        id: pageModel
        area: control.area
    }

    Keys.onPressed: event => {
        if (control.mode !== "pages") return
        let next = pages.currentIndex
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Right) next += 1
        else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left) next -= 1
        else if (event.key === Qt.Key_PageDown) next += 5
        else if (event.key === Qt.Key_PageUp) next -= 5
        else if (event.key === Qt.Key_Home) next = 0
        else if (event.key === Qt.Key_End) next = control.pageCount - 1
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { control.activatePage(pages.currentIndex); event.accepted = true; return }
        else return
        event.accepted = true
        pages.currentIndex = Math.max(0, Math.min(control.pageCount - 1, next))
        pages.positionViewAtIndex(pages.currentIndex, ListView.Contain)
    }

    background: Rectangle {
        color: Tk.Theme.color.panel
        border.width: Tk.Theme.size.border
        border.color: control.activeFocus ? Tk.Theme.color.inputFocusBorder : Tk.Theme.color.border
    }

    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            align: Tk.Flex.Center
            Tk.Segmented {
                objectName: "pageNavigatorMode"
                small: true
                tooltip: qsTr("View")
                model: [{ text: qsTr("Pages"), value: "pages" }, { text: qsTr("Outline"), value: "outline" }]
                currentIndex: control.mode === "outline" ? 1 : 0
                onActivated: index => control.mode = index === 1 ? "outline" : "pages"
            }
            Tk.Spacer {}
            Tk.IconButton {
                objectName: "pageNavigatorMoveUp"
                small: true
                iconName: "chevron-up"
                tooltip: qsTr("Move section earlier")
                focusPolicy: Qt.NoFocus
                enabled: control.mode === "pages" && control.area !== null && control.area.document !== null
                         && !control.area.document.readOnly && pages.currentIndex >= 0
                onClicked: control.moveSection(pages.currentIndex, -1)
            }
            Tk.IconButton {
                objectName: "pageNavigatorMoveDown"
                small: true
                iconName: "chevron-down"
                tooltip: qsTr("Move section later")
                focusPolicy: Qt.NoFocus
                enabled: control.mode === "pages" && control.area !== null && control.area.document !== null
                         && !control.area.document.readOnly && pages.currentIndex >= 0
                onClicked: control.moveSection(pages.currentIndex, 1)
            }
        }

        Item {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            implicitWidth: control.thumbnailWidth
            clip: true

            ListView {
                id: pages
                objectName: "pageNavigatorList"
                anchors.fill: parent
                visible: control.mode === "pages"
                model: pageModel
                reuseItems: true
                cacheBuffer: 0
                spacing: Tk.Theme.space.md
                boundsBehavior: Flickable.StopAtBounds
                currentIndex: control.currentPage
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Pages")
                delegate: Item {
                    id: thumb
                    required property int index
                    required property int pageIndex
                    required property string pageKey
                    required property string revision
                    required property string label
                    required property string heading
                    required property bool current
                    readonly property real aspect: control.area && control.area.pageSize.width > 0
                        ? control.area.pageSize.height / control.area.pageSize.width : Math.SQRT2
                    objectName: "pageThumb_" + pageKey
                    width: ListView.view.width
                    height: control.thumbnailWidth * aspect + caption.implicitHeight + Tk.Theme.space.xs
                    Accessible.role: Accessible.ListItem
                    Accessible.name: qsTr("Page %1").arg(label) + (heading.length > 0 ? ", " + heading : "")
                    Accessible.selected: current
                    Accessible.onPressAction: control.activatePage(pageIndex)

                    Rectangle {
                        id: frame
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: control.thumbnailWidth + 2 * Tk.Theme.size.focusRing
                        height: control.thumbnailWidth * thumb.aspect + 2 * Tk.Theme.size.focusRing
                        color: "transparent"
                        border.width: Tk.Theme.size.focusRing
                        border.color: thumb.current ? Tk.Theme.color.accent
                                    : thumb.index === pages.currentIndex && control.activeFocus ? Tk.Theme.color.focus
                                    : Tk.Theme.color.border
                        Tk.RichTextTile {
                            objectName: "pageThumbTile"
                            x: Tk.Theme.size.focusRing
                            y: Tk.Theme.size.focusRing
                            width: control.thumbnailWidth
                            height: control.thumbnailWidth * thumb.aspect
                            area: control.area
                            pageIndex: thumb.pageIndex
                            decorations: false
                        }
                    }
                    Tk.Caption {
                        id: caption
                        anchors.top: frame.bottom
                        anchors.topMargin: Tk.Theme.space.xs / 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: thumb.label
                        color: thumb.current ? Tk.Theme.color.accentText : Tk.Theme.color.textMuted
                    }
                    TapHandler { onTapped: control.activatePage(thumb.pageIndex) }
                }
                T.ScrollBar.vertical: Tk.ScrollBar { objectName: "pageNavigatorScrollBar" }
            }

            ListView {
                id: outline
                objectName: "pageNavigatorOutline"
                anchors.fill: parent
                visible: control.mode === "outline"
                model: control.mode === "outline" && control.area ? (pageModel.generation, pageModel.outline()) : []
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                Accessible.role: Accessible.Tree
                Accessible.name: qsTr("Outline")
                delegate: Tk.ListRow {
                    required property var modelData
                    objectName: "outlineRow_" + modelData.paragraphId
                    width: ListView.view.width
                    indent: (modelData.level - 1) * Tk.Theme.space.lg
                    text: modelData.text
                    secondaryText: qsTr("p. %1").arg(modelData.page + 1)
                    tooltip: modelData.text
                    onClicked: {
                        control.area.goToParagraph(modelData.paragraphId, 0)
                        control.headingActivated(modelData.paragraphId)
                    }
                }
                T.ScrollBar.vertical: Tk.ScrollBar {}
            }
        }

        Tk.Caption {
            objectName: "pageNavigatorStatus"
            text: control.area ? qsTr("Page %1 of %2").arg(control.currentPage + 1)
                                 .arg(control.area.paginationComplete ? control.pageCount : "~" + control.area.estimatedPageCount)
                               : ""
        }
    }
}
