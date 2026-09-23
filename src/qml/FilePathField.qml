// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: URI strings are canonical document values. Validation
// inspects local files only; remote availability is explicitly unknown.
// Browse and validation responses are accepted only for the current request.
T.Control {
    id: control
    property Tk.PropertyBinding binding: null
    property Tk.FileDialogAdapter dialogAdapter: null
    property Tk.UriValidator uriValidator: Tk.UriValidator {}
    property string mode: "open"
    property var mimeTypes: []
    property string label: ""
    property string tooltip: ""
    property string draft: ""
    property string validationStatus: ""
    property string errorMessage: ""
    property var recentPaths: []
    property string browseRequestId: ""
    property string validationRequestId: ""
    property int draftRevision: 0
    property int validationRevision: -1
    property int browseRevision: -1
    property string validationDocumentRevision: ""
    property string browseDocumentRevision: ""
    readonly property bool readOnly: !binding || !binding.writable
    readonly property string uri: binding && !binding.mixed ? String(binding.value || "") : ""
    readonly property alias inputItem: input
    signal pathCommitted(string uri)
    signal openRequested(string uri)
    signal browseRequested(string requestId)

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    padding: 0
    Accessible.role: Accessible.EditableText
    Accessible.name: tooltip || label || qsTr("Location")
    Accessible.description: errorMessage || (validationStatus === "unknown" ? qsTr("Remote location not checked") : "")

    function syncCommitted() {
        if (!input.activeFocus && !browseRequestId) {
            draft = uri
            inspectDraft()
        }
    }
    function documentRevision() {
        return binding && binding.document ? String(binding.document.revision) : ""
    }
    function inspectDraft() {
        if (!uriValidator) return
        if (validationRequestId) uriValidator.cancel(validationRequestId)
        validationRevision = draftRevision
        validationDocumentRevision = documentRevision()
        validationRequestId = uriValidator.request(draft, mode)
    }
    function setDraft(text) {
        draft = text
        ++draftRevision
        errorMessage = ""
        validationStatus = ""
        inspectDraft()
    }
    function commitDraft() {
        if (readOnly || !uriValidator) return false
        const result = uriValidator.inspect(draft, mode)
        validationStatus = result.status || "error"
        errorMessage = result.valid ? "" : result.message
        if (!result.valid || !binding.begin()) return false
        if (!binding.update(result.uri)) { binding.cancel(); errorMessage = binding.validationError; return false }
        if (!binding.commit()) { errorMessage = binding.validationError; return false }
        const latest = recentPaths.slice()
        const oldIndex = latest.indexOf(result.uri)
        if (oldIndex >= 0) latest.splice(oldIndex, 1)
        latest.unshift(result.uri)
        recentPaths = latest.slice(0, 10)
        pathCommitted(result.uri)
        return true
    }
    function browse() {
        if (readOnly || !dialogAdapter || !dialogAdapter.available) return ""
        if (browseRequestId) dialogAdapter.cancel(browseRequestId)
        browseRevision = draftRevision
        browseDocumentRevision = documentRevision()
        browseRequestId = dialogAdapter.request(mode, mimeTypes, uri, label)
        if (browseRequestId) browseRequested(browseRequestId)
        return browseRequestId
    }
    function cancelBrowse() {
        if (!browseRequestId) return
        const id = browseRequestId
        browseRequestId = ""
        if (dialogAdapter) dialogAdapter.cancel(id)
    }
    Component.onCompleted: syncCommitted()
    Connections {
        target: control.binding
        function onChanged() { control.syncCommitted() }
    }
    Connections {
        target: control.uriValidator
        function onFinished(requestId, result) {
            if (requestId !== control.validationRequestId || control.validationRevision !== control.draftRevision
                    || control.validationDocumentRevision !== control.documentRevision()) return
            control.validationRequestId = ""
            control.validationStatus = result.status
            control.errorMessage = result.valid ? "" : result.message
        }
    }
    Connections {
        target: control.dialogAdapter
        function onFinished(requestId, result) {
            if (requestId !== control.browseRequestId) return
            control.browseRequestId = ""
            if (control.browseRevision !== control.draftRevision
                    || control.browseDocumentRevision !== control.documentRevision()) return
            if (result.status === "accepted" && result.encodedUrls.length > 0) {
                // AGENT-CONTRACT: FileDialogAdapter provides FullyEncoded URI
                // strings; QML QUrl.toString() decodes escapes on some Qt versions.
                control.setDraft(result.encodedUrls[0])
                control.commitDraft()
            } else if (result.status === "error" || result.status === "unsupported") {
                control.errorMessage = result.message
            }
        }
    }
    contentItem: Tk.Flex {
        id: row
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        Tk.TextField {
            id: input
            objectName: "pathInput"
            Tk.Flex.flex: 1
            text: control.draft
            readOnly: control.readOnly
            error: control.errorMessage.length > 0 || (control.binding && control.binding.validationError.length > 0)
            placeholderText: qsTr("Location URI or local path")
            tooltip: control.tooltip || control.label
            onTextEdited: control.setDraft(text)
            onAccepted: control.commitDraft()
            Keys.onEscapePressed: function(event) {
                control.cancelBrowse()
                control.errorMessage = ""
                control.setDraft(control.uri)
                event.accepted = true
            }
        }
        Tk.IconButton {
            objectName: "pathBrowse"
            iconName: "folder-open"
            tooltip: qsTr("Browse for location")
            enabled: !control.readOnly && control.dialogAdapter && control.dialogAdapter.available
            onClicked: control.browse()
        }
        Tk.IconButton {
            objectName: "pathOpen"
            iconName: "external-link"
            tooltip: qsTr("Open location")
            enabled: control.uri.length > 0
            onClicked: control.openRequested(control.uri)
        }
    }
}
