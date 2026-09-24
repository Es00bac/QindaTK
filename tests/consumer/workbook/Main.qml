// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The WP-055 spreadsheet stack from the installed module: the views over a
// small workbook, the shared services and a second workbook for pastes.
// main.cpp drives the checks through the installed C++ headers.
Window {
    id: root
    width: 1280
    height: 720
    visible: true
    color: Tk.Theme.color.canvas

    // The workbook is the model of a DocumentSession in the shared framework.
    property Tk.DocumentSession session: null
    readonly property Tk.Workbook book: session ? session.model : null

    Component.onCompleted: {
        session = documents.createDocument("qindatk.workbook.v1")
        const data = book.sheetIdAt(0)
        book.renameSheet(data, "Data")
        book.pasteInputs(data, 0, 0, [
            ["Item", "Amount", "Count", "Named"],
            ["Rent", "1200.5", "=COUNTA(A:A)", "=SUM(Amounts)"],
            ["Travel", "=B2/2", "", ""],
            ["Total", "=SUM(B2:B3)", "", ""]])
        book.defineName("Amounts", data, 1, 1, 2, 1)
        book.applyStyle(data, 1, 1, 3, 1, { numberFormat: "#,##0.00" })
        book.restoreText(book.serializedText()) // a freshly opened document
        sheet.sheetId = data
        sheet.setCurrent(3, 1, false)
    }

    Tk.DocumentManager {
        id: documents
        objectName: "installedDocuments"
    }
    Tk.Workbook {
        id: other
        objectName: "installedOtherWorkbook"
    }
    Tk.FormatRegistry {
        objectName: "installedRegistry"
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.WorkbookTabs {
            objectName: "installedTabs"
            workbook: book
            currentSheetId: sheet.currentSheetId
            onSheetActivated: function(sheetId) { sheet.sheetId = sheetId }
        }
        Tk.FormulaBar {
            objectName: "installedFormulaBar"
            sheetView: sheet
            Tk.Flex.alignSelf: Tk.Flex.Stretch
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.alignSelf: Tk.Flex.Stretch
            Tk.SheetView {
                id: sheet
                objectName: "installedSheetView"
                workbook: book
                tooltip: "Installed sheet"
                Tk.Flex.grow: 1
                Tk.Flex.alignSelf: Tk.Flex.Stretch
            }
            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.sm
                width: Tk.Theme.size.panelMinWidth * 1.5
                Tk.ConditionalFormatEditor {
                    objectName: "installedConditionalEditor"
                    workbook: book
                    sheetId: sheet.currentSheetId
                }
                Tk.DataValidationEditor {
                    objectName: "installedValidationEditor"
                    workbook: book
                    sheetId: sheet.currentSheetId
                }
            }
        }
    }
}
