// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "SheetEditors"
    when: windowShown
    visible: true
    width: 1100
    height: 820

    Component {
        id: cfFixture
        Item {
            width: 1080
            height: 800
            property alias book: book
            property alias sheet: sheet
            property alias cf: cf
            Tk.Workbook { id: book }
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.SheetView { id: sheet; workbook: book; Tk.Flex.grow: 1 }
                Tk.ConditionalFormatEditor { id: cf; workbook: book; sheetId: sheet.currentSheetId }
            }
        }
    }
    Component {
        id: dvFixture
        Item {
            width: 1080
            height: 800
            property alias book: book
            property alias sheet: sheet
            property alias dv: dv
            Tk.Workbook { id: book }
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.sm
                Tk.SheetView { id: sheet; workbook: book; pastePolicy: dv.pastePolicy; Tk.Flex.grow: 1 }
                Tk.DataValidationEditor { id: dv; workbook: book; sheetId: sheet.currentSheetId }
            }
        }
    }
    Component {
        id: bookComponent
        Tk.Workbook {}
    }
    Component {
        id: tabsFixture
        Item {
            width: 900
            height: 400
            property alias book: book
            property alias tabs: tabs
            property alias sheet: sheet
            Tk.Workbook { id: book }
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Column
                Tk.WorkbookTabs {
                    id: tabs
                    workbook: book
                    currentSheetId: sheet.currentSheetId
                    onSheetActivated: function(sheetId) { sheet.sheetId = sheetId }
                }
                Tk.SheetView { id: sheet; workbook: book; Tk.Flex.grow: 1 }
            }
        }
    }

    function typeInto(item, text) {
        mouseClick(item)
        tryVerify(function() { return item.activeFocus })
        for (let i = 0; i < text.length; ++i) keyClick(text[i])
    }
    // Rule rows are rebuilt whenever the rules change: wait for a laid-out row.
    function ruleRow(list, id) {
        tryVerify(function() { const row = findChild(list, "sheetRule_" + id); return row !== null && row.width > 0 })
        return findChild(list, "sheetRule_" + id)
    }
    function presentation(cell) {
        const map = Object.assign({}, cell)
        delete map.row
        delete map.column
        delete map.display
        return map
    }

    function test_conditional_editor_preview_equals_committed_and_reopened() {
        const host = createTemporaryObject(cfFixture, root)
        const book = host.book, cf = host.cf, s = book.sheetIdAt(0)
        verify(book.pasteInputs(s, 0, 0, [["10"], ["20"], ["30"], ["40"]]))
        waitForRendering(host)
        cf.newRule("A1:A4")
        compare(findChild(cf, "cfRanges").text, "A1:A4")
        typeInto(findChild(cf, "cfValue1"), "25")
        compare(cf.value1, "25")
        mouseClick(findChild(cf, "cfBold"))
        compare(cf.style.bold, true)
        verify(findChild(cf, "cfBold").checked)
        mouseClick(findChild(findChild(cf, "cfFillColor"), "swatch_3"))
        compare(cf.style.fillColor, cf.documentColors[3].color)
        tryVerify(function() { return cf.check.ok })
        const preview = findChild(cf, "cfPreview")
        tryVerify(function() { return preview.visible && preview.cells.length === 4 })
        compare(preview.cells[2].bold, true) // 30 > 25
        verify(preview.cells[0].bold === undefined)
        compare(findChild(preview, "gridCell_p2_p0") !== null, true)
        const shown = preview.cells.map(presentation)
        mouseClick(findChild(cf, "cfApply"))
        compare(book.conditionalFormats(s).length, 1)
        compare(book.undoText, "Conditional format")
        const reopened = createTemporaryObject(bookComponent, root)
        verify(reopened.restoreText(book.serializedText()))
        for (let r = 0; r < 4; ++r) {
            compare(book.cellPresentation(s, r, 0), shown[r])
            compare(reopened.cellPresentation(s, r, 0), shown[r])
        }
        // The sheet draws the same presentation.
        const sheetCell = findChild(host.sheet.grid, "gridCell_r:3_c:1")
        tryVerify(function() { return findChild(sheetCell, "gridCellText").font.bold })

        // A newer rule applies first; the list reorders priorities.
        cf.newRule("A1:A4")
        cf.operator = "less"
        cf.value1 = "25"
        cf.style = ({ italic: true })
        verify(cf.apply().length > 0)
        const rules = book.conditionalFormats(s)
        compare(rules.length, 2)
        const older = rules[1].id
        const list = findChild(cf, "cfRuleList")
        mouseClick(findChild(ruleRow(list, older), "sheetRuleUp"))
        compare(book.conditionalFormats(s)[0].id, older)
        verify(book.undo())
        compare(book.conditionalFormats(s)[1].id, older)
        mouseClick(ruleRow(list, older))
        compare(cf.ruleId, older)
        compare(cf.value1, "25")

        // A rule the Workbook refuses cannot be applied, and says why.
        cf.newRule("A1")
        cf.operator = "between"
        cf.value1 = "1"
        tryVerify(function() { return findChild(cf, "cfError").visible })
        compare(findChild(cf, "cfError").text, "Enter the second value of the range")
        verify(!findChild(cf, "cfApply").available)
        compare(cf.apply(), "")

        // Scales never "match": stop-if-true is hidden and left out of the draft.
        cf.newRule("A1:A4")
        cf.stopIfTrue = true
        verify(findChild(cf, "cfStopIfTrue").visible)
        compare(cf.draft.stopIfTrue, true)
        cf.type = "colorScale"
        verify(!findChild(cf, "cfStopIfTrue").visible)
        compare(cf.draft.stopIfTrue, false)
    }

    function test_validation_editor_list_and_paste_policy() {
        const host = createTemporaryObject(dvFixture, root)
        const book = host.book, dv = host.dv, sheet = host.sheet, s = book.sheetIdAt(0)
        waitForRendering(host)
        dv.newRule("B1:B5")
        dv.type = "list"
        const items = findChild(dv, "dvItems")
        tryVerify(function() { return items.visible })
        typeInto(items, "Red,Green")
        compare(dv.itemList, ["Red", "Green"])
        typeInto(findChild(dv, "dvInputMessage"), "Pick a color")
        tryVerify(function() { return dv.check.ok })
        mouseClick(findChild(dv, "dvApply"))
        compare(book.validationAt(s, 0, 1).items, ["Red", "Green"])
        compare(book.validationAt(s, 4, 1).inputMessage, "Pick a color")
        verify(ruleRow(findChild(dv, "dvRuleList"), "dv1") !== null)

        mouseClick(findChild(findChild(dv, "dvPastePolicy"), "segment_1"))
        compare(sheet.pastePolicy, "skipInvalid")
        sheet.setCurrent(0, 1)
        verify(sheet.pasteText("Red\nBlue\nGreen"))
        compare(book.cellInput(s, 0, 1), "Red")
        compare(book.cellInput(s, 1, 1), "")
        compare(book.cellInput(s, 2, 1), "Green")
        compare(book.lastPasteReport.invalid.length, 1)
        compare(book.lastPasteReport.invalid[0].address, "B2")
        verify(book.undo())
        compare(book.cellInput(s, 0, 1), "")

        mouseClick(findChild(findChild(dv, "dvPastePolicy"), "segment_0"))
        compare(sheet.pastePolicy, "allOrNothing")
        verify(!sheet.pasteText("Red\nBlue"))
        compare(sheet.errorText, "B2: Choose one of: Red, Green")
        compare(book.cellInput(s, 0, 1), "")

        // Numbers: bounds and the invalid-value style.
        dv.newRule("C1:C5")
        dv.type = "wholeNumber"
        dv.value1 = "1"
        dv.value2 = "9"
        mouseClick(findChild(findChild(dv, "dvErrorStyle"), "segment_1"))
        compare(dv.errorStyle, "warning")
        verify(dv.apply().length > 0)
        compare(book.validateInput(s, 0, 2, "12").style, "warning")
        dv.newRule("D1")
        dv.type = "decimal"
        dv.operator = "greater"
        dv.value1 = "abc"
        tryVerify(function() { return findChild(dv, "dvError").visible })
        verify(!findChild(dv, "dvApply").available)
    }

    function test_tabs_color_hidden_and_protected() {
        const host = createTemporaryObject(tabsFixture, root)
        const book = host.book, tabs = host.tabs, sheet = host.sheet
        const first = book.sheetIdAt(0), second = book.addSheet("Two")
        waitForRendering(host)
        tryVerify(function() { return findChild(tabs, "workbookTab_" + second) !== null })
        // The tab menu opens from the keyboard; its color picker stores a document color.
        const tab = findChild(tabs, "workbookTab_" + second)
        tab.forceActiveFocus()
        keyClick(Qt.Key_F10, Qt.ShiftModifier)
        const menu = findChild(tabs, "workbookTabMenu")
        tryVerify(function() { return menu.opened })
        compare(tabs.menuSheetId, second)
        mouseClick(findChild(tabs, "workbookTabMenuColor"))
        const popover = findChild(tabs, "workbookTabColorPopover")
        tryVerify(function() { return popover.opened })
        mouseClick(findChild(findChild(popover.contentItem, "workbookTabSwatches"), "swatch_2"))
        tryVerify(function() { return !popover.visible })
        const stored = book.sheetProperties(second).tabColor
        verify(/^#[0-9a-f]{6}$/.test(stored))
        const strip = findChild(findChild(tabs, "workbookTab_" + second), "workbookTabColor")
        tryVerify(function() { return strip.visible })
        verify(Qt.colorEqual(strip.color, stored))
        verify(tabs.setTabColor(second, ""))
        compare(book.sheetProperties(second).tabColor, "")

        // Hidden sheets leave the strip; the hidden button shows them again.
        verify(tabs.setHidden(first, true))
        tryCompare(sheet, "currentSheetId", second)
        tryVerify(function() { return findChild(tabs, "workbookTab_" + first) === null })
        const hidden = findChild(tabs, "workbookTabHidden")
        verify(hidden.visible)
        compare(hidden.tooltip, "Hidden sheets (1)")
        verify(!tabs.setHidden(second, true)) // the last visible sheet stays
        verify(tabs.errorText.length > 0)
        mouseClick(hidden)
        const hiddenMenu = findChild(tabs, "workbookHiddenMenu")
        tryVerify(function() { return hiddenMenu.opened })
        mouseClick(hiddenMenu.itemAt(0))
        tryCompare(sheet, "currentSheetId", first)
        verify(!book.sheetProperties(first).hidden)

        // Protection shows a lock and makes the sheet read-only.
        verify(tabs.setProtected(first, true))
        const lock = findChild(findChild(tabs, "workbookTab_" + first), "workbookTabLock")
        tryVerify(function() { return lock.visible })
        verify(findChild(tabs, "workbookTab_" + first).Accessible.description.indexOf("Protected") === 0)
        verify(sheet.readOnly)
        verify(book.undo())
        tryVerify(function() { return !lock.visible })
    }
}
