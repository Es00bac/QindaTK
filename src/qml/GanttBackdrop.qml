// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk
import "GanttScale.js" as GanttScale

// Internal part of GanttView (D-302): the row stripes and, on the calendar
// time scale, the non-working day columns — drawn once under the rows so a
// weekend is one continuous band instead of a fill repeated per delegate.
// Only the rows and days in view exist (the day list is bounded by
// GanttScale.MAX_DAYS).
Item {
    id: backdrop
    required property Tk.GanttView view
    property real contentY: 0
    clip: true

    readonly property real rowHeight: Math.max(1, view.rowHeight)
    readonly property int firstRow: Math.max(0, Math.floor(contentY / rowHeight))
    readonly property int shownRows: Math.min(Math.max(0, view.count - firstRow),
                                             Math.ceil(height / rowHeight) + 1)

    Repeater {
        model: backdrop.shownRows
        delegate: Rectangle {
            required property int index
            readonly property int row: backdrop.firstRow + index
            objectName: "ganttStripe"
            y: row * backdrop.rowHeight - backdrop.contentY
            width: backdrop.width
            height: backdrop.rowHeight
            color: row % 2 ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
            Rectangle {
                y: parent.height - height
                width: parent.width
                height: Tk.Theme.size.border
                color: Tk.Theme.color.divider
            }
        }
    }

    Item {
        id: track
        x: backdrop.view.labelWidth
        width: Math.max(0, backdrop.width - x)
        height: backdrop.height
        clip: true
        visible: backdrop.view.calendarScale && backdrop.view.pixelsPerDay >= 3
        readonly property var days: {
            const v = backdrop.view
            if (!visible || !v.calendar) return []
            void v.calendarRevision
            const first = v.calendar.localDate(v.visibleStartUtc)
            return GanttScale.visibleDays(first, width / Math.max(0.0001, v.pixelsPerDay) + 1,
                function(iso) { return v.xFor(v.calendar.dayStartUtc(iso)) })
        }
        Repeater {
            model: track.days
            delegate: Rectangle {
                id: day
                required property var modelData
                objectName: "ganttNonWorking"
                visible: !backdrop.view.calendar.isWorkingDate(day.modelData.iso)
                x: day.modelData.x
                width: day.modelData.width
                height: track.height
                color: Tk.Theme.alpha(Tk.Theme.color.textMuted, Tk.Theme.opacity.wash * 1.5)
            }
        }
    }

    Rectangle {
        objectName: "ganttLabelDivider"
        visible: backdrop.view.labelWidth > 0
        x: backdrop.view.labelWidth - width
        width: Tk.Theme.size.border
        height: backdrop.height
        color: Tk.Theme.color.border
    }
}
