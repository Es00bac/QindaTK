// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest

// Drives examples/document-publishing/Main.qml (WP-051): the panels and the
// page view edit one rich-text document. A frame or cell chosen in a panel
// moves the caret, the caret selects them in the panels, threading a frame
// takes the overset text, and a style applied at the caret is one undo step.
TestCase {
    id: root
    name: "PublishingExample"
    when: windowShown
    visible: true
    width: 1280
    height: 900

    Loader {
        id: loader
        anchors.fill: parent
        source: Qt.resolvedUrl("../../examples/document-publishing/Main.qml")
    }

    function part(name) {
        const item = findChild(loader.item, name)
        verify(item !== null, name)
        return item
    }

    function test_panels_and_caret_share_one_document() {
        tryCompare(loader, "status", Loader.Ready)
        const example = loader.item
        const view = part("publishingDocument")
        const document = view.document
        tryVerify(function() { return document.tables.length === 1 })
        compare(document.frames.length, 3)
        tryVerify(function() { return view.paginationComplete && view.area.oversetCharacters > 0 })
        compare(view.pageCount, 2) // frames sit on two pages
        verify(part("publishingOverset").text.indexOf("overset") >= 0)

        // A frame chosen in the inspector gets the caret; the caret selects it back.
        const frames = part("publishingTextFrameEditor")
        frames.frameSelectionRequested("frame-b")
        compare(view.area.caretFrameId, "frame-b")
        compare(example.selectedFrameId, "frame-b")
        compare(frames.currentFrame.flowStatus, "overset")
        verify(frames.currentFrame.oversetChars > 0)

        // The caret in a table cell selects that cell in the TableEditor.
        const table = part("publishingTableEditor")
        const price = table.cellAt(1, 1)
        verify(view.area.goToParagraph(price.paragraphIds[0], 0))
        compare(table.selectedCellIds, [price.id])
        const merged = table.cellAt(1, 2)
        compare(merged.rowSpan, 2)

        // Threading the free frame after B takes the overset text.
        frames.connectRequested("frame-b", "frame-c")
        compare(document.undoText, "Connect text frames")
        tryVerify(function() { return view.paginationComplete && view.area.oversetCharacters === 0 })
        compare(part("publishingOverset").text, "Story fits")

        // A paragraph style applied at the caret is one undo step.
        const styles = part("publishingStyleManager")
        const target = document.paragraphIds()[3]
        compare(document.paragraphFormatMap(target).style, "body")
        verify(view.area.goToParagraph(target, 0))
        compare(styles.targetIds, [target])
        styles.chooseStyle("lead")
        verify(styles.canApply)
        findChild(styles, "styleApply").clicked()
        compare(document.paragraphFormatMap(target).style, "lead")
        compare(document.undoText, "Paragraph style")
        const undo = part("publishingUndo")
        compare(undo.tooltip, "Undo Paragraph style")
        undo.clicked()
        compare(document.paragraphFormatMap(target).style, "body")
    }
}
