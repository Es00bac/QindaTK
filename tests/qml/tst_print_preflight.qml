// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "PrintPreflight"
    when: windowShown
    visible: true
    width: 760
    height: 520

    Component {
        id: previewComponent
        Tk.PrintPreview {
            width: 680
            pageCount: 8
            pageRange: ({ startPage: 2, endPage: 5 })
            job: ({ id: "job-77", status: "pending", progress: 0.4, error: "", affectedStart: 2, affectedEnd: 5 })
            capabilities: ({ paperOptions: [{ id: "a4", label: "A4" }],
                            orientationOptions: [{ id: "portrait", label: "Portrait" }],
                            scaleOptions: [{ id: "fit", label: "Fit" }],
                            duplexOptions: [{ id: "simplex", label: "Simplex" }], maxCopies: 1 })
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    // WP-053: the controls bound to a real adapter and document.
    Tk.RichTextDocument { id: printDocument }
    Tk.PrintAdapter { id: printAdapter; document: printDocument }

    Component {
        id: boundPreviewComponent
        Tk.PrintPreview {
            id: bound
            width: 680
            pageCount: printAdapter.pageCount
            pageRange: printAdapter.settings.pageRange
            settings: printAdapter.settings
            capabilities: printAdapter.capabilities
            destinations: printAdapter.destinations
            job: printAdapter.job
            onSettingsChangeRequested: function(next) { printAdapter.applySettings(next) }
            Tk.PrintSheetView {
                objectName: "boundSheet"
                anchors.fill: parent
                adapter: printAdapter
                pageIndex: bound.currentPage
            }
        }
    }

    // Three 372 x 432 pt pages of two 18 pt lines each.
    function documentJson() {
        const uuid = n => "00000000-0000-4000-8000-" + n.toString(16).padStart(12, "0")
        const blocks = []
        for (let i = 0; i < 6; ++i) {
            const text = "Line " + i
            const format = { lineHeightPt: 18 }
            if (i > 0 && i % 2 === 0) format.pageBreakBefore = true
            blocks.push({ paragraph: { id: uuid(i + 1), text: text, spans: [[text.length, 0]], mark: 0, format: format } })
        }
        return JSON.stringify({ schema: "qindatk.richtext", version: 1, documentId: uuid(999999),
                                page: { widthPt: 372, heightPt: 432, marginsPt: [36, 36, 36, 36] },
                                defaults: { family: "sans-serif", sizePt: 11 },
                                formats: [{}], lists: [], objects: [], blocks: blocks })
    }

    function test_cancel_requests_pending_job_and_waits_for_acknowledgement() {
        const preview = createTemporaryObject(previewComponent, this)
        verify(preview !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: preview, signalName: "cancelPrintRequested" })
        const cancel = findChild(preview, "printSubmit")
        verify(cancel !== null && cancel.enabled)
        compare(cancel.text, "Cancel print")

        cancel.click()

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "job-77")
        compare(preview.job.status, "pending")
    }

    function test_adapter_drives_preview_sheet_and_preflight() {
        verify(printDocument.loadJson(documentJson()))
        compare(printAdapter.pageCount, 3)
        const preview = createTemporaryObject(boundPreviewComponent, this)
        verify(preview !== null)
        const destination = findChild(preview, "printDestination")
        verify(destination !== null)
        compare(destination.currentText, "Save as PDF")
        verify(findChild(preview, "printOutputPath").visible)
        const sheet = findChild(preview, "boundSheet")
        tryVerify(function() { return sheet.ready })
        compare(sheet.sheetSize.width, 372)
        compare(sheet.sheetSize.height, 432)
        verify(sheet.selected)

        // A setting requested by the panel reaches the adapter and the sheet.
        preview.requestSetting("paperId", "letter")
        compare(printAdapter.settings.paperId, "letter")
        tryVerify(function() { return sheet.sheetSize.width === 612 && sheet.sheetSize.height === 792 })

        // Out-of-range pages are refused before a job starts.
        verify(!printAdapter.applySettings({ pages: "x" }))
        verify(printAdapter.applySettings({ pages: "4-5" }))
        compare(printAdapter.print(), "")
        compare(printAdapter.lastErrorCode, "invalidArgument")
        compare(printAdapter.busy, false)
        const issues = printAdapter.runPreflight()
        const range = issues.filter(function(issue) { return issue.id === "output-pages" })
        compare(range.length, 1)
        compare(range[0].repairActionIds[0], "print-all-pages")
        verify(printAdapter.repair("output-pages", "print-all-pages"))
        compare(printAdapter.settings.pages, "")
        verify(printAdapter.issues.some(function(issue) { return issue.id === "output-outputPath" }))
    }
}
