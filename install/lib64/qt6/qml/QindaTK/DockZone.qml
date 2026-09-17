// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// One dock zone: lanes side by side (left/right zones: columns of stacked
// slots) or stacked (top/bottom/center zones: rows of slots), each lane
// followed (or, for right/bottom, preceded) by the seam that resizes it.
// Structure comes from `lanes` (the host's snapshot); every size is read
// live from the model so seam drags never rebuild anything.
Tk.Flex {
    id: zoneItem

    required property var host
    property string zone: "left"
    // [{lane, slots: [{panelIds}]}]
    property var lanes: []
    readonly property bool sideZone: zone === "left" || zone === "right"
    // Lane 0 sits at the host edge: for right/bottom that is the far side,
    // so the visual order is reversed and the seam goes before the lane.
    readonly property bool reversed: zone === "right" || zone === "bottom"
    readonly property real seamSign: reversed ? -1 : 1

    objectName: "dockZone_" + zone
    direction: sideZone ? Tk.Flex.Row : Tk.Flex.Column
    gap: 0
    visible: lanes.length > 0
    Tk.Flex.shrink: 0
    Tk.Flex.grow: 0

    function visualIndex(index) {
        return reversed ? lanes.length - 1 - index : index
    }

    Repeater {
        model: zoneItem.lanes
        delegate: Tk.Flex {
            id: laneItem
            required property var modelData
            required property int index
            readonly property int lane: modelData.lane
            readonly property var slots: modelData.slots
            readonly property real extent: zoneItem.host.laneExtent(zoneItem.zone, lane)

            objectName: "dockLane_" + zoneItem.zone + "_" + lane
            direction: zoneItem.sideZone ? Tk.Flex.Column : Tk.Flex.Row
            gap: 0
            Tk.Flex.order: zoneItem.visualIndex(index) * 2 + 1
            Tk.Flex.basis: extent
            Tk.Flex.shrink: 0
            Tk.Flex.grow: 0

            Repeater {
                model: laneItem.slots
                delegate: Tk.DockFrame {
                    required property var modelData
                    required property int index
                    host: zoneItem.host
                    zone: zoneItem.zone
                    lane: laneItem.lane
                    slotIndex: index
                    panelIds: modelData.panelIds
                    Tk.Flex.order: index * 2
                }
            }
            Repeater {
                model: Math.max(laneItem.slots.length - 1, 0)
                delegate: Tk.DockDivider {
                    required property int index
                    objectName: "dockShareSeam_" + zoneItem.zone + "_" + laneItem.lane + "_" + index
                    vertical: !zoneItem.sideZone
                    share: true
                    Tk.Flex.order: index * 2 + 1
                    onDragged: function(delta) {
                        zoneItem.host.adjustShares(zoneItem.zone, laneItem.lane, index, delta,
                                                   zoneItem.sideZone ? laneItem.height : laneItem.width)
                    }
                }
            }
        }
    }

    Repeater {
        model: zoneItem.lanes
        delegate: Tk.DockDivider {
            required property var modelData
            required property int index
            objectName: "dockDivider_" + zoneItem.zone + "_" + modelData.lane
            vertical: zoneItem.sideZone
            Tk.Flex.order: zoneItem.visualIndex(index) * 2 + (zoneItem.reversed ? 0 : 2)
            onDragged: function(delta) {
                zoneItem.host.resizeLane(zoneItem.zone, modelData.lane, delta * zoneItem.seamSign)
            }
        }
    }
}
