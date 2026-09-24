// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Render host-projected agenda data; event edits and navigation are requests.
Tk.Box {
    id: control
    objectName: "calendarView"
    property var events: [] // {eventId,title,startLocal,endLocal,zone,allDay,calendarId}
    property string rangeLabel: ""
    property string selectedEventId: ""
    property string zone: "UTC"
    property bool offline: false
    property string errorText: ""
    property string tooltip: qsTr("Calendar agenda")
    property string lastRequestId: ""
    property int _requestSerial: 0
    property string requestState: "idle"
    signal navigateRequested(string requestId, string direction)
    signal eventSelected(string eventId)
    signal eventChangeRequested(string requestId, string eventId, var proposedChange)

    Accessible.role: Accessible.Table
    Accessible.name: tooltip
    Accessible.description: rangeLabel + " · " + zone
    function navigate(direction) { if (offline) return false; const id = "calendar-nav-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; requestState = "pending"; navigateRequested(id, direction); return id }
    function proposeChange(eventId, proposed) { if (!eventId || offline) return false; const id = "event-edit-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; requestState = "pending"; eventChangeRequested(id, eventId, proposed); return true }
    function acknowledge(id, state) { if (id !== lastRequestId || requestState !== "pending") return false; requestState = state; return true }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Flex {
            direction: Tk.Flex.Row; align: Tk.Flex.Center; gap: Tk.Theme.space.sm
            Tk.Button { objectName: "calendarPrevious"; text: qsTr("Previous"); tooltip: qsTr("Request previous range"); enabled: !control.offline; onClicked: control.navigate("previous") }
            Tk.Heading { level: 2; text: control.rangeLabel || qsTr("Agenda"); Tk.Flex.grow: 1 }
            Tk.Button { objectName: "calendarNext"; text: qsTr("Next"); tooltip: qsTr("Request next range"); enabled: !control.offline; onClicked: control.navigate("next") }
            Tk.Badge { text: control.zone }
        }
        Tk.Notice { visible: control.offline || control.errorText.length > 0; variant: control.offline ? "warning" : "danger"; title: control.offline ? qsTr("Offline calendar") : qsTr("Calendar error"); text: control.errorText || qsTr("Showing the host's cached agenda snapshot.") }
        ListView {
            id: agenda; objectName: "calendarAgenda"; width: parent.width
            implicitHeight: Math.min(Tk.Theme.size.fieldWidth * 2.7, Math.max(Tk.Theme.size.control, contentHeight)); height: implicitHeight
            Tk.Flex.basis: implicitHeight; Tk.Flex.shrink: 0
            clip: true; spacing: Tk.Theme.space.xs; model: control.events
            Accessible.role: Accessible.List; Accessible.name: qsTr("Calendar events")
            delegate: Tk.Box {
                required property var modelData
                property string eventId: String(modelData.eventId || "")
                objectName: "calendarEvent_" + eventId
                width: agenda.width; padding: Tk.Theme.space.sm
                color: control.selectedEventId === eventId ? Tk.Theme.color.selection : Tk.Theme.color.surface
                borderColor: control.selectedEventId === eventId ? Tk.Theme.color.accent : Tk.Theme.color.divider
                Accessible.role: Accessible.ListItem; Accessible.name: String(modelData.title || qsTr("Untitled event")); Accessible.description: String(modelData.startLocal || "") + " · " + String(modelData.zone || control.zone)
                Tk.Flex {
                    direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center; width: parent.width
                    Tk.Label { text: modelData.allDay ? qsTr("All day") : String(modelData.startLocal || ""); mono: true }
                    Tk.Label { text: String(modelData.title || qsTr("Untitled event")); elide: Text.ElideRight; Tk.Flex.grow: 1 }
                    Tk.Button { text: qsTr("Select"); tooltip: qsTr("Select event"); onClicked: { control.selectedEventId = eventId; control.eventSelected(eventId) } }
                    Tk.Button { text: qsTr("Propose move"); tooltip: qsTr("Ask host to move this event by one hour"); enabled: !control.offline; onClicked: control.proposeChange(eventId, { kind: "shift-local", minutes: 60, zone: String(modelData.zone || control.zone) }) }
                }
            }
        }
        Tk.Label { objectName: "calendarRequestState"; text: control.requestState; muted: true }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
