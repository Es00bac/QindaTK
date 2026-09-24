// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Mock caption/grade host. No video burn-in or color transform is attached.
Rectangle {
    id: root
    width: 1200
    height: 900
    color: Tk.Theme.color.canvas
    property string status: qsTr("Host snapshots loaded · no video render pipeline")
    property var pending: null
    property var captionAck: ({status:"",message:""})
    property var colorAck: ({status:"",message:""})
    property var cueList: [
        {id:"cue-101",inTime:"90071992547409930000",outTime:"90071992547529930000",x0:0.10,x1:0.39,lane:0,
         text:"The city wakes before the rain.",language:"en-US",styleId:"lower-third"},
        {id:"cue-102",inTime:"90071992547529930000",outTime:"90071992547625930000",x0:0.39,x1:0.62,lane:1,
         text:"A quiet signal crosses the harbor.",language:"en-US",styleId:"dialogue"},
        {id:"cue-103",inTime:"90071992547625930000",outTime:"90071992547793930000",x0:0.62,x1:0.94,lane:0,
         text:"We have one more stop.",language:"en-US",styleId:"dialogue"}
    ]
    property var styles: [
        {id:"dialogue",label:qsTr("Dialogue"),enabled:true},
        {id:"lower-third",label:qsTr("Lower third"),enabled:true},
        {id:"speaker-label",label:qsTr("Speaker label"),enabled:false}
    ]
    property var timeMarks: [
        {x:0.10,time:"90071992547409930000",label:"01:12:08:00"},
        {x:0.26,time:"90071992547481930000",label:"01:12:09:12"},
        {x:0.39,time:"90071992547529930000",label:"01:12:10:00"},
        {x:0.50,time:"90071992547577930000",label:"01:12:10:24"},
        {x:0.62,time:"90071992547625930000",label:"01:12:11:12"},
        {x:0.78,time:"90071992547697930000",label:"01:12:12:24"},
        {x:0.94,time:"90071992547793930000",label:"01:12:14:00"}
    ]
    property var moveTargets: [
        {x:0.10,inTime:"90071992547409930000",outTime:"90071992547529930000"},
        {x:0.26,inTime:"90071992547481930000",outTime:"90071992547601930000"},
        {x:0.39,inTime:"90071992547529930000",outTime:"90071992547649930000"},
        {x:0.62,inTime:"90071992547625930000",outTime:"90071992547745930000"},
        {x:0.78,inTime:"90071992547697930000",outTime:"90071992547817930000"}
    ]
    property var gradeWheels: [
        {id:"lift",label:qsTr("Lift · shadows"),x:0.08,y:-0.04,minimumX:-0.25,maximumX:0.25,minimumY:-0.2,maximumY:0.2,stepX:0.01,stepY:0.01,enabled:true},
        {id:"gamma",label:qsTr("Gamma · midtones"),x:-0.03,y:0.06,minimumX:-0.3,maximumX:0.3,minimumY:-0.3,maximumY:0.3,stepX:0.01,stepY:0.01,enabled:true},
        {id:"gain",label:qsTr("Gain · highlights"),x:0.02,y:0.01,minimumX:-0.2,maximumX:0.2,minimumY:-0.18,maximumY:0.18,stepX:0.005,stepY:0.005,enabled:true}
    ]

    function decide(accepted) {
        if (!root.pending) return
        if (root.pending.domain === "caption") {
            root.captionAck = accepted
                ? {status:"accepted",message:qsTr("Caption request acknowledged · fixture snapshot remains unchanged")}
                : {status:"rejected",message:qsTr("Caption request rejected · cue snapshot unchanged")}
        } else {
            root.colorAck = {status:accepted ? "accepted" : "rejected",
                message:accepted ? qsTr("Grade request acknowledged · fixture parameters remain unchanged; no transform rendered")
                                 : qsTr("Grade request rejected · parameters unchanged")}
        }
        root.status = accepted ? qsTr("Host acknowledged request · downstream output remains external")
                               : qsTr("Host rejected request · snapshots remain unchanged")
        root.pending = null
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label { text:qsTr("Caption and grade controls");font.pixelSize:Tk.Theme.font.title;Accessible.role:Accessible.Heading }
            Tk.Badge { text:qsTr("Request fixture");variant:"accent" }
            Tk.Label { text:qsTr("Scene 18 · interview");color:Tk.Theme.color.textMuted }
        }
        Tk.Label {
            text:qsTr("Cue intervals and grade parameters are host snapshots. Requests need host acceptance; this fixture does not burn captions or transform video pixels.")
            muted:true;wrapMode:Text.Wrap
        }
        Item {
            objectName:"captionEditorFrame"
            implicitHeight: Tk.Theme.size.waveformHeight * 9
            Tk.Flex.shrink:0
            Tk.CaptionEditor {
                id: captions
                anchors.fill:parent
                sourceId:"source-interview-18"
                trackId:"captions-en"
                cues:root.cueList
                timeMarks:root.timeMarks
                moveTargets:root.moveTargets
                styles:root.styles
                selectedCueId:"cue-101"
                selectedCueText:"The city wakes before the rain."
                selectedCueStyleId:"lower-third"
                selectedCueLanguage:"en-US"
                warningText:qsTr("Reading speed warning · host estimate exceeds 18 chars/sec")
                editAcknowledgement:root.captionAck
                tooltip:qsTr("Caption text, timing, and style requests")
                onCueSelectionRequested:function(source,track,cue){root.status=qsTr("Cue selected · %1").arg(cue)}
                onCueEditPreviewRequested:function(id,source,track,cue,operation,inTime,outTime,text,style){
                    root.pending={domain:"caption",id:id,cue:cue,operation:operation,inTime:inTime,outTime:outTime,text:text,style:style}
                    root.status=qsTr("Caption preview · %1 · %2–%3 · %4").arg(cue).arg(inTime).arg(outTime).arg(operation)
                }
                onCueEditCommitRequested:function(id,source,track,cue,operation,inTime,outTime,text,style){
                    root.pending={domain:"caption",id:id,cue:cue,operation:operation,inTime:inTime,outTime:outTime,text:text,style:style}
                    root.captionAck={status:"pending",message:qsTr("Caption request pending host decision")}
                }
                onCueEditCanceled:function(id,source,track,cue){root.pending=null;root.status=qsTr("Caption request canceled · host data unchanged")}
                onNavigationRequested:function(source,cue,direction){root.status=qsTr("Caption navigation · %1 from %2").arg(direction).arg(cue)}
                onExportRequested:function(source,track,format){root.status=qsTr("Export requested · %1; output is host-owned").arg(format)}
            }
        }
        Tk.Flex {
            direction:Tk.Flex.Row
            gap:Tk.Theme.space.md
            Tk.Label {text:qsTr("Grade parameters");font.pixelSize:Tk.Theme.font.title;Accessible.role:Accessible.Heading}
            Tk.Label {text:qsTr("Scene-referred · host-defined ranges");color:Tk.Theme.color.textMuted}
        }
        Item {
            objectName:"colorWheelsFrame"
            Tk.Flex.grow:1
            Tk.ColorWheels {
                id:colorWheels
                anchors.fill:parent
                gradeId:"grade-scene-18"
                parameterRevision:7
                wheels:root.gradeWheels
                selectedWheelId:"gamma"
                editAcknowledgement:root.colorAck
                tooltip:qsTr("Grade parameter wheels")
                onWheelSelectionRequested:function(grade,wheel){root.status=qsTr("Grade parameter selected · %1").arg(wheel)}
                onWheelPreviewRequested:function(id,grade,wheel,x,y){
                    root.pending={domain:"grade",id:id,wheel:wheel,x:x,y:y}
                    root.status=qsTr("Preview request · %1 · X %2 · Y %3").arg(wheel).arg(x.toFixed(3)).arg(y.toFixed(3))
                }
                onWheelEditFinished:function(id,grade,wheel,x,y){
                    root.pending={domain:"grade",id:id,wheel:wheel,x:x,y:y}
                    root.colorAck={status:"pending",message:qsTr("Grade request pending host decision")}
                }
                onWheelEditCanceled:function(id,grade,wheel){root.pending=null;root.status=qsTr("Grade gesture canceled · parameters unchanged")}
            }
        }
        Tk.Flex {
            direction:Tk.Flex.Row
            gap:Tk.Theme.space.sm
            Tk.Label {objectName:"captionGradeStatus";text:root.status;color:Tk.Theme.color.textMuted;Tk.Flex.grow:1;elide:Text.ElideRight}
            Tk.Button {text:qsTr("Mock host accepts");variant:"accent";enabled:root.pending!==null;onClicked:root.decide(true)}
            Tk.Button {text:qsTr("Mock host rejects");variant:"outline";enabled:root.pending!==null;onClicked:root.decide(false)}
        }
        Tk.Caption {text:qsTr("All time values are exact strings. Theme-colored wheel markers visualize parameters only; no grade processing or caption export is connected.");color:Tk.Theme.color.textMuted}
    }
}
