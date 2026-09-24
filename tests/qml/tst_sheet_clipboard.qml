// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Copy in one workbook, paste into another through the system clipboard
// (Tk.ClipboardService): one undo step, sheets remapped by name.
TestCase {
    id: root
    name: "SheetClipboard"
    when: windowShown
    visible: true
    width: 1000
    height: 500

    Component {
        id: fixture
        Item {
            width: 980
            height: 480
            property alias leftView: leftView
            property alias rightView: rightView
            property alias leftBook: leftBook
            property alias rightBook: rightBook
            property alias clipboard: clipboard
            Tk.ClipboardService { id: clipboard }
            Tk.Workbook { id: leftBook }
            Tk.Workbook { id: rightBook }
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.SheetView { id: leftView; workbook: leftBook; clipboard: clipboard; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                Tk.SheetView { id: rightView; workbook: rightBook; clipboard: clipboard; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            }
        }
    }

    function test_copy_between_workbooks_through_the_clipboard() {
        const host = createTemporaryObject(fixture, root)
        const a = host.leftBook, b = host.rightBook
        const sa = a.sheetIdAt(0), sb = b.sheetIdAt(0)
        const dataA = a.addSheet("Data"), dataB = b.addSheet("Data")
        verify(a.setCellInput(dataA, 0, 0, "3"))
        verify(b.setCellInput(dataB, 1, 0, "30"))
        verify(a.setCellInput(sa, 0, 0, "=Data!A1*2"))
        verify(a.setCellInput(sa, 0, 1, "=A1+1"))
        verify(a.applyStyle(sa, 0, 0, 0, 1, { bold: true }))
        tryVerify(function() { return host.leftView.hasActiveCell && host.rightView.hasActiveCell })
        host.leftView.selectRange(0, 0, 0, 1)
        host.leftView.focusGrid()
        keyClick(Qt.Key_C, Qt.ControlModifier)
        compare(host.clipboard.rangePasteData().kind, "range")
        verify(host.clipboard.canPaste)

        host.rightView.setCurrent(1, 2)
        host.rightView.focusGrid()
        const steps = b.statistics.historySteps
        keyClick(Qt.Key_V, Qt.ControlModifier)
        compare(b.cellInput(sb, 1, 2), "=Data!C2*2") // moved one row, two columns
        compare(b.cellDisplay(sb, 1, 2), "0")
        compare(b.cellInput(sb, 1, 3), "=C2+1")
        compare(b.cellFormat(sb, 1, 3).bold, true)
        compare(b.statistics.historySteps, steps + 1)
        keyClick(Qt.Key_Z, Qt.ControlModifier)
        compare(b.cellInput(sb, 1, 2), "")
        verify(!b.cellFormat(sb, 1, 3).bold)

        // Plain text from another application pastes under the view's policy.
        host.clipboard.copyText("7\t8", b.documentId, "00000000-0000-4000-8000-000000000001", b.revision)
        compare(host.clipboard.rangePasteData().kind, "text")
        host.rightView.setCurrent(4, 0)
        host.rightView.focusGrid()
        keyClick(Qt.Key_V, Qt.ControlModifier)
        compare(b.cellInput(sb, 4, 0), "7")
        compare(b.cellInput(sb, 4, 1), "8")
    }
}
