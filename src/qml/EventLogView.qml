// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

T.Control {
    id: root
    objectName: "eventLogView"
    property Tk.EventLogModel model: null
    property string tooltip: ""
    property bool followTail: true
    property string _pausedAnchorId: ""
    property real _pausedOffset: 0
    property alias listView: events
    readonly property string selectedEventId: model ? model.selectedEventId : ""
    signal exportRequested(string csv)
    signal detailRequested(string eventId, string details)

    function scrollToTail() {
        // AGENT-NOTE: ListView may not have polished its content height when a
        // producer appends during Window completion. Defer the tail position.
        Qt.callLater(function() {
            if (root.followTail && events.count > 0 && !events.atYEnd)
                events.positionViewAtIndex(events.count - 1, ListView.End)
        })
    }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    padding: Tk.Theme.space.xs
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.role: Accessible.List
    Accessible.name: tooltip.length ? tooltip : qsTr("Event log")
    Accessible.focusable: enabled
    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: root.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }

    contentItem: Tk.Flex {
        objectName: "eventLogContent"
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.Flex {
            objectName: "eventLogFilters"
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.ComboBox {
                id: severityFilter
                objectName: "eventLogSeverityFilter"
                width: Tk.Theme.size.controlLg * 4
                model: [qsTr("All levels"), qsTr("Trace"), qsTr("Debug"),
                        qsTr("Info"), qsTr("Warning"), qsTr("Error"), qsTr("Critical")]
                tooltip: qsTr("Filter by severity")
                onActivated: if (root.model) root.model.severityFilter =
                    ["", "trace", "debug", "info", "warning", "error", "critical"][currentIndex]
            }
            Tk.TextField {
                objectName: "eventLogSourceFilter"
                Tk.Flex.grow: 1
                placeholderText: qsTr("Source")
                tooltip: qsTr("Filter by source")
                onTextChanged: if (root.model) root.model.sourceFilter = text
            }
            Tk.TextField {
                objectName: "eventLogTextFilter"
                Tk.Flex.grow: 1
                placeholderText: qsTr("Find in messages")
                tooltip: qsTr("Filter message and details text")
                onTextChanged: if (root.model) root.model.textFilter = text
            }
            Tk.TextField {
                objectName: "eventLogFromUtc"
                property bool invalidUtc: false
                width: Tk.Theme.size.fieldWidth + Tk.Theme.space.md
                placeholderText: qsTr("From UTC")
                tooltip: qsTr("Inclusive ISO 8601 start time")
                error: invalidUtc
                onEditingFinished: {
                    if (!root.model) return
                    const value = text.trim()
                    if (value.length === 0) {
                        root.model.fromUtc = null
                        invalidUtc = false
                    } else {
                        const zoned = /(?:Z|[+-]\d{2}:\d{2})$/i.test(value)
                        const milliseconds = Date.parse(value)
                        if (zoned && Number.isFinite(milliseconds)) {
                            root.model.fromUtc = new Date(milliseconds)
                            invalidUtc = false
                        } else invalidUtc = true
                    }
                }
            }
            Tk.TextField {
                objectName: "eventLogToUtc"
                property bool invalidUtc: false
                width: Tk.Theme.size.fieldWidth + Tk.Theme.space.md
                placeholderText: qsTr("To UTC")
                tooltip: qsTr("Inclusive ISO 8601 end time")
                error: invalidUtc
                onEditingFinished: {
                    if (!root.model) return
                    const value = text.trim()
                    if (value.length === 0) {
                        root.model.toUtc = null
                        invalidUtc = false
                    } else {
                        const zoned = /(?:Z|[+-]\d{2}:\d{2})$/i.test(value)
                        const milliseconds = Date.parse(value)
                        if (zoned && Number.isFinite(milliseconds)) {
                            root.model.toUtc = new Date(milliseconds)
                            invalidUtc = false
                        } else invalidUtc = true
                    }
                }
            }
            Tk.Button {
                objectName: "eventLogFollow"
                text: root.followTail ? qsTr("Following") : qsTr("Paused")
                variant: root.followTail ? "accent" : "outline"
                tooltip: root.followTail ? qsTr("Pause at the current position")
                                         : qsTr("Resume following new events")
                onClicked: root.followTail = !root.followTail
            }
            Tk.Button {
                objectName: "eventLogExport"
                text: qsTr("CSV")
                variant: "ghost"
                tooltip: qsTr("Export visible events as CSV")
                enabled: root.model !== null
                onClicked: root.exportRequested(root.model.exportCsv())
            }
        }

        Tk.Flex {
            objectName: "eventLogSummary"
            direction: Tk.Flex.Row
            Tk.Caption {
                objectName: "eventLogCount"
                text: root.model ? qsTr("%1 visible · %2 retained").arg(root.model.count)
                                      .arg(root.model.retainedCount) : qsTr("No event source")
                muted: true
            }
            Tk.Spacer { size: Tk.Theme.space.xs }
            Tk.Caption {
                objectName: "eventLogMode"
                text: root.followTail ? qsTr("Live") : qsTr("Inspection paused")
                muted: true
            }
        }

        Item {
            objectName: "eventLogRowsHost"
            Tk.Flex.grow: 1
            ListView {
                id: events
                objectName: "eventLogRows"
                anchors.fill: parent
                clip: true
                model: root.model
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: false
                onCountChanged: root.scrollToTail()
                onContentHeightChanged: root.scrollToTail()
                onHeightChanged: root.scrollToTail()
                T.ScrollBar.vertical: Tk.ScrollBar { }
                Accessible.role: Accessible.List
                Accessible.name: root.Accessible.name
                delegate: Item {
                    id: row
                    required property int index
                    required property string eventId
                    required property string timestamp
                    required property string severity
                    required property string source
                    required property string message
                    required property string details
                    objectName: "eventLogRow_" + eventId
                    width: events.width
                    height: Tk.Theme.size.row
                    readonly property bool selected: root.model
                        && root.model.selectedEventId === eventId
                    Rectangle {
                        anchors.fill: parent
                        color: row.selected ? Tk.Theme.color.selection
                             : row.index % 2 ? Tk.Theme.color.panelAlt
                               : Tk.Theme.alpha(Tk.Theme.color.surface, Tk.Theme.opacity.hidden)
                        border.color: row.selected ? Tk.Theme.color.accent
                            : Tk.Theme.alpha(Tk.Theme.color.surface, Tk.Theme.opacity.hidden)
                        border.width: row.selected ? Tk.Theme.size.border : 0
                    }
                    Tk.Flex {
                        anchors.fill: parent
                        anchors.leftMargin: Tk.Theme.space.xs
                        anchors.rightMargin: Tk.Theme.space.xs
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.xs
                        Tk.Mono {
                            objectName: "eventTimestamp"
                            Tk.Flex.basis: Tk.Theme.size.fieldWidth + Tk.Theme.size.controlLg
                                           + Tk.Theme.space.md
                            Tk.Flex.shrink: 0
                            text: row.timestamp.replace("T", " ").replace("Z", "")
                            muted: true
                        }
                        Tk.Overline {
                            objectName: "eventSeverity"
                            Tk.Flex.basis: Tk.Theme.size.controlLg * 3
                            Tk.Flex.shrink: 0
                            title: row.severity
                            color: row.severity === "error" || row.severity === "critical"
                                ? Tk.Theme.color.danger
                                : row.severity === "warning" ? Tk.Theme.color.warning
                                : Tk.Theme.color.textMuted
                        }
                        Tk.Label {
                            objectName: "eventSource"
                            Tk.Flex.basis: Tk.Theme.size.fieldWidth - Tk.Theme.space.xs
                            Tk.Flex.shrink: 0
                            text: row.source
                            muted: true
                            elide: Text.ElideRight
                        }
                        Tk.Label {
                            objectName: "eventMessage"
                            Tk.Flex.basis: 0
                            Tk.Flex.grow: 1
                            text: row.message
                            elide: Text.ElideRight
                            Accessible.name: row.message
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        onClicked: {
                            if (root.model) root.model.selectEvent(row.eventId)
                            root.forceActiveFocus()
                        }
                        onDoubleClicked: root.detailRequested(row.eventId, row.details)
                    }
                    Accessible.role: Accessible.ListItem
                    Accessible.name: qsTr("%1, %2, %3: %4")
                        .arg(row.timestamp).arg(row.severity).arg(row.source).arg(row.message)
                    Accessible.selected: row.selected
                }
            }
            Tk.EmptyState {
                objectName: "eventLogEmpty"
                anchors.centerIn: parent
                visible: events.count === 0
                title: qsTr("No matching events")
            }
        }

        Tk.Label {
            objectName: "eventLogDetails"
            visible: text.length > 0
            width: parent.width
            height: Math.min(implicitHeight, Tk.Theme.size.panelHeader * 3)
            clip: true
            text: root.selectedEventId && root.model
                ? root.model.eventById(root.selectedEventId).details : ""
            wrapMode: Text.Wrap
            color: Tk.Theme.color.textMuted
            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Selected event details")
        }
    }

    Connections {
        target: root.model
        function onEventsAboutToAppend() {
            if (root.followTail || !root.model || root.model.count === 0) return
            const rowHeight = Tk.Theme.size.row
            const row = Math.max(0, Math.floor(events.contentY / rowHeight))
            root._pausedAnchorId = root.model.eventAt(row).eventId || ""
            root._pausedOffset = events.contentY - row * rowHeight
        }
        function onEventsAppended() {
            if (root.followTail) {
                root.scrollToTail()
            } else {
                const row = root.model ? root.model.indexOfEventId(root._pausedAnchorId) : -1
                events.contentY = row >= 0
                    ? Math.max(0, row * Tk.Theme.size.row + root._pausedOffset) : 0
                root._pausedAnchorId = ""
            }
        }
    }
    onFollowTailChanged: if (followTail) scrollToTail()
    Component.onCompleted: if (followTail) scrollToTail()
    Tk.ToolTip { text: root.tooltip; visible: root.tooltip.length > 0 && root.hovered }

    Keys.onPressed: function(event) {
        if (!root.model || root.model.count === 0) return
        let row = root.model.indexOfEventId(root.model.selectedEventId)
        if (row < 0) row = 0
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
            row = Math.max(0, Math.min(root.model.count - 1, row + (event.key === Qt.Key_Down ? 1 : -1)))
            const item = root.model.eventAt(row)
            root.model.selectEvent(item.eventId)
            events.positionViewAtIndex(row, ListView.Contain)
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            const item = root.model.eventAt(row)
            root.detailRequested(item.eventId, item.details)
            event.accepted = true
        }
    }
}
