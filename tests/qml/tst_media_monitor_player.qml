// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtCore
import QtTest
import QindaTK as Tk

// SourceProgramMonitor in player mode on a real decoded test signal: the
// buttons and keys seek through the engine, and the identity shown is the
// frame on screen. The signal is rewritten in place under the temp folder.
TestCase {
    id: testCase
    name: "MediaMonitorPlayer"
    visible: true
    width: 640
    height: 480
    when: windowShown

    readonly property string mediaPath: StandardPaths.writableLocation(StandardPaths.TempLocation)
                                        + "/qindatk-tst-media-monitor.mkv"

    Tk.MediaCatalog { id: catalog }
    SignalSpy { id: written; target: catalog; signalName: "jobFinished" }
    Tk.MediaPlayer { id: mediaPlayer }
    Component {
        id: monitorComponent
        Tk.SourceProgramMonitor { width: 420; height: 320 }
    }

    function initTestCase() {
        const job = catalog.generateTestSignal(testCase.mediaPath, ({width: 128, height: 72, frames: 20, channels: 0}))
        verify(job.length > 0, catalog.lastError)
        written.wait(20000)
        compare(written.signalArguments[0][3], true)
        mediaPlayer.resourceId = testCase.mediaPath
        tryCompare(mediaPlayer, "state", "ready", 10000)
        tryCompare(mediaPlayer, "presentedFrame", "0", 10000)
        tryCompare(mediaPlayer.transport, "canSeek", true, 10000)
    }

    function test_monitorShowsOnlyAcknowledgedFrames() {
        const monitor = createTemporaryObject(monitorComponent, testCase, {player: mediaPlayer})
        verify(monitor !== null)
        const identity = findChild(monitor, "monitorFrameIdentity")
        const next = findChild(monitor, "monitorNextFrame")
        verify(identity !== null && next !== null)
        verify(findChild(monitor, "monitorMediaFrame").visible)
        verify(!findChild(monitor, "monitorFrame").visible)
        tryCompare(monitor, "displayedFrameId", "0")
        tryCompare(next, "enabled", true)
        mouseClick(next)
        tryCompare(mediaPlayer, "presentedFrame", "1", 5000)
        compare(monitor.displayedFrameId, "1")
        compare(monitor.displayedTicks, "28224000")
        verify(identity.text.indexOf("1 · 28224000") >= 0, identity.text)
        tryCompare(monitor, "pendingRequestId", "", 5000)
        monitor.forceActiveFocus()
        keyClick(Qt.Key_End)
        tryCompare(mediaPlayer, "presentedFrame", "19", 5000)
        tryCompare(next, "enabled", false)
        keyClick(Qt.Key_Home)
        tryCompare(mediaPlayer, "presentedFrame", "0", 5000)
        keyClick(Qt.Key_Right)
        tryCompare(mediaPlayer, "presentedFrame", "1", 5000)
    }
}
