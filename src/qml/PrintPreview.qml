// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Settings and print jobs belong to a PrintAdapter. The host inserts the same
// document page renderer used for export; this panel never paints fake pages.
Item {
    id: control
    property int pageCount: 0
    property int currentPage: 0 // zero-based
    property var pageRange: ({ startPage: 1, endPage: Math.max(1, pageCount) }) // one-based inclusive
    property var settings: ({ paperId: "a4", orientation: "portrait", scaleMode: "fit",
                              scalePercent: 100, duplex: "simplex", copies: 1, printerId: "", colorMode: "color" })
    property var capabilities: ({ paperIds: ["a4"], orientations: ["portrait"], scaleModes: ["fit"],
                                  duplexModes: ["simplex"], colorModes: ["color"], maxCopies: 1 })
    property var job: ({ id: "", status: "idle", progress: 0, error: "", affectedStart: 0, affectedEnd: 0 })
    property string previewError: ""
    property string tooltip: qsTr("Preview document pages and submit a print request")
    default property alias pageContent: previewHost.data
    readonly property bool validRange: pageRange.startPage >= 1 && pageRange.endPage >= pageRange.startPage
                                       && pageRange.endPage <= pageCount
    readonly property bool printPending: job.status === "pending" || job.status === "running"
                                         || job.status === "cancelling"
    readonly property string rangeError: pageCount < 1 ? qsTr("No laid out pages are available.")
                                      : !validRange ? qsTr("Page range must stay within 1–%1.").arg(pageCount) : ""
    signal pageRequested(int pageIndex)
    signal pageRangeChangeRequested(var pageRange)
    signal settingsChangeRequested(var settings)
    signal previewRefreshRequested(var pageRange, var settings)
    signal printRequested(var settings, var pageRange)
    signal cancelPrintRequested(string jobId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 4
    implicitHeight: layout.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function clone(value) { return Object.assign({}, value) }
    function requestRange(startPage, endPage) {
        const next = { startPage: Math.floor(startPage), endPage: Math.floor(endPage) }
        if (enabled && next.startPage >= 1 && next.endPage >= next.startPage && next.endPage <= pageCount)
            pageRangeChangeRequested(next)
    }
    function requestSetting(key, value) {
        if (!enabled) return
        const next = clone(settings)
        next[key] = value
        settingsChangeRequested(next)
    }
    function requestPreview() {
        if (enabled && validRange) previewRefreshRequested(clone(pageRange), clone(settings))
    }
    function submitOrCancel() {
        if (!enabled) return
        if (printPending) cancelPrintRequested(job.id)
        else if (validRange && rangeError.length === 0) printRequested(clone(settings), clone(pageRange))
    }

    Tk.Flex {
        id: layout
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("PRINT PREVIEW"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption {
                objectName: "printPageCount"
                text: control.pageCount > 0 ? qsTr("Page %1 of %2").arg(control.currentPage + 1).arg(control.pageCount)
                                            : qsTr("No pages")
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "printPreviousPage"
                text: qsTr("Previous")
                iconName: "chevron-left"
                tooltip: qsTr("Show previous page")
                enabled: control.currentPage > 0
                onClicked: control.pageRequested(control.currentPage - 1)
            }
            Tk.Button {
                objectName: "printNextPage"
                text: qsTr("Next")
                iconName: "chevron-right"
                tooltip: qsTr("Show next page")
                enabled: control.currentPage + 1 < control.pageCount
                onClicked: control.pageRequested(control.currentPage + 1)
            }
            Tk.Button {
                objectName: "printRefreshPreview"
                text: qsTr("Refresh preview")
                iconName: "refresh-cw"
                tooltip: qsTr("Request updated pages for the current range and settings")
                enabled: control.validRange && !control.printPending
                onClicked: control.requestPreview()
            }
        }
        Item {
            id: previewFrame
            objectName: "printPageSurface"
            implicitHeight: Tk.Theme.size.panelMinWidth * 2.2
            clip: true
            Rectangle { anchors.fill: parent; color: Tk.Theme.color.canvas; border.color: Tk.Theme.color.border; border.width: Tk.Theme.size.border }
            Item {
                id: previewHost
                anchors.fill: parent
                anchors.margins: Tk.Theme.space.sm
                visible: control.previewError.length === 0 && control.pageCount > 0
            }
            Tk.Notice {
                objectName: "printPreviewStatus"
                anchors.centerIn: parent
                width: parent.width - 2 * Tk.Theme.space.md
                visible: control.previewError.length > 0 || control.pageCount < 1
                variant: control.previewError.length > 0 ? "danger" : "info"
                text: control.previewError.length > 0 ? control.previewError
                     : qsTr("Waiting for the document page renderer. No preview is drawn by this control.")
            }
        }
        Tk.Caption {
            objectName: "printPreviewError"
            visible: control.previewError.length > 0
            color: Tk.Theme.color.danger
            text: control.previewError
            wrapMode: Text.WordWrap
        }
        Tk.Overline { title: qsTr("OUTPUT SETTINGS") }
        Tk.Grid {
            columns: "1fr 1fr"
            gap: Tk.Theme.space.xs
            columnGap: Tk.Theme.space.md
            Tk.PropertyRow {
                label: qsTr("Paper")
                Tk.ComboBox {
                    objectName: "printPaper"
                    model: control.capabilities.paperOptions || []
                    textRole: "label"
                    currentIndex: {
                        for (let i = 0; i < model.length; ++i) if (model[i].id === control.settings.paperId) return i
                        return -1
                    }
                    placeholderText: qsTr("Unsupported paper")
                    tooltip: qsTr("Output paper size")
                    enabled: model.length > 0
                    onActivated: function(index) { control.requestSetting("paperId", model[index].id) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Orientation")
                Tk.ComboBox {
                    objectName: "printOrientation"
                    model: control.capabilities.orientationOptions || []
                    textRole: "label"
                    currentIndex: {
                        for (let i = 0; i < model.length; ++i) if (model[i].id === control.settings.orientation) return i
                        return -1
                    }
                    placeholderText: qsTr("Unsupported")
                    tooltip: qsTr("Output page orientation")
                    enabled: model.length > 0
                    onActivated: function(index) { control.requestSetting("orientation", model[index].id) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Scale")
                Tk.ComboBox {
                    objectName: "printScaleMode"
                    model: control.capabilities.scaleOptions || []
                    textRole: "label"
                    currentIndex: {
                        for (let i = 0; i < model.length; ++i) if (model[i].id === control.settings.scaleMode) return i
                        return -1
                    }
                    placeholderText: qsTr("Unsupported")
                    tooltip: qsTr("Page scaling mode")
                    enabled: model.length > 0
                    onActivated: function(index) { control.requestSetting("scaleMode", model[index].id) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Scale %")
                Tk.NumberField {
                    objectName: "printScalePercent"
                    value: control.settings.scalePercent || 100
                    from: 1
                    to: 1000
                    stepSize: 1
                    tooltip: qsTr("Custom scale percentage")
                    enabled: control.settings.scaleMode === "percent"
                    onValueModified: function(value) { control.requestSetting("scalePercent", value) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Duplex")
                Tk.ComboBox {
                    objectName: "printDuplex"
                    model: control.capabilities.duplexOptions || []
                    textRole: "label"
                    currentIndex: {
                        for (let i = 0; i < model.length; ++i) if (model[i].id === control.settings.duplex) return i
                        return -1
                    }
                    placeholderText: qsTr("Unavailable")
                    tooltip: qsTr("Two-sided print mode")
                    enabled: model.length > 0
                    onActivated: function(index) { control.requestSetting("duplex", model[index].id) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Color")
                Tk.ComboBox {
                    objectName: "printColorMode"
                    model: control.capabilities.colorOptions || []
                    textRole: "label"
                    currentIndex: {
                        for (let i = 0; i < model.length; ++i) if (model[i].id === control.settings.colorMode) return i
                        return -1
                    }
                    placeholderText: qsTr("Unavailable")
                    tooltip: qsTr("Output colour mode")
                    enabled: model.length > 0
                    onActivated: function(index) { control.requestSetting("colorMode", model[index].id) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Copies")
                Tk.NumberField {
                    objectName: "printCopies"
                    value: control.settings.copies || 1
                    from: 1
                    to: control.capabilities.maxCopies || 1
                    stepSize: 1
                    tooltip: qsTr("Number of copies")
                    enabled: (control.capabilities.maxCopies || 1) > 1
                    onValueModified: function(value) { control.requestSetting("copies", value) }
                }
            }
        }
        Tk.Caption {
            objectName: "printCapabilityStatus"
            visible: (control.capabilities.unavailableOptions || []).length > 0
            color: Tk.Theme.color.warning
            text: qsTr("Unavailable: %1").arg((control.capabilities.unavailableOptions || []).join(", "))
            wrapMode: Text.WordWrap
        }
        Tk.Grid {
            columns: "auto 1fr 1fr"
            gap: Tk.Theme.space.xs
            Tk.Caption { text: qsTr("Pages"); color: Tk.Theme.color.textMuted }
            Tk.NumberField {
                objectName: "printRangeStart"
                label: qsTr("From")
                value: control.pageRange.startPage || 1
                from: 1
                to: Math.max(1, control.pageCount)
                tooltip: qsTr("First page in the inclusive print range")
                onValueModified: function(value) { control.requestRange(value, control.pageRange.endPage) }
            }
            Tk.NumberField {
                objectName: "printRangeEnd"
                label: qsTr("To")
                value: control.pageRange.endPage || Math.max(1, control.pageCount)
                from: 1
                to: Math.max(1, control.pageCount)
                tooltip: qsTr("Last page in the inclusive print range")
                onValueModified: function(value) { control.requestRange(control.pageRange.startPage, value) }
            }
        }
        Tk.Caption {
            objectName: "printRangeError"
            visible: control.rangeError.length > 0
            color: Tk.Theme.color.danger
            text: control.rangeError
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.ProgressBar {
                objectName: "printJobProgress"
                visible: control.printPending
                value: control.job.progress || 0
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Caption {
                objectName: "printJobStatus"
                visible: control.job.status !== "idle"
                text: control.job.error || qsTr("Job %1 · %2").arg(control.job.id).arg(control.job.status)
                color: control.job.status === "failed" ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                wrapMode: Text.WordWrap
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Button {
                objectName: "printSubmit"
                text: control.printPending ? qsTr("Cancel print") : qsTr("Print")
                iconName: control.printPending ? "x" : "printer"
                variant: control.printPending ? "danger" : "accent"
                tooltip: control.printPending ? qsTr("Request cancellation of the pending print job")
                                              : qsTr("Submit a print job using the selected settings")
                enabled: control.printPending ? control.job.id.length > 0 : control.validRange && control.pageCount > 0
                onClicked: control.submitOrCancel()
            }
        }
    }
}
