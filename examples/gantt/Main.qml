// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 1120
    height: 640
    color: Tk.Theme.color.bg
    readonly property string designId: "00000000-0000-0000-0000-000000000101"
    readonly property string buildId: "00000000-0000-0000-0000-000000000102"
    readonly property string verifyId: "00000000-0000-0000-0000-000000000103"

    Tk.GanttCalendar {
        id: calendar
        timeZoneId: "America/New_York"
        holidays: ["2025-03-11"]
    }
    Tk.GanttSimpleScheduleAdapter { id: scheduler }
    Tk.GanttTaskModel {
        id: tasks
        calendar: calendar
        transactionAdapter: scheduler
        Component.onCompleted: replaceTasks([
            { taskId: root.designId, label: "Design", startUtc: "1741356000000",
              durationMinutes: 480, progress: 0.75, dependencies: [],
              baselineStartUtc: "1741356000000", baselineDurationMinutes: 480 },
            { taskId: root.buildId, label: "Build", startUtc: "1741611600000",
              durationMinutes: 960, progress: 0.3, dependencies: [root.designId],
              baselineStartUtc: "1741611600000", baselineDurationMinutes: 960 },
            { taskId: root.verifyId, label: "Verify", startUtc: "1741870800000",
              durationMinutes: 480, progress: 0, dependencies: [root.buildId],
              baselineStartUtc: "1741784400000", baselineDurationMinutes: 480 }
        ])
    }
    Tk.SelectionModel { id: selection }
    QtObject {
        id: allocation
        function valueForTask(taskId) {
            if (taskId === root.designId) return 0.6
            if (taskId === root.buildId) return 0.9
            return 0.2
        }
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        padding: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: "Project schedule"; font.pixelSize: Tk.Theme.font.large }
            Tk.Button { text: "Commit preview"; onClicked: gantt.commitPreview() }
            Tk.Button { text: "Cancel"; onClicked: gantt.cancelPreview() }
            Tk.Button { text: "Undo"; onClicked: gantt.undo() }
        }
        Tk.GanttView {
            id: gantt
            objectName: "exampleGantt"
            taskModel: tasks
            selectionModel: selection
            allocationProvider: allocation
            visibleStartUtc: "1741356000000"
            pixelsPerWorkingMinute: Tk.Theme.size.row / 45
            tooltip: "Project tasks and dependencies"
            Tk.Flex.grow: 1
        }
        Tk.DataTable {
            objectName: "exampleTaskGrid"
            model: tasks
            tooltip: "Project task grid"
            currentIndex: tasks.indexOfId(selection.currentId)
            onActivated: function(index) { selection.select(tasks.get(index).taskId) }
            columns: [
                Tk.TableColumn { key: "label"; title: "Task"; flex: 1 },
                Tk.TableColumn { key: "startUtc"; title: "Start UTC ms"; width: Tk.Theme.size.fieldWidth; mono: true },
                Tk.TableColumn { key: "durationMinutes"; title: "Work minutes"; width: Tk.Theme.size.fieldWidth; mono: true },
                Tk.TableColumn { key: "progress"; title: "Progress"; width: Tk.Theme.size.fieldWidth }
            ]
            Tk.Flex.grow: 1
        }
    }
}
