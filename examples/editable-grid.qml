// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 1120
    height: 640

    Tk.EditableGridModel {
        id: inventory
        Component.onCompleted: {
            setDimensions(100000, 100)
            frozenRows = 1
            frozenColumns = 2
            beginEdit("Seed inventory preview")
            stageCell("r:1", "c:1", "Asset")
            stageCell("r:1", "c:2", "Kind")
            stageCell("r:1", "c:3", "Owner")
            stageCell("r:1", "c:4", "Duration")
            stageCell("r:2", "c:1", "Scene 010")
            stageCell("r:2", "c:2", "Video")
            stageCell("r:2", "c:3", "Editorial")
            stageCell("r:2", "c:4", "00:01:42")
            stageCell("r:3", "c:1", "Mic L")
            stageCell("r:3", "c:2", "Audio")
            stageCell("r:3", "c:3", "Sound")
            stageCell("r:3", "c:4", "00:03:18")
            commitEdit()
            setRange("r:2", "c:1", "r:2", "c:4")
        }
    }

    Tk.Box {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        color: Tk.Theme.color.canvas
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.sm
            Item {
                implicitWidth: parent.width
                implicitHeight: Tk.Theme.size.control
                Tk.Label {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Asset inventory · 100,000 rows × 100 columns"
                    color: Tk.Theme.color.text
                    font.pixelSize: Tk.Theme.font.body
                }
                Tk.Caption {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Enter edits · Shift + arrows selects · Tab returns focus"
                }
            }
            Tk.EditableGrid {
                objectName: "editableGridExample"
                Tk.Flex.grow: 1
                width: parent.width
                model: inventory
                tooltip: "Asset inventory spreadsheet"
                alternatingRows: true
                columnLabel: function(id, index) {
                    const labels = ["Asset", "Kind", "Owner", "Duration"]
                    return index < labels.length ? labels[index] : String(index + 1)
                }
            }
        }
    }
}
