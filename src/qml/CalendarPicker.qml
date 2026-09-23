// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// LocaleService supplies calendar-correct month data and ISO Gregorian dates.
Item {
    id: picker
    property Tk.LocaleService localeService: null
    property string selectedDate: ""
    property string minDate: ""
    property string maxDate: ""
    property var markedDates: []
    property bool rangeMode: false
    property string rangeStart: ""
    property string rangeEnd: ""
    property string tooltip: ""
    property int shownYear: 0
    property int shownMonth: 0
    property int activeIndex: -1
    property var monthData: ({})
    readonly property var cells: monthData.cells || []
    readonly property var weekdayLabels: monthData.weekdayLabels || []

    signal dateSelected(string isoDate)
    signal dateRangeSelected(string startIsoDate, string endIsoDate)

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Table
    Accessible.name: tooltip || qsTr("Calendar")
    Accessible.description: monthData.monthLabel || ""

    function refreshMonth() {
        if (!localeService || !shownYear || !shownMonth) { monthData = ({}); return false }
        monthData = localeService.monthGrid(shownYear, shownMonth, minDate, maxDate)
        return monthData.ok === true
    }
    function showDate(date) {
        if (!localeService || !localeService.calendarFields) return false
        const parts = localeService.calendarFields(String(date))
        if (!parts.ok) return false
        shownYear = Number(parts.year); shownMonth = Number(parts.month)
        activeIndex = -1
        const ok = refreshMonth()
        for (let i = 0; i < cells.length; ++i) if (cells[i].date === date) activeIndex = i
        return ok
    }
    function shiftMonth(amount) {
        if (!shownYear || !shownMonth) return false
        if (!localeService || !localeService.shiftMonth) return false
        const next = localeService.shiftMonth(shownYear, shownMonth, amount)
        if (!next.ok) return false
        shownYear = Number(next.year); shownMonth = Number(next.month)
        activeIndex = -1
        return refreshMonth()
    }
    function activateCell(index) {
        if (index < 0 || index >= cells.length || !cells[index].enabled) return false
        const iso = cells[index].date
        if (rangeMode) {
            if (!rangeStart || rangeEnd) { rangeStart = iso; rangeEnd = "" }
            else { rangeEnd = iso < rangeStart ? rangeStart : iso; rangeStart = iso < rangeStart ? iso : rangeStart }
            if (rangeEnd) dateRangeSelected(rangeStart, rangeEnd)
        } else {
            selectedDate = iso
            dateSelected(iso)
        }
        activeIndex = index
        return true
    }
    function moveActive(delta) {
        if (!cells.length) return
        let index = activeIndex < 0 ? 0 : activeIndex
        for (let tries = 0; tries < cells.length; ++tries) {
            index = (index + delta + cells.length) % cells.length
            if (cells[index].enabled) { activeIndex = index; const item = dayRepeater.itemAt(index); if (item) item.forceActiveFocus(); return }
        }
    }
    function isMarked(iso) { return markedDates.indexOf(iso) >= 0 }
    function handleKey(event) {
        switch (event.key) {
        case Qt.Key_Left: moveActive(-1); break
        case Qt.Key_Right: moveActive(1); break
        case Qt.Key_Up: moveActive(-7); break
        case Qt.Key_Down: moveActive(7); break
        case Qt.Key_PageUp: shiftMonth(-1); break
        case Qt.Key_PageDown: shiftMonth(1); break
        case Qt.Key_Return: case Qt.Key_Enter: activateCell(activeIndex); break
        default: return false
        }
        return true
    }

    onSelectedDateChanged: if (selectedDate) showDate(selectedDate)
    onLocaleServiceChanged: selectedDate ? showDate(selectedDate) : refreshMonth()
    onMinDateChanged: refreshMonth()
    onMaxDateChanged: refreshMonth()
    Component.onCompleted: {
        if (selectedDate) showDate(selectedDate)
        else if (localeService) {
            showDate(localeService.todayDateIso())
        }
    }
    Keys.onPressed: function(event) { event.accepted = picker.handleKey(event) }

    Connections {
        target: picker.localeService
        function onChanged() { picker.refreshMonth() }
    }
    Tk.ToolTip { text: picker.tooltip; visible: text.length > 0 && picker.activeFocus }
    Tk.Flex {
        id: column
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.IconButton { iconName: "chevron-left"; tooltip: qsTr("Previous month"); onClicked: picker.shiftMonth(-1) }
            Tk.Label { objectName: "calendarMonthLabel"; text: picker.monthData.monthLabel || ""; horizontalAlignment: Text.AlignHCenter; Tk.Flex.grow: 1 }
            Tk.IconButton { iconName: "chevron-right"; tooltip: qsTr("Next month"); onClicked: picker.shiftMonth(1) }
        }
        Tk.Grid {
            id: grid
            objectName: "calendarGrid"
            columns: "repeat(7, 1fr)"
            rows: "repeat(7, auto)"
            gap: Tk.Theme.space.xs
            activeFocusOnTab: true
            Repeater {
                model: picker.weekdayLabels
                delegate: Tk.Caption {
                    required property int index
                    required property var modelData
                    Tk.Grid.row: 0
                    Tk.Grid.column: index
                    width: Tk.Theme.size.control
                    horizontalAlignment: Text.AlignHCenter
                    text: String(modelData).slice(0, 2)
                    Accessible.role: Accessible.ColumnHeader
                    Accessible.name: String(modelData)
                }
            }
            Repeater {
                id: dayRepeater
                model: picker.cells
                delegate: Tk.Button {
                    id: dayButton
                    required property int index
                    required property var modelData
                    Tk.Grid.row: Math.floor(index / 7) + 1
                    Tk.Grid.column: index % 7
                    width: Tk.Theme.size.control
                    activeFocusOnTab: index === picker.activeIndex
                    padding: 0
                    leftPadding: 0
                    rightPadding: 0
                    topPadding: 0
                    bottomPadding: 0
                    text: modelData.label || (picker.localeService ? picker.localeService.formatNumber(modelData.day, 0) : String(modelData.day))
                    tooltip: modelData.date
                    enabled: modelData.enabled
                    small: true
                    variant: modelData.date === picker.selectedDate || (picker.rangeMode && (modelData.date === picker.rangeStart || modelData.date === picker.rangeEnd)) ? "accent" : "ghost"
                    Accessible.role: Accessible.Button
                    Accessible.name: (picker.monthData.monthLabel || "") + " " + text
                    Accessible.description: modelData.today ? qsTr("Today") : picker.isMarked(modelData.date) ? qsTr("Marked date") : ""
                    onClicked: picker.activateCell(index)
                    Keys.onPressed: function(event) { event.accepted = picker.handleKey(event) }
                }
            }
        }
    }
}
