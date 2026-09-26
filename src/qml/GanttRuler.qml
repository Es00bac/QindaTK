// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk
import "GanttScale.js" as GanttScale

// Internal part of GanttView (D-303): the header. On the working scale it is
// the original single row of local instants; on the calendar scale it is a
// two-tier ruler (weeks over days, months over weeks, or years over months
// as pixelsPerDay shrinks) when the header is at least 1.5 rows tall, else
// the lower tier alone. Marker labels (today, status date) sit on top.
Rectangle {
    id: ruler
    required property Tk.GanttView view
    objectName: "ganttHeader"
    color: Tk.Theme.color.headerBg
    clip: true

    readonly property bool twoTier: view.calendarScale && height >= Tk.Theme.size.row * 1.5
    readonly property real tierHeight: twoTier ? height / 2 : height
    readonly property var tiers: {
        const v = view
        if (!v.calendarScale || !v.calendar) return { major: [], minor: [] }
        void v.calendarRevision
        const first = v.calendar.localDate(v.visibleStartUtc)
        const days = GanttScale.visibleDays(first, v.trackWidth / Math.max(0.0001, v.pixelsPerDay) + 1,
            function(iso) { return v.xFor(v.calendar.dayStartUtc(iso)) })
        const locale = Qt.locale()
        const weekStart = locale.firstDayOfWeek === 0 ? 7 : locale.firstDayOfWeek
        return GanttScale.rulerTiers(days, v.pixelsPerDay, locale, weekStart)
    }

    Tk.Label {
        visible: ruler.view.labelWidth > 0
        x: Tk.Theme.space.sm
        width: Math.max(0, ruler.view.labelWidth - Tk.Theme.space.sm * 2)
        height: ruler.height
        text: qsTr("Task")
        color: Tk.Theme.color.headerText
    }

    Item {
        id: track
        x: ruler.view.labelWidth
        width: ruler.view.trackWidth
        height: ruler.height
        clip: true

        // Working scale: the original instant labels at tickStepMinutes.
        Repeater {
            model: ruler.view.calendarScale ? 0 : Math.min(100, Math.ceil(track.width
                / (ruler.view.tickStepMinutes * Math.max(0.0001, ruler.view.pixelsPerWorkingMinute))) + 1)
            delegate: Tk.Label {
                required property int index
                x: index * ruler.view.tickStepMinutes * ruler.view.pixelsPerWorkingMinute
                height: track.height
                text: ruler.view.calendar ? ruler.view.calendar.formatLocal(ruler.view.calendar.addWorkingMinutes(
                      ruler.view.visibleStartUtc, index * ruler.view.tickStepMinutes)) : ""
                color: Tk.Theme.color.headerText
                font.pixelSize: Tk.Theme.font.caption
            }
        }

        Repeater {
            model: ruler.twoTier ? ruler.tiers.major : []
            delegate: Item {
                id: major
                required property var modelData
                objectName: "ganttRulerMajor"
                x: major.modelData.x
                width: major.modelData.width
                height: ruler.tierHeight
                Tk.Label {
                    x: Math.max(0, -major.x) + Tk.Theme.space.sm
                    width: Math.max(0, major.width - x - Tk.Theme.space.xs)
                    height: parent.height
                    text: major.modelData.text
                    color: Tk.Theme.color.headerText
                    font.pixelSize: Tk.Theme.font.caption
                }
                Rectangle {
                    width: Tk.Theme.size.border
                    height: ruler.height
                    color: Tk.Theme.color.divider
                }
            }
        }
        Rectangle {
            visible: ruler.twoTier
            y: ruler.tierHeight
            width: track.width
            height: Tk.Theme.size.border
            color: Tk.Theme.color.divider
        }
        Repeater {
            model: ruler.view.calendarScale ? ruler.tiers.minor : []
            delegate: Item {
                id: minor
                required property var modelData
                objectName: "ganttRulerMinor"
                x: minor.modelData.x
                y: ruler.twoTier ? ruler.tierHeight : 0
                width: minor.modelData.width
                height: ruler.tierHeight
                Tk.Label {
                    width: parent.width
                    height: parent.height
                    horizontalAlignment: Text.AlignHCenter
                    text: minor.modelData.text
                    elide: Text.ElideNone
                    clip: true
                    color: ruler.view.calendar && !ruler.view.calendar.isWorkingDate(minor.modelData.iso)
                           && ruler.view.pixelsPerDay >= 16 ? Tk.Theme.color.textMuted : Tk.Theme.color.headerText
                    font.pixelSize: Tk.Theme.font.caption
                }
                Rectangle {
                    width: Tk.Theme.size.border
                    height: parent.height
                    color: Tk.Theme.color.divider
                }
            }
        }

        Repeater {
            model: ruler.view.markers || []
            delegate: Rectangle {
                id: chip
                required property var modelData
                required property int index
                readonly property real at: ruler.view.xFor(String(chip.modelData.utc || ""))
                objectName: "ganttMarkerLabel_" + index
                visible: String(chip.modelData.label || "").length > 0 && at >= 0 && at <= track.width
                x: Math.min(Math.max(0, at - width / 2), Math.max(0, track.width - width))
                // Two tiers: in the upper one, clear of the day labels.
                y: ruler.twoTier ? Math.max(0, (ruler.tierHeight - height) / 2)
                                 : ruler.height - height - Tk.Theme.space.xs
                width: chipText.implicitWidth + Tk.Theme.space.sm * 2
                height: chipText.implicitHeight + Tk.Theme.space.xs
                radius: Tk.Theme.radius.sm
                color: Tk.Theme.color[String(chip.modelData.role || "accent")] || Tk.Theme.color.accent
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: String(chip.modelData.label || "")
                    color: Tk.Theme.color[String(chip.modelData.role || "accent") + "Contrast"]
                           || Tk.Theme.color.bg
                    font.family: Tk.Theme.font.family
                    font.pixelSize: Tk.Theme.font.micro
                    font.bold: true
                }
            }
        }
    }
}
