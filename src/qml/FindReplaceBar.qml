// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Revision-tagged find/replace over a host-owned text document. The service
// performs document edits and groups replace-all into one undo operation.
Tk.Box {
    id: control
    objectName: "findReplaceBar"
    property var findReplaceService: null
    property string searchScope: "document"
    property int selectionStart: 0
    property int selectionLength: 0
    property string activeRequestId: ""
    property string acceptedQuery: ""
    property int acceptedRevision: -1
    property int requestedRevision: -1
    property bool performingReplace: false
    property bool wasComposing: false
    property string operationMessage: ""
    property string operationError: ""
    property bool caseSensitive: false
    property bool wholeWord: false
    property bool pattern: false
    property string replacement: ""
    readonly property bool resultsCurrent: !!findReplaceService
        && acceptedRevision >= 0 && acceptedRevision === findReplaceService.revision
        && acceptedQuery === queryField.text
    readonly property bool hasCurrentMatch: resultsCurrent
        && findReplaceService.matchCount > 0 && findReplaceService.currentMatch >= 0

    signal searchCompleted(int matchCount, int revision)
    signal replaceCompleted(int count, bool all)
    signal operationRejected(string message)

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Find and replace")

    function cancelActiveSearch() {
        if (findReplaceService && activeRequestId.length > 0)
            findReplaceService.cancelSearch(activeRequestId)
        activeRequestId = ""
    }
    function scopeForSearch() {
        if (searchScope === "selection")
            return { start: selectionStart, length: selectionLength }
        return ({})
    }
    function searchNow(query) {
        if (!findReplaceService || findReplaceService.composing || queryField.inputMethodComposing) return ""
        cancelActiveSearch()
        acceptedQuery = query
        acceptedRevision = -1
        requestedRevision = -1
        operationError = ""
        operationMessage = ""
        if (searchScope === "selection" && selectionLength <= 0) {
            operationMessage = qsTr("Select text to search within the selection")
            return ""
        }
        const options = { caseSensitive: caseSensitive, wholeWord: wholeWord, pattern: pattern }
        requestedRevision = findReplaceService.revision
        activeRequestId = findReplaceService.search(query, options, scopeForSearch())
        return activeRequestId
    }
    function requestSearch(query) {
        if (findReplaceService && activeRequestId.length > 0)
            findReplaceService.cancelSearch(activeRequestId)
        activeRequestId = ""
        acceptedQuery = query
        acceptedRevision = -1
        requestedRevision = -1
        operationError = ""
    }
    function setCurrentMatch(index) {
        if (!hasCurrentMatch) return false
        const count = findReplaceService.matchCount
        const normalized = ((index % count) + count) % count
        return findReplaceService.setCurrentMatch(normalized)
    }
    function moveMatch(offset) {
        if (!hasCurrentMatch) return false
        return setCurrentMatch(findReplaceService.currentMatch + offset)
    }
    function replaceOne() {
        if (!hasCurrentMatch || findReplaceService.readOnly || findReplaceService.composing) return false
        performingReplace = true
        const result = findReplaceService.replaceCurrent(acceptedRevision, replacementField.text)
        performingReplace = false
        if (!result.ok) {
            operationError = String(result.error || qsTr("The document changed; search again"))
            operationRejected(operationError)
            return false
        }
        operationMessage = qsTr("Replaced one match")
        replaceCompleted(1, false)
        searchNow(queryField.text)
        return true
    }
    function replaceEveryMatch() {
        if (!resultsCurrent || findReplaceService.readOnly || findReplaceService.composing
            || findReplaceService.matchCount === 0) return false
        const count = findReplaceService.matchCount
        performingReplace = true
        const result = findReplaceService.replaceAll(acceptedRevision, replacementField.text)
        performingReplace = false
        if (!result.ok) {
            operationError = String(result.error || qsTr("The document changed; search again"))
            operationRejected(operationError)
            return false
        }
        const replaced = Number(result.replaced !== undefined ? result.replaced : count)
        operationMessage = qsTr("Replaced %1 matches").arg(replaced)
        replaceCompleted(replaced, true)
        searchNow(queryField.text)
        return true
    }

    Connections {
        target: control.findReplaceService
        function onResultsReady(requestId, revision, results, currentMatch, error) {
            if (requestId !== control.activeRequestId || control.acceptedQuery !== queryField.text
                || revision !== control.findReplaceService.revision) return
            control.activeRequestId = requestId
            control.acceptedRevision = revision
            if (error) control.operationError = error
            control.searchCompleted(control.findReplaceService.matchCount, revision)
        }
        function onOperationFinished(requestId, result) {
            if (requestId !== control.activeRequestId) return
            if (!result.ok) control.operationError = String(result.error || qsTr("Operation failed"))
        }
        function onStateChanged() {
            if (control.performingReplace) return
            const revisionChanged = control.requestedRevision >= 0
                && control.requestedRevision !== control.findReplaceService.revision
            const compositionEnded = control.wasComposing && !control.findReplaceService.composing
            control.wasComposing = control.findReplaceService.composing
            if (revisionChanged) {
                // cancelSearch can emit stateChanged synchronously; advance the
                // observed revision before cancellation to avoid reentrant loops.
                control.requestedRevision = control.findReplaceService.revision
                control.cancelActiveSearch()
                control.acceptedRevision = -1
            } else if (control.acceptedRevision >= 0
                       && control.acceptedRevision !== control.findReplaceService.revision) {
                control.acceptedRevision = -1
            }
            if ((revisionChanged || compositionEnded) && !control.findReplaceService.composing
                && queryField.text.length > 0)
                control.searchNow(queryField.text)
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        width: parent ? parent.width : Tk.Theme.size.dialogWidth

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.SearchField {
                id: queryField
                objectName: "findQuery"
                placeholderText: qsTr("Find")
                tooltip: qsTr("Find text in the current document or selection")
                debounce: Tk.Theme.motion.base
                onSearched: function(query) { control.searchNow(query) }
                onTextEdited: control.requestSearch(text)
                Keys.onEscapePressed: {
                    if (text.length > 0) { clearText(); control.requestSearch("") }
                    else control.cancelActiveSearch()
                }
                Keys.onPressed: function(event) {
                    if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_G) {
                        control.moveMatch(event.modifiers & Qt.ShiftModifier ? -1 : 1)
                        event.accepted = true
                    }
                }
                Tk.Flex.grow: 1
            }
            Tk.ComboBox {
                id: scopePicker
                objectName: "findScope"
                model: [qsTr("Document"), qsTr("Selection")]
                currentIndex: control.searchScope === "selection" ? 1 : 0
                tooltip: qsTr("Choose whether to search the document or selected text")
                onActivated: function(index) {
                    control.searchScope = index === 1 ? "selection" : "document"
                    control.searchNow(queryField.text)
                }
            }
            Tk.Button {
                objectName: "findPrevious"
                text: qsTr("Previous")
                tooltip: qsTr("Select the previous match")
                enabled: control.hasCurrentMatch
                onClicked: control.moveMatch(-1)
            }
            Tk.Button {
                objectName: "findNext"
                text: qsTr("Next")
                tooltip: qsTr("Select the next match")
                enabled: control.hasCurrentMatch
                onClicked: control.moveMatch(1)
            }
            Tk.Button {
                objectName: "findCancel"
                visible: !!control.findReplaceService && control.findReplaceService.busy
                text: qsTr("Cancel")
                tooltip: qsTr("Cancel the active search")
                onClicked: control.cancelActiveSearch()
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.md
            Tk.CheckBox {
                objectName: "findCaseSensitive"
                text: qsTr("Case sensitive")
                checked: control.caseSensitive
                tooltip: qsTr("Match letter case")
                onToggled: { control.caseSensitive = checked; control.searchNow(queryField.text) }
            }
            Tk.CheckBox {
                objectName: "findWholeWord"
                text: qsTr("Whole word")
                checked: control.wholeWord
                tooltip: qsTr("Match whole words only")
                onToggled: { control.wholeWord = checked; control.searchNow(queryField.text) }
            }
            Tk.CheckBox {
                objectName: "findPattern"
                text: qsTr("Pattern")
                checked: control.pattern
                tooltip: qsTr("Treat the query as a regular expression")
                onToggled: { control.pattern = checked; control.searchNow(queryField.text) }
            }
            Tk.Label {
                objectName: "findMatchCount"
                text: !control.findReplaceService ? qsTr("No search service")
                    : control.findReplaceService.composing ? qsTr("Finish composing text to search")
                    : control.findReplaceService.readOnly ? qsTr("Read only")
                    : control.findReplaceService.busy ? qsTr("Searching…")
                    : queryField.text.length === 0 ? qsTr("Enter text to search")
                    : !control.resultsCurrent ? qsTr("Search results are stale")
                    : control.findReplaceService.matchCount === 0 ? qsTr("No matches")
                    : qsTr("%1 of %2").arg(control.findReplaceService.currentMatch + 1)
                        .arg(control.findReplaceService.matchCount)
                color: control.operationError.length > 0 ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
                Accessible.role: Accessible.StatusBar
                Tk.Flex.grow: 1
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.TextField {
                id: replacementField
                objectName: "replaceText"
                placeholderText: qsTr("Replace with")
                tooltip: qsTr("Replacement text")
                onTextEdited: control.replacement = text
                Tk.Flex.grow: 1
            }
            Tk.Button {
                objectName: "replaceCurrent"
                text: qsTr("Replace")
                tooltip: qsTr("Replace the selected match")
                enabled: control.hasCurrentMatch && !!control.findReplaceService
                    && !control.findReplaceService.readOnly && !control.findReplaceService.composing
                onClicked: control.replaceOne()
            }
            Tk.Button {
                objectName: "replaceAll"
                text: qsTr("Replace all")
                variant: "accent"
                tooltip: qsTr("Replace all current matches as one undoable operation")
                enabled: control.resultsCurrent && !!control.findReplaceService
                    && control.findReplaceService.matchCount > 0
                    && !control.findReplaceService.readOnly && !control.findReplaceService.composing
                onClicked: control.replaceEveryMatch()
            }
        }
        Tk.Label {
            objectName: "findOperationMessage"
            visible: control.operationMessage.length > 0 || control.operationError.length > 0
            text: control.operationError.length > 0 ? control.operationError : control.operationMessage
            color: control.operationError.length > 0 ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
            wrapMode: Text.Wrap
        }
    }
}
