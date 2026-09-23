// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 900
    height: 580
    color: Tk.Theme.color.bg
    Tk.PartWholeView {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.lg
        tooltip: qsTr("Department allocation")
        mode: Tk.PartWholeRenderer.Sunburst
        model: Tk.PartWholeModel {
            unit: "hours"
            Component.onCompleted: replaceNodes([
                {id:"all",label:"All work",value:100},
                {id:"design",parentId:"all",label:"Design",value:25},
                {id:"design-ui",parentId:"design",label:"UI",value:15},
                {id:"design-research",parentId:"design",label:"Research",value:10},
                {id:"engineering",parentId:"all",label:"Engineering",value:55},
                {id:"engineering-client",parentId:"engineering",label:"Client",value:25},
                {id:"engineering-core",parentId:"engineering",label:"Core",value:30},
                {id:"qa",parentId:"all",label:"QA",value:20},
                {id:"qa-manual",parentId:"qa",label:"Manual",value:5},
                {id:"qa-auto",parentId:"qa",label:"Automation",value:15}
            ])
        }
    }
}
