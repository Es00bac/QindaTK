// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (WP-062, docs/controls.md, docs/audio-mixer.md, D-113): a
// virtualized row of Tk.ChannelStrip over a Tk.AudioMixerModel's `strips`
// (audio tracks in timeline order, buses, master; hidden channels left out).
// The ListView creates strips only for the visible range and a bank is one
// view-width of strips. Rows move (never reset) when tracks reorder, so the
// strip under a gesture survives; while a gesture holds a channel the view
// defers bank changes and keeps that strip in view. Selection is a stable
// channel ID owned by the host (`selectedChannelId`, `channelSelected`);
// Left/Right move it, Home/End jump, Page Up/Page Down change the bank.
// No routing or engine state lives in a delegate.
T.Control {
    id: view
    objectName: "mixerView"
    property var mixer: null // Tk.AudioMixerModel
    property string selectedChannelId: ""
    property int bank: 0
    property bool detail: true
    property string tooltip: qsTr("Mixer channels")
    property alias channelList: strips
    // A bank is the whole strips that fit the view at their base width; the
    // strips then stretch so the bank fills the width exactly and no strip is
    // ever cut at the edge (with SnapToItem, any resting scroll shows whole strips).
    readonly property real baseStripWidth: detail ? Tk.Theme.size.panelMinWidth : Tk.Theme.size.fieldWidth
    readonly property int bankSize: Math.max(1, Math.floor((strips.width + strips.spacing) / (baseStripWidth + strips.spacing)))
    readonly property real stripWidth: Math.max(1, (strips.width - (bankSize - 1) * strips.spacing) / bankSize)
    readonly property int channelCount: strips.count
    readonly property int bankCount: Math.max(1, Math.ceil(channelCount / bankSize))
    readonly property bool held: mixer ? mixer.heldChannels.length > 0 : false
    property int _shownBank: 0
    property bool _followingScroll: false

    signal channelSelected(string channelId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 3
    implicitHeight: Tk.Theme.size.panelMinHeight * 4
    padding: Tk.Theme.space.sm
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.List
    Accessible.name: tooltip
    Accessible.description: qsTr("%1 channels, bank %2 of %3; selected %4")
            .arg(channelCount).arg(bank + 1).arg(bankCount)
            .arg(mixer && selectedChannelId.length ? mixer.channelName(selectedChannelId) : qsTr("none"))

    function applyBank() {
        // AGENT-GUARD (D-113): the strips never move under a held gesture;
        // the requested bank applies when the gesture ends.
        if (held) return
        const target = Math.max(0, Math.min(bankCount - 1, bank))
        strips.positionViewAtIndex(target * bankSize, ListView.Beginning)
        _shownBank = bank
    }
    function follow(channelId) {
        const row = mixer ? mixer.strips.indexOf(channelId) : -1
        if (row >= 0) strips.positionViewAtIndex(row, ListView.Contain)
    }
    function select(channelId) {
        if (!channelId) return
        channelSelected(channelId)
        if (!held) follow(channelId)
    }
    function step(direction) {
        if (!mixer || channelCount === 0) return
        const row = mixer.strips.indexOf(selectedChannelId)
        const next = row < 0 ? 0 : Math.max(0, Math.min(channelCount - 1, row + direction))
        select(mixer.strips.channelAt(next))
    }
    // The bank label follows a flick without moving the strips again.
    function syncBank() {
        const first = strips.indexAt(strips.contentX + stripWidth / 2, 0)
        if (first < 0) return
        _followingScroll = true
        bank = Math.floor(first / bankSize)
        _shownBank = bank
        _followingScroll = false
    }
    onBankChanged: if (!_followingScroll) applyBank()
    onHeldChanged: if (!held && _shownBank !== bank) applyBank()
    onStripWidthChanged: Qt.callLater(view.applyBank) // a resize or mode change keeps the bank aligned
    Keys.onLeftPressed: (event) => { view.step(-1); event.accepted = true }
    Keys.onRightPressed: (event) => { view.step(1); event.accepted = true }
    Keys.onPressed: (event) => {
        if (!view.mixer) return
        if (event.key === Qt.Key_Home) view.select(view.mixer.strips.channelAt(0))
        else if (event.key === Qt.Key_End) view.select(view.mixer.strips.channelAt(view.channelCount - 1))
        else if (event.key === Qt.Key_PageUp) view.bank = Math.max(0, view.bank - 1)
        else if (event.key === Qt.Key_PageDown) view.bank = Math.min(view.bankCount - 1, view.bank + 1)
        else return
        event.accepted = true
    }

    background: Rectangle {
        color: Tk.Theme.color.surface
        border.color: view.activeFocus ? Tk.Theme.color.focus : Tk.Theme.color.border
        border.width: Tk.Theme.size.border
        radius: Tk.Theme.radius.sm
    }
    contentItem: Tk.Flex {
        id: body
        anchors.fill: parent
        anchors.margins: view.padding
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            width: parent.width
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("Mixer") }
            Tk.Caption {
                objectName: "mixerBankLabel"
                text: qsTr("Bank %1 of %2").arg(view.bank + 1).arg(view.bankCount)
                color: Tk.Theme.color.textMuted
            }
            Tk.IconButton {
                objectName: "mixerPreviousBank"
                iconName: "chevron-left"
                small: true
                enabled: view.bank > 0
                tooltip: qsTr("Previous bank")
                onClicked: view.bank = Math.max(0, view.bank - 1)
            }
            Tk.IconButton {
                objectName: "mixerNextBank"
                iconName: "chevron-right"
                small: true
                enabled: view.bank < view.bankCount - 1
                tooltip: qsTr("Next bank")
                onClicked: view.bank = Math.min(view.bankCount - 1, view.bank + 1)
            }
            Tk.Spacer { }
            Repeater {
                model: view.mixer ? view.mixer.groups : []
                delegate: Tk.Flex {
                    id: chip
                    required property var modelData
                    objectName: "mixerGroup"
                    gap: Tk.Theme.space.xs
                    Tk.Caption { text: String(chip.modelData.name); color: Tk.Theme.color.textMuted }
                    Tk.IconButton {
                        objectName: "mixerGroupMute"
                        iconName: "volume-x"
                        small: true
                        checkable: false
                        checked: !!chip.modelData.muted
                        tooltip: chip.modelData.muted ? qsTr("Unmute group %1").arg(chip.modelData.name)
                                                      : qsTr("Mute group %1").arg(chip.modelData.name)
                        onClicked: view.mixer.setGroupMuted(String(chip.modelData.id), !chip.modelData.muted)
                    }
                    Tk.IconButton {
                        objectName: "mixerGroupSolo"
                        iconName: "target"
                        small: true
                        checkable: false
                        checked: !!chip.modelData.soloed
                        tooltip: chip.modelData.soloed ? qsTr("Unsolo group %1").arg(chip.modelData.name)
                                                       : qsTr("Solo group %1").arg(chip.modelData.name)
                        onClicked: view.mixer.setGroupSoloed(String(chip.modelData.id), !chip.modelData.soloed)
                    }
                }
            }
            Tk.IconButton {
                objectName: "mixerDetailToggle"
                iconName: view.detail ? "minimize-2" : "maximize-2"
                small: true
                tooltip: view.detail ? qsTr("Compact strips") : qsTr("Detailed strips with routing and sends")
                onClicked: view.detail = !view.detail
            }
        }
        Item {
            width: parent.width
            Tk.Flex.grow: 1
            ListView {
                id: strips
                objectName: "mixerChannelList"
                anchors.fill: parent
                orientation: ListView.Horizontal
                clip: true
                model: view.mixer ? view.mixer.strips : null
                spacing: Tk.Theme.space.sm
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                snapMode: ListView.SnapToItem
                interactive: !view.held
                onMovementEnded: view.syncBank()
                Accessible.role: Accessible.List
                Accessible.name: view.tooltip
                delegate: Item {
                    id: cell
                    required property int index
                    required property string channelId
                    required property string channelName
                    required property string channelKind
                    required property string layout
                    required property string output
                    required property string outputLabel
                    required property bool outputAvailable
                    required property bool muted
                    required property bool soloed
                    required property bool armed
                    required property bool soloSafe
                    required property bool implicitlyMuted
                    required property var groupNames
                    required property var sends
                    required property bool held
                    objectName: "mixerStripCell"
                    width: view.stripWidth
                    height: strips.height
                    // A held strip that moves (a track reorder) stays in view.
                    onIndexChanged: if (cell.held) Qt.callLater(view.follow, cell.channelId)
                    Tk.ChannelStrip {
                        objectName: "mixerChannelStrip"
                        anchors.fill: parent
                        mixer: view.mixer
                        channelId: cell.channelId
                        channelName: cell.channelName
                        channelKind: cell.channelKind
                        layout: cell.layout
                        output: cell.output
                        muted: cell.muted
                        soloed: cell.soloed
                        armed: cell.armed
                        soloSafe: cell.soloSafe
                        implicitlyMuted: cell.implicitlyMuted
                        groupNames: cell.groupNames
                        sends: cell.sends
                        detail: view.detail
                        selected: view.selectedChannelId === cell.channelId
                        routeSummary: cell.outputAvailable ? cell.outputLabel : qsTr("%1 (unavailable)").arg(cell.outputLabel)
                        tooltip: qsTr("Channel %1").arg(cell.channelName)
                        onChannelSelected: (id) => view.select(id)
                        Accessible.role: Accessible.ListItem
                        Accessible.selected: selected
                        Accessible.focusable: true
                    }
                }
            }
            Tk.EmptyState {
                objectName: "mixerEmptyState"
                anchors.fill: parent
                visible: view.channelCount === 0
                title: qsTr("NO CHANNELS")
                text: qsTr("Bind the mixer of an audio session to show its channels.")
            }
        }
    }
    Tk.ToolTip { visible: view.hovered && view.tooltip.length > 0; text: view.tooltip }
}
