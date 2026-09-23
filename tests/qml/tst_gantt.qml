// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "GanttView"
    when: windowShown
    visible: true
    width: 620
    height: 320

    Component {
        id: fixture
        Tk.GanttView {
            width: 540
            height: 220
            visibleStartUtc: "1741611600000"
            pixelsPerWorkingMinute: 1
            tooltip: "Fixture schedule"
            Tk.GanttCalendar { id: calendar; timeZoneId: "America/New_York" }
            Tk.GanttSimpleScheduleAdapter { id: scheduler }
            Tk.SelectionModel { id: selection }
            QtObject {
                id: allocation
                function valueForTask(taskId) { return taskId.length ? 0.5 : -1 }
            }
            Tk.GanttTaskModel {
                id: tasks
                calendar: calendar
                transactionAdapter: scheduler
                Component.onCompleted: replaceTasks([{
                    taskId: "00000000-0000-0000-0000-000000000201",
                    label: "Plan", startUtc: "1741611600000",
                    durationMinutes: 120, progress: 0.5, dependencies: [],
                    baselineStartUtc: "1741611600000", baselineDurationMinutes: 120
                }])
            }
            taskModel: tasks
            selectionModel: selection
            allocationProvider: allocation
        }
    }

    function test_known_geometry_and_preview() {
        const gantt = createTemporaryObject(fixture, this)
        verify(gantt !== null)
        compare(gantt.count, 1)
        const bar = findChild(gantt, "ganttTaskBar")
        verify(bar !== null)
        compare(bar.x, 0)
        compare(bar.width, 120)
        verify(findChild(gantt, "ganttBaseline") !== null)
        compare(findChild(gantt, "ganttAllocation").width, 60)
        verify(findChild(gantt, "ganttDependencies") !== null)
        const taskId = gantt.taskModel.get(0).taskId
        verify(gantt.selectionModel.select(taskId))
        compare(gantt.taskModel.indexOfId(gantt.selectionModel.currentId), 0)
        const before = gantt.taskModel.get(0).startUtc
        verify(gantt.previewMove(taskId, "1741615200000"))
        compare(gantt.taskModel.get(0).startUtc, before)
        compare(gantt.taskModel.displayGet(0).startUtc, "1741615200000")
        gantt.cancelPreview()
        compare(gantt.taskModel.displayGet(0).startUtc, before)
    }
}
