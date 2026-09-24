// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk
pragma ComponentBehavior: Bound

// AGENT-NOTE: one NodeGraph card (docs/controls.md). It reads `graph` for
// geometry, selection and search state and only asks it to emit requests;
// the host that owns the graph model decides every edit.
Rectangle {
    id: card
    required property var graph
    property var record: ({ nodeId: "", title: "", x: 0, y: 0, inputs: [], outputs: [] })
    property bool dragging: false
    property point dragDelta: Qt.point(0, 0)
    readonly property string nodeId: String(record.nodeId)
    readonly property bool selected: graph.isNodeSelected(nodeId)
    readonly property bool matched: graph.searchMatches.indexOf(nodeId) >= 0
    objectName: "node_" + nodeId
    width: graph.nodeWidth
    height: graph.nodeHeight
    radius: Tk.Theme.radius.md
    color: Tk.Theme.color.panel
    border.width: matched && !selected ? Tk.Theme.size.focusRing : Tk.Theme.size.border
    border.color: selected ? Tk.Theme.color.accent
        : matched ? Tk.Theme.color.warning : Tk.Theme.color.border
    Accessible.role: Accessible.ListItem
    Accessible.name: String(record.title || record.nodeId)
    Accessible.description: [graph.flowMode ? qsTr("Incoming %1, outgoing %2")
            .arg(record.inFlow || 0).arg(record.outFlow || 0) : "",
        matched ? qsTr("Search match") : ""].filter(Boolean).join(". ")
    Accessible.selected: selected

    Rectangle {
        objectName: "nodeHeader_" + card.nodeId
        width: parent.width
        height: Tk.Theme.size.header
        radius: Tk.Theme.radius.md
        color: card.nodeId === card.graph.selectedNodeId
            ? Tk.Theme.color.controlActiveBg : Tk.Theme.color.panelAlt
        border.color: Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        Tk.Label {
            anchors.fill: parent
            anchors.leftMargin: Tk.Theme.space.sm
            anchors.rightMargin: Tk.Theme.space.sm
            text: card.graph.flowMode ? qsTr("%1  ↓%2 ↑%3")
                .arg(card.record.title || card.record.nodeId).arg(card.record.inFlow || 0)
                .arg(card.record.outFlow || 0) : String(card.record.title || card.record.nodeId)
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: Tk.Theme.font.body
            color: Tk.Theme.color.text
        }
    }

    Repeater {
        model: card.record.inputs || []
        delegate: T.Button {
            id: inputPort
            required property var modelData
            required property int index
            property string tooltip: qsTr("Input %1, type %2%3. Activate after selecting an output.")
                .arg(text).arg(modelData.type || qsTr("unspecified"))
                .arg(modelData.optional ? qsTr(", optional") : "")
            objectName: "nodePort_" + card.nodeId + "_" + modelData.portId
            x: 0
            y: Tk.Theme.size.header + index * Tk.Theme.size.row
            width: card.width / 2
            height: Tk.Theme.size.row
            text: String(modelData.label || modelData.portId)
            Accessible.name: tooltip
            Accessible.role: Accessible.Button
            background: Rectangle {
                color: inputPort.down ? Tk.Theme.color.hover : "transparent"
            }
            contentItem: Item {
                Rectangle {
                    x: Tk.Theme.space.xs
                    y: (parent.height - height) / 2
                    width: Tk.Theme.size.iconSm
                    height: Tk.Theme.size.iconSm
                    radius: Tk.Theme.radius.full
                    color: inputPort.modelData.optional ? "transparent"
                        : card.graph.portColor(inputPort.modelData.type)
                    border.color: card.graph.portColor(inputPort.modelData.type)
                    border.width: Tk.Theme.size.border
                }
                Tk.Label {
                    anchors.fill: parent
                    anchors.leftMargin: Tk.Theme.size.iconSm + Tk.Theme.space.sm
                    anchors.rightMargin: Tk.Theme.space.xs
                    text: inputPort.text
                    color: Tk.Theme.color.text
                    font.pixelSize: Tk.Theme.font.caption
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
            onClicked: card.graph.beginOrFinishLink(card.nodeId, String(modelData.portId), "input")
            Tk.ToolTip { text: inputPort.tooltip; visible: inputPort.hovered }
        }
    }

    Repeater {
        model: card.record.outputs || []
        delegate: T.Button {
            id: outputPort
            required property var modelData
            required property int index
            property string tooltip: qsTr("Output %1, type %2. Activate to begin a connection.")
                .arg(text).arg(modelData.type || qsTr("unspecified"))
            objectName: "nodePort_" + card.nodeId + "_" + modelData.portId
            x: card.width / 2
            y: Tk.Theme.size.header + index * Tk.Theme.size.row
            width: card.width / 2
            height: Tk.Theme.size.row
            text: String(modelData.label || modelData.portId)
            Accessible.name: tooltip
            Accessible.role: Accessible.Button
            background: Rectangle {
                color: outputPort.down ? Tk.Theme.color.hover : "transparent"
            }
            contentItem: Item {
                Tk.Label {
                    anchors.fill: parent
                    anchors.leftMargin: Tk.Theme.space.xs
                    anchors.rightMargin: Tk.Theme.size.iconSm + Tk.Theme.space.sm
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    text: outputPort.text
                    color: Tk.Theme.color.text
                    font.pixelSize: Tk.Theme.font.caption
                    elide: Text.ElideLeft
                }
                Rectangle {
                    x: parent.width - width - Tk.Theme.space.xs
                    y: (parent.height - height) / 2
                    width: Tk.Theme.size.iconSm
                    height: Tk.Theme.size.iconSm
                    radius: Tk.Theme.radius.full
                    color: card.graph.portColor(outputPort.modelData.type)
                }
            }
            onClicked: card.graph.beginOrFinishLink(card.nodeId, String(modelData.portId), "output")
            Tk.ToolTip { text: outputPort.tooltip; visible: outputPort.hovered }
        }
    }

    // AGENT-GUARD: the move request is deferred with callLater because a
    // stolen grab deactivates the handler before `canceled` fires; the
    // deferral lets a canceled or Escaped drag emit nothing.
    DragHandler {
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active) {
                card.graph.dragCanceled = false
                card.dragging = true
                return
            }
            // Copy the numbers: a point read from a property is a live reference.
            const dx = card.dragDelta.x
            const dy = card.dragDelta.y
            card.dragging = false
            card.dragDelta = Qt.point(0, 0)
            Qt.callLater(card.graph.finishDrag, "node", card.nodeId, dx, dy)
        }
        onTranslationChanged: card.dragDelta = Qt.point(translation.x, translation.y)
        onCanceled: card.graph.dragCanceled = true
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.NoModifier
        onTapped: card.graph.nodeSelected(card.nodeId)
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.ControlModifier
        onTapped: card.graph.nodeSelectionToggled(card.nodeId)
    }
}
