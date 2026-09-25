// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtCore
import QtTest
import QindaTK as Tk

// The QML host path of WP-072 on a real decoded test signal: a
// ColorGradeEditSession attached through VideoFinishing to a MediaPlayer is
// edited from QML, where no frame can be fed to the session (MediaPlayer
// exposes no image). The grade must still be editable, re-render the
// presented frame and move the player-bound scopes; undo must restore both.
TestCase {
    id: testCase
    name: "MediaFinishingLive"
    visible: true
    width: 320
    height: 240
    when: windowShown

    readonly property string mediaPath: StandardPaths.writableLocation(StandardPaths.TempLocation)
                                        + "/qindatk-tst-media-finishing-live.mkv"

    Tk.MediaCatalog { id: catalog }
    SignalSpy { id: written; target: catalog; signalName: "jobFinished" }
    Tk.ColorGradeEditSession { id: session; clipId: "bars" }
    Tk.CaptionTrackModel { id: track; sourceId: "bars"; trackId: "captions"; language: "en" }
    Tk.VideoFinishing { id: finishing; gradeSession: session; captionTrack: track; burnInCaptions: true }
    Tk.MediaPlayer { id: mediaPlayer; finishing: finishing }
    Tk.VideoScopesModel { id: scopes; player: mediaPlayer }

    function initTestCase() {
        const job = catalog.generateTestSignal(testCase.mediaPath, ({width: 128, height: 72, frames: 10, channels: 0}))
        verify(job.length > 0, catalog.lastError)
        written.wait(20000)
        compare(written.signalArguments[0][3], true)
        mediaPlayer.resourceId = testCase.mediaPath
        tryCompare(mediaPlayer, "state", "ready", 10000)
        tryCompare(mediaPlayer, "presentedFrame", "0", 10000)
        tryVerify(() => scopes.snapshot.valid, 5000)
    }

    function test_programGradeEditsWithoutAHostFrame() {
        compare(session.scope, Tk.ColorGradeEditSession.ProgramGrade)
        verify(session.wheels[0].enabled, "program wheels are enabled without a host frame")
        const neutralDigest = finishing.digest
        const neutralYMax = scopes.snapshot.levels.yMax
        verify(neutralYMax > 150, "bars reach 75% white: " + neutralYMax)

        verify(session.setExposure(-2), session.errorMessage)
        compare(session.errorMessage, "")
        verify(finishing.digest !== neutralDigest)
        tryVerify(() => scopes.snapshot.valid && scopes.snapshot.levels.yMax < neutralYMax - 40, 5000,
                  "graded frame reaches the scopes")
        verify(session.canUndo)

        verify(session.previewWheel("drag", "gain", 0.4, -0.3), session.errorMessage)
        verify(session.editing)
        verify(session.finishWheel("drag", "gain", 0.4, -0.3), session.errorMessage)
        verify(!session.editing)

        verify(session.undo())
        verify(session.undo())
        compare(finishing.digest, neutralDigest)
        tryVerify(() => scopes.snapshot.levels.yMax === neutralYMax, 5000, "undo restores the frame")
    }

    function test_frameGradeStillNeedsAFrame() {
        const still = Qt.createQmlObject("import QindaTK as Tk; Tk.ColorGradeEditSession {}", testCase)
        verify(!still.wheels[0].enabled)
        verify(!still.beginGesture("g"))
        verify(still.errorMessage.length > 0)
        still.destroy()
    }
}
