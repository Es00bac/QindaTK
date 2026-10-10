// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// D-312/D-313: group header rows (collapsible, with a summary), slack bars,
// deadline markers, split bars, progress lines and the second baseline
// lane. Calendar scale at 24 px per day from Monday 2 March 2026 00:00 UTC,
// so one hour is one pixel.
TestCase {
    id: root
    name: "GanttPlanning"
    when: windowShown
    visible: true
    width: 640
    height: 320

    readonly property string monday: "1772409600000"
    function at(day, hour) { return String(Number(monday) + (day * 24 + hour) * 3600000) }
    readonly property string groupG: "00000000-0000-0000-0000-000000000401"
    readonly property string taskA: "00000000-0000-0000-0000-000000000402"
    readonly property string taskB: "00000000-0000-0000-0000-000000000403"
    readonly property string groupH: "00000000-0000-0000-0000-000000000404"

    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: fixture
        Tk.GanttView {
            id: chart
            width: 120 + 240
            height: 220
            timeScale: "calendar"
            pixelsPerDay: 24
            labelWidth: 120
            rowHeight: 22
            headerHeight: 22
            visibleStartUtc: root.monday
            progressLines: [{ utc: root.at(2, 0), role: "danger" }]
            Tk.GanttCalendar { id: utcCalendar; timeZoneId: "UTC" }
            Tk.GanttSimpleScheduleAdapter { id: planScheduler }
            Tk.SelectionModel { id: planSelection }
            Tk.GanttTaskModel {
                id: planTasks
                calendar: utcCalendar
                transactionAdapter: planScheduler
                Component.onCompleted: replaceTasks([
                    { taskId: root.groupG, kind: "group", label: "Design team", summaryText: "2 tasks · 3 d",
                      startUtc: root.at(0, 0), finishUtc: root.at(3, 0), durationMinutes: 0,
                      progress: 0.5, collapsed: false, dependencies: [] },
                    { taskId: root.taskA, label: "Sketch", startUtc: root.at(0, 8), finishUtc: root.at(1, 17),
                      durationMinutes: 960, progress: 0.5, dependencies: [],
                      slackFinishUtc: root.at(3, 17), freeSlackFinishUtc: root.at(2, 17),
                      deadlineUtc: root.at(1, 12),
                      baselineStartUtc: root.at(0, 8), baselineFinishUtc: root.at(1, 17),
                      baseline2StartUtc: root.at(0, 12), baseline2FinishUtc: root.at(2, 12) },
                    { taskId: root.taskB, label: "Build", startUtc: root.at(2, 8), finishUtc: root.at(4, 17),
                      durationMinutes: 1080, progress: 0.5, dependencies: [root.taskA],
                      deadlineUtc: root.at(5, 0),
                      segments: [{ startUtc: root.at(2, 8), finishUtc: root.at(2, 17) },
                                 { startUtc: root.at(4, 8), finishUtc: root.at(4, 17) }] },
                    { taskId: root.groupH, kind: "group", label: "Review", summaryText: "4 tasks",
                      startUtc: root.at(5, 0), finishUtc: root.at(5, 0), durationMinutes: 0,
                      collapsed: true, dependencies: [] }
                ])
            }
            taskModel: planTasks
            calendar: utcCalendar
            selectionModel: planSelection
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
    function rowAt(gantt, index) {
        return namedAll(gantt, "ganttRow").filter(function(row) { return row.index === index })[0]
    }
    function visibleIn(item, name) {
        return namedAll(item, name).filter(function(part) { return part.visible })
    }

    function test_group_rows_draw_a_band_summary_and_report_toggles() {
        const gantt = createTemporaryObject(fixture, root)
        verify(gantt !== null)
        compare(gantt.count, 4)
        waitForRendering(gantt)
        const g = rowAt(gantt, 0)
        const h = rowAt(gantt, 3)
        compare(g.kind, "group")
        compare(visibleIn(g, "ganttGroupHeader").length, 1)
        compare(visibleIn(g, "ganttTaskLabel").length, 0)
        compare(findChild(g, "ganttGroupLabel").text, "Design team")
        compare(findChild(g, "ganttGroupSummary").text, "2 tasks · 3 d")
        compare(findChild(g, "ganttGroupChevron").name, "chevron-down")
        compare(findChild(h, "ganttGroupChevron").name, "chevron-right")
        const span = findChild(g, "ganttGroupBar")
        verify(span.visible)
        compare(span.x, 0)
        compare(span.width, 72)
        compare(findChild(span, "ganttGroupProgress").width, 36)
        compare(visibleIn(g, "ganttTaskBar").length, 0)
        compare(g.Accessible.description, "group, expanded")
        compare(h.Accessible.description, "group, collapsed")
        compare(visibleIn(rowAt(gantt, 1), "ganttGroupHeader").length, 0)

        const toggled = createTemporaryObject(spyComponent, root, { target: gantt, signalName: "groupToggled" })
        mouseClick(findChild(g, "ganttGroupChevron"))
        compare(toggled.count, 1)
        compare(toggled.signalArguments[0][0], root.groupG)
        compare(toggled.signalArguments[0][1], true)
        verify(gantt.selectionModel.select(root.groupG))
        gantt.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(toggled.count, 1)
        keyClick(Qt.Key_Left)
        compare(toggled.count, 2)
        keyClick(Qt.Key_Space)
        compare(toggled.count, 3)
        compare(gantt.previewActive, false)
        verify(gantt.selectionModel.select(root.groupH))
        keyClick(Qt.Key_Right)
        compare(toggled.count, 4)
        compare(toggled.signalArguments[3][0], root.groupH)
        compare(toggled.signalArguments[3][1], false)
        verify(!gantt.previewMove(root.groupG, root.at(1, 0)))
        // The host re-projects: the collapsed flag follows its rows.
        const rows = []
        for (let i = 0; i < gantt.count; ++i) rows.push(gantt.taskModel.get(i))
        rows[0].collapsed = true
        verify(gantt.taskModel.replaceTasks([rows[0], rows[3]]))
        waitForRendering(gantt)
        compare(findChild(rowAt(gantt, 0), "ganttGroupChevron").name, "chevron-right")
    }

    function test_slack_deadline_split_and_second_baseline() {
        const gantt = createTemporaryObject(fixture, root)
        waitForRendering(gantt)
        const a = rowAt(gantt, 1)
        compare(a.laneCount, 2)
        const bar = findChild(a, "ganttTaskBar")
        compare(bar.x, 8)
        compare(bar.width, 33)
        compare(bar.y, 2)
        compare(bar.height, 7)
        compare(findChild(bar, "ganttProgress").width, 16.5)
        const baseline = visibleIn(a, "ganttBaseline")[0]
        compare(baseline.x, 8)
        compare(baseline.width, 33)
        compare(baseline.y, 11)
        const second = findChild(a, "ganttBaseline2")
        verify(second.visible)
        compare(second.x, 12)
        compare(second.width, 48)
        compare(second.y, 16)
        verify(second.y + second.height <= a.height)
        const slack = findChild(a, "ganttSlack")
        verify(slack.visible)
        compare(slack.x, 41)
        compare(slack.width, 48)
        const free = findChild(a, "ganttFreeSlack")
        verify(free.visible)
        compare(free.x, 41)
        compare(free.width, 24)
        const deadline = findChild(a, "ganttDeadline")
        verify(deadline.visible)
        compare(deadline.x + deadline.width / 2, 36)
        compare(String(deadline.color), String(Tk.Theme.color.danger))

        const b = rowAt(gantt, 2)
        compare(b.laneCount, 0)
        const split = findChild(b, "ganttTaskBar")
        compare(split.x, 56)
        compare(split.width, 57)
        compare(String(split.color), String(Qt.rgba(0, 0, 0, 0)))
        const pieces = namedAll(b, "ganttBarSegment")
        compare(pieces.length, 2)
        compare(pieces[0].x, 0)
        compare(pieces[0].width, 9)
        compare(pieces[1].x, 48)
        compare(pieces[1].width, 9)
        compare(findChild(pieces[0], "ganttSegmentProgress").width, 9)
        compare(findChild(pieces[1], "ganttSegmentProgress").width, 0)
        const gaps = visibleIn(b, "ganttSplitGap")
        compare(gaps.length, 1)
        compare(gaps[0].x, 9)
        compare(gaps[0].width, 39)
        compare(String(findChild(b, "ganttDeadline").color), String(Tk.Theme.color.success))
        verify(b.Accessible.description.indexOf("split into 2 parts") >= 0)
    }

    function test_progress_line_peaks_at_progress_points() {
        const gantt = createTemporaryObject(fixture, root)
        waitForRendering(gantt)
        const canvas = findChild(gantt, "ganttProgressLines")
        verify(canvas !== null && canvas.visible)
        compare(canvas.x, 120)
        const points = canvas.pointsFor(0)
        const expected = [[48, 0], [48, 22], [48, 22], [24.5, 33], [48, 44], [48, 44], [65, 55], [48, 66], [48, 66], [48, 88]]
        compare(points.length, expected.length)
        for (let i = 0; i < expected.length; ++i) {
            compare(points[i].x, expected[i][0], "x " + i)
            compare(points[i].y, expected[i][1], "y " + i)
        }
        gantt.progressLines = []
        compare(canvas.visible, false)
    }

    // Beside a table (no label column) a group shows its name and summary
    // as bar text.
    function test_group_caption_without_label_column() {
        const gantt = createTemporaryObject(fixture, root, { labelWidth: 0 })
        waitForRendering(gantt)
        const g = rowAt(gantt, 0)
        compare(visibleIn(g, "ganttGroupTitle").length, 0)
        compare(findChild(g, "ganttBarText").text, "Design team · 2 tasks · 3 d")
        compare(findChild(rowAt(gantt, 1), "ganttBarText").visible, false)
    }
}
