// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// WP-062 fixture: a real AudioSession mix, not a mock host. Twelve tracks,
// a drum bus, a solo-safe reverb return fed by post-fader sends, a cue bus on
// a pre-fader send, a group and a mono bass. Every control edits the
// session's mixer and the graph the engine plays; refusals (a feedback loop,
// an unavailable output) show the mixer's reason. No device or media are
// opened here, so the meters read silence; examples/audio-session plays
// through a live engine.
Item {
    id: fixture
    width: 1280
    height: 740
    property string selectedChannelId: ""
    property string status: qsTr("Mixer ready")

    Tk.AudioSession { id: session }

    Component.onCompleted: {
        const mixer = session.mixer
        const names = ["Vocal", "Kick", "Bass", "Synth", "Piano", "Guitar", "Strings", "Pads", "FX", "Choir", "Snare", "Click"]
        const tracks = names.map(function(name) { return "track:" + session.addTrack(name) })
        const drums = mixer.addBus(qsTr("Drum bus"), "Stereo").channelId
        const reverb = mixer.addBus(qsTr("Reverb"), "Stereo").channelId
        const cue = mixer.addBus(qsTr("Cue"), "Stereo").channelId
        mixer.setOutput(tracks[1], drums)
        mixer.setOutput(tracks[10], drums)
        mixer.addSend(tracks[0], reverb, false)
        mixer.addSend(tracks[6], reverb, false)
        mixer.addSend(tracks[0], cue, true)
        mixer.setSoloSafe(reverb, true)
        mixer.setLayout(tracks[2], "Mono")
        mixer.addGroup(qsTr("Rhythm"), [tracks[1], tracks[2], tracks[10]])
        mixer.setMuted(tracks[11], true)
        mixer.clearHistory()
        fixture.selectedChannelId = tracks[0]
    }

    Tk.Box {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
        padding: Tk.Theme.space.md
        Tk.Flex {
            anchors.fill: parent
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
            Tk.Flex {
                width: parent.width
                gap: Tk.Theme.space.sm
                Tk.Overline { title: qsTr("Mixer · routing") }
                Tk.Caption { text: qsTr("Pan law %1").arg(session.mixer.panLaw); color: Tk.Theme.color.textMuted }
                Tk.Spacer { }
                Tk.IconButton {
                    objectName: "mixerUndo"
                    iconName: "chevron-left"
                    enabled: session.mixer.canUndo
                    tooltip: qsTr("Undo %1").arg(session.mixer.undoText)
                    onClicked: session.mixer.undo()
                }
                Tk.IconButton {
                    objectName: "mixerRedo"
                    iconName: "chevron-right"
                    enabled: session.mixer.canRedo
                    tooltip: qsTr("Redo %1").arg(session.mixer.redoText)
                    onClicked: session.mixer.redo()
                }
            }
            Tk.Flex {
                width: parent.width
                height: parent.height - Tk.Theme.size.row * 2
                Tk.Flex.grow: 1
                gap: Tk.Theme.space.md
                Tk.MixerView {
                    objectName: "fixtureMixer"
                    Tk.Flex.grow: 2
                    height: parent.height
                    mixer: session.mixer
                    selectedChannelId: fixture.selectedChannelId
                    onChannelSelected: (id) => {
                        fixture.selectedChannelId = id
                        fixture.status = qsTr("Selected %1").arg(session.mixer.channelName(id))
                    }
                }
                Tk.RoutingMatrix {
                    objectName: "fixtureRouting"
                    Tk.Flex.grow: 1
                    height: parent.height
                    mixer: session.mixer
                    onRouteAcknowledged: (requestId, state, message) => fixture.status = message
                }
            }
            Tk.Caption {
                objectName: "mixerHostStatus"
                text: session.mixer.lastError.length ? session.mixer.lastError : fixture.status
                color: session.mixer.lastError.length ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            }
        }
    }
}
