// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Render host-projected agenda data; event edits and navigation are requests.
// AGENT-CONTRACT (LocalCalendarProjectionModel, LocalProductivityStore.shiftEventLocal):
// rows may be occurrences ({eventId, occurrenceId, recurrenceRule, overridden});
// a move proposal is {kind: "shift-local", minutes, zone, scope, occurrenceId}
// where scope is "occurrence", "following" or "series" for a recurring row
// and "series" otherwise. All-day rows move by whole days.
Tk.Box {
    id: control
    objectName: "calendarView"
    property var events: [] // {eventId,occurrenceId,title,startLocal,endLocal,zone,allDay,calendarId,recurrenceRule,overridden}
    property string rangeLabel: ""
    property string selectedEventId: ""
    property string selectedOccurrenceId: ""
    property string zone: "UTC"
    property bool offline: false
    property string errorText: ""
    property string tooltip: qsTr("Calendar agenda")
    property string moveScope: "occurrence" // for recurring rows: occurrence, following, series
    property string lastRequestId: ""
    property int _requestSerial: 0
    property string requestState: "idle"
    property string requestMessage: ""
    readonly property bool hasRecurring: events.some(function(row) { return control.isRecurring(row) })
    readonly property var scopes: [{ text: qsTr("This event"), value: "occurrence", tooltip: qsTr("Move only the chosen occurrence") },
                                   { text: qsTr("This and following"), value: "following", tooltip: qsTr("Split the series and move the rest") },
                                   { text: qsTr("All events"), value: "series", tooltip: qsTr("Move every occurrence") }]
    signal navigateRequested(string requestId, string direction)
    signal eventSelected(string eventId)
    signal occurrenceSelected(string eventId, string occurrenceId)
    signal eventChangeRequested(string requestId, string eventId, var proposedChange)

    Accessible.role: Accessible.Table
    Accessible.name: tooltip
    Accessible.description: rangeLabel + " · " + zone
    function isRecurring(row) { return !!(row && row.recurrenceRule && row.recurrenceRule.frequency) }
    function isAllDay(row) { return !!row.allDay || String(row.startLocal || "").length === 10 }
    function rowKey(row) { return String(row.occurrenceId || row.eventId || "") }
    function navigate(direction) { if (offline) return false; const id = "calendar-nav-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; requestState = "pending"; requestMessage = ""; navigateRequested(id, direction); return id }
    function proposeChange(eventId, proposed) { if (!eventId || offline) return false; const id = "event-edit-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; requestState = "pending"; requestMessage = ""; eventChangeRequested(id, eventId, proposed); return true }
    // `direction` is -1 or +1: an hour for timed rows, a day for all-day rows.
    function proposeMove(row, direction) {
        const recurring = isRecurring(row)
        return proposeChange(String(row.eventId || ""), { kind: "shift-local", minutes: direction * (isAllDay(row) ? 1440 : 60),
            zone: String(row.zone || zone), scope: recurring ? moveScope : "series",
            occurrenceId: recurring ? String(row.occurrenceId || "") : "" })
    }
    function select(row) {
        selectedEventId = String(row.eventId || "")
        selectedOccurrenceId = rowKey(row)
        eventSelected(selectedEventId)
        occurrenceSelected(selectedEventId, selectedOccurrenceId)
    }
    function acknowledge(id, state, message) { if (id !== lastRequestId || requestState !== "pending") return false; requestState = state; requestMessage = message || ""; return true }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm; width: parent.width
            Tk.Button { objectName: "calendarPrevious"; text: qsTr("Previous"); tooltip: qsTr("Request previous range"); enabled: !control.offline; onClicked: control.navigate("previous") }
            Tk.Heading { level: 2; text: control.rangeLabel || qsTr("Agenda"); Tk.Flex.grow: 1 }
            Tk.Button { objectName: "calendarNext"; text: qsTr("Next"); tooltip: qsTr("Request next range"); enabled: !control.offline; onClicked: control.navigate("next") }
            Tk.Badge { text: control.zone }
        }
        Tk.Flex {
            visible: control.hasRecurring
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Label { text: qsTr("Moving a repeating event changes"); muted: true }
            Tk.Segmented {
                objectName: "calendarMoveScope"; small: true; model: control.scopes
                currentIndex: Math.max(0, ["occurrence", "following", "series"].indexOf(control.moveScope))
                tooltip: qsTr("Scope of moves for repeating events")
                onActivated: function(index) { control.moveScope = control.scopes[index].value }
            }
        }
        Tk.Notice { visible: control.offline || control.errorText.length > 0 || control.requestState === "rejected"; variant: control.offline ? "warning" : "danger"; title: control.offline ? qsTr("Offline calendar") : control.requestState === "rejected" ? qsTr("Change not applied") : qsTr("Calendar error"); text: control.requestMessage || control.errorText || qsTr("Showing the host's cached agenda snapshot.") }
        ListView {
            id: agenda; objectName: "calendarAgenda"; width: parent.width
            implicitHeight: Math.min(Tk.Theme.size.fieldWidth * 2.7, Math.max(Tk.Theme.size.control, contentHeight)); height: implicitHeight
            Tk.Flex.basis: implicitHeight; Tk.Flex.shrink: 0
            clip: true; spacing: Tk.Theme.space.xs; model: control.events
            Accessible.role: Accessible.List; Accessible.name: qsTr("Calendar events")
            delegate: Tk.Box {
                id: eventRow
                required property var modelData
                property string eventId: String(modelData.eventId || "")
                readonly property string key: control.rowKey(modelData)
                readonly property bool selected: control.selectedOccurrenceId ? control.selectedOccurrenceId === key : control.selectedEventId === eventId
                readonly property bool allDay: control.isAllDay(modelData)
                objectName: "calendarEvent_" + eventId
                width: agenda.width; padding: Tk.Theme.space.sm
                color: selected ? Tk.Theme.color.selection : Tk.Theme.color.surface
                borderColor: selected ? Tk.Theme.color.accent : Tk.Theme.color.divider
                Accessible.role: Accessible.ListItem; Accessible.name: String(modelData.title || qsTr("Untitled event"))
                Accessible.description: String(modelData.startLocal || "") + " · " + String(modelData.zone || control.zone) + (control.isRecurring(modelData) ? qsTr(" · repeats") : "")
                Tk.Flex {
                    direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center; width: parent.width
                    Tk.Label { text: eventRow.allDay ? qsTr("All day · %1").arg(String(eventRow.modelData.startLocal || "")) : String(eventRow.modelData.startLocal || "").replace("T", " "); mono: true }
                    Tk.Icon { visible: control.isRecurring(eventRow.modelData); name: "repeat"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
                    Tk.Label { text: String(eventRow.modelData.title || qsTr("Untitled event")); elide: Text.ElideRight; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                    Tk.Badge { visible: !!eventRow.modelData.overridden; text: qsTr("Moved"); variant: "info" }
                    Tk.Button { small: true; text: qsTr("Select"); tooltip: qsTr("Select event"); onClicked: control.select(eventRow.modelData) }
                    Tk.Button { objectName: "calendarMoveEarlier"; small: true; text: eventRow.allDay ? qsTr("−1 day") : qsTr("−1 h"); tooltip: qsTr("Ask the host to move this event earlier"); enabled: !control.offline; onClicked: control.proposeMove(eventRow.modelData, -1) }
                    Tk.Button { objectName: "calendarMoveLater"; small: true; text: eventRow.allDay ? qsTr("+1 day") : qsTr("+1 h"); tooltip: qsTr("Ask the host to move this event later"); enabled: !control.offline; onClicked: control.proposeMove(eventRow.modelData, 1) }
                }
            }
        }
        Tk.Label { objectName: "calendarRequestState"; text: control.requestMessage || control.requestState; muted: true }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
