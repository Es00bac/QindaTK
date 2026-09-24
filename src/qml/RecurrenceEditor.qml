// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// The host owns recurrence interpretation and preview expansion.
Tk.Box {
    id: control
    objectName: "recurrenceEditor"
    property var rule: ({ frequency: "weekly", interval: 1, weekdays: [], endMode: "never", count: 0, until: "", exceptions: [] })
    property string eventId: ""
    property string zone: "UTC"
    property var preview: []
    property string previewState: "idle"
    property string lastRequestId: ""
    property int _requestSerial: 0
    property string requestState: "idle"
    property string requestMessage: ""
    property string tooltip: qsTr("Recurrence rule")
    signal ruleChangeRequested(string requestId, string eventId, var proposedRule)
    signal previewRequested(string requestId, string eventId, var rule)
    signal occurrenceExceptionRequested(string requestId, string eventId, string occurrenceId, bool excluded)
    readonly property var weekdays: [qsTr("Mon"), qsTr("Tue"), qsTr("Wed"), qsTr("Thu"), qsTr("Fri"), qsTr("Sat"), qsTr("Sun")]
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: qsTr("Times interpreted in %1 by the host calendar service.").arg(zone)
    function propose(field, value) { const next = Object.assign({}, rule); next[field] = value; const id = "recurrence-rule-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; requestState = "pending"; requestMessage = ""; ruleChangeRequested(id, eventId, next); return next }
    function requestPreview() { const id = "recurrence-preview-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; requestState = "pending"; previewState = "pending"; previewRequested(id, eventId, Object.assign({}, rule)); return id }
    function acknowledge(id, state, message) { if (id !== lastRequestId || requestState !== "pending") return false; requestState = state; requestMessage = message || ""; return true }
    function requestException(occurrenceId, excluded) { if (!occurrenceId) return false; const id = "recurrence-exception-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; requestState = "pending"; requestMessage = ""; occurrenceExceptionRequested(id, eventId, occurrenceId, excluded); return id }
    function toggleWeekday(index) { const next = (rule.weekdays || []).slice(); const day = index + 1; const at = next.indexOf(day); if (at < 0) next.push(day); else next.splice(at, 1); next.sort(); return propose("weekdays", next) }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Overline { title: qsTr("Repeats"); wide: true }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
            Tk.ComboBox { objectName: "recurrenceFrequency"; model: [qsTr("daily"), qsTr("weekly"), qsTr("monthly"), qsTr("yearly")]; currentIndex: Math.max(0, model.indexOf(String(control.rule.frequency || "weekly"))); tooltip: qsTr("Repeat frequency"); Accessible.name: qsTr("Repeat frequency"); onActivated: control.propose("frequency", model[currentIndex]) }
            Tk.Label { text: qsTr("every"); muted: true }
            Tk.TextField {
                objectName: "recurrenceInterval"; width: Tk.Theme.size.controlLg
                text: String(control.rule.interval || 1)
                validator: IntValidator { bottom: 1; top: 999 }
                tooltip: qsTr("Repeat interval"); Accessible.name: qsTr("Repeat interval")
                onEditingFinished: control.propose("interval", Number(text))
            }
            Tk.Label { text: qsTr("· %1").arg(control.zone); muted: true }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.xs
            Repeater {
                model: control.weekdays
                delegate: Tk.Button {
                    required property int index; required property var modelData
                    objectName: "recurrenceWeekday_" + index; text: String(modelData); checkable: true; checked: (control.rule.weekdays || []).indexOf(index + 1) >= 0
                    tooltip: qsTr("Include %1").arg(modelData); Accessible.name: tooltip
                    onClicked: control.toggleWeekday(index)
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
            Tk.ComboBox { objectName: "recurrenceEndMode"; model: [qsTr("never"), qsTr("after count"), qsTr("until date")]; currentIndex: ["never", "count", "until"].indexOf(String(control.rule.endMode || "never")); tooltip: qsTr("Recurrence end"); Accessible.name: qsTr("Recurrence end"); onActivated: control.propose("endMode", ["never", "count", "until"][currentIndex]) }
            Tk.TextField {
                objectName: "recurrenceCount"; visible: control.rule.endMode === "count"
                width: Tk.Theme.size.controlLg; placeholderText: qsTr("Count"); text: String(control.rule.count || 1)
                validator: IntValidator { bottom: 1; top: 999 }
                tooltip: qsTr("Number of occurrences"); Accessible.name: tooltip
                onEditingFinished: control.propose("count", Number(text))
            }
            Tk.TextField {
                objectName: "recurrenceUntil"; visible: control.rule.endMode === "until"
                width: Tk.Theme.size.fieldWidth; placeholderText: qsTr("YYYY-MM-DD"); text: String(control.rule.until || "")
                tooltip: qsTr("Last local date for the series"); Accessible.name: tooltip
                onEditingFinished: control.propose("until", text)
            }
            Tk.Label { text: qsTr("Exceptions: %1 · host managed").arg((control.rule.exceptions || []).length); muted: true; Tk.Flex.grow: 1 }
            Tk.Button { objectName: "recurrencePreview"; text: qsTr("Preview"); tooltip: qsTr("Request host occurrence preview"); onClicked: control.requestPreview() }
        }
        Tk.Label { objectName: "recurrencePreviewState"; text: control.requestMessage || (control.previewState === "ready" ? qsTr("%1 occurrences supplied by host").arg(control.preview.length) : control.previewState); muted: true }
        Repeater {
            model: control.preview
            delegate: Tk.Flex {
                required property var modelData
                direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
                Tk.Label { text: String(modelData.localStart || "") + " · " + String(modelData.zone || control.zone); mono: true; Tk.Flex.grow: 1 }
                Tk.Button {
                    text: qsTr("Skip once"); tooltip: qsTr("Ask the host to exclude this occurrence")
                    Accessible.name: tooltip
                    onClicked: control.requestException(String(modelData.occurrenceId || modelData.localStart || ""), true)
                }
            }
        }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
