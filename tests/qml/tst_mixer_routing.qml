// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: testCase
    name: "MixerRouting"
    visible: true
    width: 800
    height: 600
    property string commandChannelId: ""
    property string commandRequestId: ""
    property string commandName: ""
    property string routeInputId: ""
    property string routeOutputId: ""
    property string routeRequestId: ""
    readonly property var inputs: [{id: "input-stable-7", label: "Voice in", available: true}]
    readonly property var outputs: [
        {id: "output-main", label: "Main", available: true},
        {id: "output-monitor", label: "Monitor", available: true}
    ]
    readonly property var activeConnections: [{inputId: "input-stable-7", outputId: "output-main"}]

    Component {
        id: surfaces
        Item {
            width: 800; height: 600
            Tk.ChannelStrip {
                id: strip
                objectName: "testChannelStrip"
                width: 220; height: 520
                channelId: "channel-stable-44"
                channelName: "Lead Vox"
                gainParameterId: "audio/lead-vox/gain"
                onChannelCommandRequested: function(channelId, requestId, command) {
                    testCase.commandChannelId = channelId
                    testCase.commandRequestId = requestId
                    testCase.commandName = command
                }
            }
            Tk.RoutingMatrix {
                id: matrix
                objectName: "testRoutingMatrix"
                x: 240
                width: 520; height: 520
                inputPorts: testCase.inputs
                outputPorts: testCase.outputs
                connections: testCase.activeConnections
                onRouteRequested: function(requestId, inputId, outputId) {
                    testCase.routeRequestId = requestId
                    testCase.routeInputId = inputId
                    testCase.routeOutputId = outputId
                }
            }
        }
    }

    // WP-062: a real session behind the mixer views.
    Component {
        id: sessionScene
        Item {
            width: 800; height: 600
            property alias session: session
            Tk.AudioSession { id: session }
            Tk.MixerView {
                objectName: "testMixerView"
                width: 520; height: 560
                mixer: session.mixer
                detail: false
                onChannelSelected: (id) => selectedChannelId = id
            }
            Tk.RoutingMatrix {
                objectName: "testBoundMatrix"
                x: 530
                width: 260; height: 560
                mixer: session.mixer
            }
        }
    }
    Component {
        id: panScene
        Item {
            width: 400; height: 400
            property alias session: session
            Tk.AudioSession { id: session }
            Tk.PanningControl { objectName: "testPan"; width: 240; mixer: session.mixer }
        }
    }
    Component { id: surfaceBinding; Tk.AudioParameterBinding { } }
    Component { id: routingPanel; Tk.ChannelRoutingPanel { width: 200 } }

    // Strip cells the ListView has instantiated (visible or pooled).
    function stripCells(view) {
        const cells = []
        const children = view.channelList.contentItem.children
        for (let i = 0; i < children.length; ++i)
            if (children[i].objectName === "mixerStripCell") cells.push(children[i])
        return cells
    }
    function cellFor(view, channelId) {
        const cells = stripCells(view)
        for (let i = 0; i < cells.length; ++i)
            if (cells[i].visible && cells[i].channelId === channelId) return cells[i]
        return null
    }

    function test_mixer_view_virtualizes_and_keeps_gestures() {
        const root = createTemporaryObject(sessionScene, testCase)
        verify(root !== null)
        const session = root.session
        const ids = []
        for (let i = 0; i < 40; ++i) ids.push("track:" + session.addTrack("Track " + (i + 1)))
        const view = findChild(root, "testMixerView")
        tryCompare(view, "channelCount", 41) // 40 tracks and the master, after the view's next layout
        tryVerify(() => cellFor(view, ids[1]) !== null)
        verify(stripCells(view).length < 20, "strips created: " + stripCells(view).length)
        const cell = cellFor(view, ids[1])
        // A held gesture on the second channel's gain...
        const gain = session.mixer.parameterBinding(ids[1], "gain")
        verify(gain.begin(0))
        verify(gain.preview(-6))
        compare(session.mixer.heldChannels, [ids[1]])
        verify(view.held)
        // ...keeps the strips still: the bank change waits for the gesture.
        const before = view.channelList.contentX
        view.bank = 3
        compare(view.channelList.contentX, before)
        // Hiding the held channel keeps its strip; a reorder moves the same delegate.
        verify(session.mixer.setHidden(ids[1], true).ok)
        tryCompare(view, "channelCount", 41)
        verify(session.timeline.applyEdit({kind: "reorderTrack", trackId: ids[1].slice(6),
                                           beforeTrackId: ids[0].slice(6)}).ok)
        tryCompare(cell, "index", 0)
        verify(cellFor(view, ids[1]) === cell)
        verify(gain.gestureActive)
        // Ending the gesture stores one transaction, drops the hidden strip
        // and applies the bank that waited.
        verify(gain.commit(-6))
        compare(gain.lastResult, "static")
        tryCompare(view, "channelCount", 40)
        tryVerify(() => view.channelList.contentX !== before)
        compare(view.bank, 3)
        // Keyboard selection moves by stable ID.
        view.selectedChannelId = ids[5]
        view.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(view.selectedChannelId, ids[6])
    }

    function test_bound_strip_edits_the_engine_parameter() {
        const root = createTemporaryObject(sessionScene, testCase)
        const session = root.session
        const voice = "track:" + session.addTrack("Voice")
        const view = findChild(root, "testMixerView")
        tryVerify(() => cellFor(view, voice) !== null)
        const strip = findChild(cellFor(view, voice), "mixerChannelStrip")
        const fader = findChild(strip, "channelGainFader")
        verify(fader.binding === session.mixer.parameterBinding(voice, "gain"))
        const history = session.automation.historyCount
        // One key step is one engine transaction.
        fader.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(session.automation.historyCount, history + 1)
        verify(fader.value < 0)
        // Another writer of the parameter (a control surface) moves the same fader.
        const surface = createTemporaryObject(surfaceBinding, testCase,
                                              {session: session, parameterId: voice + "/gain"})
        verify(surface.commit(-12))
        compare(fader.value, -12)
        // Mute is the timeline track's own flag, answered at once.
        mouseClick(findChild(strip, "channelMute"))
        tryCompare(strip, "muted", true)
        compare(strip.commandState, "accepted")
        compare(session.timeline.trackData(voice.slice(6)).muted, true)
    }

    function test_bound_routing_explains_refusals() {
        const root = createTemporaryObject(sessionScene, testCase)
        const session = root.session
        const voice = "track:" + session.addTrack("Voice")
        const a = session.mixer.addBus("A", "Stereo").channelId
        const b = session.mixer.addBus("B", "Stereo").channelId
        const matrix = findChild(root, "testBoundMatrix")
        compare(matrix.inputPorts.length, 4) // voice, A, B, master
        matrix.requestRoute(a, b)
        compare(matrix.routeState, "accepted")
        compare(matrix.outputIdFor(a), b)
        matrix.requestRoute(b, a)
        compare(matrix.routeState, "rejected")
        verify(matrix.routeMessage.indexOf("feedback loop: B → A → B") >= 0, matrix.routeMessage)
        compare(matrix.outputIdFor(b), "master")
        matrix.requestRoute("master", "device:1-2@builtin:null") // no device runs: not offered
        compare(matrix.routeState, "rejected")
        matrix.requestRoute(voice, a)
        compare(matrix.routeState, "accepted")
        compare(session.mixer.channel(voice).output, a)
        // The strip's routing panel gives the same explanations.
        const panel = createTemporaryObject(routingPanel, root, {mixer: session.mixer, channelId: b, output: "master"})
        verify(!panel.route(a))
        verify(panel.statusIsError)
        verify(panel.status.indexOf("feedback loop") >= 0, panel.status)
        verify(!panel.addSend(a))
        verify(panel.route(""))
        compare(session.mixer.channel(b).output, "")
    }

    function test_panning_positions_are_canonical() {
        const root = createTemporaryObject(panScene, testCase)
        const session = root.session
        const voice = "track:" + session.addTrack("Voice")
        const pan = findChild(root, "testPan")
        pan.channelId = voice
        verify(pan.panBinding !== null && pan.widthBinding !== null)
        const history = session.automation.historyCount
        // Numeric entry: -50 % is pan -0.5, one transaction.
        findChild(pan, "panField").valueModified(-50)
        compare(pan.panBinding.value, -0.5)
        compare(session.automation.historyCount, history + 1)
        // The pad: the centre is pan 0, width 1; a quarter in is the same -0.5
        // the field gave; the top-left corner is the end of both ranges. Each
        // pad gesture is one undo step for both parameters.
        const pad = findChild(pan, "panPad")
        mouseClick(pad, pad.width / 2, pad.height / 2)
        compare(pan.panBinding.value, 0)
        compare(pan.widthBinding.value, 1)
        compare(session.automation.historyCount, history + 2)
        mouseClick(pad, pad.width / 4, pad.height / 4)
        compare(pan.panBinding.value, -0.5)
        compare(pan.widthBinding.value, 1.5)
        compare(session.automation.historyCount, history + 3)
        mouseClick(pad, 0, 0)
        compare(pan.panBinding.value, -1)
        compare(pan.widthBinding.value, 2)
        // Keyboard: one slider step (0.01) is one transaction.
        const slider = findChild(pan, "panSlider")
        slider.forceActiveFocus()
        keyClick(Qt.Key_Right)
        fuzzyCompare(pan.panBinding.value, -0.99, 1e-9)
        compare(session.automation.historyCount, history + 5)
        verify(session.automation.undo())
        compare(pan.panBinding.value, -1)
    }

    function test_stableChannelAndPortRequestsWaitForHostDecision() {
        const root = createTemporaryObject(surfaces, this)
        verify(root !== null)
        const strip = findChild(root, "testChannelStrip")
        const matrix = findChild(root, "testRoutingMatrix")
        verify(strip !== null)
        verify(matrix !== null)

        const muteButton = findChild(strip, "channelMute")
        verify(muteButton !== null)
        compare(muteButton.objectName, "channelMute")
        strip.requestCommand("mute", true)
        compare(commandChannelId, "channel-stable-44")
        compare(commandName, "mute")
        verify(commandRequestId.length > 0)
        compare(strip.muted, false)
        verify(strip.acknowledgeCommand(commandRequestId, "rejected", "Host refused mute"))
        compare(strip.commandState, "rejected")
        compare(strip.muted, false)

        routeRequestId = matrix.requestRoute("input-stable-7", "output-monitor")
        verify(routeRequestId.length > 0)
        compare(routeInputId, "input-stable-7")
        compare(routeOutputId, "output-monitor")
        compare(matrix.outputIdFor("input-stable-7"), "output-main")
        verify(matrix.acknowledgeRoute(routeRequestId, "rejected", "Feedback route refused"))
        compare(matrix.routeState, "rejected")
        compare(matrix.outputIdFor("input-stable-7"), "output-main")
        root.destroy()
    }
}
