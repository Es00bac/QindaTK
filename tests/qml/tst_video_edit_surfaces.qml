// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    visible: true
    name: "VideoEditSurfaces"
    when: windowShown
    property var trimRequest: null
    property var cutRequest: null
    property var acknowledgedCut: null

    Tk.TrimView {
        id: trim
        width: 920
        height: 460
        viewId: "trim-test"
        edit: ({editId: "edit-stable-8", leftClipId: "clip-left", rightClipId: "clip-right",
                legalHead: true, legalTail: true, missingLeft: false, missingRight: false})
        deltaTicks: "900719925474099312345"
        onPreviewRequested: function(requestId, editId, mode, ticks) {
            root.trimRequest = [requestId, editId, mode, ticks]
        }
    }
    Tk.MulticamView {
        id: multicam
        width: 500
        height: 500
        activeAngleId: "angle-a"
        playheadTicks: "900719925474099312345"
        angles: [
            {angleId: "angle-a", syncOffsetTicks: "0", available: true},
            {angleId: "angle-b", syncOffsetTicks: "-9007199254740993", available: false},
            {angleId: "angle-c", syncOffsetTicks: "9007199254740993", available: true}
        ]
        onAngleSwitchRequested: function(requestId, angleId, atTicks) {
            root.cutRequest = [requestId, angleId, atTicks]
        }
        onSwitchAcknowledged: function(requestId, accepted, angleId, message) {
            root.acknowledgedCut = [requestId, accepted, angleId, message]
        }
    }
    Tk.TransitionEditor {
        id: transitionEditor
        transition: ({transitionId: "transition-stable", leftClipId: "clip-a", rightClipId: "clip-b",
                      headHandleTicks: "10000000000000000000", tailHandleTicks: "10000000000000000000"})
        durationTicks: "20000000000000000001"
        alignment: "center"
    }

    // Angles beyond the rows that fit scroll; they never cover the status row.
    function test_extra_angles_scroll_instead_of_overlapping() {
        multicam.height = Tk.Theme.size.thumbnailHeight * 6
        const scroll = findChild(multicam, "multicamAngleScroll")
        const status = findChild(multicam, "multicamStatus")
        verify(scroll && status && findChild(multicam, "multicamAngle_angle-c"))
        tryVerify(function() { return scroll.scrollableY })
        const scrollBottom = scroll.mapToItem(multicam, 0, scroll.height).y
        verify(status.mapToItem(multicam, 0, 0).y >= scrollBottom - 0.5)
        verify(scrollBottom <= multicam.height)
        multicam.height = 500
    }

    function test_requests_keep_exact_ticks_and_respect_host_snapshot() {
        verify(trim.snapshotValid)
        verify(trim.requestPreview())
        compare(trimRequest[1], "edit-stable-8")
        compare(trimRequest[2], "roll")
        compare(trimRequest[3], "900719925474099312345")

        verify(multicam.snapshotValid)
        verify(!multicam.switchTo("angle-b")) // missing media cannot become a cut
        verify(!multicam.switchTo("angle-a")) // program angle is already active
        verify(multicam.switchTo("angle-c"))
        compare(cutRequest[1], "angle-c")
        compare(cutRequest[2], "900719925474099312345")
        compare(multicam.activeAngleId, "angle-a") // request does not optimistically change program state
        multicam.pendingRequestId = cutRequest[0]
        verify(multicam.acknowledgeSwitch(cutRequest[0], true, "angle-c", "accepted"))
        compare(acknowledgedCut[2], "angle-c")
        multicam.activeAngleId = "angle-c" // host snapshot is authoritative after the ack
        compare(multicam.activeAngleId, "angle-c")

        verify(transitionEditor.validSnapshot)
        verify(!transitionEditor.handlesAvailable) // exact handle math avoids Number rounding
        compare(transitionEditor.transition.transitionId, "transition-stable")
    }
}
