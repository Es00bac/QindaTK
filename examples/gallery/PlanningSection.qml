// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Planning"
    note: "GanttView · GanttCalendar · GanttTaskModel"

    Tk.GanttCalendar { id: calendar; timeZoneId: "America/New_York" }
    Tk.GanttSimpleScheduleAdapter { id: scheduler }
    Tk.GanttTaskModel {
        id: tasks
        calendar: calendar
        transactionAdapter: scheduler
        Component.onCompleted: replaceTasks([
            { taskId: "00000000-0000-0000-0000-000000000301", label: "Sketch",
              startUtc: "1741611600000", durationMinutes: 180, progress: 0.7,
              baselineStartUtc: "1741611600000", baselineDurationMinutes: 180,
              dependencies: [] },
            { taskId: "00000000-0000-0000-0000-000000000302", label: "Review",
              startUtc: "1741622400000", durationMinutes: 120, progress: 0.2,
              dependencies: ["00000000-0000-0000-0000-000000000301"] }
        ])
    }
    Tk.SelectionModel { id: selection }

    GalleryRow {
        label: "GanttView"
        Tk.GanttView {
            objectName: "galleryGanttView"
            implicitWidth: 520
            implicitHeight: 130
            taskModel: tasks
            selectionModel: selection
            visibleStartUtc: "1741611600000"
            pixelsPerWorkingMinute: Tk.Theme.size.row / 10
            tooltip: "Planning preview"
        }
    }
}
