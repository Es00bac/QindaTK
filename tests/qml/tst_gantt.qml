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

    // Calendar time scale (D-303): 24 px per day from Monday 2 March 2026
    // 00:00 UTC, no label column, a two-tier ruler, host-owned finishes.
    Component {
        id: calendarFixture
        Tk.GanttView {
            width: 540
            height: 160
            timeScale: "calendar"
            pixelsPerDay: 24
            labelWidth: 0
            rowHeight: 22
            headerHeight: 44
            visibleStartUtc: "1772409600000"
            markers: [{ utc: "1772582400000", label: "Today", role: "danger" }]
            Tk.GanttCalendar { id: utcCalendar; timeZoneId: "UTC" }
            Tk.SelectionModel { id: calendarSelection }
            Tk.GanttTaskModel {
                id: projectTasks
                calendar: utcCalendar
                Component.onCompleted: replaceTasks([
                    { taskId: "00000000-0000-0000-0000-000000000301", label: "Phase", kind: "summary",
                      startUtc: "1772438400000", finishUtc: "1772816400000", durationMinutes: 2400,
                      progress: 0.25, outlineLevel: 0, dependencies: [] },
                    { taskId: "00000000-0000-0000-0000-000000000302", label: "Design", outlineLevel: 1,
                      startUtc: "1772438400000", finishUtc: "1772557200000", durationMinutes: 960,
                      progress: 0.5, critical: true, barText: "Ana",
                      baselineStartUtc: "1772438400000", baselineFinishUtc: "1772557200000",
                      dependencies: [] },
                    { taskId: "00000000-0000-0000-0000-000000000303", label: "Build", outlineLevel: 1,
                      startUtc: "1772611200000", finishUtc: "1772816400000", durationMinutes: 1440,
                      critical: true, dependencies: ["00000000-0000-0000-0000-000000000302"],
                      linkTypes: ["FS"] },
                    { taskId: "00000000-0000-0000-0000-000000000304", label: "Done", kind: "milestone",
                      outlineLevel: 1, startUtc: "1772816400000", durationMinutes: 0, critical: true,
                      dependencies: ["00000000-0000-0000-0000-000000000303"], linkTypes: ["FF"] }
                ])
            }
            taskModel: projectTasks
            selectionModel: calendarSelection
        }
    }

    function namedAll(item, name, out) {
        out = out || []
        if (!item) return out
        if (item.objectName === name) out.push(item)
        const kids = item.children || []
        for (let i = 0; i < kids.length; ++i) namedAll(kids[i], name, out)
        return out
    }

    function test_calendar_scale_kinds_and_markers() {
        const gantt = createTemporaryObject(calendarFixture, this)
        verify(gantt !== null)
        compare(gantt.count, 4)
        waitForRendering(gantt)
        // Design: 08:00 Mon to 17:00 Tue = 33 hours at 1 px per hour.
        const bars = namedAll(gantt, "ganttTaskBar").filter(function(b) { return b.visible })
        compare(bars.length, 2)
        const design = bars.filter(function(b) { return b.x < 20 })[0]
        compare(design.x, 8)
        compare(design.width, 33)
        compare(String(design.border.color), String(Tk.Theme.color.danger))
        const progress = findChild(design, "ganttProgress")
        compare(progress.width, 16.5)
        compare(String(progress.color), String(Tk.Theme.color.danger))
        const summary = namedAll(gantt, "ganttSummaryBar").filter(function(b) { return b.visible })
        compare(summary.length, 1)
        compare(summary[0].x, 8)
        compare(summary[0].width, 105) // 08:00 Mon to 17:00 Fri
        const milestones = namedAll(gantt, "ganttMilestone").filter(function(b) { return b.visible })
        compare(milestones.length, 1)
        compare(milestones[0].x + milestones[0].width / 2, 113)
        const baselines = namedAll(gantt, "ganttBaseline").filter(function(b) { return b.visible })
        compare(baselines.length, 1)
        compare(baselines[0].x, 8)
        compare(baselines[0].width, 33)
        const texts = namedAll(gantt, "ganttBarText").filter(function(b) { return b.visible })
        compare(texts.length, 1)
        compare(texts[0].text, "Ana")
        verify(texts[0].x > design.x + design.width)
        // No label column: labels are hidden, the track starts at 0.
        const labels = namedAll(gantt, "ganttTaskLabel").filter(function(b) { return b.visible })
        compare(labels.length, 0)
        compare(findChild(gantt, "ganttTrack").x, 0)
        // Weekend 7–8 March shaded; Monday is not.
        const shaded = namedAll(gantt, "ganttNonWorking").filter(function(b) { return b.visible })
        verify(shaded.length >= 2)
        compare(shaded[0].x, 5 * 24)
        compare(shaded[0].width, 24)
        // Two ruler tiers.
        verify(namedAll(gantt, "ganttRulerMajor").length >= 1)
        verify(namedAll(gantt, "ganttRulerMinor").length >= 22)
        // Today marker at Wednesday 00:00 = 48 px; its label chip is shown.
        const marker = findChild(gantt, "ganttMarker_0")
        verify(marker !== null && marker.visible)
        compare(marker.x + marker.width / 2, 48)
        compare(String(marker.color), String(Tk.Theme.color.danger))
        verify(findChild(gantt, "ganttMarkerLabel_0").visible)
        // Scrolling: rows scroll by contentY; the window shifts by scrollBy.
        verify(gantt.contentHeight >= 88)
        gantt.contentY = 22
        compare(gantt.contentY, 22)
        gantt.scrollBy(1440)
        compare(gantt.visibleStartUtc, "1772496000000")
        compare(gantt.visibleEndUtc, String(1772496000000 + 540 / 24 * 86400000))
        // Row background click selects the task.
        gantt.contentY = 0
        waitForRendering(gantt)
        mouseClick(gantt, 400, 44 + 22 + 11)
        compare(gantt.selectionModel.currentId, "00000000-0000-0000-0000-000000000302")
    }

    function test_rejected_link_types_keep_rows() {
        const gantt = createTemporaryObject(calendarFixture, this)
        verify(gantt !== null)
        const before = gantt.taskModel.get(2)
        const bad = gantt.taskModel.get(2)
        bad.linkTypes = ["XX"]
        verify(!gantt.taskModel.replaceTasks([gantt.taskModel.get(0), gantt.taskModel.get(1), bad,
                                              gantt.taskModel.get(3)]))
        compare(gantt.taskModel.get(2).linkTypes, before.linkTypes)
    }
}
