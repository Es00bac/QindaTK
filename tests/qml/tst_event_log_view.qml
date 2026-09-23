// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "EventLogView"
    when: windowShown
    visible: true
    width: 740
    height: 420

    Component {
        id: fixture
        Item {
            width: 720
            height: 400
            Tk.EventLogModel { id: eventModel; maxEntries: 6 }
            Tk.EventLogView {
                id: view
                objectName: "eventLogFixtureView"
                anchors.fill: parent
                model: eventModel
                tooltip: "Render events"
            }
        }
    }
    SignalSpy { id: exportSpy; signalName: "exportRequested" }

    function makeEvent(id, severity, message) {
        return ({eventId: id, timestamp: "2026-09-23T09:00:00Z", severity: severity,
                 source: "render", message: message, details: "detail " + id})
    }

    function test_virtual_rows_filter_keyboard_selection_and_export() {
        const host = createTemporaryObject(fixture, root)
        verify(host)
        const view = findChild(host, "eventLogFixtureView")
        const model = view.model
        const events = findChild(view, "eventLogRows")
        const burst = []
        for (let i = 0; i < 200; ++i)
            burst.push(makeEvent("e" + i, i % 2 ? "info" : "error", "render event " + i))
        compare(model.appendEvents(burst), 6)
        compare(model.retainedCount, 6)
        compare(model.count, 6)
        compare(events.count, 6)
        verify(view.Accessible.name === "Render events")
        verify(view.Accessible.role === Accessible.List)
        tryVerify(function() { return events.itemAtIndex(4) !== null })
        const visibleRow = events.itemAtIndex(4)
        mouseClick(visibleRow, 18, 10)
        compare(model.selectedEventId, "e198")
        view.forceActiveFocus()
        verify(view.activeFocus)
        keyClick(Qt.Key_Up)
        compare(model.selectedEventId, "e197")
        view.followTail = false
        compare(view.followTail, false)
        model.textFilter = "event 199"
        compare(model.count, 1)
        compare(model.eventAt(0).eventId, "e199")
        model.textFilter = ""
        model.severityFilter = "error"
        compare(model.count, 3)
        const exportButton = findChild(view, "eventLogExport")
        verify(exportButton)
        exportSpy.target = view
        exportButton.click()
        compare(exportSpy.count, 1)
        verify(String(exportSpy.signalArguments[0][0]).includes("eventId,timestamp"))
        compare(findChild(view, "eventLogMode").text, "Inspection paused")
    }

    function test_time_filter_requires_explicit_timezone() {
        const host = createTemporaryObject(fixture, root)
        const view = findChild(host, "eventLogFixtureView")
        const field = findChild(view, "eventLogFromUtc")
        field.text = "2026-09-23T09:00:00"
        field.editingFinished()
        verify(field.error)
        verify(Number.isNaN(view.model.fromUtc.getTime()))
        field.text = "2026-09-23T09:00:00Z"
        field.editingFinished()
        verify(!field.error)
        verify(Number.isFinite(view.model.fromUtc.getTime()))
    }

    function test_paused_view_restores_stable_event_after_retention_eviction() {
        const host = createTemporaryObject(fixture, root)
        const view = findChild(host, "eventLogFixtureView")
        const model = view.model
        const events = findChild(view, "eventLogRows")
        model.maxEntries = 80
        const burst = []
        for (let i = 0; i < 100; ++i)
            burst.push(makeEvent("p" + i, "info", "event " + i))
        compare(model.appendEvents(burst), 80)
        waitForRendering(events)
        tryVerify(function() { return events.atYEnd })
        view.followTail = false
        events.contentY = Tk.Theme.size.row * 10 + Tk.Theme.space.xs
        const anchor = model.eventAt(Math.floor(events.contentY / Tk.Theme.size.row)).eventId
        compare(anchor, "p30")
        model.appendEvent(makeEvent("p100", "info", "event 100"))
        tryVerify(function() { return model.indexOfEventId(anchor) >= 0 })
        tryCompare(events, "contentY", Tk.Theme.size.row * 9 + Tk.Theme.space.xs)
        compare(model.selectedEventId, "")
    }
}
