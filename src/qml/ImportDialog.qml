// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Window
import QindaTK as Tk

// Import is format-registry driven; adoption occurs only after job success.
Tk.Dialog {
    id: control
    objectName: "importDialog"
    property var formatRegistry: null
    property var fileDialogAdapter: null
    property var documentManager: null
    property url sourceUrl: ""
    property string formatId: ""
    property var plan: ({})
    property string jobId: ""
    property string statusText: ""
    property bool allowLossy: false
    property bool busy: false
    property string requestId: ""
    readonly property var operationFormats: formatRegistry ? formatRegistry.formatsForOperation("import") : []
    signal importAdopted(var session)
    signal operationFailed(string message)

    title: qsTr("Import")
    subtitle: qsTr("Choose a supported format and review its preflight report.")
    primaryText: ""
    secondaryText: ""

    function formatIndex() {
        let detected = formatId
        if (!detected && sourceUrl && formatRegistry) detected = formatRegistry.detectImport(sourceUrl)
        for (let i = 0; i < operationFormats.length; ++i)
            if (operationFormats[i].formatId === detected) return i
        return operationFormats.length > 0 ? 0 : -1
    }
    function selectedFormat() {
        for (let i = 0; i < operationFormats.length; ++i)
            if (operationFormats[i].formatId === formatId) return operationFormats[i]
        return null
    }
    function refreshPlan() {
        if (!formatRegistry || sourceUrl.toString().length === 0 || !formatId) { plan = ({}); return }
        plan = formatRegistry.preflightImport(sourceUrl, formatId, allowLossy ? {allowLossy: true} : {})
    }
    function chooseSource() {
        if (!fileDialogAdapter) { statusText = qsTr("File picker is unavailable."); return }
        const f = selectedFormat()
        requestId = fileDialogAdapter.request("open", f && f.mimeTypes ? f.mimeTypes : [],
                                               sourceUrl, qsTr("Import file"), control.contentItem.Window.window)
    }
    function canStart() {
        return !busy && !!documentManager && sourceUrl.toString().length > 0 && !!formatId && plan.ok === true
            && (!plan.requiresAcknowledgement || allowLossy)
            && (!plan.losses || plan.losses.length === 0 || allowLossy)
    }
    function startOrRetry() {
        if (!canStart()) return
        const options = plan.options || {}
        jobId = formatRegistry.startImport(sourceUrl, formatId, options)
        if (!jobId) { statusText = formatRegistry.lastError; operationFailed(statusText); return }
        busy = true
        statusText = qsTr("Import queued.")
    }
    function jobState() { return formatRegistry && jobId ? formatRegistry.jobs.job(jobId) : ({}) }
    function cancelJob() { if (busy && formatRegistry && jobId) formatRegistry.jobs.cancelJob(jobId) }
    onFormatIdChanged: { allowLossy = false; refreshPlan() }
    onSourceUrlChanged: refreshPlan()
    onAllowLossyChanged: refreshPlan()

    content: Tk.Flex {
        direction: Tk.Flex.Column
        id: importDialogContents
        gap: Tk.Theme.space.sm
        width: parent ? parent.width : Tk.Theme.size.dialogWidth
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("Source"); width: Tk.Theme.size.control * 3 }
            Tk.Label { objectName: "importSourcePath"; text: control.sourceUrl.toString().length > 0 ? control.sourceUrl.toString() : qsTr("No file selected"); elide: Text.ElideMiddle; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "importChooseSource"; text: qsTr("Browse…"); tooltip: qsTr("Choose an import source"); onClicked: control.chooseSource() }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("Format"); width: Tk.Theme.size.control * 3 }
            Tk.ComboBox {
                id: importFormatPicker
                objectName: "importFormat"
                currentIndex: control.formatIndex()
                model: control.operationFormats
                textRole: "title"
                tooltip: qsTr("Import format")
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
        Tk.CheckBox {
            objectName: "importLossAcknowledgement"
            visible: !!control.plan.requiresAcknowledgement || !!(control.plan.losses && control.plan.losses.length > 0)
            text: qsTr("I understand and accept the reported data loss")
            tooltip: qsTr("Acknowledge the preflight loss report")
            checked: control.allowLossy
            onToggled: control.allowLossy = checked
        }
        Tk.Label {
            objectName: "importPreflightStatus"
            visible: text.length > 0
            wrapMode: Text.Wrap
            color: control.plan.ok === false ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
            text: control.statusText.length ? control.statusText
                : (control.plan.error && control.plan.error.message ? control.plan.error.message
                : control.plan.ok === true ? qsTr("Preflight passed.") : qsTr("Select a source file to preflight."))
        }
        Repeater {
            model: control.plan.warnings || []
            delegate: Tk.Notice {
                required property var modelData
                objectName: "importWarning"
                text: modelData.message || String(modelData)
                variant: "warning"
                width: parent.width
            }
        }
        Repeater {
            model: control.plan.losses || []
            delegate: Tk.Notice {
                required property var modelData
                objectName: "importLoss"
                text: modelData.message || String(modelData)
                variant: "danger"
                width: parent.width
            }
        }
        Tk.Label {
            objectName: "importPreview"
            visible: !!control.plan.preview && (control.plan.preview.excerpt !== undefined || control.plan.preview.entityCount !== undefined)
            wrapMode: Text.Wrap
            text: control.plan.preview
                ? (control.plan.preview.excerpt || qsTr("%1 entities, %2 properties").arg(control.plan.preview.entityCount || 0).arg(control.plan.preview.propertyCount || 0))
                : ""
        }
        Tk.ProgressBar {
            objectName: "importProgress"
            visible: control.busy
            value: Number(control.jobState().progress || 0) / 100
            tooltip: qsTr("Import job progress")
        }
    }
    footer: Tk.Box {
        objectName: "importFooter"
        padding: Tk.Theme.space.md
        borderTop: Tk.Theme.size.border
        borderColor: Tk.Theme.color.divider
        Tk.Flex {
            direction: Tk.Flex.Row
            justify: Tk.Flex.End
            gap: Tk.Theme.space.sm
            Tk.Button { text: control.busy ? qsTr("Cancel import") : qsTr("Close"); onClicked: control.busy ? control.cancelJob() : control.reject() }
            Tk.Button {
                objectName: "importStart"
                text: control.busy ? qsTr("Importing…") : (control.jobState().state === "failed" ? qsTr("Retry") : qsTr("Import"))
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
                    control.sourceUrl = result.urls[0]
                    const detected = control.formatRegistry.detectImport(control.sourceUrl)
                    if (detected) control.formatId = detected
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
                const session = control.formatRegistry.adoptImport(control.documentManager, control.jobId)
                if (session) { control.statusText = qsTr("Import complete."); control.importAdopted(session); control.accept() }
                else { control.statusText = control.formatRegistry.lastError; control.operationFailed(control.statusText) }
            } else {
                control.statusText = state.error || (state.state === "cancelled" ? qsTr("Import canceled.") : qsTr("Import failed."))
                if (state.state !== "cancelled") control.operationFailed(control.statusText)
                control.refreshPlan()
            }
        }
    }
}
