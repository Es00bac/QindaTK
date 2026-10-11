// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 900; height: 1000
    color: Tk.Theme.color.bg
    Tk.Scroll {
        anchors.fill: parent
        Tk.Flex {
            width: parent.width
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            Tk.Heading { text: "Office controls"; level: 1 }
            Tk.BorderEditor {
                objectName: "bordersDemo"
                width: parent.width
                borders: ({top:{style:"double",color:"#447799",width:1},left:{style:"dashed",color:"#558855",width:2}})
                onBordersEdited: next => borders = next
            }
            Tk.NumberField { objectName: "decimalDemo"; from: 0.1; to: 10; decimals: 2; value: 0.58; label: "Gap" }
            Tk.TaskBoard {
                objectName: "boardDemo"
                width: parent.width
                categoryColors: ({Finance:"#996633", Urgent:"#aa4444"})
                columns: [{status:"todo",label:"To do",tasks:[{taskId:"one",title:"Review the quarterly report",categories:["Finance","Urgent"],extraFields:[{label:"Owner",value:"Example team"}]}]}]
            }
            Tk.EditableGrid {
                objectName: "gridDemo"
                width: parent.width; height: Tk.Theme.size.panelMinHeight
                model: Tk.EditableGridModel { Component.onCompleted: setDimensions(3,3) }
                valueList: () => ({values:["Open","Closed"],limitToList:true})
                Component.onCompleted: Qt.callLater(function() { setCurrent(0,0,false); beginCellEdit() })
            }
            Tk.DockHost {
                objectName: "dockDemo"
                width: parent.width; implicitHeight: Tk.Theme.size.panelMinHeight * 2
                canvas: Tk.Label { text: "Document canvas"; horizontalAlignment: Text.AlignHCenter }
                Tk.DockPanel { panelId:"left"; title:"Properties"; zone:"left"; extent:300; Tk.Label { text:"Left pane" } }
                Tk.DockPanel { panelId:"right"; title:"Comments"; zone:"right"; extent:300; Tk.Label { text:"Right pane" } }
            }
        }
    }
    Tk.Toast {
        objectName: "toastDemo"
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: Tk.Theme.space.md
        title: "Saved"; message: "The quarterly report and its updated styles are ready."; duration: 0
        Component.onCompleted: show()
    }
}
