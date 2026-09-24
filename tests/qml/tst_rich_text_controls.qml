// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: testCase
    name: "RichTextControls"
    visible: true
    width: 1100
    height: 720
    when: windowShown

    Tk.RichTextDocument { id: pagesDocument }
    Tk.RichTextDocument { id: notesDocument }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        Tk.FormatBar { id: bar; target: view; Tk.Flex.shrink: 0 }
        Tk.Flex {
            direction: Tk.Flex.Row
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.PageNavigator { id: navigator; view: view; Tk.Flex.shrink: 0 }
            Tk.RichDocumentView { id: view; document: pagesDocument; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Item {
                implicitWidth: 240
                Tk.Flex.shrink: 0
                Tk.RichTextEditor { id: notes; width: parent.width; document: notesDocument; rows: 2; placeholderText: "Notes" }
            }
        }
    }

    // Fixed 18 pt lines on a 360 pt content box: 20 lines per page, so page
    // counts are known answers independent of font metrics.
    function documentJson(count, breakEvery) {
        const uuid = n => "00000000-0000-4000-8000-" + n.toString(16).padStart(12, "0")
        const blocks = []
        for (let i = 0; i < count; ++i) {
            const text = "Line " + i + " שלום 中文"
            const format = { lineHeightPt: 18 }
            if (breakEvery > 0 && i > 0 && i % breakEvery === 0) format.pageBreakBefore = true
            blocks.push({ paragraph: { id: uuid(i + 1), text: text, spans: [[text.length, 0]], mark: 0, format: format } })
        }
        return JSON.stringify({ schema: "qindatk.richtext", version: 1, documentId: uuid(999999),
                                page: { widthPt: 372, heightPt: 432, marginsPt: [36, 36, 36, 36] },
                                defaults: { family: "sans-serif", sizePt: 11 },
                                formats: [{}], lists: [], objects: [], blocks: blocks })
    }

    function find(item, name) {
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = find(item.children[i], name)
            if (found) return found
        }
        return null
    }

    function countVisible(item, prefix) {
        let count = item.visible && item.objectName.startsWith(prefix) ? 1 : 0
        for (let i = 0; i < item.children.length; ++i) count += countVisible(item.children[i], prefix)
        return count
    }

    function test_1_formatBarClickKeepsFocusAndIsOneUndoStep() {
        verify(pagesDocument.loadJson(documentJson(40, 0)))
        view.forceActiveFocus()
        verify(view.area.activeFocus)
        view.area.selectAll()
        const bold = find(bar, "formatBarBold")
        verify(bold)
        mouseClick(bold)
        compare(view.area.formatState.bold, Qt.Checked)
        verify(view.area.activeFocus)
        verify(view.area.hasSelection)
        compare(pagesDocument.undoText, "Bold")
        verify(bold.checked)
        verify(pagesDocument.undo())
        compare(view.area.formatState.bold, Qt.Unchecked)
        verify(!bold.checked)
    }

    function test_2_pageNavigatorKeepsThumbnailsBoundedAndNavigates() {
        verify(pagesDocument.loadJson(documentJson(600, 0)))
        tryVerify(() => view.paginationComplete, 10000)
        compare(view.pageCount, 30)
        compare(navigator.pageCount, 30)
        const list = find(navigator, "pageNavigatorList")
        verify(list)
        wait(50)
        // Delegates are bounded by the viewport (visible rows, one row of
        // overhang each side and the reuse pool), never by the page count.
        const rowHeight = list.contentItem.children[0].height + list.spacing
        const bound = Math.ceil(list.height / rowHeight) + 4
        const thumbnails = countVisible(list.contentItem, "pageThumb_")
        verify(thumbnails > 0 && thumbnails <= bound, "thumbnail delegates: " + thumbnails + " > " + bound)
        view.area.selectAll()
        const selected = view.area.selectedText
        verify(navigator.activatePage(20))
        compare(view.currentPage, 20)
        compare(view.area.selectedText, selected) // navigation keeps the selection
        navigator.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 21)
        keyClick(Qt.Key_Return)
        compare(view.currentPage, 21)
        const afterNavigation = countVisible(list.contentItem, "pageThumb_")
        verify(afterNavigation <= bound, "thumbnail delegates: " + afterNavigation)
        verify(countVisible(view.area, "richDocumentPage_") <= 3)
        view.area.contentY = 0 // scrolling the document moves the navigator with it
        compare(view.currentPage, 0)
        compare(list.currentIndex, 0)
        console.log("MEASURE PageNavigator: " + afterNavigation + " thumbnail delegates for "
                    + view.pageCount + " pages, bound " + bound)
    }

    function test_3_sectionsMoveThroughTheNavigator() {
        verify(pagesDocument.loadJson(documentJson(60, 20)))
        tryVerify(() => view.paginationComplete, 10000)
        compare(view.pageCount, 3)
        compare(navigator.pageModel.sectionCount(), 3)
        const firstBefore = pagesDocument.paragraphText(pagesDocument.paragraphIds()[0])
        verify(navigator.moveSection(0, 1))
        compare(pagesDocument.paragraphText(pagesDocument.paragraphIds()[0]), "Line 20 שלום 中文")
        compare(pagesDocument.paragraphText(pagesDocument.paragraphIds()[20]), firstBefore)
        verify(pagesDocument.undo())
        compare(pagesDocument.paragraphText(pagesDocument.paragraphIds()[0]), firstBefore)
    }

    function test_5_ctrlKEditsTheLinkInOneStep() {
        verify(pagesDocument.loadJson(documentJson(3, 0)))
        view.forceActiveFocus()
        view.area.selectAll()
        verify(!bar.linkEditorOpen)
        keyClick(Qt.Key_K, Qt.ControlModifier)
        tryVerify(() => bar.linkEditorOpen)
        for (const c of "x.org") keyClick(c)
        keyClick(Qt.Key_Return)
        tryVerify(() => !bar.linkEditorOpen)
        compare(view.area.formatState.link, "x.org")
        compare(pagesDocument.undoText, "Link")
        verify(view.area.activeFocus)
        verify(view.area.hasSelection)
    }

    function test_4_richTextEditorTypesAndGrows() {
        const placeholder = find(notes, "richTextEditorPlaceholder")
        verify(placeholder.visible)
        const empty = notes.height
        notes.forceActiveFocus()
        verify(notes.area.activeFocus)
        keyClick(Qt.Key_H)
        keyClick(Qt.Key_I)
        compare(notesDocument.text(), "hi")
        verify(!placeholder.visible)
        for (let i = 0; i < 4; ++i) keyClick(Qt.Key_Return)
        tryVerify(() => notes.height > empty)
        compare(notesDocument.paragraphCount, 5)
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(notesDocument.paragraphCount, 4)
    }
}
