// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 1000
    height: 520

    Tk.ResourcePlanModel {
        id: plan
        Component.onCompleted: {
            setResources([
                { id: "animation", name: "Animation", unit: "hours", capacity: 12 },
                { id: "render", name: "Render farm", unit: "GPU hours", capacity: 20 },
                { id: "editing", name: "Editing", unit: "hours", capacity: 16 },
                { id: "sound", name: "Sound", unit: "hours", capacity: 8 }
            ])
            setBuckets([
                { id: "week-1", name: "Week 1" }, { id: "week-2", name: "Week 2" },
                { id: "week-3", name: "Week 3" }, { id: "week-4", name: "Week 4" },
                { id: "week-5", name: "Week 5" }, { id: "week-6", name: "Week 6" },
                { id: "week-7", name: "Week 7" }, { id: "week-8", name: "Week 8" }
            ])
            requestAllocation("animation", "week-1", 10)
            requestAllocation("animation", "week-2", 14)
            requestAllocation("render", "week-1", 16)
            requestAllocation("editing", "week-2", 12)
            requestAllocation("sound", "week-3", 6)
        }
    }

    Tk.Box {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        color: Tk.Theme.color.canvas
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            Item {
                implicitWidth: parent.width
                implicitHeight: Tk.Theme.size.control
                Tk.Overline {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    title: qsTr("Resource plan")
                    color: Tk.Theme.color.text
                }
                Tk.Caption {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Enter or double-click an allocation to edit · Ctrl+Z/Y undo/redo")
                }
            }
            Tk.ResourceTable {
                model: plan
                tooltip: qsTr("Weekly production resource allocation")
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
                Tk.Flex.minHeight: 0
            }
        }
    }
}
