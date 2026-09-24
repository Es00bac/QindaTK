// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Print and issue state is host-owned. The preview surface uses the document's
// real paginated renderer; no printer output is fabricated by this fixture.
Rectangle {
    id: root
    width: 1360
    height: 940
    color: Tk.Theme.color.bg
    property string notice: qsTr("Document preview is supplied by RichDocumentView.")
    property var printSettings: ({ paperId: "a4", orientation: "portrait", scaleMode: "fit",
                                   scalePercent: 100, duplex: "long-edge", copies: 1,
                                   printerId: "studio-printer", colorMode: "color" })
    property var printCapabilities: ({
        paperOptions: [{ id: "a4", label: "A4" }, { id: "letter", label: "Letter" }],
        orientationOptions: [{ id: "portrait", label: "Portrait" }, { id: "landscape", label: "Landscape" }],
        scaleOptions: [{ id: "fit", label: "Fit to page" }, { id: "actual", label: "Actual size" }, { id: "percent", label: "Custom %" }],
        duplexOptions: [{ id: "simplex", label: "Single-sided" }, { id: "long-edge", label: "Two-sided · long edge" }],
        colorOptions: [{ id: "grayscale", label: "Grayscale" }], maxCopies: 50,
        unavailableOptions: [qsTr("Color output"), qsTr("Borderless media")]
    })
    property var printJob: ({ id: "", status: "idle", progress: 0, error: "", affectedStart: 0, affectedEnd: 0 })
    property var issues: [
        { id: "font-missing-14", severity: "warning", category: "Fonts", title: qsTr("Missing font: Inter Display"),
          description: qsTr("Substituted with the configured fallback in 3 paragraphs."), targetId: "font:Inter Display",
          scope: "document", repairActionIds: ["replace-font"], repairLabel: qsTr("Choose substitute"), repairAvailable: true },
        { id: "overset-story-7", severity: "error", category: "Layout", title: qsTr("Text frame has overset content"),
          description: qsTr("Frame 7 has 68 characters that do not fit on the last page."), targetId: "frame:7",
          scope: "selection", repairActionIds: ["open-frame-editor"], repairLabel: qsTr("Inspect frame"), repairAvailable: true },
        { id: "asset-resolution-3", severity: "warning", category: "Assets", title: qsTr("Image resolution is below profile target"),
          description: qsTr("Image asset 3 renders at 126 dpi; the selected profile recommends 300 dpi."), targetId: "asset:3",
          scope: "document", repairActionIds: ["open-image"], repairLabel: qsTr("Inspect image"), repairAvailable: true },
        { id: "profile-color-1", severity: "blocker", category: "Output", title: qsTr("Requested color output is unavailable"),
          description: qsTr("The selected printer profile exposes grayscale output only."), targetId: "profile:studio-printer",
          scope: "output profile", repairActionIds: ["select-profile"], repairLabel: qsTr("Choose profile"), repairAvailable: false }
    ]

    function applyPrintSettings(next) {
        printSettings = next
        notice = qsTr("Print settings proposal received; adapter confirms supported options.")
    }
    function startPrint(settings, range) {
        const id = "print-job-206"
        printJob = { id: id, status: "pending", progress: 0, error: "", affectedStart: range.startPage, affectedEnd: range.endPage }
        notice = qsTr("Print request %1 submitted for pages %2–%3. Awaiting adapter response.")
                    .arg(id).arg(range.startPage).arg(range.endPage)
    }
    function cancelPrint(id) {
        if (id !== printJob.id || printJob.status !== "pending") return
        printJob = Object.assign({}, printJob, { status: "cancelled" })
        notice = qsTr("Adapter acknowledged cancellation for %1.").arg(id)
    }
    function updateIssue(id, patch) {
        const next = issues.map(function(issue) { return Object.assign({}, issue) })
        const index = next.findIndex(function(issue) { return issue.id === id })
        if (index < 0) return
        Object.assign(next[index], patch)
        issues = next
    }

    Tk.RichTextDocument {
        id: document
        Component.onCompleted: loadFromFile(Qt.resolvedUrl("../rich-document/showcase.qtkrich"))
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
            Tk.Panel {
                title: qsTr("Document preview and output")
                Tk.PrintPreview {
                    id: printPreview
                    objectName: "printPreview"
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    pageCount: pageView.pageCount
                    currentPage: pageView.currentPage
                    settings: root.printSettings
                    capabilities: root.printCapabilities
                    job: root.printJob
                    onPageRequested: function(index) { pageView.goToPage(index) }
                    onPageRangeChangeRequested: function(range) { pageRange = range }
                    onSettingsChangeRequested: function(next) { root.applyPrintSettings(next) }
                    onPreviewRefreshRequested: function(range, settings) {
                        notice = qsTr("Preview refresh requested for %1–%2.").arg(range.startPage).arg(range.endPage)
                    }
                    onPrintRequested: function(settings, range) { root.startPrint(settings, range) }
                    onCancelPrintRequested: function(id) { root.cancelPrint(id) }
                    Tk.RichDocumentView {
                        id: pageView
                        anchors.fill: parent
                        document: document
                        readOnly: true
                        zoom: 0.52
                        tooltip: qsTr("Document page preview")
                    }
                }
            }
            Tk.Panel {
                objectName: "preflightPanelHost"
                title: qsTr("Output checks")
                Tk.PreflightPanel {
                    id: preflight
                    objectName: "preflightPanel"
                    anchors.fill: parent
                    anchors.margins: Tk.Theme.space.sm
                    issues: root.issues
                    selectedIssueId: "overset-story-7"
                    profileId: "studio-printer"
                    onNavigateRequested: function(id, targetId) {
                        root.notice = qsTr("Navigation requested for %1 → %2.").arg(id).arg(targetId)
                    }
                    onRepairRequested: function(id, repairId) {
                        if (repairId === "open-frame-editor")
                            root.notice = qsTr("Frame editor requested for %1; layout must reflow in the host.").arg(id)
                        else if (repairId === "replace-font")
                            root.updateIssue(id, { repairAvailable: false, error: qsTr("Choose a font in the document host.") })
                        else root.notice = qsTr("Repair request %1 for issue %2 is awaiting the host.").arg(repairId).arg(id)
                    }
                    onRefreshRequested: function(profile) {
                        preflightStatus = "running"
                        errorText = qsTr("Checking profile %1…").arg(profile)
                    }
                }
            }
        }
    }
}
