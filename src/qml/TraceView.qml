// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (WP-087): TraceIndex owns exact times and bounded queries;
// this view only projects normalized coordinates and never edits trace data.
Item {
    id: root
    objectName: "traceView"
    property Tk.TraceIndex index: null
    property string tooltip: qsTr("Trace explorer")
    property string selectedId: ""
    property string threadFilter: ""
    property string textFilter: ""
    property real zoom: 1
    property real pan: 0
    property int dataRevision: 0
    readonly property real windowSize: 1 / zoom
    readonly property var visibleRecords: {
        if (dataRevision < 0 || !index) return []
        return index.queryWindow(pan, pan + windowSize, threadFilter, textFilter, 500)
    }
    readonly property var selectedRecord: {
        if (dataRevision < 0 || !index || !selectedId) return ({})
        return index.record(selectedId)
    }
    signal exportRequested(string json)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Connections { target: root.index; function onChanged() { ++root.dataRevision } }

    function zoomBy(factor) {
        const next = Math.max(1, Math.min(64, zoom * factor))
        const midpoint = pan + windowSize / 2
        zoom = next
        pan = Math.max(0, Math.min(1 - windowSize, midpoint - windowSize / 2))
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("TRACE"); Tk.Flex.grow: 1 }
            Tk.Caption { text: root.index ? qsTr("%1 records · %2 dropped")
                                              .arg(root.index.count).arg(root.index.truncatedCount) : "" }
            Tk.Button {
                objectName: "traceZoomOut"
                text: "−"; tooltip: qsTr("Zoom out")
                onClicked: root.zoomBy(0.5)
            }
            Tk.Button {
                objectName: "traceZoomIn"
                text: "+"; tooltip: qsTr("Zoom in")
                onClicked: root.zoomBy(2)
            }
            Tk.Button {
                objectName: "traceExport"
                text: qsTr("Export"); tooltip: qsTr("Export trace as JSON")
                onClicked: if (root.index) root.exportRequested(root.index.exportJson())
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.ComboBox {
                objectName: "traceThreadFilter"
                model: [qsTr("All threads")].concat(root.index ? root.index.threads : [])
                tooltip: qsTr("Filter trace by thread")
                onCurrentIndexChanged: root.threadFilter = currentIndex > 0 ?
                                           root.index.threads[currentIndex - 1] : ""
            }
            Tk.SearchField {
                objectName: "traceSearch"
                Tk.Flex.grow: 1
                placeholderText: qsTr("Filter spans and events")
                tooltip: qsTr("Filter trace by name or category")
                onSearched: function(text) { root.textFilter = text }
            }
        }
        Tk.Slider {
            objectName: "tracePan"
            from: 0
            to: Math.max(0, 1 - root.windowSize)
            value: root.pan
            enabled: root.zoom > 1
            tooltip: qsTr("Pan through trace time")
            onValueModified: function(value) { root.pan = value }
        }
        Flickable {
            id: timeline
            objectName: "traceTimeline"
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            Tk.Flex.minHeight: Tk.Theme.size.panelMinHeight
            implicitWidth: root.implicitWidth
            implicitHeight: Tk.Theme.size.panelMinHeight
            clip: true
            contentWidth: width
            contentHeight: Math.max(height, root.visibleRecords.length * rowHeight)
            boundsBehavior: Flickable.StopAtBounds
            readonly property real labelWidth: Tk.Theme.size.panelMinWidth / 2
            readonly property real rowHeight: Tk.Theme.size.row
            Rectangle {
                width: timeline.contentWidth
                height: timeline.contentHeight
                color: Tk.Theme.color.surface
            }
            Repeater {
                model: root.visibleRecords
                delegate: Tk.Caption {
                    required property var modelData
                    required property int index
                    x: Tk.Theme.space.sm
                    y: index * timeline.rowHeight + Tk.Theme.space.xs
                    width: timeline.labelWidth - Tk.Theme.space.sm
                    text: modelData.threadId
                    elide: Text.ElideRight
                }
            }
            Repeater {
                model: root.visibleRecords
                delegate: Rectangle {
                    id: traceItem
                    required property var modelData
                    required property int index
                    objectName: "traceRecord_" + modelData.id
                    readonly property real trackWidth: Math.max(0, timeline.width - timeline.labelWidth)
                    x: timeline.labelWidth + Math.max(0, (modelData.startFraction - root.pan) * root.zoom) * trackWidth
                    y: index * timeline.rowHeight + Tk.Theme.space.xs
                    width: Math.max(Tk.Theme.space.xs,
                                    (modelData.endFraction - modelData.startFraction) * root.zoom * trackWidth)
                    height: timeline.rowHeight - 2 * Tk.Theme.space.xs
                    radius: Tk.Theme.radius.sm
                    color: root.selectedId === modelData.id ? Tk.Theme.color.accent
                           : modelData.kind === "event" ? Tk.Theme.color.warning
                           : Tk.Theme.color.selection
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.name + ", " + modelData.threadId
                    Tk.Caption {
                        anchors.fill: parent
                        anchors.leftMargin: Tk.Theme.space.xs
                        text: traceItem.modelData.name
                        color: Tk.Theme.color.text
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.selectedId = traceItem.modelData.id
                    }
                }
            }
        }
        Tk.Caption {
            objectName: "traceInspector"
            text: root.selectedId ? qsTr("%1 · %2 · %3 ns to %4 ns · %5 · parent %6")
                  .arg(root.selectedRecord.name).arg(root.selectedRecord.threadId)
                  .arg(root.selectedRecord.startNs).arg(root.selectedRecord.endNs)
                  .arg(root.selectedRecord.clockDomain)
                  .arg(root.selectedRecord.parentId || qsTr("none"))
                  : qsTr("Select a span or event to inspect exact times")
            elide: Text.ElideRight
        }
    }
}
