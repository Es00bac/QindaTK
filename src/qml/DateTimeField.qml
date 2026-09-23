// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Canonical value is an ISO date string for all-day values and ISO UTC for
// instants. LocaleService owns parsing/formatting; ambiguity is user-resolved.
T.Control {
    id: field
    property Tk.PropertyBinding binding: null
    property Tk.LocaleService localeService: null
    property bool dateOnly: false
    property string dateFormat: "yyyy-MM-dd"
    property string dateTimeFormat: "yyyy-MM-dd HH:mm"
    // Inclusive canonical ISO date bounds in dateOnly mode, UTC ISO instants otherwise.
    property string minValue: ""
    property string maxValue: ""
    property string tooltip: ""
    property string draft: ""
    property string resolutionState: "idle"
    property string validationError: ""
    property var resolutionCandidates: []
    property int candidateIndex: -1
    readonly property string canonicalValue: binding && binding.value !== undefined ? String(binding.value) : ""
    readonly property bool readOnly: !binding || !binding.writable

    signal valueModified(string canonicalValue)

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip || (dateOnly ? qsTr("Date") : qsTr("Date and time"))
    Accessible.description: validationError || resolutionDescription
    readonly property string resolutionDescription: resolutionState === "ambiguous"
        ? qsTr("This local time occurs twice. Choose an instant.")
        : resolutionState === "nonexistent" ? qsTr("This local time does not exist in the selected time zone.")
        : resolutionState === "unavailable" ? qsTr("Date and time resolution is unavailable.") : ""
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    function displayCanonical(value) {
        if (!value || !localeService) return value || ""
        if (dateOnly) return localeService.formatDate(value, dateFormat)
        return localeService.formatDateTime(new Date(value), dateTimeFormat)
    }
    function loadValue() {
        draft = displayCanonical(canonicalValue)
        validationError = ""
        resolutionState = "idle"
        resolutionCandidates = []
        candidateIndex = -1
    }
    function commitDraft() {
        if (draft.trim().length === 0) return clearValue()
        if (!localeService) return false
        if (dateOnly) {
            const parsedDate = localeService.parseDate(draft, dateFormat)
            if (!parsedDate.ok) { validationError = parsedDate.error; resolutionState = "invalid"; return false }
            return commitCanonical(parsedDate.value)
        }
        const classified = localeService.classifyLocalDateTime(draft, dateTimeFormat)
        resolutionState = classified.status
        resolutionCandidates = classified.candidates || []
        validationError = classified.error || ""
        candidateIndex = -1
        if (classified.status === "valid" && resolutionCandidates.length === 1) return commitCanonical(resolutionCandidates[0])
        if (classified.status === "ambiguous") return false
        return false
    }
    function chooseCandidate(index) {
        if (resolutionState !== "ambiguous" || index < 0 || index >= resolutionCandidates.length) return false
        candidateIndex = index
        return commitCanonical(resolutionCandidates[index])
    }
    function commitCanonical(value) {
        if (value.length > 0 && !inRange(value)) return false
        if (!binding || !binding.begin()) return false
        if (!binding.update(value)) { binding.cancel(); return false }
        if (!binding.commit()) {
            const failure = binding.validationError
            binding.cancel()
            validationError = failure
            resolutionState = "invalid"
            return false
        }
        draft = displayCanonical(value)
        validationError = ""
        resolutionState = value.length > 0 ? "valid" : "idle"
        valueModified(value)
        return true
    }
    function clearValue() { return commitCanonical("") }
    function rangeKey(value) {
        if (!value) return NaN
        if (dateOnly) {
            if (!/^\d{4}-\d{2}-\d{2}$/.test(value)) return NaN
            const key = Date.parse(value + "T00:00:00Z")
            return isFinite(key) && new Date(key).toISOString().slice(0, 10) === value ? key : NaN
        }
        if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$/.test(value)) return NaN
        return Date.parse(value)
    }
    function inRange(value) {
        const key = rangeKey(value)
        const low = minValue.length > 0 ? rangeKey(minValue) : -Infinity
        const high = maxValue.length > 0 ? rangeKey(maxValue) : Infinity
        if (!isFinite(key) || isNaN(low) || isNaN(high) || low > high) {
            validationError = qsTr("Invalid date range configuration")
        } else if (key < low || key > high) {
            validationError = qsTr("Value is outside the allowed range")
        } else return true
        if (resolutionState !== "ambiguous") resolutionState = "invalid"
        return false
    }
    function cancelDraft() { loadValue() }

    onCanonicalValueChanged: if (!input.activeFocus && resolutionState !== "ambiguous") loadValue()
    onDateFormatChanged: if (dateOnly) loadValue()
    onDateTimeFormatChanged: if (!dateOnly) loadValue()
    Component.onCompleted: if (binding || draft.length === 0) loadValue()

    Tk.ToolTip { text: field.tooltip; visible: text.length > 0 && field.hovered }
    Connections {
        target: field.localeService
        function onChanged() { if (!input.activeFocus && field.resolutionState !== "ambiguous") field.loadValue() }
    }
    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.TextField {
                id: input
                objectName: "dateTimeInput"
                Tk.Flex.grow: 1
                text: field.draft
                enabled: !field.readOnly
                error: field.validationError.length > 0 || field.resolutionState === "nonexistent" || field.resolutionState === "invalid"
                tooltip: field.tooltip
                Accessible.name: field.tooltip || (field.dateOnly ? qsTr("Date") : qsTr("Date and time"))
                onTextEdited: field.draft = text
                onEditingFinished: field.commitDraft()
                Keys.onEscapePressed: field.cancelDraft()
            }
            Tk.Button {
                objectName: "dateTimeCommit"
                text: qsTr("Set")
                small: true
                tooltip: qsTr("Apply date and time")
                enabled: !field.readOnly && field.draft.length > 0
                onClicked: field.commitDraft()
            }
            Tk.Button {
                objectName: "dateTimeClear"
                text: qsTr("Clear")
                small: true
                variant: "ghost"
                tooltip: qsTr("Clear date and time")
                enabled: !field.readOnly && (field.canonicalValue.length > 0 || field.draft.length > 0)
                onClicked: field.clearValue()
            }
        }
        Tk.Flex {
            id: candidatePicker
            visible: field.resolutionState === "ambiguous" && field.resolutionCandidates.length > 1
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Repeater {
                model: field.resolutionCandidates
                delegate: Tk.Button {
                    required property int index
                    required property var modelData
                    text: field.localeService ? field.localeService.formatDateTime(new Date(modelData), "yyyy-MM-dd HH:mm t") : String(modelData)
                    tooltip: qsTr("Choose occurrence %1").arg(index + 1)
                    variant: field.candidateIndex === index ? "accent" : "outline"
                    onClicked: field.chooseCandidate(index)
                }
            }
        }
        Tk.Caption {
            objectName: "dateTimeValidation"
            visible: field.validationError.length > 0 || field.resolutionDescription.length > 0
            text: field.validationError.length > 0 ? field.validationError : field.resolutionDescription
            color: field.validationError.length > 0 || field.resolutionState === "nonexistent" ? Tk.Theme.color.danger : Tk.Theme.color.warning
            wrapMode: Text.Wrap
        }
    }
}
