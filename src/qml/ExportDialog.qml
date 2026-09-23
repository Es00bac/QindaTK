// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QindaTK as Tk

// Export delegates format decisions and stale/conflict guards to FormatRegistry.
Tk.Dialog {
    id: control
    objectName: "exportDialog"
    property var formatRegistry: null
    property var fileDialogAdapter: null
    property var documentSession: null
    property var document: null
    property url destinationUrl: ""
    property string formatId: ""
    property var plan: ({})
    property string jobId: ""
    property string statusText: ""
    property bool busy: false
    property bool allowLossy: false
    property bool allowExtensionMismatch: false
    property string conflictPolicy: "fail"
    property string lineEnding: "lf"
    property string entityId: ""
    property bool bom: false
    property string requestId: ""
    readonly property var operationFormats: formatRegistry ? formatRegistry.formatsForOperation("export") : []
    signal exportFinished(var result)
    signal operationFailed(string message)

    title: qsTr("Export")
    subtitle: qsTr("Choose an available format, destination, and conflict policy.")
    primaryText: ""
    secondaryText: ""

    function formatIndex() {
        for (let i = 0; i < operationFormats.length; ++i)
            if (operationFormats[i].formatId === formatId) return i
        return operationFormats.length > 0 ? 0 : -1
    }
    function selectedFormat() {
        for (let i = 0; i < operationFormats.length; ++i)
            if (operationFormats[i].formatId === formatId) return operationFormats[i]
        return null
    }
    function optionsForPlan() {
        const result = { conflictPolicy: conflictPolicy }
        if (allowLossy) result.allowLossy = true
        if (allowExtensionMismatch) result.allowExtensionMismatch = true
        if (formatId === "text.utf8") {
            result.lineEnding = lineEnding
            result.bom = bom
            if (entityId.length > 0) result.entityId = entityId
        }
        return result
    }
    function refreshPlan() {
        if (!formatRegistry || destinationUrl.toString().length === 0 || !formatId) { plan = ({}); return }
        const options = optionsForPlan()
        if (documentSession)
            plan = formatRegistry.preflightExportSession(documentSession, destinationUrl, formatId, options)
        else if (document)
            plan = formatRegistry.preflightExport(document, destinationUrl, formatId, options)
        else plan = ({ ok: false, error: {message: qsTr("No document is available to export.")} })
    }
    function chooseDestination() {
        if (!fileDialogAdapter) { statusText = qsTr("File picker is unavailable."); return }
        const f = selectedFormat()
        requestId = fileDialogAdapter.request("save", f && f.mimeTypes ? f.mimeTypes : [],
                                               destinationUrl, qsTr("Export file"), control.contentItem.Window.window)
    }
    function canStart() {
        return !busy && destinationUrl.toString().length > 0 && !!formatId && plan.ok === true
            && (!plan.requiresAcknowledgement || allowLossy)
            && (!plan.losses || plan.losses.length === 0 || allowLossy)
    }
    function startOrRetry() {
        if (!canStart()) return
        // Preserve registry-normalized expectedRevision/hash guards unchanged.
        const options = plan.options || optionsForPlan()
        jobId = documentSession
            ? formatRegistry.startExportSession(documentSession, destinationUrl, formatId, options)
            : formatRegistry.startExport(document, destinationUrl, formatId, options)
        if (!jobId) { statusText = formatRegistry.lastError; operationFailed(statusText); return }
        busy = true
        statusText = qsTr("Export queued.")
    }
    function jobState() { return formatRegistry && jobId ? formatRegistry.jobs.job(jobId) : ({}) }
    function cancelJob() { if (busy && formatRegistry && jobId) formatRegistry.jobs.cancelJob(jobId) }
    onFormatIdChanged: {
        allowLossy = false
        allowExtensionMismatch = false
        lineEnding = "lf"
        bom = false
        entityId = ""
        refreshPlan()
    }
    onDestinationUrlChanged: refreshPlan()
    onAllowLossyChanged: refreshPlan()
    onAllowExtensionMismatchChanged: refreshPlan()
    onConflictPolicyChanged: refreshPlan()
    onLineEndingChanged: refreshPlan()
    onBomChanged: refreshPlan()
    onEntityIdChanged: refreshPlan()

    content: Tk.Flex {
        direction: Tk.Flex.Column
        id: exportDialogContents
        gap: Tk.Theme.space.sm
        width: parent ? parent.width : Tk.Theme.size.dialogWidth
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("Destination"); width: Tk.Theme.size.control * 4 }
            Tk.Label { objectName: "exportDestinationPath"; text: control.destinationUrl.toString().length > 0 ? control.destinationUrl.toString() : qsTr("No destination selected"); elide: Text.ElideMiddle; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "exportChooseDestination"; text: qsTr("Browse…"); tooltip: qsTr("Choose an export destination"); onClicked: control.chooseDestination() }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("Format"); width: Tk.Theme.size.control * 4 }
            Tk.ComboBox {
                objectName: "exportFormat"
                currentIndex: control.formatIndex()
                model: control.operationFormats
                textRole: "title"
                tooltip: qsTr("Export format")
                Tk.Flex.grow: 1
                onActivated: function(index) {
                    const value = control.operationFormats[index]
                    if (value) control.formatId = value.formatId
                }
                Component.onCompleted: {
                    if (!control.formatId && currentIndex >= 0)
                        control.formatId = control.operationFormats[currentIndex].formatId
                }
            }
        }
        Tk.Flex {
            visible: control.formatId === "text.utf8"
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("Entity ID"); width: Tk.Theme.size.control * 4 }
            Tk.TextField {
                objectName: "exportEntityId"
                placeholderText: qsTr("Required when exporting multiple entities")
                text: control.entityId
                tooltip: qsTr("Stable entity identifier whose text is exported")
                onTextChanged: control.entityId = text
                Tk.Flex.grow: 1
            }
        }
        Tk.Flex {
            visible: control.formatId === "text.utf8"
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("Line endings"); width: Tk.Theme.size.control * 4 }
            Tk.ComboBox {
                objectName: "exportLineEnding"
                model: [qsTr("LF (Unix)"), qsTr("CRLF (Windows)")]
                currentIndex: control.lineEnding === "crlf" ? 1 : 0
                tooltip: qsTr("Text line ending format")
                onActivated: control.lineEnding = currentIndex === 1 ? "crlf" : "lf"
                Tk.Flex.grow: 1
            }
            Tk.CheckBox { objectName: "exportBom"; text: qsTr("UTF-8 BOM"); tooltip: qsTr("Write a UTF-8 byte order mark"); checked: control.bom; onToggled: control.bom = checked }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("If destination exists"); width: Tk.Theme.size.control * 4 }
            Tk.ComboBox {
                objectName: "exportConflictPolicy"
                model: [qsTr("Fail safely"), qsTr("Replace if unchanged")]
                currentIndex: control.conflictPolicy === "replaceIfHash" ? 1 : 0
                tooltip: qsTr("Choose how an existing destination is handled")
                onActivated: control.conflictPolicy = currentIndex === 1 ? "replaceIfHash" : "fail"
                Tk.Flex.grow: 1
            }
        }
        Tk.CheckBox {
            objectName: "exportExtensionMismatch"
            text: qsTr("Allow a file extension that does not match this format")
            tooltip: qsTr("Permit a destination suffix outside the format's advertised extensions")
            checked: control.allowExtensionMismatch
            onToggled: control.allowExtensionMismatch = checked
        }
        Tk.CheckBox {
            objectName: "exportLossAcknowledgement"
            visible: !!control.plan.requiresAcknowledgement || !!(control.plan.losses && control.plan.losses.length > 0)
            text: qsTr("I understand and accept the reported data loss")
            tooltip: qsTr("Explicitly acknowledge the preflight loss report")
            checked: control.allowLossy
            onToggled: control.allowLossy = checked
        }
        Tk.Label {
            objectName: "exportPreflightStatus"
            visible: text.length > 0
            wrapMode: Text.Wrap
            color: control.plan.ok === false ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
            text: control.statusText.length ? control.statusText
                : (control.plan.error && control.plan.error.message ? control.plan.error.message
                : control.plan.ok === true ? qsTr("Preflight passed.") : qsTr("Select a destination to preflight."))
        }
        Repeater {
            model: control.plan.warnings || []
            delegate: Tk.Notice { required property var modelData; objectName: "exportWarning"; text: modelData.message || String(modelData); variant: "warning"; width: parent.width }
        }
        Repeater {
            model: control.plan.losses || []
            delegate: Tk.Notice { required property var modelData; objectName: "exportLoss"; text: modelData.message || String(modelData); variant: "danger"; width: parent.width }
        }
        Tk.Label {
            objectName: "exportPreview"
            visible: !!control.plan.preview && (control.plan.preview.excerpt !== undefined || control.plan.preview.entityId !== undefined || control.plan.preview.entityCount !== undefined)
            wrapMode: Text.Wrap
            text: control.plan.preview
                ? (control.plan.preview.excerpt || qsTr("%1 entities, %2 properties").arg(control.plan.preview.entityCount || 0).arg(control.plan.preview.propertyCount || 0))
                : ""
        }
        Tk.ProgressBar { objectName: "exportProgress"; visible: control.busy; value: Number(control.jobState().progress || 0) / 100; tooltip: qsTr("Export job progress") }
    }
    footer: Tk.Box {
        objectName: "exportFooter"
        padding: Tk.Theme.space.md
        borderTop: Tk.Theme.size.border
        borderColor: Tk.Theme.color.divider
        Tk.Flex {
            direction: Tk.Flex.Row
            justify: Tk.Flex.End
            gap: Tk.Theme.space.sm
            Tk.Button { text: control.busy ? qsTr("Cancel export") : qsTr("Close"); onClicked: control.busy ? control.cancelJob() : control.reject() }
            Tk.Button {
                objectName: "exportStart"
                text: control.busy ? qsTr("Exporting…") : (control.jobState().state === "failed" ? qsTr("Retry") : qsTr("Export"))
                variant: "accent"
                enabled: control.canStart()
                onClicked: control.startOrRetry()
            }
        }
    }
    Connections {
        target: control.fileDialogAdapter
        function onFinished(id, result) {
            // The no-backend adapter can complete synchronously inside request().
            Qt.callLater(function() {
                if (id !== control.requestId) return
                control.requestId = ""
                if (result.status === "accepted" && result.urls && result.urls.length > 0) {
                    control.destinationUrl = result.urls[0]
                    control.statusText = ""
                } else if (result.status !== "canceled") control.statusText = result.message || qsTr("The file picker is unavailable.")
            })
        }
    }
    Connections {
        target: control.formatRegistry ? control.formatRegistry.jobs : null
        function onChanged() {
            const state = control.jobState()
            if (!state.state) return
            if (["running", "queued", "cancelling"].includes(state.state)) { control.busy = true; return }
            control.busy = false
            if (state.state === "succeeded") {
                control.statusText = qsTr("Export complete.")
                control.exportFinished(state.result || {})
            } else {
                control.statusText = state.error || (state.state === "cancelled" ? qsTr("Export canceled.") : qsTr("Export failed."))
                if (state.state !== "cancelled") control.operationFailed(control.statusText)
                control.refreshPlan()
            }
        }
    }
}
