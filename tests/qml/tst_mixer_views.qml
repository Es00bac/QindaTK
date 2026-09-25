// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-062 audit gaps: RoutingMatrix selection and keyboard editing by stable
// ID, gestures dropped when a control's binding changes, MixerView groups,
// modes and keys, meters of large mixes that follow the strips on screen,
// and the spatial pad over Quad and 5.1 speakers (AUD-15).
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: testCase
    name: "MixerViews"
    visible: true
    width: 800
    height: 600

    Component {
        id: matrixScene
        Item {
            width: 800; height: 600
            property alias session: session
            Tk.AudioSession { id: session }
            Tk.RoutingMatrix { objectName: "matrix"; width: 420; height: 560; mixer: session.mixer }
        }
    }
    Component {
        id: hostMatrix
        Tk.RoutingMatrix {
            width: 420; height: 300
            outputPorts: [{id: "out", label: "Out", available: true}]
        }
    }
    Component {
        id: controlScene
        Item {
            width: 400; height: 400
            property alias session: session
            Tk.AudioSession { id: session }
            Tk.Fader { objectName: "fader"; width: 60; height: 300 }
            Tk.Knob { objectName: "knob"; x: 70; y: 320 } // clear of the pad: its drag handler would take a pad drag
            Tk.PanningControl { objectName: "pan"; x: 150; width: 240; mixer: session.mixer }
        }
    }
    Component { id: looseFader; Tk.Fader { width: 60; height: 300 } }
    Component {
        id: mixerScene
        Item {
            width: 800; height: 600
            property alias session: session
            Tk.AudioSession { id: session }
            Tk.MixerView {
                objectName: "view"
                width: 780; height: 560
                mixer: session.mixer
                onChannelSelected: (id) => selectedChannelId = id
            }
        }
    }
    Component {
        id: meterScene
        Item {
            width: 800; height: 600
            property alias session: session
            Tk.AudioEngine { id: engine }
            Tk.AudioSession { id: session; engine: engine }
            Tk.MixerView {
                objectName: "view"
                width: 780; height: 560
                mixer: session.mixer
                detail: false
                onChannelSelected: (id) => selectedChannelId = id
            }
        }
    }

    Component {
        id: spatialScene
        Item {
            width: 400; height: 500
            property alias session: session
            Tk.AudioSession { id: session }
            Tk.PanningControl { objectName: "pan"; width: 240; mixer: session.mixer }
        }
    }

    function cellFor(view, channelId) {
        const children = view.channelList.contentItem.children
        for (let i = 0; i < children.length; ++i)
            if (children[i].objectName === "mixerStripCell" && children[i].visible && children[i].channelId === channelId)
                return children[i]
        return null
    }

    function test_routing_matrix_keeps_selection_by_id_through_changes() {
        const root = createTemporaryObject(matrixScene, testCase)
        const session = root.session
        const voice = "track:" + session.addTrack("Voice")
        const drums = "track:" + session.addTrack("Drums")
        const a = session.mixer.addBus("A", "Stereo").channelId
        const matrix = findChild(root, "matrix")
        tryCompare(matrix.inputList, "count", 4) // Voice, Drums, A, master
        // Keyboard: Down selects the first row, then the next, by ID.
        matrix.inputList.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(matrix.selectedInputId, voice)
        keyClick(Qt.Key_Down)
        compare(matrix.selectedInputId, drums)
        compare(matrix.inputList.currentIndex, 1)
        const item = matrix.inputList.currentItem
        const combo = findChild(item, "routingDestination")
        // Return edits the row's destination; Up proposes the previous one (A).
        keyClick(Qt.Key_Return)
        verify(combo.activeFocus)
        const history = session.mixer.historyCount
        keyClick(Qt.Key_Up)
        compare(matrix.routeState, "accepted")
        compare(session.mixer.channel(drums).output, a)
        compare(session.mixer.historyCount, history + 1)
        // The accepted route kept the row, its delegate and the focused editor.
        compare(matrix.inputList.currentItem, item)
        verify(combo.activeFocus)
        compare(combo.currentText, "A")
        compare(matrix.selectedInputId, drums)
        // A reorder moves the selected row: the selection follows its ID.
        verify(session.timeline.applyEdit({kind: "reorderTrack", trackId: drums.slice(6),
                                           beforeTrackId: voice.slice(6)}).ok)
        tryCompare(matrix.inputList, "currentIndex", 0)
        compare(matrix.inputList.currentItem, item)
        verify(combo.activeFocus)
        // A row that appears above keeps the selection on the same input.
        const bass = "track:" + session.addTrack("Bass")
        verify(session.timeline.applyEdit({kind: "reorderTrack", trackId: bass.slice(6),
                                           beforeTrackId: drums.slice(6)}).ok)
        tryCompare(matrix.inputList, "currentIndex", 1)
        compare(matrix.selectedInputId, drums)
        // Escape returns to the rows; End and Home jump.
        keyClick(Qt.Key_Escape)
        verify(matrix.inputList.activeFocus)
        keyClick(Qt.Key_End)
        compare(matrix.selectedInputId, "master")
        keyClick(Qt.Key_Home)
        compare(matrix.selectedInputId, bass)
    }

    function test_host_matrix_refinds_rows_by_id() {
        const matrix = createTemporaryObject(hostMatrix, testCase, {
            inputPorts: [{id: "a", label: "A"}, {id: "b", label: "B"}, {id: "c", label: "C"}]})
        verify(matrix.selectInput("b"))
        compare(matrix.inputList.currentIndex, 1)
        // The host replaces its array (a reorder): the row is found by ID again.
        matrix.inputPorts = [{id: "c", label: "C"}, {id: "a", label: "A"}, {id: "b", label: "B"}]
        tryCompare(matrix.inputList, "currentIndex", 2)
        compare(matrix.selectedInputId, "b")
    }

    function test_fader_and_knob_drop_a_gesture_when_their_binding_changes() {
        const root = createTemporaryObject(controlScene, testCase)
        const session = root.session
        const one = "track:" + session.addTrack("One")
        const two = "track:" + session.addTrack("Two")
        const history = session.automation.historyCount
        // A key gesture stays open until the key is released.
        const fader = findChild(root, "fader")
        const first = session.mixer.parameterBinding(one, "gain")
        const second = session.mixer.parameterBinding(two, "gain")
        fader.binding = first
        fader.forceActiveFocus()
        keyPress(Qt.Key_Down)
        verify(first.gestureActive)
        compare(session.mixer.heldChannels, [one])
        // The control now edits another channel (a recycled strip): the open
        // gesture is canceled where it began and never reaches the new one.
        fader.binding = second
        verify(!first.gestureActive)
        compare(session.mixer.heldChannels, [])
        keyRelease(Qt.Key_Down)
        compare(session.automation.historyCount, history)
        compare(first.value, 0)
        compare(second.value, 0)
        const knob = findChild(root, "knob")
        const panOne = session.mixer.parameterBinding(one, "pan")
        const panTwo = session.mixer.parameterBinding(two, "pan")
        knob.binding = panOne
        knob.forceActiveFocus()
        keyPress(Qt.Key_Right)
        verify(panOne.gestureActive)
        knob.binding = panTwo
        verify(!panOne.gestureActive)
        keyRelease(Qt.Key_Right)
        compare(session.automation.historyCount, history)
        compare(panTwo.value, 0)
        // A control destroyed mid-gesture releases it too.
        const loose = createTemporaryObject(looseFader, root, {binding: first})
        loose.forceActiveFocus()
        keyPress(Qt.Key_Down)
        verify(first.gestureActive)
        loose.destroy()
        tryVerify(() => !first.gestureActive)
        keyRelease(Qt.Key_Down)
        compare(session.mixer.heldChannels, [])
        compare(session.automation.historyCount, history)
    }

    function test_panning_drops_a_gesture_when_its_channel_changes() {
        const root = createTemporaryObject(controlScene, testCase)
        const session = root.session
        const one = "track:" + session.addTrack("One")
        const two = "track:" + session.addTrack("Two")
        const pan = findChild(root, "pan")
        pan.channelId = one
        const onePan = pan.panBinding
        const oneWidth = pan.widthBinding
        const history = session.automation.historyCount
        const pad = findChild(pan, "panPad")
        mousePress(pad, pad.width / 4, pad.height / 2)
        mouseMove(pad, pad.width / 8, pad.height / 4, -1, Qt.LeftButton)
        verify(onePan.gestureActive && oneWidth.gestureActive)
        compare(session.mixer.heldChannels, [one])
        pan.channelId = two
        verify(!onePan.gestureActive)
        verify(!oneWidth.gestureActive)
        compare(session.mixer.heldChannels, [])
        // The rest of the drag edits nothing: no commit lands on Two.
        mouseMove(pad, pad.width * 3 / 4, pad.height / 2, -1, Qt.LeftButton)
        mouseRelease(pad, pad.width * 3 / 4, pad.height / 2)
        compare(session.automation.historyCount, history)
        compare(pan.panBinding.value, 0)
        compare(pan.widthBinding.value, 1)
        compare(onePan.value, 0)
        compare(session.mixer.heldChannels, [])
    }

    function test_mixer_view_groups_detail_and_keys() {
        const root = createTemporaryObject(mixerScene, testCase)
        const session = root.session
        const ids = []
        for (let i = 0; i < 12; ++i) ids.push("track:" + session.addTrack("Track " + (i + 1)))
        const view = findChild(root, "view")
        tryCompare(view, "channelCount", 13)
        verify(session.mixer.addGroup("Drums", [ids[0], ids[1]]).ok)
        // The group bar: mute, then solo, apply to every member.
        // The bar's chips are rebuilt whenever the groups change: find the
        // buttons again, once laid out after the Spacer at the bar's right end.
        const button = (name) => {
            const found = findChild(view, name)
            return found && found.mapToItem(view, 0, 0).x > view.width / 2 ? found : null
        }
        const click = (name) => {
            tryVerify(() => button(name) !== null)
            mouseClick(button(name))
        }
        click("mixerGroupMute")
        compare(session.mixer.groups[0].muted, true)
        compare(session.mixer.channel(ids[1]).effectiveMuted, true)
        compare(session.mixer.channel(ids[2]).effectiveMuted, false)
        tryVerify(() => button("mixerGroupMute") !== null && button("mixerGroupMute").checked)
        click("mixerGroupMute")
        compare(session.mixer.groups[0].muted, false)
        click("mixerGroupSolo")
        compare(session.mixer.groups[0].soloed, true)
        compare(session.mixer.channel(ids[2]).implicitlyMuted, true)
        compare(session.mixer.channel(ids[0]).implicitlyMuted, false)
        click("mixerGroupSolo")
        compare(session.mixer.channel(ids[2]).implicitlyMuted, false)
        // Compact strips: narrower, so a bank holds more; no routing panel.
        tryVerify(() => cellFor(view, ids[0]) !== null)
        const strip = findChild(cellFor(view, ids[0]), "mixerChannelStrip")
        verify(strip.detail)
        verify(findChild(strip, "channelRouting").visible)
        const detailBank = view.bankSize
        mouseClick(findChild(view, "mixerDetailToggle"))
        compare(view.detail, false)
        compare(strip.detail, false)
        verify(!findChild(strip, "channelRouting").visible)
        verify(view.bankSize > detailBank, view.bankSize + " vs " + detailBank)
        const firstShown = () => view.channelList.indexAt(view.channelList.contentX + view.stripWidth / 2, 1)
        // Keys: End and Home select by ID and bring the strip into view;
        // Page Down and Page Up move one bank.
        view.forceActiveFocus()
        keyClick(Qt.Key_End)
        compare(view.selectedChannelId, "master")
        tryVerify(() => cellFor(view, "master") !== null)
        keyClick(Qt.Key_Home)
        compare(view.selectedChannelId, ids[0])
        // (ListView may move its origin after the strips change width, so the
        // first strip is found by index at the left edge, not at contentX 0.)
        tryVerify(() => firstShown() === 0)
        keyClick(Qt.Key_PageDown)
        compare(view.bank, 1)
        tryVerify(() => firstShown() === view.bankSize)
        keyClick(Qt.Key_PageUp)
        compare(view.bank, 0)
        tryVerify(() => firstShown() === 0)
    }

    function test_spatial_pad_positions_are_canonical() {
        const root = createTemporaryObject(spatialScene, testCase)
        const session = root.session
        const voice = "track:" + session.addTrack("Voice")
        const quad = session.mixer.addBus("Room", "Quad").channelId
        verify(session.mixer.setOutput(voice, quad).ok)
        compare(session.mixer.channel(voice).outputLayout, "Quad")
        const pan = findChild(root, "pan")
        pan.channelId = voice
        pan.destinationLayout = session.mixer.channel(voice).outputLayout
        verify(pan.spatial && pan.depthBinding !== null)
        compare(pan.speakers.length, 4)
        verify(findChild(pan, "depthField").visible)
        const history = session.automation.historyCount
        // Numeric entry: -50 % depth is -0.5, one transaction.
        findChild(pan, "depthField").valueModified(-50)
        compare(pan.depthBinding.value, -0.5)
        compare(session.automation.historyCount, history + 1)
        // The pad's y is depth: the centre is (0, 0); a quarter from the
        // bottom-left is the same (-0.5, -0.5) the fields give; the top-left
        // corner is the front-left speaker. Each pad gesture is one step.
        const pad = findChild(pan, "panPad")
        mouseClick(pad, pad.width / 2, pad.height / 2)
        compare(pan.panBinding.value, 0)
        compare(pan.depthBinding.value, 0)
        compare(session.automation.historyCount, history + 2)
        mouseClick(pad, pad.width / 4, pad.height * 3 / 4)
        compare(pan.panBinding.value, -0.5)
        compare(pan.depthBinding.value, -0.5)
        compare(session.automation.historyCount, history + 3)
        mouseClick(pad, 0, 0)
        compare(pan.panBinding.value, -1)
        compare(pan.depthBinding.value, 1)
        // Keyboard: one depth slider step (0.01) is one transaction.
        const slider = findChild(pan, "depthSlider")
        slider.forceActiveFocus()
        keyClick(Qt.Key_Left)
        fuzzyCompare(pan.depthBinding.value, 0.99, 1e-9)
        compare(session.automation.historyCount, history + 5)
        // Width stays its own parameter; 5.1 adds the centre speaker.
        compare(pan.widthBinding.value, 1)
        pan.destinationLayout = "5.1"
        compare(pan.speakers.length, 5)
    }

    function test_large_mix_meters_the_strips_on_screen() {
        const root = createTemporaryObject(meterScene, testCase)
        const session = root.session
        const ids = []
        for (let i = 0; i < 130; ++i) ids.push("track:" + session.addTrack("Track " + (i + 1)))
        const view = findChild(root, "view")
        tryCompare(view, "channelCount", 131)
        const unmetered = () => session.mixer.channelIds.filter((id) => session.mixer.meterProblem(id).length > 0)
        // More channels than meter slots: the master and the strips on
        // screen are metered; the last track, off screen, is not, and says why.
        tryVerify(() => cellFor(view, ids[0]) !== null)
        compare(session.mixer.meterProblem("master"), "")
        compare(session.mixer.meterProblem(ids[0]), "")
        verify(session.mixer.meterProblem(ids[129]).indexOf("not metered") >= 0, session.mixer.meterProblem(ids[129]))
        compare(unmetered().length, 3)
        // Scrolling to the end moves the watch: the last strip gets a slot and
        // shows no problem.
        view.forceActiveFocus()
        keyClick(Qt.Key_End)
        tryVerify(() => cellFor(view, ids[129]) !== null)
        tryCompare(findChild(cellFor(view, ids[129]), "mixerChannelStrip"), "meterProblem", "")
        compare(session.mixer.meterProblem("master"), "")
        compare(unmetered().length, 3)
    }
}
