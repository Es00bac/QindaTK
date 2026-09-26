// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A small launch plan on the calendar time scale (D-303): outline levels,
// summary brackets, milestones, a critical path in the danger role,
// baselines under slipped tasks, SS/FS/FF links, an inactive task, resource
// names beside the bars, and today/status markers. Demo data only.
Rectangle {
    id: root
    width: 1120
    height: 640
    color: Tk.Theme.color.bg

    // Demo data: March 2026 on a UTC calendar, hours as wall-clock times.
    function at(day, hour) { return String(Date.UTC(2026, 2, day, hour)) }
    function uuid(n) { return "00000000-0000-0000-0000-" + String(n).padStart(12, "0") }
    function task(n, label, level, from, to, extra) {
        const row = { taskId: uuid(n), label: label, outlineLevel: level,
                      startUtc: at(from[0], from[1]), finishUtc: at(to[0], to[1]),
                      durationMinutes: 0, progress: 0, dependencies: [] }
        for (const key in extra) row[key] = extra[key]
        return row
    }

    Tk.GanttCalendar { id: calendar; timeZoneId: "UTC"; holidays: ["2026-03-16"] }
    Tk.GanttSimpleScheduleAdapter { id: scheduler }
    Tk.GanttTaskModel {
        id: tasks
        calendar: calendar
        transactionAdapter: scheduler
        Component.onCompleted: {
            const ok = replaceTasks([
                root.task(1, "Spring launch", 0, [2, 8], [20, 17], { kind: "summary", progress: 0.42, durationMinutes: 6720 }),
                root.task(2, "Plan", 1, [2, 8], [6, 17], { kind: "summary", progress: 0.8, durationMinutes: 2400 }),
                root.task(3, "Kick-off", 2, [2, 8], [2, 8], { kind: "milestone", critical: true, progress: 1 }),
                root.task(4, "Scope and budget", 2, [2, 8], [4, 17], {
                    critical: true, progress: 1, durationMinutes: 1440, barText: "Ana",
                    dependencies: [root.uuid(3)], linkTypes: ["FS"],
                    baselineStartUtc: root.at(2, 8), baselineFinishUtc: root.at(3, 17) }),
                root.task(5, "Vendor shortlist", 2, [3, 8], [6, 17], {
                    progress: 0.6, durationMinutes: 1920, barText: "Ben",
                    dependencies: [root.uuid(4)], linkTypes: ["SS"] }),
                root.task(6, "Build", 1, [5, 8], [18, 17], { kind: "summary", progress: 0.2, durationMinutes: 4320 }),
                root.task(7, "Venue and catering", 2, [5, 8], [11, 17], {
                    critical: true, progress: 0.4, durationMinutes: 2400, barText: "Ana, Chris[50%]",
                    dependencies: [root.uuid(4)], linkTypes: ["FS"],
                    baselineStartUtc: root.at(4, 8), baselineFinishUtc: root.at(10, 17) }),
                root.task(8, "Invitations", 2, [9, 8], [13, 17], {
                    progress: 0.1, durationMinutes: 2400, barText: "Dana",
                    dependencies: [root.uuid(5)], linkTypes: ["FS"] }),
                root.task(9, "Swag bags (cut)", 2, [9, 8], [10, 17], { inactive: true, durationMinutes: 960 }),
                root.task(10, "Programme and speakers", 2, [12, 8], [18, 17], {
                    critical: true, durationMinutes: 2400, barText: "Ana",
                    dependencies: [root.uuid(7)], linkTypes: ["FS"],
                    baselineStartUtc: root.at(11, 8), baselineFinishUtc: root.at(17, 17) }),
                root.task(11, "Rehearsal", 1, [19, 8], [20, 12], {
                    critical: true, durationMinutes: 720, barText: "Everyone",
                    dependencies: [root.uuid(10)], linkTypes: ["FS"] }),
                root.task(12, "Launch day", 1, [20, 17], [20, 17], {
                    kind: "milestone", critical: true, dependencies: [root.uuid(11)], linkTypes: ["FF"],
                    baselineStartUtc: root.at(19, 17) })
            ])
            if (!ok) console.warn("gantt example:", lastError)
        }
    }
    Tk.SelectionModel { id: selection }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: "Spring launch"; font.pixelSize: Tk.Theme.font.large }
            Tk.Button { text: "Earlier"; onClicked: gantt.scrollBy(-7 * 1440) }
            Tk.Button { text: "Later"; onClicked: gantt.scrollBy(7 * 1440) }
            Tk.Button { text: "Zoom in"; onClicked: gantt.pixelsPerDay = Math.min(64, gantt.pixelsPerDay * 1.5) }
            Tk.Button { text: "Zoom out"; onClicked: gantt.pixelsPerDay = Math.max(2, gantt.pixelsPerDay / 1.5) }
            Tk.Button { text: "Undo move"; onClicked: gantt.undo() }
        }
        Tk.GanttView {
            id: gantt
            objectName: "exampleGantt"
            taskModel: tasks
            selectionModel: selection
            timeScale: "calendar"
            pixelsPerDay: Tk.Theme.size.control * 1.5
            labelWidth: Tk.Theme.size.labelWidth * 2.2
            headerHeight: Tk.Theme.size.row * 2
            visibleStartUtc: root.at(1, 0)
            markers: [{ utc: root.at(9, 0), label: "Status", role: "info" },
                      { utc: root.at(10, 12), label: "Today", role: "danger" }]
            tooltip: "Launch plan tasks and dependencies"
            Tk.Flex.grow: 1
        }
        Tk.DataTable {
            objectName: "exampleTaskGrid"
            model: tasks
            tooltip: "Launch plan task grid"
            currentIndex: tasks.indexOfId(selection.currentId)
            onActivated: function(index) { selection.select(tasks.get(index).taskId) }
            columns: [
                Tk.TableColumn { key: "label"; title: "Task"; flex: 1 },
                Tk.TableColumn { key: "kind"; title: "Kind"; width: Tk.Theme.size.fieldWidth
                                 formatter: function(value) { return value || "task" } },
                Tk.TableColumn { key: "startUtc"; title: "Start"; width: Tk.Theme.size.fieldWidth * 1.4
                                 formatter: function(value) { return calendar.formatLocal(value) } },
                Tk.TableColumn { key: "finishUtc"; title: "Finish"; width: Tk.Theme.size.fieldWidth * 1.4
                                 formatter: function(value) { return value ? calendar.formatLocal(value) : "" } },
                Tk.TableColumn { key: "progress"; title: "Done"; width: Tk.Theme.size.fieldWidth * 0.6
                                 formatter: function(value) { return Math.round(Number(value || 0) * 100) + "%" } }
            ]
            Tk.Flex.basis: Tk.Theme.size.row * 8
        }
    }
}
