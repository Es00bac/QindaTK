// SPDX-License-Identifier: LGPL-3.0-or-later
import QtCore
import QtQuick
import QindaTK as Tk

// Print preview, output and preflight for one document (WP-053). Every value
// comes from Tk.PrintAdapter: the sheet preview uses the same geometry and
// painter as the PDF and printer jobs, and the issues are real checks of the
// showcase document. The panels only send requests back to the adapter.
Rectangle {
    id: root
    width: 1360
    height: 940
    color: Tk.Theme.color.bg
    property int currentPage: 0
    property string notice: qsTr("Output checks run against the printed sheets.")
    readonly property string outputFile: String(StandardPaths.writableLocation(StandardPaths.TempLocation))
                                         .replace(/^file:\/\//, "") + "/print-preflight.pdf"

    Tk.RichTextDocument {
        id: document
        // One handler orders the steps: load, choose A4 output, then check it.
        Component.onCompleted: {
            loadFromFile(Qt.resolvedUrl("../rich-document/showcase.qtkrich"))
            output.applySettings({ paperId: "a4", scaleMode: "fit", outputPath: root.outputFile })
            output.runPreflight()
        }
    }

    Tk.PrintAdapter {
        id: output
        objectName: "printAdapter"
        document: document
        onJobFinished: function(jobId, status) {
            root.notice = status === "completed" ? qsTr("%1 finished: %2").arg(jobId).arg(job.outputPath || job.printerId)
                                                 : qsTr("%1 %2: %3").arg(jobId).arg(status).arg(job.error)
        }
    }

    function navigateTo(issue) {
        if (issue.paragraphId) pageView.area.goToParagraph(issue.paragraphId, issue.offset)
        else if (issue.pageIndex >= 0) pageView.goToPage(issue.pageIndex)
        if (issue.pageIndex >= 0) root.currentPage = issue.pageIndex
    }

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Heading { text: qsTr("Print and preflight"); level: 2; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { objectName: "printFixtureNotice"; text: root.notice; wrapMode: Text.WordWrap }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.md
            Tk.Flex.grow: 1
            Tk.Panel {
                title: qsTr("Document")
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.RichDocumentView {
                    id: pageView
                    anchors.fill: parent
                    document: document
                    readOnly: true
                    zoom: 0.52
                    tooltip: qsTr("Document pages")
                }
            }
            Tk.Panel {
                title: qsTr("Preview and output")
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.PrintPreview {
                    id: printPreview
                    objectName: "printPreview"
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    pageCount: output.pageCount
                    currentPage: Math.min(root.currentPage, Math.max(0, output.pageCount - 1))
                    pageRange: output.settings.pageRange
                    settings: output.settings
                    capabilities: output.capabilities
                    destinations: output.destinations
                    job: output.job
                    onPageRequested: function(index) { root.currentPage = index }
                    onPageRangeChangeRequested: function(range) { output.applySettings({ pageRange: range }) }
                    onSettingsChangeRequested: function(next) {
                        if (!output.applySettings(next)) root.notice = output.lastError
                        else output.runPreflight()
                    }
                    onPreviewRefreshRequested: output.runPreflight()
                    onPrintRequested: function(settings, range) {
                        if (output.print({ pageRange: range }) === "") root.notice = output.lastError
                    }
                    onCancelPrintRequested: function(id) { output.cancel(id) }
                    Tk.PrintSheetView {
                        objectName: "printSheet"
                        anchors.fill: parent
                        adapter: output
                        pageIndex: printPreview.currentPage
                        Accessible.role: Accessible.Graphic
                        Accessible.name: qsTr("Printed sheet for page %1").arg(pageIndex + 1)
                    }
                }
            }
            Tk.Panel {
                objectName: "preflightPanelHost"
                title: qsTr("Output checks")
                Tk.Flex.basis: Tk.Theme.size.panelMinWidth * 2.3
                Tk.PreflightPanel {
                    id: preflight
                    objectName: "preflightPanel"
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    issues: output.issues
                    profileId: output.profileId
                    onIssueSelectionRequested: function(id) { root.navigateTo(output.issue(id)) }
                    onNavigateRequested: function(id, targetId) { root.navigateTo(output.issue(id)) }
                    onRepairRequested: function(id, repairId) {
                        // Repairs that need a file choice come back through a file dialog in a real app.
                        const argument = repairId === "choose-output-file" ? root.outputFile : undefined
                        if (output.repair(id, repairId, argument)) root.notice = qsTr("Repaired: %1").arg(id)
                        else root.notice = output.lastError
                    }
                    onRefreshRequested: function(profile) { output.runPreflight() }
                }
            }
        }
    }
}
