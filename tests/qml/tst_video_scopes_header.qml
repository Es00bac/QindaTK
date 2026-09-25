// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// A player-bound VideoScopesModel names each snapshot by the player's
// presentedFrameKey, a file URI with revision and frame. The header must keep
// the scope mode whole and fit the frame ID beside it (elided), never paint
// one over the other.
TestCase {
    id: testCase
    name: "VideoScopesHeader"
    visible: true
    width: 360
    height: 300
    when: windowShown

    readonly property string longKey: "file:///home/user/.cache/tmp/qindatk-grading-scopes-live/harbor.mkv@1727261234567-1048576#42"

    Tk.VideoScopesModel { id: model }
    Component {
        id: scopesComponent
        Tk.VideoScopes { width: 320; height: 260 }
    }

    function test_longFrameIdStaysBesideTheMode() {
        model.analyzeColorBarsFixture(testCase.longKey)
        verify(model.snapshot.valid)
        const scopes = createTemporaryObject(scopesComponent, testCase, {snapshot: model.snapshot, scopeMode: "waveform"})
        verify(scopes !== null)
        const mode = findChild(scopes, "videoScopesMode")
        const frameId = findChild(scopes, "videoScopesFrameId")
        verify(mode !== null && frameId !== null)
        compare(frameId.text, testCase.longKey)
        // Flex lays out on the next polish; wait for the header to settle.
        tryVerify(() => frameId.x > 0 && frameId.width < frameId.implicitWidth, 2000,
                  "header laid out: " + [mode.x, mode.width, frameId.x, frameId.width, frameId.parent.width])
        verify(mode.width >= mode.implicitWidth - 0.5, "mode keeps its width: " + mode.width + " < " + mode.implicitWidth)
        verify(mode.x + mode.width <= frameId.x + 0.5, "frame ID starts after the mode")
        verify(frameId.x + frameId.width <= frameId.parent.width + 0.5, "frame ID fits the header")
        verify(frameId.truncated, "the long key is elided")
    }

    function test_targetLabelsStayOnTheCanvas() {
        const scopes = createTemporaryObject(scopesComponent, testCase, {scopeMode: "vectorscope"})
        // Room on the left: the label sits beside its box, right-aligned.
        let place = scopes.targetLabelPlacement(60, 100, 12, 14, 150, 150, 300)
        compare(place.align, "right")
        compare(place.x, 48)
        // Yellow near the left edge of a square canvas: it moves above the box.
        place = scopes.targetLabelPlacement(14, 140, 12, 14, 150, 150, 300)
        compare(place.align, "center")
        compare(place.baseline, "bottom")
        verify(place.x - 7 >= 0)
        compare(place.y, 128)
        // Right edge, below the centre: below the box, still on the canvas.
        place = scopes.targetLabelPlacement(292, 200, 12, 14, 150, 150, 300)
        compare(place.baseline, "top")
        verify(place.x + 7 <= 300)
    }
}
