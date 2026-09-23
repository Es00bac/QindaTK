// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "TransportBar"
    when: windowShown
    visible: true
    width: 700
    height: 120

    Tk.TimeMap { id: timeMap }
    Tk.TransportModel { id: transport; timeMap: timeMap }
    Tk.TransportBar {
        id: bar
        anchors.top: parent.top
        width: parent.width
        transport: transport
        displayMode: "samples"
        tooltip: "Playback transport"
    }

    function test_readout_is_exact_snapshot_projection() {
        compare(bar.readoutText, "0")
        compare(findChild(bar, "transportTime").text, "0")
        bar.displayMode = "timecode"
        compare(bar.readoutText, "00:00:00:00")
        compare(bar.Accessible.name, "Playback transport")
    }
    function test_unavailable_controls_explain_capability() {
        const play = findChild(bar, "transportPlay")
        const locate = findChild(bar, "transportLocateStart")
        const record = findChild(bar, "transportRecord")
        verify(play && locate && record)
        verify(!play.enabled && !locate.enabled && !record.enabled)
        verify(play.tooltip.length > 0)
        verify(record.tooltip.length > 0)
        bar.forceActiveFocus()
        tryVerify(function() { return bar.activeFocus })
        keyClick(Qt.Key_Space)
        compare(transport.lastError, "Playback is unavailable")
        verify(findChild(bar, "transportStatus").visible)
    }
}
