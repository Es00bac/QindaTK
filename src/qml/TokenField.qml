// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT: committed tokens are {id,label,value,valid}; IDs survive
// reorder and label edits. completionProvider.request(query) returns a request
// ID, then finished(id,{candidates:[{label,value,valid}]}). Only the current
// ID, draft and edit revision may publish suggestions. Completion never edits
// a committed token; choosing a suggestion makes one PropertyBinding edit.
T.Control {
    id: control
    property Tk.PropertyBinding binding: null
    property var completionProvider: null
    property string duplicatePolicy: "value" // "value", "label", or "allow"
    property string label: ""
    property string tooltip: ""
    property string draft: ""
    property string errorMessage: ""
    property var suggestions: []
    property string completionRequestId: ""
    property string completionDraft: ""
    property int editRevision: 0
    property int completionRevision: -1
    property string completionDocumentRevision: ""
    property int selectedIndex: -1
    property var tokens: binding && !binding.mixed && binding.value ? binding.value : []
    readonly property bool readOnly: !binding || !binding.writable
    readonly property alias inputItem: input
    signal tokenAdded(string id, string value)
    signal tokenRemoved(string id)
    signal tokenReordered(string id, int from, int to)

    implicitWidth: body.implicitWidth
    implicitHeight: body.implicitHeight
    padding: 0
    Accessible.role: Accessible.EditableText
    Accessible.name: tooltip || label || qsTr("Tokens")
    Accessible.description: errorMessage

    Tk.TokenEditorService { id: tokenService }

    function commitTokens(next) {
        if (readOnly || !tokenService.commit(binding, next)) {
            errorMessage = binding ? binding.validationError : qsTr("Read only")
            return false
        }
        ++editRevision
        completionRequestId = ""
        suggestions = []
        selectedIndex = -1
        return true
    }
    function isDuplicate(token, list) {
        if (duplicatePolicy === "allow") return false
        for (let i = 0; i < list.length; ++i) {
            if (duplicatePolicy === "label" && list[i].label === token.label) return true
            if (duplicatePolicy === "value" && list[i].value === token.value) return true
        }
        return false
    }
    function appendCandidates(candidates) {
        const next = tokens.slice()
        const added = []
        for (let i = 0; i < candidates.length; ++i) {
            const candidate = candidates[i]
            const token = tokenService.createToken(candidate.label || String(candidate),
                                                   candidate.value || "", candidate.valid !== false)
            if (!token.id || isDuplicate(token, next)) continue
            next.push(token)
            added.push(token)
        }
        if (added.length === 0) return false
        if (!commitTokens(next)) return false
        draft = ""
        for (let i = 0; i < added.length; ++i) tokenAdded(added[i].id, added[i].value)
        return true
    }
    function commitDraft() {
        if (input.preeditText.length > 0) return false
        const parsed = tokenService.splitText(draft)
        if (!parsed.ok) { errorMessage = parsed.error; return false }
        const candidates = []
        for (let i = 0; i < parsed.labels.length; ++i) candidates.push({label: parsed.labels[i]})
        return appendCandidates(candidates)
    }
    function removeAt(index) {
        if (index < 0 || index >= tokens.length) return false
        const next = tokens.slice()
        const removed = next.splice(index, 1)[0]
        if (!commitTokens(next)) return false
        tokenRemoved(removed.id)
        return true
    }
    function move(from, to) {
        if (from < 0 || from >= tokens.length || to < 0 || to >= tokens.length || from === to) return false
        const next = tokens.slice()
        const item = next.splice(from, 1)[0]
        next.splice(to, 0, item)
        if (!commitTokens(next)) return false
        tokenReordered(item.id, from, to)
        return true
    }
    function queryCompletion() {
        suggestions = []
        completionRequestId = ""
        if (!completionProvider || !draft.trim() || input.preeditText.length > 0) return
        completionDraft = draft
        completionRevision = editRevision
        completionDocumentRevision = binding && binding.document ? String(binding.document.revision) : ""
        completionRequestId = completionProvider.request(draft)
    }
    Connections {
        target: control.binding
        function onChanged() {
            ++control.editRevision
            control.completionRequestId = ""
            control.suggestions = []
        }
    }
    Connections {
        target: control.completionProvider
        ignoreUnknownSignals: true
        function onFinished(requestId, result) {
            if (requestId !== control.completionRequestId || control.completionRevision !== control.editRevision
                    || control.completionDraft !== control.draft || input.preeditText.length > 0
                    || control.completionDocumentRevision !== (control.binding && control.binding.document
                        ? String(control.binding.document.revision) : "")) return
            control.completionRequestId = ""
            control.suggestions = result.candidates || []
        }
    }
    contentItem: Tk.Flex {
        id: body
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            id: row
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Repeater {
                model: control.tokens
                delegate: Tk.Flex {
                    required property var modelData
                    required property int index
                    objectName: "tokenItem"
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Tk.Icon {
                        visible: parent.modelData.valid === false
                        name: "circle-alert"
                        size: Tk.Theme.size.iconSm
                        color: Tk.Theme.color.danger
                    }
                    Tk.Chip {
                        objectName: "tokenChip"
                        small: true
                        text: parent.modelData.label
                        tooltip: parent.modelData.valid === false ? qsTr("Invalid token: %1").arg(text) : text
                        active: control.selectedIndex === parent.index
                        onClicked: control.selectedIndex = parent.index
                    }
                    Tk.IconButton {
                        objectName: "tokenRemove"
                        small: true
                        iconName: "x"
                        tooltip: qsTr("Remove %1").arg(parent.modelData.label)
                        enabled: !control.readOnly
                        onClicked: control.removeAt(parent.index)
                    }
                }
            }
            Tk.TextField {
                id: input
                objectName: "tokenInput"
                Tk.Flex.grow: 1
                text: control.draft
                readOnly: control.readOnly
                error: control.errorMessage.length > 0
                placeholderText: qsTr("Add token")
                tooltip: control.tooltip || control.label
                onTextEdited: {
                    control.draft = text
                    control.errorMessage = ""
                    control.selectedIndex = -1
                    if (preeditText.length === 0 && /[,;\r\n]/.test(text)) control.commitDraft()
                    else control.queryCompletion()
                }
                onAccepted: control.commitDraft()
                Keys.onPressed: function(event) {
                    if (input.preeditText.length > 0) return
                    if (event.key === Qt.Key_Backspace && input.text.length === 0 && control.tokens.length > 0) {
                        if (control.selectedIndex < 0) control.selectedIndex = control.tokens.length - 1
                        else control.removeAt(control.selectedIndex)
                        event.accepted = true
                    } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Left && control.selectedIndex > 0) {
                        const index = control.selectedIndex
                        if (control.move(index, index - 1)) control.selectedIndex = index - 1
                        event.accepted = true
                    } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Right
                            && control.selectedIndex >= 0 && control.selectedIndex + 1 < control.tokens.length) {
                        const index = control.selectedIndex
                        if (control.move(index, index + 1)) control.selectedIndex = index + 1
                        event.accepted = true
                    } else if (event.key === Qt.Key_Delete && control.selectedIndex >= 0) {
                        control.removeAt(control.selectedIndex)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Left && input.cursorPosition === 0 && control.tokens.length > 0) {
                        control.selectedIndex = Math.max(0, control.selectedIndex < 0 ? control.tokens.length - 1 : control.selectedIndex - 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Right && control.selectedIndex >= 0) {
                        control.selectedIndex = control.selectedIndex + 1 < control.tokens.length ? control.selectedIndex + 1 : -1
                        event.accepted = true
                    } else if (event.key === Qt.Key_Escape) {
                        control.draft = ""
                        control.suggestions = []
                        control.completionRequestId = ""
                        control.selectedIndex = -1
                        event.accepted = true
                    }
                }
            }
        }
        Tk.Flex {
            visible: control.suggestions.length > 0
            direction: Tk.Flex.Column
            Repeater {
                model: control.suggestions
                delegate: Tk.Button {
                    required property var modelData
                    objectName: "tokenSuggestion"
                    text: modelData.label || String(modelData)
                    tooltip: text
                    onClicked: control.appendCandidates([modelData])
                }
            }
        }
    }
}
