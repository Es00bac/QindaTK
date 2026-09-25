// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "CaptionAndColorControls"
    when: windowShown
    visible: true
    width: 720
    height: 560
    property var lastCaption: null
    property var lastWheel: null
    property var finishedWheel: null
    property string canceledId: ""

    Tk.CaptionEditor {
        id: captions
        width: 680
        height: 300
        visible: true
        sourceId: "source-test"
        trackId: "caption-track"
        selectedCueId: "cue-stable"
        selectedCueText: "A test caption"
        selectedCueStyleId: "style-dialogue"
        cues: [{id:"cue-stable",inTime:"900719925474099312345",outTime:"900719925474099399999",
                x0:0.2,x1:0.8,lane:0,text:"A test caption",styleId:"style-dialogue"}]
        timeMarks: [{x:0.2,time:"900719925474099312345"},{x:0.5,time:"900719925474099355555"},
                    {x:0.8,time:"900719925474099399999"}]
        moveTargets: [{x:0.2,inTime:"900719925474099312345",outTime:"900719925474099399999"},
                      {x:0.5,inTime:"900719925474099355555",outTime:"900719925474099443209"}]
        styles: [{id:"style-dialogue",label:"Dialogue",enabled:true}]
        onCueEditPreviewRequested: function(id, source, track, cue, operation, start, end, text, style) {
            root.lastCaption = [id,source,track,cue,operation,start,end,text,style]
        }
        onCueEditCommitRequested: function(id, source, track, cue, operation, start, end, text, style) {
            root.lastCaption = [id,source,track,cue,operation,start,end,text,style]
        }
        onCueEditCanceled: function(id) { root.canceledId = id }
    }
    Tk.ColorWheels {
        id: wheels
        width: 680
        height: 220
        visible: true
        gradeId: "grade-test"
        selectedWheelId: "lift"
        wheels: [
            {id:"lift",label:"Lift",x:0,y:0,minimumX:-0.25,maximumX:0.25,minimumY:-0.2,maximumY:0.2,stepX:0.01,stepY:0.01},
            {id:"gamma",label:"Gamma",x:0.02,y:-0.03,minimumX:-0.5,maximumX:0.5,minimumY:-0.5,maximumY:0.5,stepX:0.02,stepY:0.02},
            {id:"gain",label:"Gain",x:0,y:0,minimumX:-1,maximumX:1,minimumY:-1,maximumY:1,stepX:0.05,stepY:0.05}
        ]
        onWheelPreviewRequested: function(id, grade, wheel, x, y) { root.lastWheel = [id,grade,wheel,x,y] }
        onWheelEditFinished: function(id, grade, wheel, x, y) { root.finishedWheel = [id,grade,wheel,x,y] }
    }

    function test_caption_exact_time_and_wheel_parameter_requests() {
        verify(captions.beginCueGesture("cue-stable", "trim-in", 136)) // host mark at x=.2
        verify(captions.updateCueGesture(340)) // host mark at x=.5
        compare(lastCaption[3], "cue-stable")
        compare(lastCaption[4], "trim-in")
        compare(lastCaption[5], "900719925474099355555")
        compare(lastCaption[6], "900719925474099399999")
        verify(captions.finishCueGesture())
        compare(lastCaption[5], "900719925474099355555")

        verify(captions.previewText("Updated host preview"))
        compare(lastCaption[7], "Updated host preview")
        compare(lastCaption[8], "style-dialogue")
        verify(captions.commitTextAndStyle())
        compare(lastCaption[6], "900719925474099399999")

        verify(wheels.beginWheelEdit("lift", 113, 110))
        verify(wheels.updateWheelAt(145, 78))
        compare(lastWheel[1], "grade-test")
        compare(lastWheel[2], "lift")
        verify(lastWheel[3] > 0)
        verify(lastWheel[4] > 0)
        verify(wheels.finishWheelEdit())
        verify(wheels.beginWheelEdit("lift", 113, 110))
        const gesture = wheels._gestureId
        verify(wheels.cancelWheelEdit())
        verify(gesture.length > 0)
    }

    function test_numeric_value_emits_the_drag_signals() {
        lastWheel = null
        finishedWheel = null
        verify(wheels.requestWheelValue("gamma", 0.2, -0.9)) // y clamps to the wheel's -0.5
        compare(lastWheel[2], "gamma")
        compare(lastWheel[3], 0.2)
        compare(lastWheel[4], -0.5)
        compare(finishedWheel[0], lastWheel[0])
        compare(finishedWheel[3], 0.2)
        compare(finishedWheel[4], -0.5)
        verify(!wheels.requestWheelValue("missing", 0, 0))
    }

    function test_pointer_wheel_drag_emits_preview_and_finish() {
        const lift = wheels.wheelGeometry(0)
        mousePress(wheels, lift.centerX + 3, lift.centerY)
        mouseMove(wheels, lift.centerX + 35, lift.centerY - 32)
        verify(lastWheel !== null)
        compare(lastWheel[2], "lift")
        mouseRelease(wheels, lift.centerX + 35, lift.centerY - 32)
        verify(finishedWheel !== null)
        compare(finishedWheel[2], "lift")
    }
}
